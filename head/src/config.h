#pragma once

#include <Arduino.h>
#include <Adafruit_TCS34725.h>

// ==================== GPIO 引脚分配 ====================
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

// ==================== 运动与尺寸参数 ====================
#define STEPS_PER_MM_CONVEYOR   80.0f   // 输送电机当量 (steps/mm)
#define STEPS_PER_MM_Y          160.0f  // Y 轴丝杆当量 (steps/mm，可按需微调)

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
