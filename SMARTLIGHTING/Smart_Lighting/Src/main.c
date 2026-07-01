/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : 智能灯控系统主程序
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "adc.h"
#include "i2c.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include "oled.h"
#include <string.h>  
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define AUTO_OFF_DELAY  10000     //自动关灯延时时间
#define LIGHT_DARK_THRESHOLD 1500 // 光线判暗阈值
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
uint16_t light_value = 0;  // 光敏传感器ADC采集值 (0-4095)
uint16_t smoke_value = 0;  // 烟雾传感器ADC采集值 (0-4095)
uint8_t human_state = 0;   // 人体红外传感器状态 (0:无人, 1:有人)
uint8_t sound_state = 0;   // 声音传感器状态 (0:无声, 1:有声)

uint8_t cloud_light_on = 0; // 照明控制模式标志位 (0:自动控制, 1:手动控制)
uint8_t need_reply = 0;     // 云端指令响应标志位 (0:空闲, 1:需要发送回执)
char reply_id[20] = {0};    // 暂存云端下发的流水号ID，用于匹配回执

volatile uint8_t exti_short_press = 0; // 物理按键短按标志位
volatile uint8_t exti_long_press = 0;  // 物理按键长按标志位
uint32_t exti_down_time = 0;           // 记录按键按下的系统时间戳

uint32_t auto_light_off_time = 0; // 自动关灯的时间节点记录

volatile uint8_t need_reconnect = 0; // 网络断线重连标志位 (0:正常, 1:触发重连流程)
volatile uint8_t is_connecting = 0;  // 网络连接状态标志位 (0:空闲/已连接, 1:正在连接中)

uint32_t last_heartbeat_tick = 0;    // 上次发送心跳包的时间戳
volatile uint8_t error_count = 0;    // 通信错误容错计数器

// AT指令响应标志位，用于提前结束等待，加快指令执行速度
volatile uint8_t at_cmd_ok = 0; 
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
void System_Core_Task(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* --- 串口接收缓冲区定义 --- */
uint8_t rx_data;          
char rx_buffer[512];      
uint16_t rx_index = 0;    

/* --- 串口接收中断回调函数 --- */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2) 
    {
        rx_buffer[rx_index++] = rx_data; 
        if (rx_index >= 512) rx_index = 0; // 防止缓冲区溢出

        if (rx_data == '\n') 
        {
            rx_buffer[rx_index] = '\0'; // 接收到换行符，完成一帧数据接收

            // 1. 解析云端下发的继电器开关指令
            if (strstr(rx_buffer, "\"relay\":true") != NULL) 
            {
                cloud_light_on = 1; 
                HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_SET); 
            }
            else if (strstr(rx_buffer, "\"relay\":false") != NULL) 
            {
                cloud_light_on = 0; 
                HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_RESET); 
            }

            // 2. 提取云端指令流水号ID，准备发送执行回执
            char *id_ptr = strstr(rx_buffer, "\"id\":\"");
            if (id_ptr != NULL) 
            {
                id_ptr += 6; 
                int i = 0;
                while (*id_ptr != '\"' && i < 19 && *id_ptr != '\0') {
                    reply_id[i++] = *id_ptr++;
                }
                reply_id[i] = '\0'; 
                need_reply = 1;     
            }

            // 3. 检测指令执行成功标志位 (OK 或 获取到IP)，用于跳出延时阻塞
            if (strstr(rx_buffer, "OK") != NULL || strstr(rx_buffer, "GOT IP") != NULL) {
                at_cmd_ok = 1; 
            }

            // 4. 网络异常监测与容错机制 (仅在正常运行状态下检测)
            if (is_connecting == 0) 
            {
                // 检测到明确的断开连接提示，立即触发重连
                if (strstr(rx_buffer, "DISCONNECT") != NULL || strstr(rx_buffer, "CLOSED") != NULL) 
                {
                    need_reconnect = 1; 
                    error_count = 0;
                }
                // 检测到通信错误，进行容错计数累计
                else if (strstr(rx_buffer, "ERROR") != NULL) 
                {
                    error_count++;
                    // 连续3次错误则判定网络断开，避免偶发卡顿导致频繁重连
                    if (error_count >= 3) {  
                        need_reconnect = 1;
                        error_count = 0;
                    }
                }
                // 收到正常回复，清零通信错误计数
                else if (strstr(rx_buffer, "OK") != NULL)
                {
                    error_count = 0;
                }
            }

            // 清理缓冲区，准备接收下一帧数据
            rx_index = 0; 
            memset(rx_buffer, 0, 512);
        }
        // 重新开启中断接收
        HAL_UART_Receive_IT(&huart2, &rx_data, 1);
    }
}

