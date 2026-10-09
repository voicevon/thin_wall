---
name: ponytail
description: Prevent over-engineering and premature abstraction. Enforce YAGNI, maximize reuse of existing codebase and standard libraries, and write the minimum code necessary across ESP32, Android, and Python projects.
---

# Ponytail Protocol — 拒绝过度工程与极简极效设计

> "Think like the laziest senior dev in the room. The best code is the code you never have to write, debug, or maintain."

Ponytail 是面向 AI 辅助编程与工程落地的**反过度工程 (Anti-Overengineering) 规则集**。它强制要求在编写任何新代码、设计新架构前，按阶梯递进评估必要性。

---

## 核心优先阶梯 (The Priority Ladder)

在动手编写任何代码、创建新文件或引入新抽象之前，必须依次通过以下四级检验：

```text
[1. 必要性检验 (YAGNI)] ──(必须存在)──> [2. 存量复用检验] ──(无现成可用)──> [3. 原生能力检验] ──(无原生支持)──> [4. 极简直接实现]
         │                                      │                                    │
    (非当务之急)                           (已有相似实现)                        (标准库已提供)
         ↓                                      ↓                                    ↓
     直接不做                                直接复用/调用                        直接调用标准库/原生平台
```

### 1. Does this need to exist? (必要性与 YAGNI 原则)
- **拒绝为假想未来买单**：严禁实现“以后可能会用到”的灵活性、参数、回调或配置项。只解决当前明确的工程诉求。
- **杜绝空头抽象**：只有 1 个实现类时，严禁提前抽象多层基类、接口或工厂工厂模式。
- **杜绝冗余装饰**：不要为一行简单赋值、读配置或位运算编写冗长繁琐的包装层（Wrapper）。

### 2. Does it already exist in the codebase? (优先复用既有模块)
- **搜索既有资产**：在实现新功能前，必须先在各子工程中查找是否有已验证的工具函数或模块：
  - **ESP32 固件** (`water_sensor_hx711`, `water_relay`)：复用既有的 NVS 配置读写、RingBuffer/DataCache 缓存、BLE 广播/扫描与 MQTT 通信逻辑，严禁各模块散落重复实现打包或解析。
  - **Android 端** (`water_android`)：复用既有的 BLE/MQTT 协议解析类、Repository 与 Jetpack 基础组件。
  - **Python 服务** (`water_sensor_logger`)：复用既有的数据解析、文件持久化与 API 路由结构。

### 3. Can the standard library or platform handle it? (优先平台原生与标准库能力)
- **零新依赖原则**：严禁因单一小功能随意引入重量级第三方依赖库。
- **优先各平台原生生态**：
  - C++ / ESP32：优先使用标准 C/C++ 与 ESP-IDF / Arduino 自带核心库，精简内存占用；
  - Android：优先使用 Kotlin 标准库与 Android Jetpack 推荐 API；
  - Python：优先使用 Python 标准库（`struct`, `pathlib`, `typing`, `json` 等）与既有框架（FastAPI）。

### 4. Write the minimum amount of code necessary (最小代码量与平铺设计)
- **扁平优于嵌套 (Flat is better than nested)**：优先使用简单的顶层函数与平铺的数据结构，减少多层嵌套。
- **直观优于精巧**：不要为了所谓“优雅”使用过于隐晦的黑魔法、深层回调闭包或复杂继承链。
- **就地解决**：若改动仅有 2~3 行，直接在调用点清晰实现，避免为了抽函数而新增大量胶水文件。

---

## 核心工程准则

1. **零向后兼容包袱 (Zero Backward Compatibility)**：
   - 协议与架构升级时，旧调用处全量迁移，旧字段/旧接口物理删除；
   - 绝不保留兼容废弃特性的 fallback、alias 或临时双轨逻辑。
2. **单一真理源 (Single Source of Truth, SSOT)**：
   - 通信协议包定义、数据结构与关键状态必须保持唯一持有者与严格对齐，严禁两套定义并行。
3. **工作区与代码清洁**：
   - 临时排查脚本、测试输出随用随删，严禁污染业务代码库。
