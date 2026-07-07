/**
 * @file    app_tasks.c
 * @brief   Application tasks: Display (clock/settings/card) and CardRead (RC522)
 * @note    UTF-8 encoding
 */

/* Includes ------------------------------------------------------------------*/
#include "app_tasks.h"
#include "oled.h"
#include "rc522.h"
#include "bsp_rtc.h"
#include <stdio.h>
#include <string.h>
#include "usart.h"
/* -------------------------------------------------------------------------- */
/*  Constants and Macros                                                      */
/* -------------------------------------------------------------------------- */

/* Task periods (ms) */
#define DISPLAY_PERIOD_MS       50U    /* Display task polling interval       */
#define CARD_READ_PERIOD_MS     300U   /* Card polling interval               */

/* Key debounce and timing (ms) */
#define KEY_LONG_PRESS_MS       600U   /* Hold duration before auto-repeat    */
#define KEY_REPEAT_MS           150U   /* Auto-repeat interval                */
#define BLINK_PERIOD_MS         500U   /* Blink on/off period                 */
#define CARD_DISPLAY_MS         3000U  /* How long card info stays on screen  */
#define BEEP_DURATION_MS        80U    /* Beep length on card detect          */

/* Display layout (pixels) - 6x10 font: ~21 chars/line, ~6 lines */
#define LINE1_Y   12U
#define LINE2_Y   25U
#define LINE3_Y   38U
#define LINE4_Y   51U
#define LINE5_Y   62U

/* Key pin macros */
#define KEY_MODE_PIN      KEY1_Pin
#define KEY_MODE_PORT     KEY1_GPIO_Port
#define KEY_UP_PIN        KEY2_Pin
#define KEY_UP_PORT       KEY2_GPIO_Port
#define KEY_DOWN_PIN      KEY3_Pin
#define KEY_DOWN_PORT     KEY3_GPIO_Port

/* Key press detection (all three keys use Pull-Up, pressed = LOW) */
#define KEY_IS_PRESSED(port, pin)  (HAL_GPIO_ReadPin((port), (pin)) == GPIO_PIN_RESET)

/* -------------------------------------------------------------------------- */
/*  Display State Machine                                                     */
/* -------------------------------------------------------------------------- */

