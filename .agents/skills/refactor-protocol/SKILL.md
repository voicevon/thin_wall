---
name: refactor-protocol
description: Step-by-step protocol for refactoring code (splitting large modules, adjusting communication contracts, moving responsibilities, removing deprecated interfaces). Use when the user asks to refactor a file, class, protocol, or function.
---

# Refactor Protocol

本规范定义跨端 IoT 项目（ESP32 / Android / Python）重构的标准化落地流程。

---

## 1. 诊断（先读后动）

1. **通读与调用链排查**：通读目标文件，并用 `grep_search` 找出系统内所有调用点（跨 ESP32、Android、Python 端检索）。
2. **坏味道与风险定级**：列出具体重构诱因（如跨端协议字段定义分裂、职责混杂、阻塞式 IO、动态内存滥用、边界缺失），按风险高低排序。
3. **方案共识**：提出明确的重构方案、改动波及范围与潜在风险，等用户确认后再开始实施；若涉及重大设计取舍，用 `ask_question` 澄清。

---

## 2. 命名决策 (Strict Rule)

- 拆分出的新文件、类、函数、常量命名一律走 `ask_question`：提供 3~5 个候选 + 理由，经用户明确选择后方可落地。
- **严禁擅自重命名现有公共标识符**，除非事先获得用户确认。

---

## 3. 实施规则

- **不留兼容层 (Zero Backward Compatibility)**：
  - 旧接口/旧协议字段的所有调用处进行全量迁移，然后**物理删除**旧代码；
  - 严禁保留别名 (alias)、fallback 回退分支、双轨制解码（既支持旧包体又支持新包体）等增加系统复杂度的胶水代码。
- **单一真理源 (Single Source of Truth, SSOT)**：
  - 协议包结构、数据换算公式、状态枚举等核心逻辑必须有且仅有唯一定义，跨端保持精确对齐。
- **保护既有文档与注释**：
  - 完整保留与改动无关的原有注释、说明和头文件声明。
- **临时代码随用随清**：
  - 验证过程中的排查脚本用完即删，不得遗留在代码目录中。

---

## 4. 验证（按端构建）

1. 按照 `run-tests` Skill，对所有涉及改动的子工程分别执行构建与验证：
   - ESP32 固件：`pio run -d <subproject>`
   - Android：`.\gradlew compileDebugKotlin` / `.\gradlew test`
   - Python：语法/单元测试验证
2. 执行 `git status -s` 确认只有预期文件被改动，无意外构建残留。

---

## 5. 收尾汇报

用结构化格式向用户汇报：
- **“重构前问题 → 重构后方案”** 对比摘要；
- 各子工程编译/测试结果；
- 提示可能需要进行的硬件烧录或实测验证要点。
