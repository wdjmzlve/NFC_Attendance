/**
 * @file    app_tasks.c
 * @brief   Application tasks: Display (splash/clock/card/settings) and CardRead
 * @note    UTF-8 encoding
 */

/* Includes ------------------------------------------------------------------*/
#include "app_tasks.h"
#include "oled.h"
#include "rc522.h"
#include "bsp_rtc.h"
#include "gui.h"
#include "ds18b20.h"
#include <stdio.h>
#include <string.h>
#include "usart.h"
#include "u8g2.h"
/* -------------------------------------------------------------------------- */
/*  Constants and Macros                                                      */
/* -------------------------------------------------------------------------- */

/* Task periods (ms) */
#define DISPLAY_PERIOD_MS       50U
#define CARD_READ_PERIOD_MS     300U
#define SPLASH_DURATION_MS      3000U
#define CARD_DISPLAY_MS         3000U
#define TEMP_READ_PERIOD_MS     2000U
#define BEEP_DURATION_MS        80U

/* Key timing (ms) */
#define KEY_LONG_PRESS_MS       600U
#define KEY_REPEAT_MS           150U
#define BLINK_PERIOD_MS         500U

/* Display layout Y coordinates (pixels) */
#define LINE1_Y   12U
#define LINE2_Y   26U
#define LINE3_Y   40U
#define LINE4_Y   54U

/* Name/Sector bitmap dimensions (SSD1306 page format: 161 = ceil(80/8) * 16 + 1) */
#define NAME_BMP_W     80U
#define NAME_BMP_H     16U
#define SECTOR_BMP_W   80U
#define SECTOR_BMP_H   16U
#define BMP_TOTAL_BYTES 322U  /* 161 Name + 161 Sector */

/* Key pin macros */
#define KEY_MODE_PIN      KEY1_Pin
#define KEY_MODE_PORT     KEY1_GPIO_Port
#define KEY_UP_PIN        KEY2_Pin
#define KEY_UP_PORT       KEY2_GPIO_Port
#define KEY_DOWN_PIN      KEY3_Pin
#define KEY_DOWN_PORT     KEY3_GPIO_Port
#define KEY_SAVE_PIN      KEY4_Pin
#define KEY_SAVE_PORT     KEY4_GPIO_Port

#define KEY_IS_PRESSED(port, pin)  (HAL_GPIO_ReadPin((port), (pin)) == GPIO_PIN_RESET)

/*
 * LED macros: LEDs are open-drain output, anode to VCC, cathode to GPIO.
 * GPIO_PIN_RESET (LOW)  = NMOS on  = current flows = LED ON
 * GPIO_PIN_SET   (HIGH) = NMOS off = Hi-Z         = LED OFF
 */
#define LED_ON(port, pin)   HAL_GPIO_WritePin((port), (pin), GPIO_PIN_RESET)
#define LED_OFF(port, pin)  HAL_GPIO_WritePin((port), (pin), GPIO_PIN_SET)

/* -------------------------------------------------------------------------- */
/*  Display State Machine                                                     */
/* -------------------------------------------------------------------------- */

typedef enum {
    DISP_MODE_SPLASH,     /* Boot splash screen (3 sec)                       */
    DISP_MODE_CLOCK,      /* Clock + temperature standby                      */
    DISP_MODE_SETTING,    /* Date/time setting via keys                       */
    DISP_MODE_CARD        /* Card detected info overlay                       */
} DispMode_t;

typedef enum {
    FIELD_YEAR = 0,
    FIELD_MONTH,
    FIELD_DAY,
    FIELD_HOUR,
    FIELD_MINUTE,
    FIELD_SECOND,
    FIELD_COUNT
} SetField_t;

/* -------------------------------------------------------------------------- */
/*  Message Queue                                                             */
/* -------------------------------------------------------------------------- */

#define CARD_QUEUE_SIZE  4U
osMessageQueueId_t cardQueueHandle;

/* Card bitmap buffers: read from sectors 9+, used by display task */
static uint8_t card_name_bmp[161];
static uint8_t card_sector_bmp[161];
static uint8_t card_has_bmp = 0;

