#include <Arduino.h>
#include <Wire.h>
#include <AccelStepper.h>
#include "config.h"
#include "protocol.h"

// ==================== 全局外设与变量 ====================
// 电机驱动实例 (使用 Step/Dir)
AccelStepper stepperConveyor(AccelStepper::DRIVER, CONVEYOR_STEP_PIN, CONVEYOR_DIR_PIN);
AccelStepper stepperY(AccelStepper::DRIVER, Y_STEP_PIN, Y_DIR_PIN);

// 颜色传感器实例
Adafruit_TCS34725 tcs1 = Adafruit_TCS34725(TCS_INTEGRATION_TIME, TCS_GAIN);
Adafruit_TCS34725 tcs2 = Adafruit_TCS34725(TCS_INTEGRATION_TIME, TCS_GAIN);

// 状态机枚举
// 核心状态机枚举 (Head 测量火车头工作状态)
enum HeadState {
    STATE_Y_HOMING = 0,     // Y 轴测径电机回零归位 (上电复位)
    STATE_IDLE,             // 待机空闲：输送静止，等待放入芦笋
    STATE_FAST_ADV,         // 高速推进：传感器 1 侦测到进料，输送电机高速向前推进
    STATE_SLOW_SCAN,        // 低速精测：传感器 1 预警白根，平滑降速供传感器 2 高精捕捉分界点
    STATE_Y_MEASURE,        // 激光扫径：到达测径工位，输送电机短暂停机，Y 轴横向扫描测外径
    STATE_WAIT_DOWNSTREAM,  // 等待后机空闲：测长完成，输送电机原地暂停，等待下游车厢报告 IDLE
    STATE_EJECT,            // 出料交接：下游空闲，发送测量 JSON 数据并推进固定位移交接芦笋
    STATE_FAULT             // 故障保护：防空转超程或传感器采样异常，停机告警并等待复位
};

HeadState currentState = STATE_Y_HOMING;
HeadState resumeStateAfterMeasure = STATE_FAST_ADV;

// 测量过程数据
static uint32_t currentItemId = 1000;
static long stepP0 = 0;              // 绿头起点步数
static bool p0Recorded = false;
static long stepBoundary = 0;        // 绿白分界步数
static bool boundaryRecorded = false;
static bool diamMeasured = false;
static float measuredGreenLength = 0.0f;
static float measuredDiameter = 0.0f;
static long ejectTargetStep = 0;
static unsigned long stateEntryTime = 0;

// 下游心跳与通信
static NodeState downstreamState = NodeState::UNKNOWN;
static unsigned long lastDownstreamHeartbeatTime = 0;
static char serial2RxBuf[128];
static size_t serial2RxIdx = 0;

// ==================== 辅助传感器函数 ====================
// 颜色类别互斥枚举
enum class ColorType {
    BACKGROUND = 0, // 背景底色 / 空载 (无物料)
    GREEN,          // 绿色嫩茎
    WHITE,          // 白根
    TRANSITION      // 绿白过渡区 / 未定 (落在 GREEN 与 WHITE 阈值之间)
};

struct ColorSample {
    uint16_t r, g, b, c;

    ColorType getType() const {
        if (c < BG_CLEAR_THRESHOLD) {
            return ColorType::BACKGROUND;
        }
        float total = (float)(r + g + b);
        if (total <= 0.0f) return ColorType::BACKGROUND;

        float gRatio = (float)g / total;
        if (gRatio >= GREEN_RATIO_THRESHOLD) {
            return ColorType::GREEN;
        }
        if (gRatio <= WHITE_RATIO_THRESHOLD) {
            return ColorType::WHITE;
        }
        return ColorType::TRANSITION;
    }
};

ColorSample readSensor1() {
    ColorSample s;
    tcs1.getRawData(&s.r, &s.g, &s.b, &s.c);
    return s;
}

ColorSample readSensor2() {
    ColorSample s;
    tcs2.getRawData(&s.r, &s.g, &s.b, &s.c);
    return s;
}

