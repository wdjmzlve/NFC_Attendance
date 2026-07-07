#ifndef __OLED_H
#define __OLED_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "u8g2.h"

/* Private defines -----------------------------------------------------------*/
#define OLED_ADDRESS  0x78  /* SSD1306 I2C address (SA0=0) */

/* Exported functions --------------------------------------------------------*/

/**
  * @brief  Initialize OLED display (SSD1306 128x64 via I2C1)
  * @note   Must be called after I2C1 is initialized and scheduler started
  * @retval None
  */
void OLED_Init(void);

/**
  * @brief  Clear the display buffer
  * @retval None
  */
void OLED_Clear(void);

/**
  * @brief  Draw a string at specified pixel position
  * @param  x: X coordinate in pixels (0-127)
  * @param  y: Y coordinate in pixels (0-63), baseline of the first character row
  * @param  str: null-terminated string to display
  * @retval None
  */
void OLED_ShowString(uint8_t x, uint8_t y, const char *str);

/**
  * @brief  Send buffer content to the OLED display
  * @retval None
  */
void OLED_Refresh(void);

/**
  * @brief  Draw a bitmap in display-native page format (SSD1306 vertical pages)
  * @param  x: X coordinate (0-127)
  * @param  y: Y coordinate (0-63)
  * @param  w: bitmap width in pixels
  * @param  h: bitmap height in pixels
  * @param  bitmap: pointer to bitmap data
  * @retval None
  */
void OLED_DrawBitmap(uint8_t x, uint8_t y, uint8_t w, uint8_t h, const uint8_t *bitmap);

/**
  * @brief  Set current font for subsequent text drawing
  * @param  font: pointer to u8g2 font data
  * @retval None
  */
void OLED_SetFont(const uint8_t *font);

/**
  * @brief  Draw UTF-8 encoded string at specified position
  * @param  x: X coordinate (0-127)
  * @param  y: Y coordinate of character baseline (0-63)
  * @param  str: null-terminated UTF-8 string
  * @retval None
  */
void OLED_DrawUTF8(uint8_t x, uint8_t y, const char *str);

/* u8g2 callback functions ---------------------------------------------------*/
uint8_t u8x8_byte_hw_i2c(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr);
uint8_t u8x8_gpio_and_delay(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr);

#ifdef __cplusplus
}
#endif

#endif /* __OLED_H */
