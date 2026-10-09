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
enum HeadState {
    STATE_Y_HOMING = 0,
    STATE_IDLE,
    STATE_FAST_ADV,
    STATE_SLOW_SCAN,
    STATE_Y_MEASURE,
    STATE_WAIT_DOWNSTREAM,
    STATE_EJECT,
    STATE_FAULT
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
struct ColorSample {
    uint16_t r, g, b, c;
    bool isBackground() const {
        return c < BG_CLEAR_THRESHOLD;
    }
    bool isGreen() const {
        if (isBackground()) return false;
        float total = (float)(r + g + b);
        if (total <= 0.0f) return false;
        return ((float)g / total) >= GREEN_RATIO_THRESHOLD;
    }
    bool isWhite() const {
        if (isBackground()) return false;
        float total = (float)(r + g + b);
        if (total <= 0.0f) return false;
        return ((float)g / total) <= WHITE_RATIO_THRESHOLD;
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
            if (!s1.isBackground()) {
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
                if (s2.isGreen()) {
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
            if (s1.isWhite()) {
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
            if (!boundaryRecorded && s2.isWhite()) {
                stepBoundary = stepperConveyor.currentPosition();
                boundaryRecorded = true;
                if (p0Recorded && stepBoundary >= stepP0) {
                    measuredGreenLength = (float)(stepBoundary - stepP0) / STEPS_PER_MM_CONVEYOR;
                }
                Serial.printf("[Head] Green-White boundary at %ld steps. Green Length: %.1f mm\n",
                              stepBoundary, measuredGreenLength);
            }

            // 传感器 2 恢复背景底色 (芦笋尾部离开)
            if (boundaryRecorded && s2.isBackground()) {
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
