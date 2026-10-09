#pragma once

#include <Arduino.h>

// ==================== GPIO 引脚规划 ====================
// 上游串口 (UART2) - 连接前机
#define UART_UP_NUM             2
#define UART_UP_RX_PIN          16  // 接收前机数据
#define UART_UP_TX_PIN          17  // 向前机报状态心跳

// 下游串口 (UART1) - 连接后机
#define UART_DOWN_NUM           1
#define UART_DOWN_TX_PIN        25  // 向后机转发数据
#define UART_DOWN_RX_PIN        26  // 监听后机状态心跳

// 传送带步进电机 (A4988)
#define CONVEYOR_STEP_PIN       18
#define CONVEYOR_DIR_PIN        19
#define CONVEYOR_EN_PIN         23

// 传感与执行器
#define ENTRY_SENSOR_PIN        32  // 入口光电传感器 (遮挡低电平)
#define DIVERTER_PIN            27  // 分流执行器输出 (高电平有效)
#define STATUS_LED_PIN          2   // 板载/状态指示灯

// 节点编号 4 位拨码开关 (低电平表示拨到 ON)
#define NODE_ID_PIN_0           21
#define NODE_ID_PIN_1           22
#define NODE_ID_PIN_2           4
#define NODE_ID_PIN_3           33

// ==================== 运动与尺寸参数 ====================
#define STEPS_PER_MM_CONVEYOR   80.0f   // 本节传送带当量 (steps/mm)
#define CONVEYOR_SPEED_MMPS     60.0f   // 运转线速度 (mm/s)
#define CONVEYOR_ACCEL_MMPS2    500.0f  // 加速度 (mm/s^2)

#define DIVERTER_OFFSET_MM      120.0f  // 入口光电到分流机构位移 (mm)
#define HANDOFF_DISTANCE_MM     150.0f  // 向下游车厢推送料交接位移 (mm)
#define DIVERTER_HOLD_MS        400     // 分流执行器动作持续时间 (ms)

// ==================== 通信与定时参数 ====================
#define CHAIN_BAUD              115200  // 级联通信波特率
#define HEARTBEAT_INTERVAL_MS   50      // 向上游报告心跳间隔 (ms)
#define DOWNSTREAM_TIMEOUT_MS   5000    // 下游空闲等待超时 (ms)
#define HEARTBEAT_LOSS_TIMEOUT_MS 250   // 心跳掉线判定阈值 (ms)
#define ENTRY_TIMEOUT_MS        3000    // 上游交接后等待物料到达入口的最大超时 (ms)
