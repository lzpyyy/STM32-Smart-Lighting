# 基于STM32的小区楼道照明系统

## 📋 项目简介

本系统以 **STM32F103C8T6** 为主控芯片，实现小区楼道的智能照明控制。系统通过多种传感器采集环境数据，结合WiFi模块实现云端远程控制，达到节能、智能、安全的楼道照明管理目标。

## ✨ 功能特点

- **多传感器融合**：集成光敏传感器、烟雾传感器、人体红外传感器、声音传感器
- **双模式控制**：支持自动感应控制和云端手动远程控制两种模式
- **OLED实时显示**：通过I2C接口驱动OLED屏幕，实时显示传感器数据与系统状态
- **WiFi联网通信**：基于AT指令的WiFi模块，支持心跳包保活、断线自动重连
- **物理按键交互**：支持短按/长按物理按键，实现本地控制
- **烟雾报警**：实时监测烟雾浓度，异常时触发报警
- **自动节能**：无人时自动延时关灯，有效节约能源

## 🛠️ 技术栈

| 类别 | 技术 |
|------|------|
| 主控芯片 | STM32F103C8T6（ARM Cortex-M3） |
| 开发环境 | Keil MDK-ARM + STM32CubeMX |
| HAL库 | STM32F1xx HAL Driver |
| 传感器 | 光敏(ADC)、烟雾(ADC)、人体红外(GPIO)、声音(GPIO) |
| 显示模块 | OLED 0.96寸（I2C，SSD1306） |
| 通信模块 | WiFi（AT指令，USART） |
| 移动端 | Android APP（APK） |
| PCB设计 | 立创EDA 专业版 |

## 📁 项目结构

`
基于STM32的小区楼道照明系统/
├── SMARTLIGHTING/                    # 立创EDA项目导出目录
│   └── Smart_Lighting/              # Keil MDK 嵌入式代码
│       ├── Drivers/                 # 驱动库
│       │   ├── CMSIS/              # ARM Cortex-M 核心支持
│       │   └── STM32F1xx_HAL_Driver/  # STM32 HAL 库
│       ├── Inc/                     # 头文件
│       ├── Src/                     # 源代码
│       │   ├── main.c              # 主程序（传感器采集/云端通信/控制逻辑）
│       │   ├── adc.c               # ADC驱动（光敏+烟雾）
│       │   ├── gpio.c              # GPIO驱动（人体红外+声音+按键）
│       │   ├── i2c.c               # I2C驱动
│       │   ├── usart.c             # 串口驱动（WiFi AT通信）
│       │   ├── OLED.c / OLED.h     # OLED显示驱动
│       │   └── stm32f1xx_it.c     # 中断服务函数
│       ├── MDK-ARM/                # Keil 工程文件
│       │   ├── Smart_Lighting.uvprojx   # Keil 工程
│       │   └── startup_stm32f103xb.s   # 启动文件
│       └── Smart_Lighting.ioc      # STM32CubeMX 配置
├── lzp_SmartControl.apk            # 安卓控制APP
└── 基于STM32的小区楼道照明系统.eprj2  # 立创EDA工程（原理图+PCB）
`

## 🚀 快速开始

### 环境要求

- **IDE**：Keil MDK-ARM 5.x
- **芯片包**：STM32F1xx DFP
- **配置工具**：STM32CubeMX（可选，用于修改引脚配置）
- **烧录工具**：ST-Link / J-Link / USB-TTL串口下载

### 编译与烧录

1. 使用 Keil MDK 打开 SMARTLIGHTING\Smart_Lighting\MDK-ARM\Smart_Lighting.uvprojx
2. 点击 **Build**（F7）编译工程
3. 使用 ST-Link 连接开发板，点击 **Download**（F8）烧录

### 硬件连接

| 外设 | STM32引脚 | 说明 |
|------|-----------|------|
| OLED SDA | PB7 | I2C数据线 |
| OLED SCL | PB6 | I2C时钟线 |
| 光敏传感器 | PA0 | ADC通道0 |
| 烟雾传感器 | PA1 | ADC通道1 |
| 人体红外 | PB3 | GPIO输入 |
| 声音传感器 | PB4 | GPIO输入 |
| 物理按键 | PB5 | 外部中断 |
| WiFi TX | PA9 | USART1 TX |
| WiFi RX | PA10 | USART1 RX |

### APP安装

将 lzp_SmartControl.apk 安装到Android手机即可远程控制照明系统。

## 📝 主要控制逻辑

- **自动模式**：检测到有人且光线暗时自动开灯，无人后延时10秒关灯
- **手动模式**：通过云端指令或物理按键强制开关灯
- **烟雾检测**：实时监测烟雾浓度，超标时通过OLED显示报警并上传云端
- **心跳保活**：定时发送心跳包维持云端连接，断线自动重连


## 📄 开源协议

本项目仅用于毕业设计展示与学习交流。