/* -------------------------------------------------------------------------- */
/*  Helper: Short Beep                                                        */
/* -------------------------------------------------------------------------- */
static void Beep(uint32_t duration_ms)
{
    HAL_GPIO_WritePin(BEEP_GPIO_Port, BEEP_Pin, GPIO_PIN_SET);
    osDelay(duration_ms);
    HAL_GPIO_WritePin(BEEP_GPIO_Port, BEEP_Pin, GPIO_PIN_RESET);
}

/* -------------------------------------------------------------------------- */
/*  Helper: Blink-field String Builder                                        */
/* -------------------------------------------------------------------------- */
/**
 * @brief  Replace the selected date/time field with spaces when blink is off
 * @param  buf:      string buffer to modify in-place (e.g. "2026-07-01")
 * @param  blink_on: 1 = show field, 0 = hide field
 * @param  field_id: which field is selected (FIELD_YEAR .. FIELD_SECOND)
 */
static void ApplyBlink(char *buf, uint8_t blink_on, uint8_t field_id)
{
    struct {
        uint8_t id;
        uint8_t pos;
        uint8_t width;
    } fields[] = {
        {FIELD_YEAR,   0, 4},   /* "YYYY"-MM-DD   */
        {FIELD_MONTH,  5, 2},   /* YYYY-"MM"-DD   */
        {FIELD_DAY,    8, 2},   /* YYYY-MM-"DD"   */
        {FIELD_HOUR,   0, 2},   /* "HH":MM:SS     */
        {FIELD_MINUTE, 3, 2},   /* HH:"MM":SS     */
        {FIELD_SECOND, 6, 2},   /* HH:MM:"SS"     */
    };
    const uint8_t field_count = sizeof(fields) / sizeof(fields[0]);
    uint8_t i;

    if (blink_on) return;

    for (i = 0; i < field_count; i++) {
        if (fields[i].id == field_id) {
            memset(buf + fields[i].pos, ' ', fields[i].width);
            return;
        }
    }
}

/* -------------------------------------------------------------------------- */
/*  Helper: Clamp Day to Valid Range                                          */
/* -------------------------------------------------------------------------- */
static uint8_t ClampDay(uint8_t day, uint16_t year, uint8_t month)
{
    uint8_t max_day = BSP_RTC_DaysInMonth(year, month);
    if (day > max_day) day = max_day;
    if (day < 1U) day = 1U;
    return day;
}

