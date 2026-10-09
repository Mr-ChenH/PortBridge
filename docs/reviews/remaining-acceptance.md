# PortBridge 剩余验收补充报告

> 后续 **ui-latency-fix-1** 已关闭本机声明负载内RA-UI01/P2：五分钟UDP/TCP心跳152/43ms，完整10/10回归、逐字节采集、实际跨屏和主动GUI停顿复验通过。见 [响应修复报告](ui-latency-fix.md) 与 [新机器回执](../validation/ui-latency-fix/receipt.json)。以下正文和原回执保留本轮补充验收当时的结果，434/2364ms及其他失败仍为失败，不改写为通过。外部条件限制保持。

依据用户“根据审核的结果，将没有完成的任务完成”的授权，补做本机可执行的实际验收并提供可重复入口。用户确认目前只有本机；软件基准仍为 `http-sequence-fix-1`，生产源码、当前EXE与ZIP保持原版本。新增代码为独立验收探针及驱动，不加入普通CTest。

**本轮为部分完成：生产10分钟截止、依赖阻塞退出、实际跨屏DPR同步，以及UI停顿期间原始接收/实盘推进已验证；长期界面心跳200ms门槛未通过，新增RA-UI01保持打开。** 物理串口、双机2.5G、干净Windows和原Qt Socket根因仍受条件限制。不能将未达标的界面响应或缺少设备的项目标为完成。

本轮已实际完成生产HTTP顺序10分钟截止和受控依赖阻塞退出验证。持续工作台/实盘、跨屏与UI停顿的证据追加在机器回执；物理串口、两机2.5G、干净Windows和原Qt Socket根因继续受条件限制。执行工具完成不等于所有外部验收通过。

## 已完成的专项

### RA01：真实10分钟截止

执行的是生产 `HttpSequenceRunner` 的600000ms计时器，安排128步，每次本地HTTP响应实际延迟47秒。**墙钟599996ms时自动停止**：12步通过、1步取消、115步跳过，HTTP资源释放，没有后续请求。再观察15001ms，晚到服务计时器未改变最终报告。Qt普通计时器存在调度精度，本轮记录实际值，不改短计时器冒充10分钟。

证据：[实际截止回执](../validation/remaining-acceptance/deadline-result.json)。此项关闭原审核HS04的“只检查源码、未实跑”缺口。

### RA04：DNS/文件写入阻塞与退出

在独立子进程内拦截指定测试域名的 `getaddrinfo` 和QtCore对 `PBCAP001` 的 `WriteFile`，等待事件直到显式释放。生产SessionController与Recorder销毁约3秒返回，阻塞仍在进行；晚释放后进程安全，记录器报告 `Shutdown finalization exceeded 3 seconds; capture is incomplete`。

另两项保持工作线程阻塞直到进程退出，父进程15秒门槛内退出码均0，文件元数据保持未完成。四项实际结果分别约3025/3011/3029/3007ms销毁，父进程用时约4.243/3.125/3.127/3.101秒。没有修改生产DLL或系统DNS配置；导入地址表只在探针进程中改动。

证据：[四项驱动回执](../validation/remaining-acceptance/fault-final/receipt.json)、[DNS不释放退出](../validation/remaining-acceptance/fault-final/dns-exit-stall-result.json)、[文件不释放退出](../validation/remaining-acceptance/fault-final/file-exit-stall-result.json)。这是可重复依赖API阻塞注入，关闭“没有fault injection”的验证缺口；真实OS永久挂起与异常硬件的覆盖边界继续保留。

### RA06：等待诊断

1000次连续start/stop在同一会话上复现994次清理保护拒绝，错误明确为 `Previous connections are still closing; retry after cleanup`，清理后新连接恢复。保护队列有界，不把拒绝误当已连接。另实际录入1028条、128字节轮转配置，在并行通信/实盘场景中约8628ms完成1028个文件，记录失败0，目录完整。

另最终可复用入口在原生负载测试结束后单独执行，通过编译快照、build产物目录和驱动集成检查。此次1028文件轮转耗时 **18775ms**，清理保护拒绝995次后恢复连接。这再次观察到旧测试15000ms时限不足的条件，且完成时记录无损失；尚未定位导致具体I/O延迟的系统因素。见 [最终入口回执](../validation/remaining-acceptance/final-entry-check/receipt.json)。该次原始探针context文字沿用早期并行标签，总回执已注明实际为单独执行，后续工具也修正为不预设并行负载。

