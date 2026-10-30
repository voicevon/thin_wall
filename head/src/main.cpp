#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include "config.h"
#include "protocol.h"
#include "web_page.h"

// ==================== Web & WebSocket 实例 ====================
static WebServer server(WEB_SERVER_PORT);
static WebSocketsServer webSocket(WS_SERVER_PORT);

// ==================== 全局外设与变量 ====================
Adafruit_TCS34725 tcs = Adafruit_TCS34725(TCS_INTEGRATION_TIME, TCS_GAIN);

// 状态机枚举
enum HeadState {
    STATE_IDLE = 0,             // 待机空闲：电机停机，补光常亮，检测芦笋放入
    STATE_TRANSPORT_INSPECT,    // 推进识别：电机前进输送，持续采集颜色判定，直到尾部离开
    STATE_WAIT_DOWNSTREAM,      // 原地等待：尾部离开后电机暂停，监听等待下游车厢 IDLE
    STATE_HANDOFF,              // 交接推进：向下游发送数据包，并启动电机推料送入下游车厢
    STATE_FAULT                 // 故障保护：防卡料超时或传感器故障，停机告警
};

static HeadState currentState = STATE_IDLE;
static unsigned long stateEntryTime = 0;

// 物料检测与数据记录
static uint32_t currentItemId = 1000;
static uint32_t greenSampleCount = 0;
static uint32_t whiteSampleCount = 0;
static uint32_t totalObjectSampleCount = 0;
static bool sensorOnline = false;
static bool ledState = true; // 补光 LED 状态 (开机默认开启)

// 滤波连续计数器
static uint8_t objectDetectCounter = 0;
static uint8_t objectLeaveCounter = 0;

// 下游心跳与串口通信
static NodeState downstreamState = NodeState::UNKNOWN;
static unsigned long lastDownstreamHeartbeatTime = 0;
static char serial2RxBuf[128];
static size_t serial2RxIdx = 0;