/* -------------------------------------------------------------------------- */
/*  Task: Display (Splash + Clock + Settings + Card UI)                       */
/* -------------------------------------------------------------------------- */
void Task_Display(void *argument)
{
    (void)argument;

    /* Wait for SSD1306 power-on reset (~150ms needed). */
    osDelay(200);

    /* Initialize OLED, DS18B20 temperature sensor */
    OLED_Init();
    ds18b20_init();

    /* Turn off all LEDs initially */
    LED_OFF(LED1_GPIO_Port, LED1_Pin);
    LED_OFF(LED2_GPIO_Port, LED2_Pin);
    LED_OFF(LED3_GPIO_Port, LED3_Pin);

    /* ---- Splash mode state ---- */
    DispMode_t mode = DISP_MODE_SPLASH;
    uint32_t splash_deadline = osKernelGetTickCount() + SPLASH_DURATION_MS;

    /* ---- Clock / card state ---- */
	BSP_RTC_DateTime_t dt = {
		.year = 2026, .month = 6, .day = 20,
		.hour = 14, .minute = 30, .second = 0
	};
	if (BSP_RTC_IsFirstPowerOn()) {
    BSP_RTC_MarkInitialized();

	}

    CardInfo_t card_info;
    uint32_t card_show_deadline = 0;
    float    temperature = 0.0f;
    uint32_t last_temp_read = 0;

    /* ---- Setting mode state ---- */
    SetField_t field = FIELD_YEAR;
    uint8_t  key_mode_prev = 0, key_up_prev = 0, key_down_prev = 0, key_save_prev = 0;
    uint32_t key_up_tick    = 0, key_down_tick    = 0;
    uint32_t key_up_repeat  = 0, key_down_repeat  = 0;
    uint8_t  blink_on       = 1;
    uint32_t last_blink_tick = 0;

    /* Display buffers */
    char line_buf[24];
    const char *weekdays[]   = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
    const char *field_names[] = {"YEAR","MONTH","DAY","HOUR","MIN","SEC"};

    for (;;) {
        uint32_t now = osKernelGetTickCount();

        /* ---- Read keys ---- */
        uint8_t key_mode  = KEY_IS_PRESSED(KEY_MODE_PORT, KEY_MODE_PIN);
        uint8_t key_up    = KEY_IS_PRESSED(KEY_UP_PORT,   KEY_UP_PIN);
        uint8_t key_down  = KEY_IS_PRESSED(KEY_DOWN_PORT, KEY_DOWN_PIN);
        uint8_t key_save  = KEY_IS_PRESSED(KEY_SAVE_PORT, KEY_SAVE_PIN);

        /* ---- Read RTC (skip in setting mode to preserve edits) ---- */
        if (mode != DISP_MODE_SETTING) {
            BSP_RTC_GetDateTime(&dt);
        }

        /* ---- Read temperature (only in clock mode, every 2 sec) ---- */
        if (mode == DISP_MODE_CLOCK
            && (now - last_temp_read >= TEMP_READ_PERIOD_MS)) {
            temperature = ds18b20_read();
            last_temp_read = now;
        }

        /* ---- Splash -> Clock transition ---- */
        if (mode == DISP_MODE_SPLASH) {
            if ((int32_t)(now - splash_deadline) >= 0) {
                mode = DISP_MODE_CLOCK;
            }
        }

        /* ---- Key MODE: cycle field / save & exit / enter setting ---- */
        if (key_mode && !key_mode_prev) {
            if (mode == DISP_MODE_CLOCK) {
                BSP_RTC_GetDateTime(&dt);
                dt.second = 0;
                mode  = DISP_MODE_SETTING;
                field = FIELD_YEAR;
                blink_on = 1;
                last_blink_tick = now;
            } else if (mode == DISP_MODE_SETTING) {
                field = (SetField_t)((uint8_t)field + 1U);
                if (field >= FIELD_COUNT) {
                    dt.weekday = BSP_RTC_CalcWeekday(dt.year, dt.month, dt.day);
                    BSP_RTC_SetDateTime(&dt);
                    BSP_RTC_MarkInitialized();
                    mode = DISP_MODE_CLOCK;
                }
                blink_on = 1;
                last_blink_tick = now;
            } else if (mode == DISP_MODE_CARD) {
                mode = DISP_MODE_CLOCK;
                LED_OFF(LED1_GPIO_Port, LED1_Pin);
                LED_OFF(LED2_GPIO_Port, LED2_Pin);
                LED_OFF(LED3_GPIO_Port, LED3_Pin);
            }
        }

        /* ---- Key SAVE (KEY4): save & exit in setting mode ---- */
        if (key_save && !key_save_prev) {
            if (mode == DISP_MODE_SETTING) {
                dt.weekday = BSP_RTC_CalcWeekday(dt.year, dt.month, dt.day);
                BSP_RTC_SetDateTime(&dt);
                BSP_RTC_MarkInitialized();
                mode = DISP_MODE_CLOCK;
            }
        }

        /* ---- Blink timer ---- */
        if (now - last_blink_tick >= BLINK_PERIOD_MS) {
            blink_on = !blink_on;
            last_blink_tick = now;
        }

        /* ---- Key UP: increment ---- */
        if (key_up && !key_up_prev) {
            key_up_tick = now;
            key_up_repeat = now;
            if (mode == DISP_MODE_SETTING) {
                switch (field) {
                case FIELD_YEAR:
                    if (dt.year < 2099U) dt.year++; break;
                case FIELD_MONTH:
                    dt.month = (dt.month % 12U) + 1U;
                    dt.day = ClampDay(dt.day, dt.year, dt.month);
                    break;
                case FIELD_DAY:
                    dt.day = ClampDay(dt.day + 1U, dt.year, dt.month);
                    break;
                case FIELD_HOUR:
                    dt.hour = (dt.hour + 1U) % 24U; break;
                case FIELD_MINUTE:
                    dt.minute = (dt.minute + 1U) % 60U; break;
                case FIELD_SECOND:
                    dt.second = (dt.second + 1U) % 60U; break;
                default: break;
                }
            }
        }
        /* Key UP: long-press auto-repeat */
        if (key_up && (now - key_up_tick >= KEY_LONG_PRESS_MS)
            && (now - key_up_repeat >= KEY_REPEAT_MS)) {
            key_up_repeat = now;
            if (mode == DISP_MODE_SETTING) {
                switch (field) {
                case FIELD_YEAR:
                    if (dt.year < 2099U) dt.year++; break;
                case FIELD_MONTH:
                    dt.month = (dt.month % 12U) + 1U;
                    dt.day = ClampDay(dt.day, dt.year, dt.month);
                    break;
                case FIELD_DAY:
                    dt.day = ClampDay(dt.day + 1U, dt.year, dt.month);
                    break;
                case FIELD_HOUR:
                    dt.hour = (dt.hour + 1U) % 24U; break;
                case FIELD_MINUTE:
                    dt.minute = (dt.minute + 1U) % 60U; break;
                case FIELD_SECOND:
                    dt.second = (dt.second + 1U) % 60U; break;
                default: break;
                }
            }
        }

        /* ---- Key DOWN: decrement ---- */
        if (key_down && !key_down_prev) {
            key_down_tick = now;
            key_down_repeat = now;
            if (mode == DISP_MODE_SETTING) {
                switch (field) {
                case FIELD_YEAR:
                    if (dt.year ) dt.year--; break;
                case FIELD_MONTH:
                    dt.month = (dt.month == 1U) ? 12U : dt.month - 1U;
                    dt.day = ClampDay(dt.day, dt.year, dt.month);
                    break;
                case FIELD_DAY:
                    dt.day = ClampDay(dt.day - 1U, dt.year, dt.month);
                    break;
                case FIELD_HOUR:
                    dt.hour = (dt.hour == 0U) ? 23U : dt.hour - 1U; break;
                case FIELD_MINUTE:
                    dt.minute = (dt.minute == 0U) ? 59U : dt.minute - 1U; break;
                case FIELD_SECOND:
                    dt.second = (dt.second == 0U) ? 59U : dt.second - 1U; break;
                default: break;
                }
            }
        }
        /* Key DOWN: long-press auto-repeat */
        if (key_down && (now - key_down_tick >= KEY_LONG_PRESS_MS)
            && (now - key_down_repeat >= KEY_REPEAT_MS)) {
            key_down_repeat = now;
            if (mode == DISP_MODE_SETTING) {
                switch (field) {
                case FIELD_YEAR:
                    if (dt.year ) dt.year--; break;
                case FIELD_MONTH:
                    dt.month = (dt.month == 1U) ? 12U : dt.month - 1U;
                    dt.day = ClampDay(dt.day, dt.year, dt.month);
                    break;
                case FIELD_DAY:
                    dt.day = ClampDay(dt.day - 1U, dt.year, dt.month);
                    break;
                case FIELD_HOUR:
                    dt.hour = (dt.hour == 0U) ? 23U : dt.hour - 1U; break;
                case FIELD_MINUTE:
                    dt.minute = (dt.minute == 0U) ? 59U : dt.minute - 1U; break;
                case FIELD_SECOND:
                    dt.second = (dt.second == 0U) ? 59U : dt.second - 1U; break;
                default: break;
                }
            }
        }

        /* ---- Check card queue (non-blocking) ---- */
        if (osMessageQueueGet(cardQueueHandle, &card_info, NULL, 0) == osOK) {
            mode = DISP_MODE_CARD;
            card_show_deadline = now + CARD_DISPLAY_MS;

            /* Update card status LEDs */
            LED_ON(LED1_GPIO_Port, LED1_Pin);
            if (card_info.card_type == CARD_TYPE_NORMAL) {
                LED_ON(LED2_GPIO_Port, LED2_Pin);
                LED_OFF(LED3_GPIO_Port, LED3_Pin);
            } else if (card_info.card_type == CARD_TYPE_IMAGE) {
                LED_OFF(LED2_GPIO_Port, LED2_Pin);
                LED_ON(LED3_GPIO_Port, LED3_Pin);
            } else {
                LED_OFF(LED2_GPIO_Port, LED2_Pin);
                LED_OFF(LED3_GPIO_Port, LED3_Pin);
            }
        }

        /* ---- Card display timeout ---- */
        if (mode == DISP_MODE_CARD
            && ((int32_t)(now - card_show_deadline) >= 0)) {
            mode = DISP_MODE_CLOCK;
            LED_OFF(LED1_GPIO_Port, LED1_Pin);
            LED_OFF(LED2_GPIO_Port, LED2_Pin);
            LED_OFF(LED3_GPIO_Port, LED3_Pin);
        }

        /* ---- Render to OLED ---- */
        OLED_Clear();

        if (mode == DISP_MODE_SPLASH) {
            /* Logo on left half: 64x64 native page-format bitmap */
            OLED_DrawBitmap(0, 0, 64, 64, logo);
            OLED_SetFont(u8g2_font_wqy12_t_gb2312);
            OLED_DrawUTF8(68, 36,  "姓名:杨城");


            /* Chinese course name on right side (wqy12, 12px font) */
            OLED_SetFont(u8g2_font_wqy12_t_gb2312);
            OLED_DrawUTF8(68, 12,  "专业综合");
            OLED_DrawUTF8(68, 25, "实践||");

            /* Project name in ASCII (6x10 font) */
            OLED_SetFont(u8g2_font_6x10_tf);
            OLED_ShowString(68, 46, "NFC");
            OLED_ShowString(68, 56, "Attendance");

        } else if (mode == DISP_MODE_CLOCK) {
            OLED_SetFont(u8g2_font_6x10_tf);

            /* Date + weekday */
            snprintf(line_buf, sizeof(line_buf),
                     "%04u-%02u-%02u %s",
                     dt.year, dt.month, dt.day, weekdays[dt.weekday]);
            OLED_ShowString(0, LINE1_Y, line_buf);

            /* Time */
            snprintf(line_buf, sizeof(line_buf),
                     "     %02u:%02u:%02u",
                     dt.hour, dt.minute, dt.second);
            OLED_ShowString(0, LINE2_Y, line_buf);

            /* Status text */
            OLED_ShowString(0, LINE3_Y, "Waiting card...");

            /* Temperature: right-aligned at bottom */
            snprintf(line_buf, sizeof(line_buf),
                     "%.1fC", (double)temperature);
            {
                uint8_t temp_len = (uint8_t)strlen(line_buf);
                OLED_ShowString((uint8_t)(128U - temp_len * 6U),
                                LINE4_Y, line_buf);
            }

        } else if (mode == DISP_MODE_SETTING) {
            OLED_SetFont(u8g2_font_6x10_tf);

            OLED_ShowString(0, 4, "=== SET DATE/TIME ===");

            /* Date: blink only when editing YEAR/MONTH/DAY */
            OLED_ShowString(0, 16, "D:");
            snprintf(line_buf, sizeof(line_buf),
                     "%04u-%02u-%02u", dt.year, dt.month, dt.day);
            ApplyBlink(line_buf,
                       (field <= FIELD_DAY) ? blink_on : 1, field);
            OLED_ShowString(12, 16, line_buf);

            /* Time: blink only when editing HOUR/MINUTE/SECOND */
            snprintf(line_buf, sizeof(line_buf),
                     "%02u:%02u:%02u", dt.hour, dt.minute, dt.second);
            ApplyBlink(line_buf,
                       (field >= FIELD_HOUR) ? blink_on : 1, field);
            OLED_ShowString(0, 30, "T:");
            OLED_ShowString(12, 30, line_buf);

            /* Field name and hints */
            snprintf(line_buf, sizeof(line_buf),
                     "Set: %s  +-:adj", field_names[field]);
            OLED_ShowString(0, 44, line_buf);
            OLED_ShowString(0, 56, "KEY4:save MODE:next");

        } else if (mode == DISP_MODE_CARD) {
            OLED_SetFont(u8g2_font_6x10_tf);

            /* Card UID header */
            snprintf(line_buf, sizeof(line_buf),
                     "Card: %02X%02X%02X%02X",
                     card_info.uid[0], card_info.uid[1],
                     card_info.uid[2], card_info.uid[3]);
            OLED_ShowString(0, 8, line_buf);

            /* Name bitmap (Chinese dot-matrix from card sectors 9+) */
            OLED_SetFont(u8g2_font_wqy12_t_gb2312);
            OLED_DrawUTF8(0, 20, "姓名:");
            if (card_has_bmp) {
                OLED_DrawBitmap(24, 10, NAME_BMP_W, NAME_BMP_H, card_name_bmp);
            }

            /* Student ID in decimal */
            snprintf(line_buf, sizeof(line_buf),
                     "ID: %lu", (unsigned long)card_info.id_num);
            OLED_ShowString(0, 32, line_buf);

            /* Department label (Chinese) + Sector bitmap from card */
            OLED_SetFont(u8g2_font_wqy12_t_gb2312);
            OLED_DrawUTF8(0, 44, "部门:");
            if (card_has_bmp) {
                OLED_DrawBitmap(24, 35, SECTOR_BMP_W, SECTOR_BMP_H,
                                card_sector_bmp);
            }
			

            
            /* Swipe timestamp */
            snprintf(line_buf, sizeof(line_buf),
                     "%04u-%02u-%02u %02u:%02u:%02u",
                     card_info.timestamp.year,
                     card_info.timestamp.month,
                     card_info.timestamp.day,
                     card_info.timestamp.hour,
                     card_info.timestamp.minute,
                     card_info.timestamp.second);
            OLED_ShowString(0, 60, line_buf);

//            OLED_ShowString(0, 56, "MODE: back to clock");
        }

        OLED_Refresh();

        /* Save previous key states */
        key_mode_prev  = key_mode;
        key_up_prev    = key_up;
        key_down_prev  = key_down;
        key_save_prev  = key_save;

        osDelay(DISPLAY_PERIOD_MS);
    }
}