证据：[等待诊断](../validation/remaining-acceptance/wait-diagnostics-result.json)。历史connected失败未保存当时lastError，因此这一可复现保护路径是候选原因，**不能据此宣布历史根因已证实**。历史采集等待15000ms不足、约18900ms才完成的记录继续保留；小文件创建/刷新/原子元数据提交涉及实际存储调度，本轮更快完成不证明历史原因已经消失。

## 持续通信、实盘与界面验证

最终探针使用正常 `QApplication::exec()`、独立发送线程、真实生产MainWindow与记录器；发送不依赖GUI推进。实际创建独立测试方案，配置记录队列16MiB、32MiB轮转，使用与产品相同的中文字体；进度由单独后台线程原子写入。固定屏幕持续负载与跨屏专项分开，每7秒实际点击一次“暂停／恢复显示”，同时记录心跳、事件耗时、RSS、采集及停止收尾。

UDP1472字节×1000报/秒、TCP4096字节×1000测试帧/秒各5分钟；这是声明清楚的本机功能与持续录制基线，不是2.5G验收。独立Python读取每个PBCAP001文件，重组跨块/跨文件TCP帧，核对每个原始字节、UDP边界、序号模式、DONE尾部和元数据；不只看实时统计。最终数值与结果见 [总回执](../validation/remaining-acceptance/receipt.json)。

探针早期版本在GUI线程写进度，并用手动processEvents循环与GUI线程发送，影响心跳观测；之后修正验收工具为后台进度、正常Qt主循环、独立发送。所有原始失败、迭代和轮转文件保留。诊断源码副本对周期刷新阶段的墙钟/线程CPU做了额外观察，没有修改产品源码，没有把副本结果当实际交付版本通过。

早期并行工作台加快速重启压力的5分钟回执达到TCP1228800000B、UDP441604416B录制，应用丢弃/记录失败/序号缺失均0，但心跳527/769ms，不能计为界面响应通过。手动事件循环中间版还有948ms失败。正常Qt主循环的首轮UDP与另一阻塞探针编译/执行有时段重叠，心跳434ms，也保留未通过200ms门槛的结果。后续通过不删除这些超标记录，不证明任意并行压力下响应达标。

### RA02：持续实盘通过，响应门槛未通过

| 指标 | UDP | TCP |
|---|---:|---:|
| 时长 / 实际发送完成测试帧 | 300秒 / 299990 | 300秒 / 299989 |
| 接收与完整落盘有效负载 | 441585280B | 1228754944B |
| 完成且逐字节校验的轮转文件 | 14 | 38 |
| 应用丢弃 / 记录失败 / 测试帧缺失 | 0 / 0 / 0 | 0 / 0 / 0 |
| 记录队列实际高水位（预算16MiB） | 1066572B | 6084786B |
| 显示管线采样峰值（预算2MiB） | 127624B | 839066B |
| RSS峰值 | 160108544B | 162869248B |
| RSS首/末一分钟中位数 | 156753920 / 157165568B | 156076032 / 146427904B |
| 实际暂停/恢复点击最大同步耗时 | 5ms | 146ms |
| 停止记录调用 / 最终收尾 | 0 / 21ms | 0 / 111ms |
| 正常GUI心跳最大间隔（目标小于200ms） | **434ms，未通过** | **2364ms，未通过** |

文件检查证据：[UDP逐字节校验](../validation/remaining-acceptance/production-loop-final/udp-capture-verification.json)、[TCP逐字节校验](../validation/remaining-acceptance/production-loop-final/tcp-capture-verification.json)。TCP238132个真实读取记录重组为299989个测试帧，未把读取块当报文。采集校验器另通过有效样本、payload篡改、截断尾部和未完成元数据四个正/反检查，见 [校验器检查](../validation/remaining-acceptance/capture-verifier-check.json)。显示省略计数与原始采集完整性分开，未把requested300000当实际完成数量。