// ==================== 颜色结构与读取 ====================
enum class ColorType {
    BACKGROUND = 0, // 背景底色 / 空载
    GREEN,          // 绿色嫩茎
    WHITE,          // 白根
    TRANSITION      // 过渡区
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

ColorSample readColorSensor() {
    ColorSample s = {0, 0, 0, 0};
    if (sensorOnline) {
        tcs.getRawData(&s.r, &s.g, &s.b, &s.c);
    }
    return s;
}

// ==================== 板载按键控制 (GPIO 0 BOOT) ====================
void handleButton() {
    static int lastBtnReading = HIGH;
    static unsigned long lastDebounceTime = 0;
    static bool buttonPressed = false;

    int reading = digitalRead(BUTTON_PIN);
    if (reading != lastBtnReading) {
        lastDebounceTime = millis();
        lastBtnReading = reading;
    }

    if ((millis() - lastDebounceTime) > 35) {
        if (reading == LOW && !buttonPressed) {
            buttonPressed = true;
            ledState = !ledState;
            digitalWrite(LED_CTRL_PIN, ledState ? HIGH : LOW);
            Serial.printf("[BUTTON] GPIO0 Pressed -> LED %s\n", ledState ? "ON" : "OFF");
        } else if (reading == HIGH && buttonPressed) {
            buttonPressed = false;
        }
    }
}

// ==================== RGB 转 HSV 算法 (OpenCV 8-bit 规范) ====================
// H: 0 ~ 180 (标准色相角 0~360 除以 2)
// S: 0 ~ 255 (饱和度归一化至单字节 0~255)
// V: 0 ~ 255 (明度归一化至单字节 0~255)
struct HsvColorOpenCV {
    uint8_t h; // 0 ~ 180
    uint8_t s; // 0 ~ 255
    uint8_t v; // 0 ~ 255
};

HsvColorOpenCV rgbToHsvOpenCV(uint16_t r, uint16_t g, uint16_t b, uint16_t c) {
    HsvColorOpenCV hsv = {0, 0, 0};
    float rf = (float)r;
    float gf = (float)g;
    float bf = (float)b;

    float rawMax = max(rf, max(gf, bf));
    // 采用自适应防溢出缩放：当未达到 255 时保持真实光强 scale=1.0；超过 255 时等比缩放
    float scale = (rawMax > 255.0f) ? (255.0f / rawMax) : 1.0f;
    rf *= scale;
    gf *= scale;
    bf *= scale;

    float maxVal = max(rf, max(gf, bf));
    float minVal = min(rf, min(gf, bf));
    float delta = maxVal - minVal;

    // 明度 V: 真实反映绝对强度 (0 ~ 255)
    hsv.v = (uint8_t)constrain((int)roundf(maxVal), 0, 255);

    // 若无色差或全黑
    if (maxVal <= 0.0f || delta <= 0.0f) {
        hsv.s = 0;
        hsv.h = 0;
        return hsv;
    }

    // 饱和度 S: 0 ~ 255
    float sNorm = (delta / maxVal) * 255.0f;
    hsv.s = (uint8_t)constrain((int)roundf(sNorm), 0, 255);

    // 色相 H: 0 ~ 180 (OpenCV 规范)
    float hDeg = 0.0f;
    if (rf >= maxVal) {
        hDeg = 60.0f * fmodf(((gf - bf) / delta), 6.0f);
    } else if (gf >= maxVal) {
        hDeg = 60.0f * (((bf - rf) / delta) + 2.0f);
    } else {
        hDeg = 60.0f * (((rf - gf) / delta) + 4.0f);
    }

    if (hDeg < 0.0f) {
        hDeg += 360.0f;
    }

    float hOpenCV = hDeg / 2.0f;
    hsv.h = (uint8_t)constrain((int)roundf(hOpenCV), 0, 180);

    return hsv;
}

// ==================== 5Hz (200ms) 采样推流与串口打印 ====================
void handlePeriodicPublish(const ColorSample& s) {
    static unsigned long lastPubTime = 0;
    unsigned long now = millis();
    if (now - lastPubTime >= SAMPLE_INTERVAL_MS) {
        lastPubTime = now;
        HsvColorOpenCV hsv = rgbToHsvOpenCV(s.r, s.g, s.b, s.c);

        // 1. 串口打印 (OpenCV 8-bit HSV 规范)
        Serial.printf("[COLOR] RGB:[%5u, %5u, %5u] | HSV:[%3u, %3u, %3u] | Clear:%5u\n",
                      s.r, s.g, s.b, hsv.h, hsv.s, hsv.v, s.c);

        // 2. WebSocket 广播 JSON 数据包至已连接的浏览器客户端
        if (webSocket.connectedClients() > 0) {
            char jsonBuf[128];
            snprintf(jsonBuf, sizeof(jsonBuf),
                     "{\"r\":%u,\"g\":%u,\"b\":%u,\"c\":%u,\"h\":%u,\"s\":%u,\"v\":%u,\"led\":%d}",
                     s.r, s.g, s.b, s.c, hsv.h, hsv.s, hsv.v, ledState ? 1 : 0);
            webSocket.broadcastTXT(jsonBuf);
        }
    }
}

// ==================== 直流电机驱动 ====================
void motorInit() {
    ledcSetup(MOTOR_PWM_CH_IN1, MOTOR_PWM_FREQ, MOTOR_PWM_RES_BITS);
    ledcSetup(MOTOR_PWM_CH_IN2, MOTOR_PWM_FREQ, MOTOR_PWM_RES_BITS);
    ledcAttachPin(MOTOR_IN1_PIN, MOTOR_PWM_CH_IN1);
    ledcAttachPin(MOTOR_IN2_PIN, MOTOR_PWM_CH_IN2);
    ledcWrite(MOTOR_PWM_CH_IN1, 0);
    ledcWrite(MOTOR_PWM_CH_IN2, 0);
}

void motorForward(uint8_t speed) {
    ledcWrite(MOTOR_PWM_CH_IN1, speed);
    ledcWrite(MOTOR_PWM_CH_IN2, 0);
}

void motorStop() {
    ledcWrite(MOTOR_PWM_CH_IN1, 0);
    ledcWrite(MOTOR_PWM_CH_IN2, 0);
}

void motorBrake() {
    ledcWrite(MOTOR_PWM_CH_IN1, 255);
    ledcWrite(MOTOR_PWM_CH_IN2, 255);
}

// ==================== 状态切换辅助函数 ====================
const char* stateToString(HeadState st) {
    switch (st) {
        case STATE_IDLE:                return "IDLE";
        case STATE_TRANSPORT_INSPECT:   return "TRANSPORT_INSPECT";
        case STATE_WAIT_DOWNSTREAM:     return "WAIT_DOWNSTREAM";
        case STATE_HANDOFF:             return "HANDOFF";
        case STATE_FAULT:               return "FAULT";
        default:                        return "UNKNOWN";
    }
}

void switchState(HeadState next) {
    if (currentState == next) return;
    Serial.printf("[HEAD] State: %s -> %s\n", stateToString(currentState), stateToString(next));
    currentState = next;
    stateEntryTime = millis();
}

// ==================== 下游串口心跳与数据通信 ====================
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
        } else {
            if (serial2RxIdx < sizeof(serial2RxBuf) - 1) {
                serial2RxBuf[serial2RxIdx++] = ch;
            }
        }
    }

    if (millis() - lastDownstreamHeartbeatTime > HEARTBEAT_LOSS_TIMEOUT_MS) {
        downstreamState = NodeState::UNKNOWN;
    }
}

