#pragma once

#include <Arduino.h>
#include <Adafruit_TCS34725.h>

// ==================== 运行模式与网络配置 ====================
// 0 = 完整芦笋检测与进料状态机模式 (带下游背压互锁)
// 1 = 独立单机调试模式 (忽略下游握手，直接推进交接，且串口持续输出检测日志)
#define STANDALONE_DEBUG_MODE       0

// WiFi 与 Web 实时示波器配置
#define WIFI_SSID                   "Perfect"
#define WIFI_PASSWORD               "12344321"
#define WEB_SERVER_PORT             80
#define WS_SERVER_PORT              81
#define SAMPLE_INTERVAL_MS          200     // 5Hz 采样与推流周期 (200ms)

// ==================== ESP32 GPIO 引脚分配 ====================
// 按键控制 (ESP32 板载 BOOT 按键，按下为低电平)
#define BUTTON_PIN                  0

// I2C 颜色传感器 (硬件 I2C_0)
#define I2C_SDA_PIN                 21
#define I2C_SCL_PIN                 19

// 补光 LED 控制 (高电平使能常亮)
#define LED_CTRL_PIN                18

// 直流电机 H 桥控制 (TB6612FNG / DRV8833: IN1 & IN2)
#define MOTOR_IN1_PIN               12
#define MOTOR_IN2_PIN               12

// 下行通信串口 (UART2: 与 downstream body_1 通信)
#define DATA_UART_NUM               2
#define DATA_UART_TX_PIN            17  // 发往 body_1 RX
#define DATA_UART_RX_PIN            16  // 接收 body_1 状态心跳 TX
#define DATA_UART_BAUD              115200

// ==================== 直流电机 PWM 配置 ====================
#define MOTOR_PWM_FREQ              20000   // 20kHz 避开可闻噪声
#define MOTOR_PWM_RES_BITS          8       // 8位分辨率 (0 ~ 255)
#define MOTOR_PWM_CH_IN1            0       // ESP32 LEDC 通道 0
#define MOTOR_PWM_CH_IN2            1       // ESP32 LEDC 通道 1

#define MOTOR_SPEED_DEFAULT         200     // 默认输送调速值 (0~255)
#define MOTOR_SPEED_HANDOFF         220     // 交接推料速度 (可稍快)

// ==================== 传感与算法参数 ====================
#define TCS_INTEGRATION_TIME        TCS34725_INTEGRATIONTIME_24MS

// TCS34725 硬件放大倍数 (PGA Gain) - 芯片底层支持以下全部 4 个档位：
// - TCS34725_GAIN_1X   :  1倍放大 (强光/直射光环境)
// - TCS34725_GAIN_4X   :  4倍放大 (标称标准倍数)
// - TCS34725_GAIN_16X  : 16倍放大 (高灵敏度，当前推荐，比原先4X高4倍)
// - TCS34725_GAIN_60X  : 60倍放大 (芯片物理最大放大倍数，适合暗光弱信号)
#define TCS_GAIN                    TCS34725_GAIN_60X
// #define TCS_GAIN                 TCS34725_GAIN_60X
// #define TCS_GAIN                 TCS34725_GAIN_4X
// #define TCS_GAIN                 TCS34725_GAIN_1X

#define COLOR_CONFIRM_SAMPLES       3       // 状态切换连续确认采样次数
#define LED_WARMUP_MS               500     // 上电后等待补光稳定的毫秒数

// 颜色识别初步基准阈值 (Clear 强度与色度比例)
#define BG_CLEAR_THRESHOLD          150     // Clear 低于此值为空载背景，高于此值判定有物料
#define GREEN_RATIO_THRESHOLD       0.38f   // G/(R+G+B) 大于此值判定为绿色
#define WHITE_RATIO_THRESHOLD       0.33f   // G/(R+G+B) 低于此值判定为白根

// ==================== 时序与超时保护参数 ====================
#define HANDOFF_DURATION_MS         1500    // 尾部离开传感器后，继续推料交接的持续时间 (ms)
#define MAX_RUN_TIME_MS             8000    // 单次最大输送保护时间 (防卡料/空转超时, ms)
#define DOWNSTREAM_TIMEOUT_MS       5000    // 等待下游空闲超时 (ms)
#define HEARTBEAT_LOSS_TIMEOUT_MS   350     // 下游心跳丢失超时判定 (ms)
