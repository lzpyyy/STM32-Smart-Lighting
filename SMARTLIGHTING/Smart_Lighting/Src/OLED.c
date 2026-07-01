#include "oled.h"
#include "i2c.h"         // 引入 CubeMX 生成的 I2C 句柄
#include "Oled_Font.h"   // 引入你的字库文件 (名字如果不对请自行修改)

// 底层写命令函数 (HAL库特供版)
void OLED_Write_Cmd(uint8_t cmd)
{
    // 0x00 代表写入的是命令
    HAL_I2C_Mem_Write(&hi2c1, OLED_ADDRESS, 0x00, 1, &cmd, 1, 100);
}

// 底层写数据函数 (HAL库特供版)
void OLED_Write_Data(uint8_t data)
{
    // 0x40 代表写入的是显存数据
    HAL_I2C_Mem_Write(&hi2c1, OLED_ADDRESS, 0x40, 1, &data, 1, 100);
}

// 设置光标位置 (X: 0~127, Y: 0~7)
void OLED_Set_Pos(uint8_t x, uint8_t y) 
{ 
    OLED_Write_Cmd(0xb0+y);
    OLED_Write_Cmd(((x&0xf0)>>4)|0x10);
    OLED_Write_Cmd((x&0x0f)|0x01);
}   

// 清屏函数 (把屏幕刷成全黑)
void OLED_Clear(void)  
{  
    uint8_t i,n;        
    for(i=0;i<8;i++)  
    {  
        OLED_Write_Cmd(0xb0+i);
        OLED_Write_Cmd(0x00);
        OLED_Write_Cmd(0x10); 
        for(n=0;n<128;n++) OLED_Write_Data(0); 
    } 
}

// 显示一个字符
void OLED_ShowChar(uint8_t x, uint8_t y, uint8_t chr, uint8_t Char_Size)
{      	
    unsigned char c=0,i=0;	
    c = chr - ' ';// 获取字符在字库中的偏移量		
    if(x > 128 - Char_Size / 2) { x = 0; y += 2; } // 换行处理
    
    if(Char_Size == 16)
    {
        OLED_Set_Pos(x, y);	
        // 👇 注意这里：改成了二维数组的写法 [c][i]
        for(i=0; i<8; i++) OLED_Write_Data(OLED_F8x16[c][i]); 
        
        OLED_Set_Pos(x, y+1);
        // 👇 这里也改成了 [c][i+8]
        for(i=0; i<8; i++) OLED_Write_Data(OLED_F8x16[c][i+8]); 
    }
}

// 显示一串字符
void OLED_ShowString(uint8_t x, uint8_t y, uint8_t *chr, uint8_t Char_Size)
{
    unsigned char j=0;
    while (chr[j] != '\0')
    {		
        OLED_ShowChar(x, y, chr[j], Char_Size);
        x += Char_Size / 2; // 光标后移
        if(x > 120) { x = 0; y += 2; }
        j++;
    }
}

// 初始化 OLED 屏幕 (标准的 SSD1306 唤醒流程)
void OLED_Init(void)
{
    HAL_Delay(200); // 等待屏幕上电稳定
    
    OLED_Write_Cmd(0xAE); // 关闭显示
    OLED_Write_Cmd(0x00); // 设置低列地址
    OLED_Write_Cmd(0x10); // 设置高列地址
    OLED_Write_Cmd(0x40); // 设置起始行地址
    OLED_Write_Cmd(0x81); // 设置对比度
    OLED_Write_Cmd(0xCF); 
    OLED_Write_Cmd(0xA1); // 段重定向设置
    OLED_Write_Cmd(0xC8); 
    OLED_Write_Cmd(0xA6); // 正常显示
    OLED_Write_Cmd(0xA8); // 多路复用率
    OLED_Write_Cmd(0x3F); 
    OLED_Write_Cmd(0xD3); // 设置显示偏移
    OLED_Write_Cmd(0x00); 
    OLED_Write_Cmd(0xd5); // 设置振荡器频率
    OLED_Write_Cmd(0x80); 
    OLED_Write_Cmd(0xD9); // 设置预充电周期
    OLED_Write_Cmd(0xF1); 
    OLED_Write_Cmd(0xDA); // 设置 COM 硬件引脚配置
    OLED_Write_Cmd(0x12); 
    OLED_Write_Cmd(0xDB); // 设置 VCOMH
    OLED_Write_Cmd(0x40); 
    OLED_Write_Cmd(0x20); // 设置内存寻址模式
    OLED_Write_Cmd(0x02); // 页寻址模式
    OLED_Write_Cmd(0x8D); // 电荷泵设置
    OLED_Write_Cmd(0x14); // 开启电荷泵
    OLED_Write_Cmd(0xAF); // 开启显示
    OLED_Clear();
}


// ======================================================
// 🌟 终极纯净版：OLED 中文点阵显示 (匹配标准前16后16数据)
// ======================================================
void OLED_ShowChinese(uint8_t x, uint8_t y, uint8_t no)
{      
    uint8_t t;
    
    // 1. 画汉字的上半截 (顺序读取前 16 个字节)
    OLED_Set_Pos(x, y);    
    for(t = 0; t < 16; t++)
    {
        OLED_Write_Data(Hzk[no][t]);    
    }
    
    // 2. 画汉字的下半截 (顺序读取后 16 个字节)
    OLED_Set_Pos(x, y + 1);    
    for(t = 0; t < 16; t++)
    {
        OLED_Write_Data(Hzk[no][t + 16]);   
    }
}