void sendMeasureReport(const char* statusStr) {
    MeasureReport report;
    report.item_id = ++currentItemId;
    report.green_length_mm = 0.0f;
    report.diameter_mm = 0.0f;
    strncpy(report.status, statusStr, sizeof(report.status) - 1);
    report.status[sizeof(report.status) - 1] = '\0';

    char txBuf[160];
    if (report.serialize(txBuf, sizeof(txBuf))) {
        Serial2.print(txBuf);
        Serial.printf("[TX->BODY] Sent Report: %s", txBuf);
    } else {
        Serial.println("[HEAD] Error: Report serialize failed!");
    }
}

// ==================== 串口调试指令交互 ====================
void handleDebugConsole() {
    while (Serial.available()) {
        char cmd = (char)Serial.read();
        if (cmd == '\r' || cmd == '\n' || cmd == ' ') continue;

        switch (cmd) {
            case 'f':
                Serial.println("[CMD] Motor Forward Test (2000ms)...");
                motorForward(MOTOR_SPEED_DEFAULT);
                delay(2000);
                motorStop();
                Serial.println("[CMD] Motor Stopped.");
                break;
            case 's':
                motorStop();
                Serial.println("[CMD] Motor Stopped.");
                break;
            case 'c': {
                ColorSample s = readColorSensor();
                float total = (float)(s.r + s.g + s.b);
                float gRatio = (total > 0.0f) ? (float)s.g / total : 0.0f;
                Serial.printf("[CMD] Sensor R:%u G:%u B:%u Clear:%u | gRatio:%.3f (Type:%d)\n",
                              s.r, s.g, s.b, s.c, gRatio, (int)s.getType());
                break;
            }
            case 'r':
                Serial.println("[CMD] Reset State to IDLE.");
                motorStop();
                switchState(STATE_IDLE);
                break;
            case '?':
                Serial.println("--- Debug Commands ---");
                Serial.println(" f : Test motor forward 2s");
                Serial.println(" s : Stop motor");
                Serial.println(" c : Read TCS34725 sample");
                Serial.println(" r : Force reset state to IDLE");
                Serial.println("----------------------");
                break;
            default:
                break;
        }
    }
}