// ==================== 下游串口心跳处理 ====================
void pollDownstreamHeartbeat() {
    while (Serial2.available()) {
        char ch = (char)Serial2.read();
        if (ch == '\n' || ch == '\r') {
            if (serial2RxIdx > 0) {
                serial2RxBuf[serial2RxIdx] = '\0';
                HeartbeatMsg hb;
                if (hb.deserialize(serial2RxBuf)) {
                    downstreamState = hb.state;
                    lastDownstreamHeartbeatTime = millis();
                }
                serial2RxIdx = 0;
            }
        } else if (serial2RxIdx + 1 < sizeof(serial2RxBuf)) {
            serial2RxBuf[serial2RxIdx++] = ch;
        }
    }

    if (millis() - lastDownstreamHeartbeatTime > HEARTBEAT_LOSS_TIMEOUT_MS) {
        downstreamState = NodeState::UNKNOWN;
    }
}

// ==================== Y 轴归零与测径 ====================
void homeYAxis() {
    Serial.println("[Head] Y-Axis Homing started...");
    stepperY.setMaxSpeed(Y_HOMING_SPEED_MMPS * STEPS_PER_MM_Y);
    stepperY.setAcceleration(Y_HOMING_SPEED_MMPS * STEPS_PER_MM_Y * 4.0f);
    
    // 向限位方向缓慢移动
    stepperY.setSpeed(-Y_HOMING_SPEED_MMPS * STEPS_PER_MM_Y);
    unsigned long startHoming = millis();
    while (digitalRead(Y_LIMIT_PIN) != LOW) {
        stepperY.runSpeed();
        if (millis() - startHoming > 10000) {
            Serial.println("[Head] ERROR: Y-Axis Homing Timeout!");
            currentState = STATE_FAULT;
            return;
        }
    }
    stepperY.setCurrentPosition(0);
    // 稍微向正方向退离限位
    stepperY.runToNewPosition((long)(2.0f * STEPS_PER_MM_Y));
    stepperY.setCurrentPosition(0);
    Serial.println("[Head] Y-Axis Homing Complete.");
}

void performYMeasure() {
    Serial.println("[Head] Performing Y-Axis Laser Diameter Scan...");
    stepperY.setMaxSpeed(Y_SCAN_SPEED_MMPS * STEPS_PER_MM_Y);
    stepperY.setAcceleration(Y_SCAN_SPEED_MMPS * STEPS_PER_MM_Y * 4.0f);

    long maxScanSteps = (long)(Y_MAX_TRAVEL_MM * STEPS_PER_MM_Y);
    long stepLaserStart = -1;
    long stepLaserEnd = -1;

    stepperY.moveTo(maxScanSteps);
    while (stepperY.distanceToGo() > 0) {
        stepperY.run();
        bool blocked = (digitalRead(DIAM_LASER_PIN) == LOW); // 假设遮挡为低电平
        if (blocked) {
            if (stepLaserStart < 0) stepLaserStart = stepperY.currentPosition();
            stepLaserEnd = stepperY.currentPosition();
        }
    }

    if (stepLaserStart >= 0 && stepLaserEnd >= stepLaserStart) {
        float rawSteps = (float)(stepLaserEnd - stepLaserStart);
        float rawWidth = rawSteps / STEPS_PER_MM_Y;
        measuredDiameter = rawWidth - LASER_SPOT_MM;
        if (measuredDiameter < 0.0f) measuredDiameter = 0.0f;
        Serial.printf("[Head] Diameter Scan Success: %.2f mm\n", measuredDiameter);
    } else {
        measuredDiameter = 0.0f;
        Serial.println("[Head] Diameter Scan: No object blocked the laser beam.");
    }
    diamMeasured = true;

    // 回零
    stepperY.runToNewPosition(0);
}

