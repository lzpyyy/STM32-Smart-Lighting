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

```
├── firmware/                        # 嵌入式固件代码（Keil MDK）
│   ├── Drivers/                     # 驱动库（CMSIS + HAL）
│   ├── Inc/                         # 头文件
│   ├── Src/                         # 源代码
│   ├── MDK-ARM/                    # Keil 工程文件
│   └── Smart_Lighting.ioc          # CubeMX 配置
├── hardware/                        # PCB 设计
│   └── *.eprj2                      # 立创EDA 工程（原理图+PCB）
├── app/                             # 移动端
│   └── lzp_SmartControl.apk        # 安卓控制 APP
├── README.md
└── .gitignore
```

## 🚀 快速开始

### 环境要求

- **IDE**：Keil MDK-ARM 5.x
- **芯片包**：STM32F1xx DFP
- **配置工具**：STM32CubeMX（可选，用于修改引脚配置）
- **烧录工具**：ST-Link / J-Link / USB-TTL串口下载

### 编译与烧录

1. 使用 Keil MDK 打开 `firmware\MDK-ARM\Smart_Lighting.uvprojx`
2. 点击 **Build**（F7）编译工程
3. 使用 ST-Link 连接开发板，点击 **Download**（F8）烧录

### 可选：启用 Wi-Fi / OneNET

默认固件不联网，保留本地照明和传感器功能。需要联网时：

1. 将 `firmware/Inc/network_config.local.example.h` 复制为同目录下的 `network_config.local.h`。
2. 在本地文件填入 Wi-Fi 名称、密码，以及 OneNET 设备名、产品 ID、新令牌和 MQTT 服务器地址。
3. 在 Keil 的 **Options for Target → C/C++ → Define** 中追加 `LIGHTING_USE_LOCAL_NETWORK_CONFIG`，保留现有宏。重新编译并烧录。此宏及本地配置不用提交。
4. 不要提交本地配置，也不要发布用真实凭据编译出的固件。分享演示版时，去掉该宏再重新编译。

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

旧版 `app/lzp_SmartControl.apk` 使用预置的云端配置，不建议继续安装或分发。此处没有可重新配置并构建的 Android 源码。

## 📝 主要控制逻辑

- **自动模式**：检测到有人且光线暗时自动开灯，无人后延时10秒关灯
- **手动模式**：通过云端指令或物理按键强制开关灯
- **烟雾检测**：实时监测烟雾浓度，超标时通过OLED显示报警并上传云端
- **心跳保活**：定时发送心跳包维持云端连接，断线自动重连

## 📄 开源协议

本项目仅用于毕业设计展示与学习交流。