// ==================== WebSocket 事件处理 ====================
void onWebSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
    if (type == WStype_CONNECTED) {
        IPAddress ip = webSocket.remoteIP(num);
        Serial.printf("[WS] Client #%u connected from %s\n", num, ip.toString().c_str());
    } else if (type == WStype_DISCONNECTED) {
        Serial.printf("[WS] Client #%u disconnected\n", num);
    } else if (type == WStype_TEXT) {
        String msg = String((char*)payload);
        if (msg.indexOf("TOGGLE_LED") >= 0) {
            ledState = !ledState;
            digitalWrite(LED_CTRL_PIN, ledState ? HIGH : LOW);
            Serial.printf("[WS] Toggle LED via Web -> %s\n", ledState ? "ON" : "OFF");
        } else if (msg.indexOf("SET_GAIN_") >= 0) {
            if (msg.indexOf("1X") >= 0) {
                tcs.setGain(TCS34725_GAIN_1X);
                Serial.println("[TCS] Gain switched to 1X");
            } else if (msg.indexOf("4X") >= 0) {
                tcs.setGain(TCS34725_GAIN_4X);
                Serial.println("[TCS] Gain switched to 4X");
            } else if (msg.indexOf("16X") >= 0) {
                tcs.setGain(TCS34725_GAIN_16X);
                Serial.println("[TCS] Gain switched to 16X (+4x Sensitivity)");
            } else if (msg.indexOf("60X") >= 0) {
                tcs.setGain(TCS34725_GAIN_60X);
                Serial.println("[TCS] Gain switched to 60X (Max)");
            }
        }
    }
}

// ==================== WiFi & Web 服务器启动 ====================
void setupNetwork() {
    Serial.printf("[WiFi] Connecting to '%s' ...\n", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    unsigned long startMs = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - startMs < 10000)) {
        delay(400);
        Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("=================================================");
        Serial.printf(" [WiFi] Connected! IP Address: %s\n", WiFi.localIP().toString().c_str());
        Serial.printf(" [WEB] Open in Browser: http://%s\n", WiFi.localIP().toString().c_str());
        Serial.println("=================================================");
    } else {
        Serial.println("[WiFi] Connection timeout. ESP32 will keep trying in background.");
    }

    server.on("/", HTTP_GET, []() {
        server.send_P(200, "text/html", PAGE_INDEX_HTML);
    });
    server.begin();
    webSocket.begin();
    webSocket.onEvent(onWebSocketEvent);
}

// ==================== 主设置 setup ====================
void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("\n=============================================");
    Serial.println("  thin_wall - head (ESP32 DC Motor + TCS34725)");
    Serial.println("=============================================");

    // 1. 初始化按键与补光 LED
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    pinMode(LED_CTRL_PIN, OUTPUT);
    digitalWrite(LED_CTRL_PIN, ledState ? HIGH : LOW);
    Serial.printf("[HEAD] LED Warmup (%d ms, State: %s)...\n", LED_WARMUP_MS, ledState ? "ON" : "OFF");
    delay(LED_WARMUP_MS);

    // 2. 初始化 I2C 颜色传感器
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    if (tcs.begin(0x29, &Wire)) {
        sensorOnline = true;
        Serial.println("[HEAD] TCS34725 Color Sensor initialized successfully (0x29).");
    } else {
        sensorOnline = false;
        Serial.println("[HEAD] WARNING: TCS34725 not found on I2C bus!");
    }

    // 3. 初始化直流电机 H 桥
    motorInit();
    Serial.println("[HEAD] DC Motor PWM initialized.");

    // 4. 初始化下行数据串口
    Serial2.begin(DATA_UART_BAUD, SERIAL_8N1, DATA_UART_RX_PIN, DATA_UART_TX_PIN);
    Serial.printf("[HEAD] Data UART2 started (TX:%d, RX:%d, Baud:%d).\n",
                  DATA_UART_TX_PIN, DATA_UART_RX_PIN, DATA_UART_BAUD);

    // 5. 连接 WiFi 并启动 Web & WebSocket 服务
    setupNetwork();

    switchState(STATE_IDLE);
}