**RA-UI01 / P2 / 打开：本机持续负载下GUI心跳偶发超出200ms。** 已记录慢事件（含 `uiSnapshotTimer`）；阶段诊断副本未稳定重现相同长停顿，生产代码归因和历史等待对应关系尚未证实。没有把一个推测性代码改动写成“已修复”。正常短检查、单次操作同步耗时达标及记录完整，都不能替代长时间最大响应指标。本轮RA02仅关闭指定负载的接收/实盘一致性，保留响应部分未通过；5分钟RSS结果也不证明无限时长无泄漏。

### RA03、RA05：实际跨屏与主动UI停顿

跨屏专项在本机三块实际屏幕间移动，窗口DPR和屏幕DPR在100%/125%均同步，可见且exposed，UDP/TCP各三个屏幕检查通过。整个UDP跨屏套件心跳228ms导致总状态failed，TCP心跳187ms、总状态passed；这里只认证实际DPR同步，不把UDP总结果写成通过。不包含修改系统缩放、物理150%和逐控件像素审查。

主动GUI500ms停顿期间，UDP原始接收/落盘都推进737472B；TCP原始接收推进1994752B、落盘推进2023424B（包含此前待写数据）。两项结束后所有采集字节/尾部/元数据均校验通过，丢弃/记录失败0。UDP正常心跳58ms，总状态passed；TCP正常心跳280ms，总状态failed，仍保留响应未通过。注入的500ms单列，不纳入正常心跳门槛。见 [跨屏回执](../validation/remaining-acceptance/dpi-final/receipt.json) 和 [停顿回执](../validation/remaining-acceptance/overload-final/receipt.json)。

## 当前条件与阻塞事项

[环境清单](../validation/remaining-acceptance/inventory.json) 显示没有COM设备；物理Ethernet未连接，Wi-Fi907Mbps，虚拟适配器标示速率不能当物理2.5G。有三块实际屏幕，可检查现有100%/125%跨屏DPR；Windows Sandbox程序入口不存在，不能因为存在VMware虚拟网卡就声称有干净Windows环境。

| 事项 | 本轮后的边界 |
|---|---|
| 物理串口FR-001～003 / AC-001 | 仍需设备、对端/回环、拔插/占用/流控/高波特率及115200bps一小时；当前无COM。 |
| NFR-011 / AC-011 / 正式2.5G矩阵 | 仍需第二台机器、真实2.5G链路与设备负载。localhost和虚拟网卡标示速率不替代。 |
| AC-010～012持续负载/磁盘饱和 | 本机5分钟指定负载与500ms UI停顿是新增证据；持续高吞吐磁盘饱和、所有并行负载的UI/内存/退出仍未认证。 |
| 物理DPI | 覆盖本机实际100%/125%与跨屏同步；不改变Windows设置，不声称物理150%和任意多屏组合已测。 |
| 干净Windows NFR-010 | 系统PATH-only历史证据仍有效；独立干净环境仍未提供。 |
| 永久真实OS/异常硬件 | 新增可控依赖API阻塞及子进程退出；真实系统永久故障仍有边界。 |
| 原Qt Socket与历史等待根因 | 缺原程序/负载/日志/抓包；保护路径已复现，历史对应关系未证实。 |

[执行指南](../remaining-acceptance-guide.md) 已补齐外部验收操作、两机发送/接收命令模板和实际完成数对照规则。RA07入口准备完成，外部执行条件阻塞继续保留。

## 交付与证据

新增 `scripts/validate-local-acceptance.py`、`scripts/verify-capture-stream.py` 和 `tools/acceptance/` 三个探针，提供deadline/soak/dpi/overload/faults/waits入口，允许固定UDP/TCP协议与30～3600秒复验。构建对象、EXE与大采集文件放在被忽略的build目录，报告保留哈希和移位索引。

最终探针格式化后再编译/链接通过，Windows/Winsock/PSAPI头文件依赖顺序已固定；格式化引起的编译失败记录保留。最终入口及采集校验器正/反检查实际执行通过。探针运行快照、后续格式化工具源码哈希分开保留，未把后续源码哈希当早期二进制编译哈希。

生产源87个冻结输入和10个测试EXE哈希复核与 `http-sequence-fix-1` 一致。本轮没有改产品，所以不冒称重跑完整CTest；此前10/10完整回归继续作为该同源同二进制的软件基准。新专项为实际新增执行。ZIP与交付EXE哈希未变，不重新打包；本轮补充文档和工具留在工作区。没有提交或推送。
