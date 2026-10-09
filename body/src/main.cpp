#include <Arduino.h>
#include <AccelStepper.h>
#include <Preferences.h>
#include "config.h"
#include "protocol.h"

// ==================== 硬件对象与端口 ====================
HardwareSerial SerialUp(UART_UP_NUM);
HardwareSerial SerialDown(UART_DOWN_NUM);
AccelStepper stepper(AccelStepper::DRIVER, CONVEYOR_STEP_PIN, CONVEYOR_DIR_PIN);
Preferences prefs;

// ==================== 节点与规则配置 ====================
static uint8_t nodeId = 1;

struct SortingRule {
    float len_min = 0.0f;
    float len_max = 999.0f;
    float dia_min = 0.0f;
    float dia_max = 999.0f;
    bool catch_all = false;

    bool isMatch(const MeasureReport& rep) const {
        if (catch_all) return true;
        if (strcmp(rep.status, "OK") != 0) return false;
        return (rep.green_length_mm >= len_min && rep.green_length_mm < len_max &&
                rep.diameter_mm >= dia_min && rep.diameter_mm < dia_max);
    }
} currentRule;

// ==================== 运行状态 ====================
static NodeState myState = NodeState::IDLE;
static NodeState downstreamState = NodeState::UNKNOWN;
static unsigned long lastDownstreamHeartbeat = 0;
static unsigned long lastHeartbeatSent = 0;

static MeasureReport currentItem;
static bool currentItemMine = false;
static long entryStep = 0;
static bool entryTriggered = false;
static unsigned long receiveStartTime = 0;
static unsigned long diverterStartTime = 0;
static bool diverterActive = false;

// 串口缓冲区
static char upRxBuf[256];
static size_t upRxIdx = 0;
static char downRxBuf[128];
static size_t downRxIdx = 0;
static char usbRxBuf[256];
static size_t usbRxIdx = 0;

// ==================== 辅助函数 ====================
uint8_t readNodeIdFromDIP() {
    pinMode(NODE_ID_PIN_0, INPUT_PULLUP);
    pinMode(NODE_ID_PIN_1, INPUT_PULLUP);
    pinMode(NODE_ID_PIN_2, INPUT_PULLUP);
    pinMode(NODE_ID_PIN_3, INPUT_PULLUP);

    uint8_t id = 0;
    if (digitalRead(NODE_ID_PIN_0) == LOW) id |= (1 << 0);
    if (digitalRead(NODE_ID_PIN_1) == LOW) id |= (1 << 1);
    if (digitalRead(NODE_ID_PIN_2) == LOW) id |= (1 << 2);
    if (digitalRead(NODE_ID_PIN_3) == LOW) id |= (1 << 3);
    return (id == 0) ? 1 : id;
}

void loadRulesFromNVS() {
    prefs.begin("rules", true);
    currentRule.len_min = prefs.getFloat("len_min", 0.0f);
    currentRule.len_max = prefs.getFloat("len_max", 999.0f);
    currentRule.dia_min = prefs.getFloat("dia_min", 0.0f);
    currentRule.dia_max = prefs.getFloat("dia_max", 999.0f);
    currentRule.catch_all = prefs.getBool("catch_all", false);
    prefs.end();
}

void saveRulesToNVS() {
    prefs.begin("rules", false);
    prefs.putFloat("len_min", currentRule.len_min);
    prefs.putFloat("len_max", currentRule.len_max);
    prefs.putFloat("dia_min", currentRule.dia_min);
    prefs.putFloat("dia_max", currentRule.dia_max);
    prefs.putBool("catch_all", currentRule.catch_all);
    prefs.end();
}

// ==================== 串口处理 ====================
void sendUpstreamHeartbeat() {
    if (millis() - lastHeartbeatSent >= HEARTBEAT_INTERVAL_MS) {
        lastHeartbeatSent = millis();
        HeartbeatMsg hb;
        hb.node = nodeId;
        hb.state = myState;
        char buf[64];
        if (hb.serialize(buf, sizeof(buf))) {
            SerialUp.print(buf);
        }
    }
}

void pollDownstreamHeartbeat() {
    while (SerialDown.available()) {
        char ch = (char)SerialDown.read();
        if (ch == '\n' || ch == '\r') {
            if (downRxIdx > 0) {
                downRxBuf[downRxIdx] = '\0';
                HeartbeatMsg hb;
                if (hb.deserialize(downRxBuf)) {
                    downstreamState = hb.state;
                    lastDownstreamHeartbeat = millis();
                }
                downRxIdx = 0;
            }
        } else if (downRxIdx + 1 < sizeof(downRxBuf)) {
            downRxBuf[downRxIdx++] = ch;
        }
    }

    if (millis() - lastDownstreamHeartbeat > HEARTBEAT_LOSS_TIMEOUT_MS) {
        downstreamState = NodeState::UNKNOWN;
    }
}

