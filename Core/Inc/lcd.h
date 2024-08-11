#ifndef _LCD_H_
#define _LCD_H_

#include "stm32h7xx_hal.h"
#include <stdint.h>

extern uint32_t framebufferBG[320 * 240]  __attribute__((section (".lcd")));
extern uint16_t framebufferSEG0[320 * 240]  __attribute__((section (".lcd")));
extern uint16_t framebufferSEG1[320 * 240]  __attribute__((section (".lcd")));

#define GFX_MAX_WIDTH 320

void bufferSwap(LTDC_HandleTypeDef *ltdc, int whichBuffer);

void lcd_init(SPI_HandleTypeDef *spi, LTDC_HandleTypeDef *ltdc);
void lcd_deinit(SPI_HandleTypeDef *spi);
void lcd_backlight_on();
void lcd_backlight_off();
#endif
