---
name: code-review
description: Conduct a systematic, rigorous code review for Git diffs, staged changes, pull requests, or specified source files. Evaluates architectural compliance, logic correctness, concurrency/resource safety, performance bottlenecks, and style across ESP32 (C++), Android (Kotlin), and Python projects.
---

# Code Review Protocol

Use this skill whenever the user asks for a code review, checks staged/uncommitted changes, audits a pull request, or evaluates code quality (e.g. "帮我做下代码评审", "审查最近的代码", "Review this PR/diff", "/review").

## 1. Scope Identification

1. **Detect Target Changes**:
   - For uncommitted/staged working tree changes: run `git status` and `git diff HEAD` (or staged diff `git diff --cached`).
   - For branch/PR comparisons: run `git diff main...HEAD` (or the relevant target branch).
   - For specific commits: run `git show <commit_hash>`.
   - For explicitly specified files: read and inspect the full content and recent diffs of those files.
2. **Context Gathering**:
   - Identify which sub-projects are impacted (`water_sensor_hx711`, `water_relay`, `water_android`, `water_sensor_logger`).
   - Check if changes touch communication contracts (BLE payload, MQTT topics/JSON schema), state machines, hardware drivers, or multi-threading.

## 2. Review Dimensions

Evaluate code against the following dimensions:

### A. 架构设计与协议一致性 (Architecture & Protocol SSOT)
- **零向后兼容包袱 (Zero Backward Compatibility)**：核查是否引入任何废弃字段分支、双轨制逻辑；协议升级必须全端对齐并物理清理旧代码。
- **单一真理源 (Single Source of Truth, SSOT)**：跨端协议（如 BLE 广播包结构、MQTT 载荷定义、数据单位如 g/kg/mV）必须保持完全一致。
- **协议边界保护**：
  - BLE 广播包大小限制（Legacy Advertising PDU 总长度 31 字节，扣除 Flags 和 AD Header 后的剩余负载空间）；
  - 字节序对齐（Little-Endian vs Big-Endian）与端侧转换一致性；
  - 符号位扩展（如 24-bit 有符号数解包为 32-bit int 时符号位保留）。

### B. Correctness & Edge Cases
- **Logic & Flow**: Check for off-by-one errors, inverted conditions, unreachable code, unhandled edge cases (zero division, empty lists, null checks).
- **Concurrency & State Safety**:
  - **ESP32 / FreeRTOS**: Mutex / Semaphore 保护跨任务共享数据（如 BLE 扫描回调、MQTT 接收与主循环缓存访问）；避免死锁与高优先级任务饥饿；
  - **Android (Kotlin)**: 协程调度器使用（`Dispatchers.IO` vs `Dispatchers.Main`），避免阻塞主线程；状态更新与 Flow 收集生命周期感知；
  - **Python (FastAPI)**: 异步任务非阻塞调用，共享状态访问安全。

### C. Resource & Performance (嵌入式与移动端特化)
- **嵌入式内存与硬件健康 (ESP32)**:
  - 避免频繁的动态内存申请与释放（尤其是长效循环中的 `String` 频繁拼接，易造成堆碎片）；优先使用定长 buffer 或静态数组；
  - 资源泄露防范：NVS 句柄、文件句柄、网络客户端重连时旧连接未断开等；
  - 看门狗与长延时：禁止在主循环中无休止同步阻塞，确保适当 yield 或 `vTaskDelay`。
- **移动端与系统级性能 (Android / Python)**:
  - 防止后台服务 (Service) 内存泄露与未释放的 BroadcastReceiver / BLE 连接；
  - UI 避免高频无谓重组 (Compose Recomposition)。

### D. 命名审查原则 (严格规范)
- **严禁擅自修改**：任何文件名、类名、函数名、变量名优化，严禁直接在代码中重命名或代用户做主；
- **提供 3~5 候选**：针对待优化命名，必须列出 3 至 5 个备选方案并附带推荐理由与侧重点；
- **交互式点选确认**：必须使用 `ask_question` 工具以单选/多选交互式选择题呈现，经用户明确点选确认后方可实施修改。

## 3. Output Format

Present the review results in clear, actionable markdown:

```markdown
### 📋 评审概述 (Overview)
- **改动范围**: [简要说明改动的子工程、模块与主要意图]
- **总体结论**: ✅ 通过 / ⚠️ 建议修改后合并 / ❌ 阻断性问题需重构

---

### 🔍 关键发现 (Findings by Severity)

#### 🔴 P0 - 阻断性问题 (Blocker / Must Fix)
> 严重逻辑缺陷、数据损坏隐患、跨端协议不匹配、广播包超长、内存泄露。
- **[文件路径:行号]**: 问题描述及影响。
  ```diff
  - 待修改代码
  + 建议修改代码
  ```

#### 🟡 P1 - 重要建议 (Major / Should Fix)
> 性能隐患、并发/临界区未加锁、重连容错缺失、边界条件未处理。
- **[文件路径:行号]**: 改进说明与示例。

#### 🟢 P2 - 细节与代码风格 (Minor / Nitpick)
> 类型注解优化、常量命名、代码注释清晰度、局部冗余精简。
- **[文件路径:行号]**: 优化建议。

#### 🏷️ 命名优化建议 (Naming Proposals - 供用户选择)
> 规则：严禁直接修改，必须给出 3~5 个备选名字供用户裁定。
- **待优化项**: `[当前文件名 / 类名 / 函数名 / 变量名]`（位置：`path/to/file:L123`）
  - **当前问题**: 为什么现有命名不够合理/精准
  - **推荐候选方案 (请选择)**:
    1. `candidate_name_1` - [推荐理由/侧重点]
    2. `candidate_name_2` - [推荐理由/侧重点]
    3. `candidate_name_3` - [推荐理由/侧重点]
    4. `candidate_name_4` (可选) - [推荐理由/侧重点]
    5. `candidate_name_5` (可选) - [推荐理由/侧重点]

---

### 💡 综合建议与下一步行动 (Next Steps)
- [列出开发者合并/烧录/部署前需执行的动作与验证指令]
```