/* --- 发送WiFi模块AT指令 --- */
void WiFi_SendData(char *cmd) {
    HAL_UART_Transmit(&huart2, (uint8_t *)cmd, strlen(cmd), 100); 
}

/*开机初始化网络配置流程*/
void WiFi_Init_Boot(void) {
    is_connecting = 1; // 标记系统正在连接网络
    // OLED 显示启动画面
    OLED_Clear();
    OLED_ShowString(0, 0, (uint8_t *)"Booting...", 16); 
    OLED_ShowString(0, 2, (uint8_t *)"WiFi connecting", 16);
    // 延时等待 ESP8266 模块内部系统启动完成
    for(int i=0; i<10; i++) { HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13); HAL_Delay(200); }
    // 配置为 Station 模式
    rx_index = 0; memset(rx_buffer, 0, 512);
    at_cmd_ok = 0; WiFi_SendData("AT+CWMODE=1\r\n");
    for(int i=0; i<3; i++) { HAL_Delay(200); if(at_cmd_ok) break; } 
    // 连接指定的 WiFi 热点
    rx_index = 0; memset(rx_buffer, 0, 512);
    at_cmd_ok = 0; WiFi_SendData("AT+CWJAP=\"Xiaomi 17 Pro Max\",\"qwqwqwe3322\"\r\n");
    for(int i=0; i<50; i++) { HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13); HAL_Delay(200); if(at_cmd_ok) break; }
    // 如果连接 WiFi 热点失败，跳出初始化流程，由主循环的重连机制接管
    if (at_cmd_ok == 0) {
        need_reconnect = 1; 
        is_connecting = 1;
        rx_index = 0; memset(rx_buffer, 0, 512);
        OLED_Clear(); 
        return; 
    }
    // WiFi 连接成功，继续配置 MQTT 云端连接
    rx_index = 0; memset(rx_buffer, 0, 512);
    at_cmd_ok = 0; WiFi_SendData("AT+MQTTCLEAN=0\r\n");
    for(int i=0; i<4; i++) { HAL_Delay(250); if(at_cmd_ok) break; }  
    // 配置 OneNET 鉴权参数
    rx_index = 0; memset(rx_buffer, 0, 512);
    at_cmd_ok = 0; WiFi_SendData("AT+MQTTUSERCFG=0,1,\"lzp_stm32\",\"mXiyma6Eg3\",\"version=2018-10-31&res=products%2FmXiyma6Eg3%2Fdevices%2Flzp_stm32&et=1893456000&method=md5&sign=J7ErFPY6lxl7INeQKbVtxQ%3D%3D\",0,0,\"\"\r\n");
    for(int i=0; i<4; i++) { HAL_Delay(250); if(at_cmd_ok) break; }  
    // 发起云端 MQTT 连接
    rx_index = 0; memset(rx_buffer, 0, 512);
    at_cmd_ok = 0; WiFi_SendData("AT+MQTTCONN=0,\"mqtts.heclouds.com\",1883,0\r\n");
    for(int i=0; i<20; i++) { HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13); HAL_Delay(250); if(at_cmd_ok) break; }  
    // 订阅云端控制 Topic
    rx_index = 0; memset(rx_buffer, 0, 512);
    at_cmd_ok = 0; WiFi_SendData("AT+MQTTSUB=0,\"$sys/mXiyma6Eg3/lzp_stm32/thing/property/set\",0\r\n");
    for(int i=0; i<8; i++) { HAL_Delay(250); if(at_cmd_ok) break; }
    // 检查网络初始化最终结果
    if (at_cmd_ok == 1) {
        need_reconnect = 0; 
        is_connecting = 0; // 初始化成功，恢复常规运行状态
        error_count = 0;
    } else {
        need_reconnect = 1; 
        is_connecting = 1; // 初始化失败，维持连接状态并触发重连
    }
    rx_index = 0; memset(rx_buffer, 0, 512);
    OLED_Clear(); 
}
/*系统核心任务调度：传感器读取、逻辑控制与UI刷新*/
void System_Core_Task(void) {
    // 1. 物理按键事件处理 (保证在阻塞重连期间依然有效响应)
    if (exti_short_press == 1) 
    {
        exti_short_press = 0; 
        cloud_light_on = !cloud_light_on; // 切换工作模式
        if (cloud_light_on == 1) HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_SET);  
        else                     HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_RESET); 
        
        // 蜂鸣器短音反馈
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_RESET);
        HAL_Delay(50);
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_SET);
    }
    
    if (exti_long_press == 1) 
    {
        exti_long_press = 0; 
        // 蜂鸣器长音反馈
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_RESET);
        HAL_Delay(500);
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_SET);
        need_reconnect = 1; // 触发网络重新配网流程
    }

    // 2. 系统状态指示灯控制 (PC13)
    if (is_connecting == 1) {
        // 网络连接中：快速闪烁
        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
    } else if (cloud_light_on == 1) {
        // 手动模式：常亮 (注意：低电平 RESET 才是亮)
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET); 
    } else {
        // 自动模式：心跳慢闪 (短促闪亮100ms，熄灭1900ms)
        uint32_t cycle_time = HAL_GetTick() % 2000; 
        if (cycle_time < 100) HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET); // 亮
        else                  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);   // 灭
    }
    
    // 3. 读取环境光照与烟雾浓度 (ADC采集)
    ADC_ChannelConfTypeDef sConfig = {0};
    sConfig.Channel = ADC_CHANNEL_0; 
    sConfig.Rank = 1;
    sConfig.SamplingTime = ADC_SAMPLETIME_55CYCLES_5;
    HAL_ADC_ConfigChannel(&hadc1, &sConfig);
    HAL_ADC_Start(&hadc1);
    HAL_ADC_PollForConversion(&hadc1, 10);
    light_value = 4095 - HAL_ADC_GetValue(&hadc1);
    HAL_ADC_Stop(&hadc1);
    
    sConfig.Channel = ADC_CHANNEL_4; 
    HAL_ADC_ConfigChannel(&hadc1, &sConfig);
    HAL_ADC_Start(&hadc1);
    HAL_ADC_PollForConversion(&hadc1, 10);
    smoke_value = HAL_ADC_GetValue(&hadc1);
    HAL_ADC_Stop(&hadc1);

    // 4. 读取人体红外与声音传感器状态 (GPIO)
    human_state = HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_1); 

    // 连续多次采样以提高声音传感器的捕获率
    static uint32_t sound_last_time = 0; 
    for(int i = 0; i < 50; i++) {
        if(HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_12) == GPIO_PIN_RESET) {
            sound_last_time = HAL_GetTick(); 
            break; 
        }
        HAL_Delay(1); 
    }
    // 维持声音状态 2 秒钟，防止状态闪烁
    if(HAL_GetTick() - sound_last_time < 2000) sound_state = 1; 
    else sound_state = 0; 
        
    // 5. OLED 界面数据显示
    char str_val[10]; 

    OLED_ShowChinese(0,  0, 0); // "光"
    OLED_ShowChinese(16, 0, 1); // "照"
    OLED_ShowString(32,  0, (uint8_t *)":", 16); 
    sprintf(str_val, "%04d", light_value); 
    OLED_ShowString(48,  0, (uint8_t *)str_val, 16); 
    
    // 根据网络状态和工作模式显示右上角状态角标
    if (is_connecting == 1) {
        OLED_ShowString(96, 0, (uint8_t *)"Conn", 16); 
    } else if(cloud_light_on == 0) {
        OLED_ShowString(96, 0, (uint8_t *)"Auto", 16); 
    } else {
        OLED_ShowString(96, 0, (uint8_t *)"Manu", 16); 
    }

    OLED_ShowChinese(0,  2, 2); // "烟"
    OLED_ShowChinese(16, 2, 3); // "雾"
    OLED_ShowString(32,  2, (uint8_t *)":", 16); 
    sprintf(str_val, "%04d", smoke_value);
    OLED_ShowString(48,  2, (uint8_t *)str_val, 16);

    OLED_ShowChinese(0,  4, 4); // "人"
    OLED_ShowChinese(16, 4, 5); // "员"
    OLED_ShowString(32,  4, (uint8_t *)":", 16); 
    if(human_state == 1) OLED_ShowChinese(48, 4, 8); // "有"
    else                 OLED_ShowChinese(48, 4, 9); // "无"

    OLED_ShowChinese(0,  6, 6); // "声"
    OLED_ShowChinese(16, 6, 7); // "音"
    OLED_ShowString(32,  6, (uint8_t *)":", 16); 
    if(sound_state == 1) OLED_ShowChinese(48, 6, 8); // "有"
    else                 OLED_ShowChinese(48, 6, 9); // "无"
        
    // 6. 自动化控制逻辑与报警触发
    if (smoke_value > 2500) 
    {
        // 触发火灾警报：强制开灯并蜂鸣器报警
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_SET); 
        for(int beep = 0; beep < 3; beep++) {
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_RESET); 
            HAL_Delay(80);  
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_SET);   
            HAL_Delay(40);  
        }
        HAL_Delay(150);
    }
    else if (light_value < LIGHT_DARK_THRESHOLD && (human_state == 1 || sound_state == 1)) 
    {
        // 触发自动照明：环境变暗且有人/有声
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_SET); 
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_SET); 
        auto_light_off_time = HAL_GetTick() + AUTO_OFF_DELAY; // 刷新延迟关灯时间戳
    }
    else
    {
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_SET); 
        // 延时自动关灯 (仅在自动模式下生效)
        if (cloud_light_on == 0) 
        {
            if (HAL_GetTick() >= auto_light_off_time) 
            {
                HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_RESET); 
            }
        }
    }
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_ADC1_Init();
  MX_USART1_UART_Init();
  MX_I2C1_Init();
  MX_USART2_UART_Init();
  /* USER CODE BEGIN 2 */
  OLED_Init();       
  OLED_Clear();      
  // 开启串口接收中断
  HAL_UART_Receive_IT(&huart2, &rx_data, 1);
  // 上电硬件自检提示音
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_RESET); 
  HAL_Delay(200);                                       
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_SET);   
  // 执行开机网络初始化
  WiFi_Init_Boot();
  // 网络连接成功提示音
  if (at_cmd_ok == 1) {
      HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_RESET); 
      HAL_Delay(100);                                       
      HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_SET); 
  }
  // 初始化心跳包时间基准
  last_heartbeat_tick = HAL_GetTick();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
        // 串口溢出错误(ORE)恢复机制：防止大量数据并发导致中断死锁
        if(__HAL_UART_GET_FLAG(&huart2, UART_FLAG_ORE) != RESET) {
            __HAL_UART_CLEAR_OREFLAG(&huart2);
            HAL_UART_Receive_IT(&huart2, &rx_data, 1); 
        }
        // 1. 调度执行系统核心任务
        System_Core_Task(); 
        // 2. 网络断线重连处理任务
        if (need_reconnect == 1) 
        {
            is_connecting = 1;  
            // OLED 屏幕显示断线重连提示 
            OLED_Clear();
            OLED_ShowString(0, 0, (uint8_t *)"Net Lost!", 16);
            OLED_ShowString(0, 2, (uint8_t *)"Reconnecting...", 16);
            // 蜂鸣器短促报警两次提示网络断开
            for(int i=0; i<2; i++) {
                HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_RESET);
                HAL_Delay(200);
                HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_SET);
                HAL_Delay(100);
            }
            HAL_Delay(500); 
            OLED_Clear(); // 提示结束，返回主界面运行态
            // 执行网络重连指令序列 (加入系统调度延时以维持界面刷新)
            WiFi_SendData("AT+RST\r\n"); // 模块复位以确保状态清除
            for(int i=0; i<15; i++) { System_Core_Task(); HAL_Delay(200); } 
            rx_index = 0; memset(rx_buffer, 0, 512);
            at_cmd_ok = 0; WiFi_SendData("AT+CWMODE=1\r\n");
            for(int i=0; i<3; i++) { System_Core_Task(); HAL_Delay(200); if(at_cmd_ok) break; }
            rx_index = 0; memset(rx_buffer, 0, 512);
            at_cmd_ok = 0; WiFi_SendData("AT+CWJAP=\"Xiaomi 17 Pro Max\",\"qwqwqwe3322\"\r\n");
            for(int i=0; i<50; i++) { System_Core_Task(); HAL_Delay(200); if(at_cmd_ok) break; } 
            rx_index = 0; memset(rx_buffer, 0, 512);
            at_cmd_ok = 0; WiFi_SendData("AT+MQTTCLEAN=0\r\n");
            for(int i=0; i<4; i++) { System_Core_Task(); HAL_Delay(250); if(at_cmd_ok) break; }
            rx_index = 0; memset(rx_buffer, 0, 512);
            at_cmd_ok = 0; WiFi_SendData("AT+MQTTUSERCFG=0,1,\"lzp_stm32\",\"mXiyma6Eg3\",\"version=2018-10-31&res=products%2FmXiyma6Eg3%2Fdevices%2Flzp_stm32&et=1893456000&method=md5&sign=J7ErFPY6lxl7INeQKbVtxQ%3D%3D\",0,0,\"\"\r\n");
            for(int i=0; i<4; i++) { System_Core_Task(); HAL_Delay(250); if(at_cmd_ok) break; }
            rx_index = 0; memset(rx_buffer, 0, 512);
            at_cmd_ok = 0; WiFi_SendData("AT+MQTTCONN=0,\"mqtts.heclouds.com\",1883,0\r\n");
            for(int i=0; i<20; i++) { System_Core_Task(); HAL_Delay(250); if(at_cmd_ok) break; }
            rx_index = 0; memset(rx_buffer, 0, 512);
            at_cmd_ok = 0; WiFi_SendData("AT+MQTTSUB=0,\"$sys/mXiyma6Eg3/lzp_stm32/thing/property/set\",0\r\n");
            for(int i=0; i<4; i++) { System_Core_Task(); HAL_Delay(250); if(at_cmd_ok) break; }
            // 验证重连结果并恢复系统状态
            if (at_cmd_ok == 1) 
            {
                // 重连成功，蜂鸣器提示并延时稳定
                HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_RESET); 
                HAL_Delay(150);                                       
                HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_SET); 
                for(int i=0; i<2; i++) { System_Core_Task(); HAL_Delay(200); }
                need_reconnect = 0;        
                error_count = 0;           
                is_connecting = 0; // 恢复常规运行网络状态
            } 
            else 
            {
                // 重连失败，维持异常标记以触发下一次重试流程
                need_reconnect = 1; 
                is_connecting = 1;  
                // 防过热保护延时
                for(int i=0; i<15; i++) { System_Core_Task(); HAL_Delay(200); }
            }
            // 清理缓冲区残留数据
            rx_index = 0;
            memset(rx_buffer, 0, 512); 
            last_heartbeat_tick = HAL_GetTick(); // 重置心跳包时间基准，防止刚连上就超时
        }               

        // 3. 云端通信任务：回执响应与心跳数据上报
        if (need_reply == 1)
        {
            need_reply = 0; 
            char reply_cmd[256]; 
            char reply_payload[100]; 
            // 组装执行结果回执 JSON
            sprintf(reply_payload, "{\"id\":\"%s\",\"code\":200,\"msg\":\"success\"}", reply_id);
            sprintf(reply_cmd, "AT+MQTTPUBRAW=0,\"$sys/mXiyma6Eg3/lzp_stm32/thing/property/set_reply\",%d,0,0\r\n", strlen(reply_payload));
            
            HAL_UART_Transmit(&huart2, (uint8_t *)reply_cmd, strlen(reply_cmd), 500); 
            HAL_Delay(100);
            HAL_UART_Transmit(&huart2, (uint8_t *)reply_payload, strlen(reply_payload), 500); 
        }
        
        uint32_t current_tick = HAL_GetTick();
            
        // 定时上报传感器数据 (每3秒一次)
        if (current_tick - last_heartbeat_tick >= 3000) 
        {
            // 仅在网络正常运行状态下上报数据
            if (is_connecting == 0) 
            {
                char topic_cmd[256]; 
                char payload[300]; 
                char* relay_state = "false"; 
                if(HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_0) == GPIO_PIN_SET) relay_state = "true"; 
                
                // 组装环境数据上报 JSON
                sprintf(payload, "{\"id\":\"123\",\"version\":\"1.0\",\"params\":{\"light\":{\"value\":%d},\"smoke\":{\"value\":%d},\"human\":{\"value\":%d},\"sound\":{\"value\":%d},\"relay\":{\"value\":%s}}}", 
                        light_value, smoke_value, human_state, sound_state, relay_state);
                sprintf(topic_cmd, "AT+MQTTPUBRAW=0,\"$sys/mXiyma6Eg3/lzp_stm32/thing/property/post\",%d,0,0\r\n", strlen(payload));
                
                HAL_UART_Transmit(&huart2, (uint8_t *)topic_cmd, strlen(topic_cmd), 500); 
                HAL_Delay(100);
                HAL_UART_Transmit(&huart2, (uint8_t *)payload, strlen(payload), 500); 
            }
            last_heartbeat_tick = current_tick; 
        }

  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Initializes the CPU, AHB and APB busses clocks 
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }
  /** Initializes the CPU, AHB and APB busses clocks 
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_ADC;
  PeriphClkInit.AdcClockSelection = RCC_ADCPCLK2_DIV6;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* --- 重定向 printf 到串口 --- */
int fputc(int ch, FILE *f)
{
  HAL_UART_Transmit(&huart1, (uint8_t *)&ch, 1, 1000);
  return ch;
}

/*外部中断处理回调函数 (物理按键响应)*/
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if(GPIO_Pin == GPIO_PIN_5) 
    {
        // 捕获下降沿，记录按键按下时间戳
        if(HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_5) == GPIO_PIN_RESET) 
        {
            exti_down_time = HAL_GetTick(); 
        } 
        // 捕获上升沿，计算按键保持时间以区分长短按
        else 
        {
            uint32_t duration = HAL_GetTick() - exti_down_time; 
            if(duration > 2000) exti_long_press = 1;      // 按压超过 2 秒判定为长按
            else if(duration > 15) exti_short_press = 1;  // 大于 15ms 判定为有效短按(已消抖)
        }
    }
}
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */

  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  * where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{ 
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     tex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
