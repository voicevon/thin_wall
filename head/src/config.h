#pragma once

#include <Arduino.h>
#include <Adafruit_TCS34725.h>

// ==================== 运行模式选择 ====================
// 1 = 仅步进电机测试模式 (跳过未接入的 I2C/限位/串口，让 X 轴电机往复旋转测试)
// 0 = 完整芦笋测长测径状态机模式
#define STEPPER_TEST_ONLY       1

// 1 = YoraHome ESP32 (Gerber 1.3A) 三轴主板引脚
// 0 = 原始通用开发板引脚 (DevKitC 杜邦线方案)
#define USE_YORAHOME_BOARD      1

#if USE_YORAHOME_BOARD
// ==================== YoraHome ESP32 主板 GPIO 引脚分配 ====================
// 输送电机 (插在主板 X 轴驱动座)
#define CONVEYOR_STEP_PIN       12  // X_STEP
#define CONVEYOR_DIR_PIN        14  // X_DIR
#define CONVEYOR_EN_PIN         13  // 全局使能 (低电平有效，X/Y/Z 驱动座并联)

// Y 轴测径电机 (插在主板 Y 轴驱动座)
#define Y_STEP_PIN              26  // Y_STEP
#define Y_DIR_PIN               15  // Y_DIR
#define Y_EN_PIN                13  // 全局使能 (共用)

// I2C 总线
#define I2C0_SDA_PIN            21  // 复用 COOLANT 端子
#define I2C0_SCL_PIN            22  // 复用 SPINDLE_EN 端子
#define I2C1_SDA_PIN            16  // 复用 Z-LIM 端子
#define I2C1_SCL_PIN            17  // 复用 X-LIM 端子

// 补光 LED 与指示灯
#define LED_CTRL_1_PIN          2   // 复用 LASER / PWM 端子
#define LED_CTRL_2_PIN          33  // 复用 Z_DIR 端子
#define STATUS_LED_PIN          2   // 板载测试 LED (与 LASER 同步)

// 传感器与限位信号输入
#define Y_LIMIT_PIN             4   // Y-LIM 物理端子
#define DIAM_LASER_PIN          32  // PROBE 物理端子

// 数据串口 (UART2) - 往下游 body
#define DATA_UART_NUM           2
#define DATA_UART_TX_PIN        27  // 复用 Z_STEP 端子
#define DATA_UART_RX_PIN        35  // 扩展输入端口 RX
#define DATA_UART_BAUD          115200

#else
// ==================== 原始 DevKitC 杜邦线 GPIO 分配 ====================
// I2C 总线
#define I2C0_SDA_PIN            21
#define I2C0_SCL_PIN            22
#define I2C1_SDA_PIN            16
#define I2C1_SCL_PIN            17

// 补光 LED (高电平开启)
#define LED_CTRL_1_PIN          15
#define LED_CTRL_2_PIN          13

// 输送电机 (A4988)
#define CONVEYOR_STEP_PIN       18
#define CONVEYOR_DIR_PIN        19
#define CONVEYOR_EN_PIN         23

// Y 轴测径电机 (A4988)
#define Y_STEP_PIN              25
#define Y_DIR_PIN               26
#define Y_EN_PIN                27

// 传感器与限位信号输入
#define Y_LIMIT_PIN             32  // 低电平触发或高电平，内部上拉
#define DIAM_LASER_PIN          33  // 激光被遮挡输入
#define STATUS_LED_PIN          4

// 数据串口 (UART2) - 往下游 body 双向通信
#define DATA_UART_NUM           2
#define DATA_UART_TX_PIN        14  // 发往后机 TX
#define DATA_UART_RX_PIN        34  // 接收后机状态 RX (仅输入引脚)
#define DATA_UART_BAUD          115200
#endif

// ==================== 运动与尺寸参数 ====================
// 输送电机机械传动参数:
// 同步带轮规格: 5M 齿形, 节距(Pitch) = 5.0mm, 齿数(Teeth) = 18 齿
// 旋转一圈周长(Circumference): 18 * 5.0mm = 90.0mm / rev
// 42 步进电机: 步距角 1.8° -> 200 整步/圈
// 在 A4988 1/16 细分下: 200 * 16 = 3200 steps/rev
// 脉冲当量: 3200 steps / 90.0mm = 35.555556 steps/mm (即 320.0f / 9.0f)
#define CONVEYOR_PULLEY_TEETH       18.0f   // 齿数
#define CONVEYOR_PULLEY_PITCH_MM    5.0f    // 齿距 (mm)
#define CONVEYOR_PULLEY_CIRCUM_MM   (CONVEYOR_PULLEY_TEETH * CONVEYOR_PULLEY_PITCH_MM) // 90.0 mm/rev
#define CONVEYOR_MICROSTEPS         16.0f   // 细分数 (若跳线调为 1/8 则改为 8.0f)
#define STEPS_PER_MM_CONVEYOR       ((200.0f * CONVEYOR_MICROSTEPS) / CONVEYOR_PULLEY_CIRCUM_MM) // 35.555556f steps/mm
#define STEPS_PER_MM_Y              160.0f  // Y 轴丝杆当量 (steps/mm，可按需微调)

#define SENSOR_DISTANCE_MM      50.0f   // 传感器 1 与 传感器 2 物理间距 D (mm)
#define LASER_OFFSET_MM         60.0f   // 测径工位距传感器 2 物理间距 (mm)
#define DIAM_POINT_MM           30.0f   // 测量点距芦笋头部的目标距离 (mm)
#define LASER_SPOT_MM           0.5f    // 激光光斑直径补偿 (mm)
#define EJECT_DISTANCE_MM       50.0f   // 尾部脱离后推进交接距离 (mm)
#define MAX_TRAVEL_MM           600.0f  // 单次最大安全位移 (mm)

// 速度配置 (步进驱动转换)
#define CONVEYOR_SPEED_HIGH_MMPS    100.0f // 高速推进速度 (mm/s)
#define CONVEYOR_SPEED_LOW_MMPS     25.0f  // 低速高精测长速度 (mm/s)
#define CONVEYOR_ACCEL_MMPS2        500.0f // 加减速度 (mm/s^2)

#define Y_SCAN_SPEED_MMPS           40.0f  // Y 轴扫径速度 (mm/s)
#define Y_HOMING_SPEED_MMPS         20.0f  // Y 轴回零速度 (mm/s)
#define Y_MAX_TRAVEL_MM             50.0f  // Y 轴最大扫描行程 (mm)

// ==================== 传感与算法参数 ====================
#define TCS_INTEGRATION_TIME    TCS34725_INTEGRATIONTIME_24MS
#define TCS_GAIN                TCS34725_GAIN_4X
#define COLOR_CONFIRM_SAMPLES   3      // 连续采样确认次数
#define LED_WARMUP_MS           500    // 上电预热毫秒

// 颜色识别初步基准阈值 (归一化 G / (R+G+B) 或 Clear 强度)
// 绿芦笋典型 G 比较高，白根接近灰白，背景黑/空载 Clear 很低
#define BG_CLEAR_THRESHOLD      150    // 低于此值视为背景空载 (无物料)
#define GREEN_RATIO_THRESHOLD   0.38f  // G/(R+G+B) 大于此值视为绿色
#define WHITE_RATIO_THRESHOLD   0.33f  // G/(R+G+B) 低于此值且 Clear 较高视为白根

// ==================== 通信与超时参数 ====================
#define DOWNSTREAM_TIMEOUT_MS   5000   // 等待下游空闲超时 (ms)
#define HEARTBEAT_LOSS_TIMEOUT_MS 250  // 连续无心跳判定超时 (ms)