/* -------------------------------------------------------------------------- */
/*  Task: Card Read (RC522 Polling + Data Read)                               */
/* -------------------------------------------------------------------------- */
void Task_CardRead(void *argument)
{
    (void)argument;

    uint8_t uid[4];
    uint8_t block_data[16];
    uint8_t write_buf[16];
    uint8_t default_key[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t i;
    uint16_t sum;
    CardInfo_t card_info;

    /* Initialize RC522 platform */
    RC522_Platform_Init();
    RC522_ConfigISOType('A');

    /*
     * One-time card issuance: write default account header to
     * Sector 0, Block 1 of the first card presented after boot.
     *
     * Block 1 layout (16 bytes):
     *   [0..3]   UID (from anticollision)
     *   [4..7]   Student ID (default: 23040722 -> 0x01,0x5F,0x92,0xD2)
     *   [8..11]  Reserved (0x00)
     *   [12]     Card type (0x00=Normal, 0x01=Image, 0x02=Admin)
     *   [13]     Status flag (0x00)
     *   [14..15] Checksum = sum(bytes 0-13), little-endian
     */
    if (RC522_ScanCard(uid) == RC522_OK) {
        memset(write_buf, 0, sizeof(write_buf));
        memcpy(&write_buf[0], uid, 4);               /* UID from card        */
        write_buf[4]  = 0x01;                         /* Student ID byte 0    */
        write_buf[5]  = 0x5F;                         /* Student ID byte 1    */
        write_buf[6]  = 0x92;                         /* Student ID byte 2    */
        write_buf[7]  = 0xD2;                         /* Student ID byte 3    */
        /* bytes 8-11 remain 0 (reserved)                                     */
        write_buf[12] = CARD_TYPE_NORMAL;             /* Card type = Normal   */
        /* byte 13 remains 0 (status flag)                                    */

        /* 16-bit sum of bytes 0-13, little-endian */
        sum = 0;
        for (i = 0; i < 14U; i++) {
            sum += write_buf[i];
        }
        write_buf[14] = (uint8_t)(sum & 0xFFU);
        write_buf[15] = (uint8_t)((sum >> 8U) & 0xFFU);

        /* Authenticate Sector 0 (trailer = block 3), then write block 1 */
        if (RC522_AuthState(RC522_PICC_AUTHENT1A,
                            3, default_key, uid) == RC522_OK) {
            RC522_WriteBlock(0, 1, write_buf);
        }

        /*
         * Write Name[] + Sector[] to sectors 9-13, blocks 0-2 (240 bytes).
         * Bytes are walked sequentially, packed into 16-byte blocks, and
         * written to consecutive data blocks across 5 sectors.
         */
        {
            uint8_t  blk_buf[16];
            uint8_t  sec = 9, blk = 0;
            uint16_t pos = 0, arr_idx;
            const uint8_t *arrays[2];
            uint16_t arr_len[2];

            arrays[0]  = (const uint8_t *)Name;
            arrays[1]  = (const uint8_t *)Sector;
            arr_len[0] = 161U;
            arr_len[1] = 161U;

            for (arr_idx = 0; arr_idx < 2U; arr_idx++) {
                uint16_t j;
                for (j = 0; j < arr_len[arr_idx]; j++) {
                    blk_buf[pos % 16U] = arrays[arr_idx][j];
                    pos++;
                    if ((pos % 16U) == 0U) {
                        if (RC522_AuthState(RC522_PICC_AUTHENT1A,
                                            (uint8_t)(sec * 4U + 3U),
                                            default_key, uid) == RC522_OK) {
                            RC522_WriteBlock(sec, blk, blk_buf);
                        }
                        blk++;
                        if (blk >= 3U) { blk = 0U; sec++; }
                    }
                }
            }
            /* Write remaining partial block padded with 0x00 */
            if ((pos % 16U) != 0U) {
                while ((pos % 16U) != 0U) {
                    blk_buf[pos % 16U] = 0x00;
                    pos++;
                }
                if (RC522_AuthState(RC522_PICC_AUTHENT1A,
                                    (uint8_t)(sec * 4U + 3U),
                                    default_key, uid) == RC522_OK) {
                    RC522_WriteBlock(sec, blk, blk_buf);
                }
            }
        }

        RC522_Halt();
        RC522_WaitCardOff();
    }

    for (;;) {
        char status = RC522_ScanCard(uid);
        printf("%d\r\n", (uint8_t)status);

        if (status == RC522_OK) {
            /* Fill card info basics */
            memcpy(card_info.uid, uid, 4);
            BSP_RTC_GetDateTime(&card_info.timestamp);
            card_info.card_type = 0;
            card_info.id_num    = 0;

            /*
             * Read Sector 0, Block 1 (account header).
             * Auth on block 3 (trailer of sector 0).
             *
             * Block 1 layout:
             *   [0..3]   UID
             *   [4..7]   Student ID (big-endian uint32)
             *   [8..11]  Reserved
             *   [12]     Card type (0x00=Normal, 0x01=Image, 0x02=Admin)
             *   [13]     Status flag
             *   [14..15] Checksum = sum(bytes 0-13), little-endian
             */
            status = RC522_AuthState(RC522_PICC_AUTHENT1A,
                                     3, default_key, uid);
            if (status == RC522_OK) {
                status = RC522_Read(1, block_data);
                if (status == RC522_OK) {
                    /* Verify checksum: 16-bit sum of bytes 0-13 */
                    sum = 0;
                    for (i = 0; i < 14U; i++) {
                        sum += block_data[i];
                    }
                    if ((uint8_t)(sum & 0xFFU) == block_data[14]
                        && (uint8_t)((sum >> 8U) & 0xFFU) == block_data[15]) {
                        card_info.card_type = block_data[12];
                        /* Big-endian ID -> uint32_t */
                        card_info.id_num =
                            ((uint32_t)block_data[4] << 24U)
                          | ((uint32_t)block_data[5] << 16U)
                          | ((uint32_t)block_data[6] << 8U)
                          |  (uint32_t)block_data[7];
                    }
                }
            }

            /*
             * Read Name + Sector bitmaps from Sectors 9+ onward.
             * Blocks are filled sequentially: 0, 1, 2 of each sector.
             */
            {
                uint8_t  blk_buf[16];
                uint8_t  sec = 9, blk = 0;
                uint16_t pos = 0;

                while (pos < BMP_TOTAL_BYTES) {
                    if (blk >= 3U) { blk = 0U; sec++; }
                    if (RC522_AuthState(RC522_PICC_AUTHENT1A,
                                        (uint8_t)(sec * 4U + 3U),
                                        default_key, uid) == RC522_OK) {
                        if (RC522_ReadBlock(sec, blk, blk_buf) == RC522_OK) {
                            uint8_t k;
                            for (k = 0; k < 16U && pos < BMP_TOTAL_BYTES; k++) {
                                if (pos < 161U) {
                                    card_name_bmp[pos] = blk_buf[k];
                                } else {
                                    card_sector_bmp[pos - 161U] = blk_buf[k];
                                }
                                pos++;
                            }
                        }
                    }
                    blk++;
                }
                card_has_bmp = 1;
            }

            /* Halt card so it can be re-detected */
            RC522_Halt();

            /* Send to display task (non-blocking, drop if queue full) */
            osMessageQueuePut(cardQueueHandle, &card_info, 0, 0);

            /* Audible feedback */
            Beep(BEEP_DURATION_MS);

            /* Wait for card removal before scanning again */
            RC522_WaitCardOff();
        }
        osDelay(CARD_READ_PERIOD_MS);
    }
}