void handleUSBCommands() {
    while (Serial.available()) {
        char ch = (char)Serial.read();
        if (ch == '\n' || ch == '\r') {
            if (usbRxIdx > 0) {
                usbRxBuf[usbRxIdx] = '\0';
                StaticJsonDocument<256> doc;
                if (deserializeJson(doc, usbRxBuf) == DeserializationError::Ok) {
                    const char* cmd = doc["cmd"] | "";
                    if (strcmp(cmd, "set_rule") == 0) {
                        currentRule.len_min = doc["len_min"] | currentRule.len_min;
                        currentRule.len_max = doc["len_max"] | currentRule.len_max;
                        currentRule.dia_min = doc["dia_min"] | currentRule.dia_min;
                        currentRule.dia_max = doc["dia_max"] | currentRule.dia_max;
                        saveRulesToNVS();
                        Serial.println("{\"result\":\"OK\",\"msg\":\"Rule updated\"}");
                    } else if (strcmp(cmd, "set_catch_all") == 0) {
                        currentRule.catch_all = doc["enable"] | false;
                        saveRulesToNVS();
                        Serial.println("{\"result\":\"OK\",\"msg\":\"Catch-all updated\"}");
                    } else if (strcmp(cmd, "get_config") == 0) {
                        StaticJsonDocument<256> resp;
                        resp["node"] = nodeId;
                        resp["state"] = nodeStateToString(myState);
                        resp["len_min"] = currentRule.len_min;
                        resp["len_max"] = currentRule.len_max;
                        resp["dia_min"] = currentRule.dia_min;
                        resp["dia_max"] = currentRule.dia_max;
                        resp["catch_all"] = currentRule.catch_all;
                        serializeJson(resp, Serial);
                        Serial.println();
                    } else if (strcmp(cmd, "reset_state") == 0) {
                        myState = NodeState::IDLE;
                        stepper.stop();
                        digitalWrite(DIVERTER_PIN, LOW);
                        diverterActive = false;
                        Serial.println("{\"result\":\"OK\",\"msg\":\"State reset to IDLE\"}");
                    }
                }
                usbRxIdx = 0;
            }
        } else if (usbRxIdx + 1 < sizeof(usbRxBuf)) {
            usbRxBuf[usbRxIdx++] = ch;
        }
    }
}

// ==================== 初始化 ====================
void setup() {
    Serial.begin(115200);
    delay(200);

    nodeId = readNodeIdFromDIP();
    loadRulesFromNVS();

    Serial.printf("\n--- Thin_Wall [Body Unit #%u] Booting ---\n", nodeId);
    Serial.printf("Current Rule: Len[%.1f, %.1f), Dia[%.1f, %.1f), CatchAll: %s\n",
                  currentRule.len_min, currentRule.len_max,
                  currentRule.dia_min, currentRule.dia_max,
                  currentRule.catch_all ? "YES" : "NO");

    // 级联通信串口初始化
    SerialUp.begin(CHAIN_BAUD, SERIAL_8N1, UART_UP_RX_PIN, UART_UP_TX_PIN);
    SerialDown.begin(CHAIN_BAUD, SERIAL_8N1, UART_DOWN_RX_PIN, UART_DOWN_TX_PIN);

    // 引脚与外设初始化
    pinMode(ENTRY_SENSOR_PIN, INPUT_PULLUP);
    pinMode(DIVERTER_PIN, OUTPUT);
    digitalWrite(DIVERTER_PIN, LOW);

    pinMode(STATUS_LED_PIN, OUTPUT);
    digitalWrite(STATUS_LED_PIN, LOW);

    pinMode(CONVEYOR_EN_PIN, OUTPUT);
    digitalWrite(CONVEYOR_EN_PIN, LOW); // 使能 A4988

    stepper.setMaxSpeed(CONVEYOR_SPEED_MMPS * STEPS_PER_MM_CONVEYOR);
    stepper.setAcceleration(CONVEYOR_ACCEL_MMPS2 * STEPS_PER_MM_CONVEYOR);

    myState = NodeState::IDLE;
    Serial.println("[Body] Ready in IDLE state.");
}

