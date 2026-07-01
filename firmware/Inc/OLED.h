#ifndef __OLED_H
#define __OLED_H

#include "main.h"

// OLED I2C 地址 (一般是 0x78 或 0x7A，最常见的是 0x78)
#define OLED_ADDRESS 0x78

// 函数声明
void OLED_Write_Cmd(uint8_t cmd);
void OLED_Write_Data(uint8_t data);
void OLED_Init(void);
void OLED_Clear(void);
void OLED_Set_Pos(uint8_t x, uint8_t y);
void OLED_ShowChar(uint8_t x, uint8_t y, uint8_t chr, uint8_t Char_Size);
void OLED_ShowString(uint8_t x, uint8_t y, uint8_t *chr, uint8_t Char_Size);
void OLED_ShowChinese(uint8_t x, uint8_t y, uint8_t no);

#endif
