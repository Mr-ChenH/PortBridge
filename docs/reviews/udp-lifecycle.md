# UDP 绑定/关闭不生成数据消息（udp-lifecycle-1）

用户要求去掉 UDP 绑定和关闭端口时出现的消息。本轮移除 UDP `Bound`、`Disconnected` 在数据区和原始采集中的 SYSTEM 记录，版本仍为 0.1.0，修订为 `udp-lifecycle-1`。当前协调者直接修改并验证，没有新增子 agent。

原来网络引擎的绑定事件及 SessionController 的用户停止事件被序列化为 JSON SYSTEM 记录，和真实收发样本一起显示。它们没有调用业务 UDP send，也不计入 TX 字节，但界面容易被理解为发送消息。现在统一在 `recordEvent()` 入口跳过这两类 UDP 生命周期记录和已有的 `UdpTargetReady` 配置记录，覆盖正常绑定、手动关闭、方案替换、窗口关闭及异步断开路径。

端口状态、实际绑定端点、记录收尾、停止周期、代次隔离继续执行。实际 RX/TX 保留；UDP 的错误、发送拒绝及接收截断诊断仍记录。TCP/串口的生命周期记录保持原有语义，TCP 服务端 Listening/Disconnected 也有回归断言。

## 验证

新增真实 UDP 界面回归：

- 连续两次绑定/关闭，数据表始终 0 行、文本预览为空、RX/TX 为 0，对端没有数据报（包含零长度包的检测）。
- 每次关闭后，另一个 socket 能独占绑定相同本地端口，证明释放实际完成。
- 第三次绑定后开启记录，主动发送 3 B，对端逐字节收到 `AA 55 01`；对端回复 5 B `reply`，数据表恰为 2 行。关闭后没有新增行、没有额外发包，统计仍是 TX 3 B / RX 5 B。
- 采集文件完整收尾，记录数恰为 2；导出后逐条核对真实 TX/RX 内容，没有绑定或关闭的 SYSTEM 消息。

会话原有 SYSTEM 测试改为检查 UDP 绑定无记录、错误和拒绝可重建、关闭不添加生命周期消息，并检查 TCP 监听/关闭仍有记录。离线切换隔离测试原本用绑定/关闭消息作为样本，现改用明确注入的真实 RX 检查本地序号连续及停止后证据保留，其他代次/错误/写入隔离断言保留。

最终完整 CTest **3/3**：网络 **11** 组、会话 **27/27**、UI **37/37**（Qt Test 包含初始化/清理）。源码冻结前后哈希一致。证据：[CTest](../validation/udp-lifecycle/ctest.txt)、[会话](../validation/udp-lifecycle/session.txt)、[UI](../validation/udp-lifecycle/ui.txt)、[验证状态](../validation/udp-lifecycle/result.json)、[源哈希](../validation/udp-lifecycle/source-hashes.json)。

Windows 原生 100%、125%、150% 专项各 **5/5**：生命周期零消息/零发包、直接发送、保留后台绑定三项综合测试及初始化/清理。[100%](../validation/udp-lifecycle/native-1.txt)、[125%](../validation/udp-lifecycle/native-1.25.txt)、[150%](../validation/udp-lifecycle/native-1.5.txt)。截图：[关闭后仅有真实业务记录](../validation/udp-lifecycle/udp-closed-business-records-only.png)。软件缩放不是物理多显示器 DPI 验收。

第一轮因旧测试依赖 UDP Bound 样本而失败，同时一次多代次序号测试的绑定等待失败；保留 `ctest-before-fixture-update.txt`、`session-before-fixture-update.txt`。序号测试独立重放通过（`sequence-focused.txt`）；未更改该测试，后续完整回归再次验证。此修订不将第一轮失败结果覆盖为成功证据。

## 交付

新版路径 `dist/PortBridge-udp-lifecycle-1/PortBridge.exe`，旧进程和旧目录保留。ZIP 为 `dist/PortBridge-0.1.0-windows-x64.zip`，上一修订备份到 `build/package-backups/composer-fix-1/`。核验 Release/部署/解压 EXE 一致、受限环境启动、全部文件哈希和 UDP/TCP 各 500 帧回环；最终归档证据位于工作区 `docs/validation/udp-lifecycle-zip.json`。

DNS 解析仍可能触发系统 DNS 查询，此处“零发包”指端口配置/关闭不向业务目标发送 UDP 数据报。物理串口、两机 2.5G、持续磁盘负载和干净 Windows 验收结论不变。
