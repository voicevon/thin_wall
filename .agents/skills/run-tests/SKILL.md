---
name: run-tests
description: Run compilation, build verification, and test suites across the water system subprojects (ESP32 PlatformIO, Android Gradle, Python logger). Use after any code change, refactor, or before declaring a task done.
---

# Run Tests & Build Verification Protocol

本系统包含多个跨端子工程（ESP32 固件、Android 应用、Python 记录服务）。任何代码修改或重构后，必须针对受影响的子工程执行对应的构建或测试验证，严禁“只写代码不跑编译”。

---

## 1. 常用验证命令 (按子工程)

### A. ESP32 传感器与继电器固件 (`water_sensor_hx711` / `water_relay`)
- **编译/语法检验**（无需连接硬件板卡）：
  ```powershell
  # 验证传感器固件
  pio run -d water_sensor_hx711

  # 验证继电器网关固件
  pio run -d water_relay
  ```
- **预期结果**：输出包含 `SUCCESS`，检查 RAM 与 Flash 占用率未溢出。

### B. Android 客户端 (`water_android`)
- **Kotlin 编译校验**：
  ```powershell
  cd water_android; .\gradlew compileDebugKotlin
  ```
- **单元测试执行**：
  ```powershell
  cd water_android; .\gradlew test
  ```
- **预期结果**：输出 `BUILD SUCCESSFUL`。

### C. Python 记录与分析服务 (`water_sensor_logger`)
- **单元测试 / 语法编译校验**：
  ```powershell
  # 语法与编译检查
  python -m py_compile water_sensor_logger/main.py

  # 若有单元测试套件
  pytest water_sensor_logger
  # 或
  python -m unittest discover water_sensor_logger
  ```

---

## 2. 结果判定与排错原则

1. **绝对判定标准**：
   - ESP32 必须 PlatformIO `[SUCCESS]`；
   - Android 必须 `BUILD SUCCESSFUL`；
   - Python 必须零编译/语法异常、测试全绿。
2. **严禁弱化断言或掩盖错误**：
   - 出现编译报错或单测失败必须直接修复根因，严禁靠删除断言或屏蔽校验使流程通过。
3. **跨端协议对齐验证**：
   - 若改动涉及 BLE 广播包或 MQTT 载荷字段，必须**同时**检验固件、Android、Python 三端的解析逻辑是否一致匹配。

---

## 3. 跑完后的工作区清洁检查（必做）

1. 执行 `git status -s`，确认改动仅包含本次任务预期的源码文件。
2. **严禁引入构建产物与临时文件**：
   - 核查是否有 `.pio/`, `.gradle/`, `build/`, `__pycache__/`, `.pytest_cache/` 等临时产物未被 `.gitignore` 过滤并散落到仓库；
   - 临时验证脚本随用随删，严禁污染业务代码库。
