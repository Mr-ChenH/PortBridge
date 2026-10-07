# 中文字节详情与接收诊断整理（text-diagnostics-1）

用户反馈文本发送后 ASCII 区显示“......”，随后要求检查丢失诊断是否有用，无用则移除。本轮新增 UTF-8 字节详情用于阅读中文，保留真实字节的 HEX/ASCII；检查实际诊断数据来源后移除两个无数据来源的占位，保留可验证的计数并改名为“接收诊断”。版本仍为 0.1.0，交付修订为 `text-diagnostics-1`，当前协调者直接完成，没有新增子 agent。

## 文本显示

ASCII 检查器原先逐字节仅显示 0x20–0x7E，其他字节统一显示点。中文是 UTF-8 多字节字符，因此出现多个点；这不会把实际发送内容改成点。保留这个逐字节视图，增加 **HEX / ASCII / UTF-8** 三个页签。选中新记录时，首屏解码有效且包含非 ASCII 字符就自动显示 UTF-8；也可手动切换。选择中文、切换 UTF-8 或翻页后会自动滚动外层详情，让文本正文可见，不被上方元数据挡在 viewport 下方。

UTF-8 可阅读中文、英文、emoji 和多行内容；ASCII/HEX 仍严格对应原始字节。无效 UTF-8 显示替换符并明确提示错误，避免静默把二进制当作文本。NUL、CR 等控制字节以 `\xNN` 展示，LF/TAB 保留文本排版；转义只影响显示。没有加入 GBK/GB18030 发送或解码。

每页仍为 4096 原始字节，UTF-8 文本按完整字符处理边界：跨页字符在起始字节所在页完整显示，最多借用后续 3 字节；下一页跳过这几个续字节，避免重复或错误替换。边界调整前严格验证完整 Unicode 标量，拒绝过长编码、代理范围及超过 U+10FFFF 的序列；无效字节留在原页并报告。详情工具提示显示实际文本解码范围，HEX/ASCII 范围和复制内容不变。仅解码当前页，保持约 4099 字节的解码上限；TCP/串口不同记录之间的半个字符应在原有文本流中查看，不把独立读取块假装成完整协议报文。

截图：[发送中文/emoji 的 UTF-8 详情](../validation/text-diagnostics/utf8-sent-chinese.png)、[中文跨页后的尾部](../validation/text-diagnostics/utf8-page-boundary-tail.png)。已实际打开检查。

## 诊断实际作用

原页并非全部无效：

| 指标 | 数据来源及用途 |
| --- | --- |
| 应用接收 | 实际 RX 字节数/UDP 数据报数 |
| 接收截断 | UDP 接收缓冲不足事件，独立计数 |
| 应用队列丢弃 | 本地有界队列不能保留的真实记录/字节 |
| 原始记录 | 实际写出量、记录失败、队列峰值 |
| 显示抽样 | 暂停/抽样/缓存省略，用于区分界面省略与业务数据丢失 |
| 协议序号 | 显式启用连续 uint64 分析后计算确认缺失；普通文本不自动推断丢包 |

发送端总量、网卡/内核丢失没有接入外部计数，旧页固定“未知”；这两行已移除。页面改名 **接收诊断**，只呈现实测管线计数，并说明网络丢失仍需要发送端/系统证据。没有把零丢弃或零失败等同于网络完整。

真实 UDP 测试收到 4112 B 后直接核对诊断概要与实际 RX 一致；截断专项继续检查独立计数及方案隔离。序号窗口、重复/乱序、回绕、饱和和记录过载等既有会话测试仍保留。截图：[实收数据后的诊断](../validation/text-diagnostics/receive-diagnostics-live.png)。

## 验证

新增综合回归以界面主动发送 `中文消息 Hello 😀\n第二行`，对端逐字节核对 UTF-8 内容，选中 TX 后默认 UTF-8、正文可见且原文相同；ASCII 保留点、完整 HEX 复制不变。实际接收 4095 个 A 加中文/emoji 尾部，逐页确认中文不乱码、不重复、字节范围不变。无效 UTF-8/NUL 提示与显示、换记录/清空生命周期、4 字节字符的三种跨界位置、过长/代理/超范围/截断编码均验证。

完整 CTest **3/3**：网络 **11** 组、会话 **27/27**、UI **38/38**，Qt 数量包含初始化/清理。构建无编译警告，最终源码前后哈希相同。证据：[CTest](../validation/text-diagnostics/ctest.txt)、[结果](../validation/text-diagnostics/result.json)、[源哈希](../validation/text-diagnostics/source-hashes.json)。

Windows 原生 100%、125%、150% 专项各 **6/6**：UTF-8/诊断综合回归、既有分色/完整 ASCII、实际统计与截断、原生布局四项测试及初始化/清理。[100%](../validation/text-diagnostics/native-1.txt)、[125%](../validation/text-diagnostics/native-1.25.txt)、[150%](../validation/text-diagnostics/native-1.5.txt)。截图已检查，软件缩放不等同物理多显示器 DPI 验收。

中间回归全通过但收尾时源文件变化，源稳定检查拒绝认证，结果保留为 `status-before-source-freeze.json` / `ctest-before-source-freeze.txt`。第一批原生截图暴露右侧正文在外层滚动区域下方，保留 `native-before-text-visibility.txt`；进一步的 `utf8-viewport-probe.txt` 验证 Qt 默认滚动仅保证光标位置，未保证整个多行正文区域。改为按文本区域定位外层滚动后，`utf8-viewport-after.txt` 通过实际 viewport 包含断言，随后重新冻结、构建、验证；首轮可见性失败证据保留在 `ctest-before-viewport-layout.txt` / `ui-before-viewport-layout.txt`。

## 交付

新版路径 `dist/PortBridge-text-diagnostics-1/PortBridge.exe`；不结束用户旧进程，前版 ZIP 和 source manifest 已保留。新版 ZIP 仍为 `dist/PortBridge-0.1.0-windows-x64.zip`，前版归档于 `build/package-backups/udp-lifecycle-1/`。打包验证 Release/部署/解压 EXE 一致、全部文件哈希、受限 PATH 启动及 UDP/TCP 各 500 帧回环；最终归档证据位于工作区 `docs/validation/text-diagnostics-zip.json`。

本轮不增加物理串口、双机 2.5G、持续磁盘负载、干净 Windows 或病态 OS 阻塞退出的验收结论。
