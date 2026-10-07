# UDP 配置与主动发送修复（udp-fix-1）

用户反馈 UDP 页面混入 TCP 字段，并缺少明确的客户端发送配置。此前目标地址/端口存在于共同的连接侧栏，但绑定后随整个配置区一起禁用；“连接超时”和 TCP/串口说明也继续出现在 UDP 页面。这些行为不足以支持清楚、独立的 UDP 收发操作。

## 现在的操作

1. 左侧“本地接收绑定”配置本地 IPv4 和端口；端口 0 由系统分配，实际端点显示在工作台标题下。
2. 下方发送区有明确的“UDP 发送目标”：目标 IP/域名、目标端口。首次绑定使用填写的目标，目标就绪后主动发送。
3. 绑定期间可编辑目标并点击“应用目标”。新目标解析期间禁止发送；成功后恢复发送，解析失败仍保留本地接收。字段已修改但未应用时，发送按钮禁用，避免误发到旧地址。
4. 切换目标前需停止周期任务并等已接受的发送队列排空。已接受的数据不能被改发到新目标；没有重绑、清空统计、停止记录或切换会话。

UDP 无需建立 TCP 式客户端连接；“目标就绪”只表示地址已准备好，不表示对端在线。当前仍为 IPv4。TCP 客户端的连接超时与侧栏目标配置保留，UDP 不显示连接超时、TCP 客户端选择/广播/断开操作。普通显示、表头和提示使用 UDP 数据报/端点语义，UDP 来源列不再附带内部连接编号，文本页称为“文本预览”。协议仍由方案决定，未恢复侧栏协议选择器。

## 实现与验证

- `NetworkEngine::setUdpTarget()` 在 admission 锁内检查绑定状态、正在解析的请求以及全部待发送描述符（包括零字节数据报）；目标变更和发送提交在同一 I/O 队列上排序。每次最多一个目标解析，失败不关闭 UDP socket。start/stop/终止错误同步清理就绪状态，旧代次结果不能重新启用发送。
- `SessionController::setUdpTarget()` 验证目标、拒绝周期运行中切换，并只更新当前目标字段；新就绪事件进入有界 SYSTEM 记录，不增加 RX/TX。新增事件对应的文本映射已补全，并对未知事件使用安全回退。
- 发送区复用实际目标控件，UDP 放在底部独立一行，TCP 客户端仍放在左侧。切回协议时恢复父布局，精确配置持久化继续使用原 schema。
- 小窗口/软件缩放下检查控件完整处于所有父容器内，并断言发送按钮与更多/历史入口不重叠。截图检查发现的初始底部裁剪已修复，未放宽断言。

新增真实 socket 网络回归验证：零字节数据报已排队时拒绝目标变更；原数据发到原端口；队列排空后发送到第二端口；两次均使用同一本地端口；TX 元数据中的目标正确。IPv6 字面量在 IPv4 resolver 上实际失败后，socket 继续接收真实 UDP，再应用有效目标恢复发送，停止后拒绝变更。

新增原生 UI 回归使用两个真实 UDP 对端，验证明确的配置控件、TCP 项隐藏、域名及 IPv4 目标、实际发送/回包、未应用目标禁止发送、无效端口拒绝、会话代次与本地端口不变、统计保留、记录不断、周期任务锁定目标、目标保存，以及切回 TCP 布局。Qt Test 总数包含初始化/清理；原生各轮的 3 个结果对应 1 个综合业务回归。

最新完整测试见 [CTest](../validation/udp-fix/ctest.txt)，网络 **10 组**、会话 **27/27**、UI **32/32**。原生 UDP 回归在 [100%](../validation/udp-fix/native-1.txt)、[125%](../validation/udp-fix/native-1.25.txt)、[150%](../validation/udp-fix/native-1.5.txt) 均为 **3/3**，包括 1100×760 DIP 的实际容器裁剪与重叠检查。最终源文件清单与验证状态见 `docs/validation/udp-fix/source-hashes.json` 和 `result.json`。

截图：[深色](../validation/udp-fix/udp-target-dark.png)、[浅色](../validation/udp-fix/udp-target-light.png)、[小窗口](../validation/udp-fix/udp-target-small.png)、[150% 小窗口](../validation/udp-fix/scale-1.5/udp-target-small.png)。以上截图已实际打开检查；软件缩放不代表真实多显示器 DPI 切换认证。

早期会话回归还暴露了 fixture 将 Bound 当成发送目标就绪，以及在启动 SYSTEM 事件完成之前开启“精确一条记录”采集的假设。Peer 现在等待 UdpTargetReady；三处需要精确文件/显示条数的 fixture 等待启动事件完整发布后再开始操作。原条数断言保持不变，失败尝试保存在 `ctest-before-fixture-readiness.txt` 和 `ctest-before-capture-startup-wait.txt`。

早期验证发现新增就绪事件未加入 SYSTEM 名称表，导致回归崩溃；已补全映射并加安全回退，初始失败保存在 `ctest-before-event-name-fix.txt`。另一次把构建和多个测试串联在单次工具调用中超过 60 秒上限，保留 `ui-tool-timeout.txt`，之后通过后台驱动完成最终验证。这些早期尝试不作为通过证据。

## 交付

当前修订 `udp-fix-1`，产品版本 0.1.0。新版部署到 `dist/PortBridge-udp-fix-1/PortBridge.exe`，并更新 Windows x64 ZIP；原 `dist/PortBridge/PortBridge.exe` 因正在运行而无法覆盖，保留当前会话。上一版 ZIP 保留于 `build/package-backups/audit-fix-1/`。打包仍检查 Release/部署/解压 EXE 一致、文件哈希、系统 PATH 下启动及 UDP/TCP 各 500 帧实际回环，最终归档证据在 `docs/validation/udp-fix-zip.json`。历史 audit-fix-1 结果与清单保留，不能当作本次新增功能的独立复核；本轮由协调者直接修复，未新增子 agent。

本轮未进行物理串口、双机 2.5G、长时间记录、干净 Windows 或永久阻塞 OS 调用验收。原 Qt Socket 丢失原因仍未定位。