// ==================== 初始化 ====================
void setup() {
    Serial.begin(115200);

#if STEPPER_TEST_ONLY
    delay(500);
    Serial.println("\n========================================================");
    Serial.println("  Thin_Wall [Head Unit] - X 轴步进电机单向连续运转测试");
    Serial.println("  硬件载体: YoraHome ESP32 (Gerber 1.3A)");
    Serial.printf("  X_STEP 引脚: GPIO %d,  X_DIR 引脚: GPIO %d\n", CONVEYOR_STEP_PIN, CONVEYOR_DIR_PIN);
    Serial.printf("  全局使能引脚: GPIO %d (低电平使能)\n", CONVEYOR_EN_PIN);
    Serial.printf("  当前传动当量: %.3f steps/mm (5M 18T 同步轮)\n", STEPS_PER_MM_CONVEYOR);
    Serial.println("========================================================");

    // 状态指示灯与全局使能
    pinMode(STATUS_LED_PIN, OUTPUT);
    digitalWrite(STATUS_LED_PIN, LOW);

    pinMode(CONVEYOR_EN_PIN, OUTPUT);
    digitalWrite(CONVEYOR_EN_PIN, LOW); // 低电平使能 A4988 步进驱动

    // 输送电机平滑连续运行参数配置 (500 steps/s^2 平滑加速，约 2 秒升至巡航速度)
    stepperConveyor.setMaxSpeed(1000.0f);     // 稳定巡航速度 steps/s (~28.1 mm/s)
    stepperConveyor.setAcceleration(500.0f);  // 加速度 steps/s^2 (平滑启动防失步)
    stepperConveyor.setCurrentPosition(0);

    // 开机延时 3 秒倒计时
    Serial.println("[开机延时] 等待 3 秒后启动单向连续运转...");
    for (int i = 3; i > 0; i--) {
        Serial.printf("[倒计时] %d ...\n", i);
        digitalWrite(STATUS_LED_PIN, HIGH);
        delay(500);
        digitalWrite(STATUS_LED_PIN, LOW);
        delay(500);
    }
    digitalWrite(STATUS_LED_PIN, HIGH); // 运行期间状态灯常亮

    // 设置远端目标，启动连续单向运动
    stepperConveyor.moveTo(2000000000L);

    Serial.println("[Test Ready] 已启动单向连续运转！正在加速至巡航速度 (1000 steps/s)...");
    Serial.println("[串口指令提示] 's'=暂停, 'r'=恢复连续运转, '+'=加速, '-'=减速, 'd'=切换旋转方向");
    return;
#endif

    Serial.println("\n--- Thin_Wall [Head Unit] Booting ---");

    // 数据串口 (UART2)
    Serial2.begin(DATA_UART_BAUD, SERIAL_8N1, DATA_UART_RX_PIN, DATA_UART_TX_PIN);

    // 引脚模式
    pinMode(LED_CTRL_1_PIN, OUTPUT);
    pinMode(LED_CTRL_2_PIN, OUTPUT);
    pinMode(STATUS_LED_PIN, OUTPUT);
    pinMode(Y_LIMIT_PIN, INPUT_PULLUP);
    pinMode(DIAM_LASER_PIN, INPUT_PULLUP);

    pinMode(CONVEYOR_EN_PIN, OUTPUT);
    pinMode(Y_EN_PIN, OUTPUT);
    digitalWrite(CONVEYOR_EN_PIN, LOW); // 低电平使能 A4988
    digitalWrite(Y_EN_PIN, LOW);

    // 补光灯开启并常亮
    digitalWrite(LED_CTRL_1_PIN, HIGH);
    digitalWrite(LED_CTRL_2_PIN, HIGH);
    digitalWrite(STATUS_LED_PIN, HIGH);
    delay(LED_WARMUP_MS);

    // I2C 颜色传感器初始化
    Wire.begin(I2C0_SDA_PIN, I2C0_SCL_PIN, 400000);
    Wire1.begin(I2C1_SDA_PIN, I2C1_SCL_PIN, 400000);

    if (tcs1.begin(TCS34725_ADDRESS, &Wire)) {
        Serial.println("[Head] Sensor #1 (I2C0) initialized.");
    } else {
        Serial.println("[Head] WARNING: Sensor #1 (I2C0) not detected!");
    }

    if (tcs2.begin(TCS34725_ADDRESS, &Wire1)) {
        Serial.println("[Head] Sensor #2 (I2C1) initialized.");
    } else {
        Serial.println("[Head] WARNING: Sensor #2 (I2C1) not detected!");
    }

    // 输送电机配置
    stepperConveyor.setMaxSpeed(CONVEYOR_SPEED_HIGH_MMPS * STEPS_PER_MM_CONVEYOR);
    stepperConveyor.setAcceleration(CONVEYOR_ACCEL_MMPS2 * STEPS_PER_MM_CONVEYOR);

    // 执行上电回零
    homeYAxis();
    if (currentState != STATE_FAULT) {
        currentState = STATE_IDLE;
        Serial.println("[Head] State -> IDLE. Waiting for asparagus feed.");
    }
}