// ==================== 主循环 loop ====================
void loop() {
    // 0. 网络与 Web 服务轮询
    server.handleClient();
    webSocket.loop();

    // 1. 按键控制补光 LED (GPIO 0 单击翻转)
    handleButton();

    // 2. 监听下游心跳与调试命令
    pollDownstreamHeartbeat();
    handleDebugConsole();

    // 3. 周期读取颜色传感器并通过 200ms 周期推流 (串口 + WebSocket)
    ColorSample sample = readColorSensor();
    handlePeriodicPublish(sample);

    unsigned long now = millis();

    switch (currentState) {
        case STATE_IDLE: {
            motorStop();
            // 待机下检测物料放入 (Clear 超过空载背景阈值)
            if (sample.getType() != ColorType::BACKGROUND) {
                if (++objectDetectCounter >= COLOR_CONFIRM_SAMPLES) {
                    objectDetectCounter = 0;
                    objectLeaveCounter = 0;
                    greenSampleCount = 0;
                    whiteSampleCount = 0;
                    totalObjectSampleCount = 0;
                    Serial.println("[HEAD] Object detected! Starting feed motor...");
                    motorForward(MOTOR_SPEED_DEFAULT);
                    switchState(STATE_TRANSPORT_INSPECT);
                }
            } else {
                objectDetectCounter = 0;
            }
            break;
        }

        case STATE_TRANSPORT_INSPECT: {
            totalObjectSampleCount++;
            ColorType ct = sample.getType();
            if (ct == ColorType::GREEN) {
                greenSampleCount++;
            } else if (ct == ColorType::WHITE) {
                whiteSampleCount++;
            }

            // 判断物料尾部是否离开 (读数恢复背景底色)
            if (ct == ColorType::BACKGROUND) {
                if (++objectLeaveCounter >= COLOR_CONFIRM_SAMPLES) {
                    objectLeaveCounter = 0;
                    motorStop(); // 尾部离开检测区，立即停机
                    Serial.printf("[HEAD] Object tail passed. Samples: Total=%u, Green=%u, White=%u\n",
                                  totalObjectSampleCount, greenSampleCount, whiteSampleCount);
                    switchState(STATE_WAIT_DOWNSTREAM);
                }
            } else {
                objectLeaveCounter = 0;
            }

            // 防卡料与超时保护
            if (now - stateEntryTime > MAX_RUN_TIME_MS) {
                motorStop();
                Serial.println("[HEAD] ERROR: Feed timeout! Entering FAULT state.");
                switchState(STATE_FAULT);
            }
            break;
        }

        case STATE_WAIT_DOWNSTREAM: {
            motorStop();

#if STANDALONE_DEBUG_MODE
            // 单机调试模式：直接越过下游握手
            Serial.println("[HEAD] Standalone mode: Bypassing downstream check.");
            sendMeasureReport("OK");
            motorForward(MOTOR_SPEED_HANDOFF);
            switchState(STATE_HANDOFF);
#else
            // 工业背压互锁：检查下游 body_1 状态
            if (downstreamState == NodeState::IDLE) {
                const char* qualityStatus = (greenSampleCount >= whiteSampleCount) ? "OK" : "WHITE_ROOT";
                Serial.printf("[HEAD] Downstream IDLE confirmed. Quality: %s\n", qualityStatus);
                sendMeasureReport(qualityStatus);
                motorForward(MOTOR_SPEED_HANDOFF);
                switchState(STATE_HANDOFF);
            } else if (now - stateEntryTime > DOWNSTREAM_TIMEOUT_MS) {
                Serial.printf("[HEAD] WARNING: Downstream wait timeout! Current state: %s. Continuing wait...\n",
                              nodeStateToString(downstreamState));
                stateEntryTime = now; // 延长并保持等待
            }
#endif
            break;
        }

        case STATE_HANDOFF: {
            // 推料交接计时
            if (now - stateEntryTime >= HANDOFF_DURATION_MS) {
                motorStop();
                Serial.println("[HEAD] Handoff complete. Ready for next asparagus.");
                switchState(STATE_IDLE);
            }
            break;
        }

        case STATE_FAULT: {
            motorStop();
            // 在故障状态下，若检测到空载且超时 3 秒后尝试自动恢复 IDLE
            if (sample.getType() == ColorType::BACKGROUND && (now - stateEntryTime > 3000)) {
                Serial.println("[HEAD] Fault cleared. Resetting to IDLE.");
                switchState(STATE_IDLE);
            }
            break;
        }
    }

    delay(20); // 周期步进 20ms (约 50Hz 采样率)
}
