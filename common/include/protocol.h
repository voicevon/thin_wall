#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

/**
 * @brief 节点运行状态枚举
 */
enum class NodeState {
    IDLE = 0,       // 空闲：可接料、可接收数据
    RECEIVING,      // 接收中：上游料正在进入本节
    SENDING,        // 处理/发送中：本节正在分选入仓，或正在向下游交接推料
    UNKNOWN         // 未知 / 离线超时
};

inline const char* nodeStateToString(NodeState state) {
    switch (state) {
        case NodeState::IDLE:      return "IDLE";
        case NodeState::RECEIVING: return "RECEIVING";
        case NodeState::SENDING:   return "SENDING";
        default:                   return "UNKNOWN";
    }
}

inline NodeState stringToNodeState(const char* str) {
    if (!str) return NodeState::UNKNOWN;
    if (strcmp(str, "IDLE") == 0) return NodeState::IDLE;
    if (strcmp(str, "RECEIVING") == 0) return NodeState::RECEIVING;
    if (strcmp(str, "SENDING") == 0) return NodeState::SENDING;
    return NodeState::UNKNOWN;
}

/**
 * @brief 测量结果数据报文 (head 发送, body 转发)
 */
struct MeasureReport {
    uint32_t item_id = 0;
    float green_length_mm = 0.0f;
    float diameter_mm = 0.0f;
    char status[24] = "OK"; // "OK", "NO_DIAMETER", "FAULT_TRAVEL", "FAULT_SENSOR"

    bool serialize(char* buffer, size_t maxLen) const {
        StaticJsonDocument<128> doc;
        doc["item_id"] = item_id;
        doc["green_length_mm"] = serialized(String(green_length_mm, 1));
        doc["diameter_mm"] = serialized(String(diameter_mm, 1));
        doc["status"] = status;
        size_t len = serializeJson(doc, buffer, maxLen);
        if (len + 1 < maxLen) {
            buffer[len] = '\n';
            buffer[len + 1] = '\0';
            return true;
        }
        return false;
    }

    bool deserialize(const char* jsonStr) {
        StaticJsonDocument<256> doc;
        DeserializationError err = deserializeJson(doc, jsonStr);
        if (err) return false;

        item_id = doc["item_id"] | 0;
        green_length_mm = doc["green_length_mm"] | 0.0f;
        diameter_mm = doc["diameter_mm"] | 0.0f;
        const char* st = doc["status"] | "OK";
        strncpy(status, st, sizeof(status) - 1);
        status[sizeof(status) - 1] = '\0';
        return true;
    }
};

/**
 * @brief 反向状态心跳报文 (body 向上游发送)
 */
struct HeartbeatMsg {
    uint8_t node = 0;
    NodeState state = NodeState::UNKNOWN;

    bool serialize(char* buffer, size_t maxLen) const {
        int written = snprintf(buffer, maxLen, "{\"node\":%u,\"state\":\"%s\"}\n",
                               node, nodeStateToString(state));
        return written > 0 && (size_t)written < maxLen;
    }

    bool deserialize(const char* jsonStr) {
        StaticJsonDocument<128> doc;
        DeserializationError err = deserializeJson(doc, jsonStr);
        if (err) return false;

        node = doc["node"] | 0;
        const char* st = doc["state"] | "UNKNOWN";
        state = stringToNodeState(st);
        return true;
    }
};