// ==================== 主循环状态机 ====================
void loop() {
#if STEPPER_TEST_ONLY
    static bool isRunning = true;
    static float currentSpeed = 1000.0f;
    static bool forwardDir = true;
    static unsigned long lastLogTime = 0;

    // 串口交互指令
    if (Serial.available()) {
        char cmd = (char)Serial.read();
        if (cmd == 's' || cmd == 'S') {
            isRunning = false;
            stepperConveyor.stop();
            digitalWrite(STATUS_LED_PIN, LOW);
            Serial.println("[Cmd] 电机已暂停减速停机。发送 'r' 恢复运转。");
        } else if (cmd == 'r' || cmd == 'R') {
            isRunning = true;
            digitalWrite(STATUS_LED_PIN, HIGH);
            stepperConveyor.setMaxSpeed(currentSpeed);
            long target = forwardDir ? 2000000000L : -2000000000L;
            stepperConveyor.moveTo(target);
            Serial.printf("[Cmd] 恢复单向连续运转，目标速度: %.0f steps/s (%.1f mm/s)\n",
                          currentSpeed, currentSpeed / STEPS_PER_MM_CONVEYOR);
        } else if (cmd == '+' || cmd == '=') {
            currentSpeed += 200.0f;
            if (currentSpeed > 4000.0f) currentSpeed = 4000.0f;
            stepperConveyor.setMaxSpeed(currentSpeed);
            Serial.printf("[Cmd] 加速 -> 设定速度: %.0f steps/s (%.1f mm/s)\n",
                          currentSpeed, currentSpeed / STEPS_PER_MM_CONVEYOR);
        } else if (cmd == '-' || cmd == '_') {
            currentSpeed -= 200.0f;
            if (currentSpeed < 200.0f) currentSpeed = 200.0f;
            stepperConveyor.setMaxSpeed(currentSpeed);
            Serial.printf("[Cmd] 减速 -> 设定速度: %.0f steps/s (%.1f mm/s)\n",
                          currentSpeed, currentSpeed / STEPS_PER_MM_CONVEYOR);
        } else if (cmd == 'd' || cmd == 'D') {
            forwardDir = !forwardDir;
            long target = forwardDir ? 2000000000L : -2000000000L;
            stepperConveyor.moveTo(target);
            Serial.printf("[Cmd] 切换方向 -> 当前方向: %s\n", forwardDir ? "正向 (+)" : "反向 (-)");
        }
    }

    if (isRunning) {
        // 确保距离目标始终充裕，绝不减速停步
        if (abs(stepperConveyor.distanceToGo()) < 1000000L) {
            long target = forwardDir ? (stepperConveyor.currentPosition() + 2000000000L)
                                     : (stepperConveyor.currentPosition() - 2000000000L);
            stepperConveyor.moveTo(target);
        }

        // 每隔 5 秒在串口打印一次运行状态（步数与毫米数），方便观察
        if (millis() - lastLogTime >= 5000) {
            lastLogTime = millis();
            long pos = stepperConveyor.currentPosition();
            float mm = (float)pos / STEPS_PER_MM_CONVEYOR;
            Serial.printf("[Running] 运行中: 累计位移 = %ld 步 (约 %.1f mm), 实际转速 = %.0f steps/s\n",
                          pos, mm, stepperConveyor.speed());
        }
    }

    stepperConveyor.run();
    return;
#endif

    pollDownstreamHeartbeat();

    // 基础防空转位移保护
    if (currentState == STATE_FAST_ADV || currentState == STATE_SLOW_SCAN) {
        if (stepperConveyor.currentPosition() > (long)(MAX_TRAVEL_MM * STEPS_PER_MM_CONVEYOR)) {
            Serial.println("[Head] ERROR: Max travel exceeded without exit! Emergency STOP.");
            stepperConveyor.stop();
            currentState = STATE_FAULT;
        }
    }

    switch (currentState) {
        case STATE_Y_HOMING:
            homeYAxis();
            if (currentState != STATE_FAULT) currentState = STATE_IDLE;
            break;

        case STATE_IDLE: {
            stepperConveyor.stop();
            ColorSample s1 = readSensor1();
            if (s1.getType() != ColorType::BACKGROUND) {
                // 物料放入触发
                Serial.println("[Head] Sensor #1 triggered! Starting high speed feed.");
                stepperConveyor.setCurrentPosition(0);
                stepperConveyor.setMaxSpeed(CONVEYOR_SPEED_HIGH_MMPS * STEPS_PER_MM_CONVEYOR);
                stepperConveyor.setSpeed(CONVEYOR_SPEED_HIGH_MMPS * STEPS_PER_MM_CONVEYOR);
                p0Recorded = false;
                boundaryRecorded = false;
                diamMeasured = false;
                measuredGreenLength = 0.0f;
                measuredDiameter = 0.0f;
                currentState = STATE_FAST_ADV;
            }
            break;
        }

        case STATE_FAST_ADV: {
            stepperConveyor.runSpeed();

            // 传感器 2 捕捉绿头起点 P0
            if (!p0Recorded) {
                ColorSample s2 = readSensor2();
                if (s2.getType() == ColorType::GREEN) {
                    stepP0 = stepperConveyor.currentPosition();
                    p0Recorded = true;
                    Serial.printf("[Head] Green head detected at P0 = %ld steps\n", stepP0);
                }
            }

            // 检查测径工位到达
            if (p0Recorded && !diamMeasured) {
                long targetDiamSteps = stepP0 + (long)((LASER_OFFSET_MM + DIAM_POINT_MM) * STEPS_PER_MM_CONVEYOR);
                if (stepperConveyor.currentPosition() >= targetDiamSteps) {
                    resumeStateAfterMeasure = STATE_FAST_ADV;
                    currentState = STATE_Y_MEASURE;
                    break;
                }
            }

            // 传感器 1 侦测白根预警 (前哨降速)
            ColorSample s1 = readSensor1();
            if (s1.getType() == ColorType::WHITE) {
                Serial.println("[Head] Sensor #1 detected white root! Slowing down...");
                stepperConveyor.setMaxSpeed(CONVEYOR_SPEED_LOW_MMPS * STEPS_PER_MM_CONVEYOR);
                stepperConveyor.setSpeed(CONVEYOR_SPEED_LOW_MMPS * STEPS_PER_MM_CONVEYOR);
                currentState = STATE_SLOW_SCAN;
            }
            break;
        }

        case STATE_SLOW_SCAN: {
            stepperConveyor.runSpeed();

            // 检查测径工位到达 (若在高速段尚未触发)
            if (p0Recorded && !diamMeasured) {
                long targetDiamSteps = stepP0 + (long)((LASER_OFFSET_MM + DIAM_POINT_MM) * STEPS_PER_MM_CONVEYOR);
                if (stepperConveyor.currentPosition() >= targetDiamSteps) {
                    resumeStateAfterMeasure = STATE_SLOW_SCAN;
                    currentState = STATE_Y_MEASURE;
                    break;
                }
            }

            // 传感器 2 捕获绿白分界点
            ColorSample s2 = readSensor2();
            if (!boundaryRecorded && s2.getType() == ColorType::WHITE) {
                stepBoundary = stepperConveyor.currentPosition();
                boundaryRecorded = true;
                if (p0Recorded && stepBoundary >= stepP0) {
                    measuredGreenLength = (float)(stepBoundary - stepP0) / STEPS_PER_MM_CONVEYOR;
                }
                Serial.printf("[Head] Green-White boundary at %ld steps. Green Length: %.1f mm\n",
                              stepBoundary, measuredGreenLength);
            }

            // 传感器 2 恢复背景底色 (芦笋尾部离开)
            if (boundaryRecorded && s2.getType() == ColorType::BACKGROUND) {
                Serial.println("[Head] Tail cleared sensor #2. Pausing conveyor to wait downstream IDLE.");
                stepperConveyor.stop();
                stateEntryTime = millis();
                currentState = STATE_WAIT_DOWNSTREAM;
            }
            break;
        }

        case STATE_Y_MEASURE: {
            performYMeasure();
            // 恢复推进
            if (resumeStateAfterMeasure == STATE_FAST_ADV) {
                stepperConveyor.setSpeed(CONVEYOR_SPEED_HIGH_MMPS * STEPS_PER_MM_CONVEYOR);
            } else {
                stepperConveyor.setSpeed(CONVEYOR_SPEED_LOW_MMPS * STEPS_PER_MM_CONVEYOR);
            }
            currentState = resumeStateAfterMeasure;
            break;
        }

        case STATE_WAIT_DOWNSTREAM: {
            // 原地停机，等待后机通告 IDLE
            if (downstreamState == NodeState::IDLE) {
                Serial.println("[Head] Downstream is IDLE! Sending data and handing off asparagus.");
                
                // 发送数据报文给后机
                MeasureReport rep;
                rep.item_id = currentItemId++;
                rep.green_length_mm = measuredGreenLength;
                rep.diameter_mm = measuredDiameter;
                if (!diamMeasured) strcpy(rep.status, "NO_DIAMETER");
                else strcpy(rep.status, "OK");

                char jsonBuffer[128];
                if (rep.serialize(jsonBuffer, sizeof(jsonBuffer))) {
                    Serial2.print(jsonBuffer);
                    Serial.printf("[Head] TX -> Body: %s", jsonBuffer);
                }

                // 启动交接出料位移
                ejectTargetStep = stepperConveyor.currentPosition() + (long)(EJECT_DISTANCE_MM * STEPS_PER_MM_CONVEYOR);
                stepperConveyor.setMaxSpeed(CONVEYOR_SPEED_LOW_MMPS * STEPS_PER_MM_CONVEYOR);
                stepperConveyor.moveTo(ejectTargetStep);
                currentState = STATE_EJECT;
            } else {
                if (millis() - stateEntryTime > DOWNSTREAM_TIMEOUT_MS) {
                    Serial.printf("[Head] WARNING: Downstream busy timeout! (Current: %s). Still waiting...\n",
                                  nodeStateToString(downstreamState));
                    stateEntryTime = millis();
                }
            }
            break;
        }

        case STATE_EJECT: {
            stepperConveyor.run();
            if (stepperConveyor.distanceToGo() == 0) {
                Serial.println("[Head] Handoff complete. Resetting to IDLE.");
                currentState = STATE_IDLE;
            }
            break;
        }

        case STATE_FAULT: {
            stepperConveyor.stop();
            digitalWrite(STATUS_LED_PIN, (millis() / 200) % 2); // 故障闪烁
            // 串口每隔 3 秒输出一次告警
            static unsigned long lastFaultLog = 0;
            if (millis() - lastFaultLog > 3000) {
                Serial.println("[Head] FAULT STATE. Please check mechanical & sensor status.");
                lastFaultLog = millis();
            }
            break;
        }
    }
}
