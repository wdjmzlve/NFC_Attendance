/**
  ******************************************************************************
  * @file    oled.c
  * @brief   OLED SSD1306 driver using u8g2 library over I2C1
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "oled.h"
#include "i2c.h"

/* Private variables ---------------------------------------------------------*/
static u8g2_t u8g2;  /* u8g2 display object, shared across OLED functions */

/* External variables --------------------------------------------------------*/
extern I2C_HandleTypeDef hi2c1;

/* -------------------------------------------------------------------------- */
/*                          u8g2 I2C byte callback                            */
/* -------------------------------------------------------------------------- */
uint8_t u8x8_byte_hw_i2c(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
    static uint8_t buffer[128];  /* u8g2 sends max 128 bytes per transfer */
    static uint8_t buf_idx;
    uint8_t *data;

    switch (msg)
    {
    case U8X8_MSG_BYTE_INIT:
        /* I2C1 is already initialized in main() via MX_I2C1_Init() */
        break;

    case U8X8_MSG_BYTE_START_TRANSFER:
        buf_idx = 0;
        break;

    case U8X8_MSG_BYTE_SEND:
        data = (uint8_t *)arg_ptr;
        while (arg_int > 0)
        {
            buffer[buf_idx++] = *data;
            data++;
            arg_int--;
        }
        break;

    case U8X8_MSG_BYTE_END_TRANSFER:
        if (HAL_I2C_Master_Transmit(&hi2c1, OLED_ADDRESS, buffer, buf_idx, 1000) != HAL_OK)
            return 0;
        break;

    case U8X8_MSG_BYTE_SET_DC:
        break;

    default:
        return 0;
    }

    return 1;
}

/* -------------------------------------------------------------------------- */
/*                     u8g2 GPIO and delay callback                           */
/* -------------------------------------------------------------------------- */
uint8_t u8x8_gpio_and_delay(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
    switch (msg)
    {
    case U8X8_MSG_DELAY_100NANO:  /* delay arg_int * 100 ns */
        __NOP();
        break;

    case U8X8_MSG_DELAY_10MICRO:  /* delay arg_int * 10 us */
        for (uint16_t n = 0; n < 320; n++)
        {
            __NOP();
        }
        break;

    case U8X8_MSG_DELAY_MILLI:    /* delay arg_int * 1 ms */
        HAL_Delay(1);
        break;

    case U8X8_MSG_DELAY_I2C:      /* I2C bus delay (~5us at 100KHz) */
        for (uint32_t i = 0; i < 40; i++)
        {
            __NOP();
        }
        break;

    case U8X8_MSG_GPIO_I2C_CLOCK: /* software I2C clock (unused, HW I2C) */
        break;

    case U8X8_MSG_GPIO_I2C_DATA:  /* software I2C data (unused, HW I2C) */
        break;

    case U8X8_MSG_GPIO_MENU_SELECT:
        u8x8_SetGPIOResult(u8x8, 0);
        break;

    case U8X8_MSG_GPIO_MENU_NEXT:
        u8x8_SetGPIOResult(u8x8, 0);
        break;

    case U8X8_MSG_GPIO_MENU_PREV:
        u8x8_SetGPIOResult(u8x8, 0);
        break;

    case U8X8_MSG_GPIO_MENU_HOME:
        u8x8_SetGPIOResult(u8x8, 0);
        break;

    default:
        u8x8_SetGPIOResult(u8x8, 1);
        break;
    }

    return 1;
}

/* -------------------------------------------------------------------------- */
/*                          Public OLED API                                   */
/* -------------------------------------------------------------------------- */

/**
  * @brief  Initialize the SSD1306 OLED via u8g2
  * @retval None
  */
void OLED_Init(void)
{
    u8g2_Setup_ssd1306_i2c_128x64_noname_f(&u8g2, U8G2_R2,
                                            u8x8_byte_hw_i2c, u8x8_gpio_and_delay);
    u8g2_InitDisplay(&u8g2);
    u8g2_SetPowerSave(&u8g2, 0);             /* Wake up display */
    u8g2_SetFont(&u8g2, u8g2_font_6x10_tf);  /* Set 6x10 font (closest to 6x8) */
    u8g2_ClearBuffer(&u8g2);
    u8g2_SendBuffer(&u8g2);
}

/**
  * @brief  Clear the display buffer (call OLED_Refresh to show)
  * @retval None
  */
void OLED_Clear(void)
{
    u8g2_ClearBuffer(&u8g2);
}

/**
  * @brief  Draw string at (x, y) in display buffer (call OLED_Refresh to show)
  * @param  x: pixel X coordinate (0-127)
  * @param  y: pixel Y coordinate of character baseline
  * @param  str: null-terminated string
  * @retval None
  */
void OLED_ShowString(uint8_t x, uint8_t y, const char *str)
{
    u8g2_DrawStr(&u8g2, x, y, str);
	
}

/**
  * @brief  Send buffer content to physical OLED display
  * @retval None
  */
void OLED_Refresh(void)
{
    u8g2_SendBuffer(&u8g2);
}