// ==================== 主调度循环 ====================
void loop() {
    handleUSBCommands();
    sendUpstreamHeartbeat();
    pollDownstreamHeartbeat();

    digitalWrite(STATUS_LED_PIN, (myState == NodeState::IDLE) ? HIGH : LOW);

    switch (myState) {
        case NodeState::IDLE: {
            stepper.stop();
            // 监听上游发来的测量报文
            while (SerialUp.available()) {
                char ch = (char)SerialUp.read();
                if (ch == '\n' || ch == '\r') {
                    if (upRxIdx > 0) {
                        upRxBuf[upRxIdx] = '\0';
                        if (currentItem.deserialize(upRxBuf)) {
                            currentItemMine = currentRule.isMatch(currentItem);
                            Serial.printf("[Body #%u] RX Item #%u: Len=%.1f, Dia=%.1f. Mine=%s\n",
                                          nodeId, currentItem.item_id,
                                          currentItem.green_length_mm, currentItem.diameter_mm,
                                          currentItemMine ? "YES" : "NO");

                            // 立即转入 RECEIVING，阻止上游再发
                            myState = NodeState::RECEIVING;
                            receiveStartTime = millis();
                            entryTriggered = false;

                            // 启动电机接料运转
                            stepper.setSpeed(CONVEYOR_SPEED_MMPS * STEPS_PER_MM_CONVEYOR);
                        }
                        upRxIdx = 0;
                    }
                } else if (upRxIdx + 1 < sizeof(upRxBuf)) {
                    upRxBuf[upRxIdx++] = ch;
                }
            }
            break;
        }

        case NodeState::RECEIVING: {
            stepper.runSpeed();

            // 入口光电检测
            if (!entryTriggered && digitalRead(ENTRY_SENSOR_PIN) == LOW) {
                entryTriggered = true;
                entryStep = stepper.currentPosition();
                Serial.printf("[Body #%u] Entry sensor triggered at step %ld\n", nodeId, entryStep);
            }

            if (entryTriggered) {
                // 转入 SENDING 处理阶段
                myState = NodeState::SENDING;
                diverterActive = false;
            } else if (millis() - receiveStartTime > ENTRY_TIMEOUT_MS) {
                Serial.printf("[Body #%u] WARNING: Entry sensor timeout! Resetting.\n", nodeId);
                myState = NodeState::IDLE;
            }
            break;
        }

        case NodeState::SENDING: {
            if (currentItemMine) {
                // 属于本节等级：分流动作
                long targetDivertStep = entryStep + (long)(DIVERTER_OFFSET_MM * STEPS_PER_MM_CONVEYOR);
                if (!diverterActive) {
                    if (stepper.currentPosition() < targetDivertStep) {
                        stepper.runSpeed();
                    } else {
                        // 到达分流位，电机刹停并动作执行器
                        stepper.stop();
                        digitalWrite(DIVERTER_PIN, HIGH);
                        diverterActive = true;
                        diverterStartTime = millis();
                        Serial.printf("[Body #%u] Diverter ACTIVE for Item #%u\n", nodeId, currentItem.item_id);
                    }
                } else {
                    if (millis() - diverterStartTime >= DIVERTER_HOLD_MS) {
                        digitalWrite(DIVERTER_PIN, LOW);
                        diverterActive = false;
                        Serial.printf("[Body #%u] Diverter done. Reset to IDLE.\n", nodeId);
                        myState = NodeState::IDLE;
                    }
                }
            } else {
                // 属于下游等级：等待下游 IDLE 后交接推料
                static bool forwardingSent = false;
                static long handoffTargetStep = 0;

                if (!forwardingSent) {
                    // 原地等待下游空闲
                    if (downstreamState == NodeState::IDLE) {
                        // 下游已空闲，发送数据报文
                        char repBuf[128];
                        if (currentItem.serialize(repBuf, sizeof(repBuf))) {
                            SerialDown.print(repBuf);
                            Serial.printf("[Body #%u] Forwarded Item #%u to downstream\n", nodeId, currentItem.item_id);
                        }
                        forwardingSent = true;
                        handoffTargetStep = stepper.currentPosition() + (long)(HANDOFF_DISTANCE_MM * STEPS_PER_MM_CONVEYOR);
                        stepper.moveTo(handoffTargetStep);
                    } else {
                        stepper.stop(); // 下游未空闲，原地停机等待
                    }
                } else {
                    stepper.run();
                    if (stepper.distanceToGo() == 0) {
                        Serial.printf("[Body #%u] Handoff completed. Reset to IDLE.\n", nodeId);
                        forwardingSent = false;
                        myState = NodeState::IDLE;
                    }
                }
            }
            break;
        }

        default:
            myState = NodeState::IDLE;
            break;
    }
}
