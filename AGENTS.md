# 全局工程规范与行为准则 (Global Workspace Rules)

本规则适用于当前工作空间下所有子项目（包括 `water_sensor_hx711`、`water_relay`、`water_android`、`water_sensor_logger` 等）。

---

## 🚫 Git 操作准则：严禁自动提交 (No Automatic Git Commit)

- **绝对禁止自动执行 `git commit`**：
  - 无论完成代码修改、Bug 修复、重构还是审查，**严禁代理助手擅自、自动运行 `git commit` 或 `git commit -a`**。
  - **严禁**代用户擅自决定提交时机与提交内容。
- **提交流程与规范**：
  1. 完成代码编写与验证（编译、测试、检查）后，仅向用户清晰汇报修改内容、测试结果及文件差异；
  2. 询问用户是否需要提交或由用户自行手动执行 Git 提交；
  3. **只有在用户明确发送指令（例如明确说“帮我提交”、“commit 这次修改”）且确认提交内容后**，方可执行提交操作。

---

## ⚡ 固件编译后自动下载规则 (Auto Firmware Upload Post-Build)

- **当前状态**：**已开启自动下载 (Auto-Upload: ENABLED)**（跨会话持久化生效）
- **触发条件与行为**：
  - 对任何嵌入式子项目（包括 `head`、`water_sensor_hx711`、`water_relay` 等）完成代码编写或修改后；
  - 自动运行编译命令 `pio run -d <subproject>`，若编译成功 (`[SUCCESS]`)，**立即自动执行下载烧录**：
    ```powershell
    pio run -d <subproject> -t upload
    ```
  - 烧录完成后向用户汇报烧录耗时、固件大小与结果。
- **状态切换**：
  - 用户说“关闭自动下载”或“只编译不烧录”时关闭；
  - 用户说“开启自动下载”或“进入下载”时恢复开启。