typedef enum {
    DISP_MODE_CLOCK,      /* Normal clock display                            */
    DISP_MODE_SETTING,    /* Time setting mode                               */
    DISP_MODE_CARD        /* Card detected overlay                           */
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
/*  Helper: Blink-field String Builder                                       */
/* -------------------------------------------------------------------------- */
/**
 * @brief  Replace the selected date/time field with spaces when blink is off
 * @param  buf:      string buffer to modify in-place (e.g. "2026-07-01")
 * @param  blink_on: 1 = show field, 0 = hide field (replace with spaces)
 * @param  field_id: which field is currently selected (FIELD_YEAR .. FIELD_SECOND)
 */
static void ApplyBlink(char *buf, uint8_t blink_on, uint8_t field_id)
{
    /* Field definitions: {field_id, start_pos, width} */
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

    if (blink_on) return; /* Nothing to hide */

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
/*  Task: Display (Clock + Settings + Card UI)                                */
/* -------------------------------------------------------------------------- */
void Task_Display(void *argument)
{
    (void)argument;

    DispMode_t mode = DISP_MODE_CLOCK;
    SetField_t field = FIELD_YEAR;
    BSP_RTC_DateTime_t dt;        /* Working copy for setting mode */
    CardInfo_t card_info;
    uint32_t card_show_deadline = 0;

    /* Key state tracking */
    uint8_t  key_mode_prev = 0, key_up_prev = 0, key_down_prev = 0;
    uint32_t key_up_tick  = 0, key_down_tick  = 0;
    uint32_t key_up_repeat  = 0, key_down_repeat = 0;

    /* Blink state */
    uint8_t  blink_on = 1;
    uint32_t last_blink_tick = 0;

    /* Display buffers */
    char line_buf[24];
    const char *weekdays[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
    const char *field_names[] = {"YEAR","MONTH","DAY","HOUR","MIN","SEC"};

    /* Wait for SSD1306 power-on reset to complete (~150ms needed).
     * On cold boot the OLED chip is not ready when MCU starts;
     * on warm reset power was already stable, so no issue. */
    osDelay(200);

    /* Initialize OLED once */
    OLED_Init();

    for (;;) {
        uint32_t now = osKernelGetTickCount();

        /* ---- Read keys ---- */
        uint8_t key_mode  = KEY_IS_PRESSED(KEY_MODE_PORT, KEY_MODE_PIN);
        uint8_t key_up    = KEY_IS_PRESSED(KEY_UP_PORT, KEY_UP_PIN);
        uint8_t key_down  = KEY_IS_PRESSED(KEY_DOWN_PORT, KEY_DOWN_PIN);

        /* ---- Read RTC (only in clock/card modes to preserve setting edits) ---- */
        if (mode != DISP_MODE_SETTING) {
            BSP_RTC_GetDateTime(&dt);
        }

        /* ---- Key MODE: cycle field / save & exit / enter setting ---- */
        if (key_mode && !key_mode_prev) {
            if (mode == DISP_MODE_CLOCK) {
                /* Enter setting mode: snapshot current time */
                BSP_RTC_GetDateTime(&dt);
                dt.second = 0;  /* Zero seconds on entry for cleaner setting */
                mode = DISP_MODE_SETTING;
                field = FIELD_YEAR;
                blink_on = 1;
                last_blink_tick = now;
            } else if (mode == DISP_MODE_SETTING) {
                field = (SetField_t)((uint8_t)field + 1U);
                if (field >= FIELD_COUNT) {
                    /* Save and exit */
                    dt.weekday = BSP_RTC_CalcWeekday(dt.year, dt.month, dt.day);
                    BSP_RTC_SetDateTime(&dt);
                    BSP_RTC_MarkInitialized();
                    mode = DISP_MODE_CLOCK;
                }
                blink_on = 1;
                last_blink_tick = now;
            } else if (mode == DISP_MODE_CARD) {
                /* Dismiss card display early */
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
                    if (dt.year > 2020U) dt.year--; break;
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
                    if (dt.year > 2020U) dt.year--; break;
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
        }

        /* ---- Card display timeout ---- */
        if (mode == DISP_MODE_CARD && ((int32_t)(now - card_show_deadline) >= 0)) {
            mode = DISP_MODE_CLOCK;
        }

        /* ---- Render to OLED ---- */
        OLED_Clear();

        if (mode == DISP_MODE_CLOCK) {
            /* Line 1: YYYY-MM-DD  Www */
            snprintf(line_buf, sizeof(line_buf),
                     "%04u-%02u-%02u  %s",
                     dt.year, dt.month, dt.day, weekdays[dt.weekday]);
            OLED_ShowString(0, LINE1_Y, line_buf);

            /* Line 2-3: Large-style time centered */
            snprintf(line_buf, sizeof(line_buf),
                     "     %02u:%02u:%02u",
                     dt.hour, dt.minute, dt.second);
            OLED_ShowString(0, LINE2_Y, line_buf);

            /* Line 4: status */
            OLED_ShowString(0, LINE4_Y, "  Waiting card...");

        } else if (mode == DISP_MODE_SETTING) {
            /* Title */
            OLED_ShowString(0, LINE1_Y, "=== SET DATE/TIME ===");

            /* Date: blink only when editing YEAR/MONTH/DAY */
            OLED_ShowString(0, LINE2_Y, "D:");
            snprintf(line_buf, sizeof(line_buf),
                     "%04u-%02u-%02u", dt.year, dt.month, dt.day);
            ApplyBlink(line_buf,
                       (field <= FIELD_DAY) ? blink_on : 1, field);
            OLED_ShowString(12, LINE2_Y, line_buf);

            /* Time: blink only when editing HOUR/MINUTE/SECOND */
            snprintf(line_buf, sizeof(line_buf),
                     "%02u:%02u:%02u", dt.hour, dt.minute, dt.second);
            ApplyBlink(line_buf,
                       (field >= FIELD_HOUR) ? blink_on : 1, field);
            OLED_ShowString(0, LINE3_Y, "T:");
            OLED_ShowString(12, LINE3_Y, line_buf);

            /* Field indicator */
            snprintf(line_buf, sizeof(line_buf),
                     "Set: %s  +-:adj", field_names[field]);
            OLED_ShowString(0, LINE4_Y, line_buf);

            /* Bottom hint */
            OLED_ShowString(0, LINE5_Y, "MODE:next/save");

        } else if (mode == DISP_MODE_CARD) {
            snprintf(line_buf, sizeof(line_buf),
                     "UID: %02X%02X%02X%02X",
                     card_info.uid[0], card_info.uid[1],
                     card_info.uid[2], card_info.uid[3]);
            OLED_ShowString(0, LINE1_Y, line_buf);

            snprintf(line_buf, sizeof(line_buf),
                     "%04u-%02u-%02u %02u:%02u:%02u",
                     card_info.timestamp.year,
                     card_info.timestamp.month,
                     card_info.timestamp.day,
                     card_info.timestamp.hour,
                     card_info.timestamp.minute,
                     card_info.timestamp.second);
            OLED_ShowString(0, LINE3_Y, line_buf);

            OLED_ShowString(0, LINE5_Y, "MODE: back to clock");
        }

        OLED_Refresh();

        /* ---- Save previous key states ---- */
        key_mode_prev  = key_mode;
        key_up_prev    = key_up;
        key_down_prev  = key_down;

        osDelay(DISPLAY_PERIOD_MS);
    }
}

/* -------------------------------------------------------------------------- */
/*  Task: Card Read (RC522 Polling)                                           */
/* -------------------------------------------------------------------------- */
void Task_CardRead(void *argument)
{
    (void)argument;
    uint8_t uid[4];
    CardInfo_t card_info;

    /* Initialize RC522 platform (DWT + RC522 chip) */
		RC522_Platform_Init();

	RC522_ConfigISOType('A');

    for (;;) {
        char status = RC522_ScanCard(uid);
		printf("%d\r\n",(uint8_t)status);
        if (status == RC522_OK) {
            /* Populate card info */
            memcpy(card_info.uid, uid, 4);
            BSP_RTC_GetDateTime(&card_info.timestamp);

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
