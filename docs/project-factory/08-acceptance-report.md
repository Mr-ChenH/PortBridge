# PortBridge 0.1.0 软件实施与验收报告

> 当前修订为 **profile-picker-1**：连接方案改为当前卡片和按需搜索弹窗，后台活动方案提供一键返回；搜索、真实点击和键盘选择均保持 UDP 绑定、接收与记录。最新证据见 [方案选择优化报告](../reviews/profile-picker.md)。以下为历史交付记录。

> **text-diagnostics-1**：字节详情新增 UTF-8 文本，让中文发送/接收可阅读且分页完整；保留实测接收/记录诊断，移除两个无数据来源的“未知”占位，改名“接收诊断”。最新证据见 [文本与诊断修复报告](../reviews/text-diagnostics.md)。以下为历史交付记录。

> **udp-lifecycle-1**：UDP 绑定和关闭仅更新状态，不生成数据样本/原始采集的 SYSTEM 消息，不发送业务数据报；错误诊断保留。最新证据见 [生命周期消息修复报告](../reviews/udp-lifecycle.md)。以下为历史交付记录。

> **composer-fix-1**：串口、TCP 客户端、TCP 服务端、UDP 统一扩大多行发送输入区，支持全宽输入、拖动分隔条和展开/收起，保留发送规则。最新证据见 [发送区优化报告](../reviews/composer-fix.md)。以下内容为历史交付记录。

> **direct-send-1**：收发改为绿/橙配色及不同背景，UDP 删除“应用目标”步骤，直接按每次点击时的地址发送；修改配置不发 UDP 数据报。最新证据见 [直接发送修复报告](../reviews/direct-send.md)。以下内容为历史交付记录，物理及性能验收边界不变。

> **interaction-fix-1**。浏览方案不再停止活动 UDP 绑定，文本预览支持收发分色和格式化，ASCII/HEX 可分页滚动查看完整字节；最新证据见 [交互修复报告](../reviews/interaction-fix.md)。以下 **udp-fix-1**，UDP 本地接收绑定与发送目标已分开，绑定后可单独切换目标；移除 UDP 页面的 TCP 专用参数和混杂说明。最新修复、真实 UDP 双目标测试与截图见 [UDP 修复报告](../reviews/udp-fix.md)。下述 **audit-fix-1**。原先“功能全部完成”的结论被后续设计/功能审核发现的 A01～A07、G01～G04 修正；本轮已按审核清单修复并复验。最新证据以 [审核修复验收](../reviews/audit-fix-acceptance.md) 为准，以下旧版本统计仅为历史记录。真实串口、两机2.5G、持续磁盘/声明负载、干净Windows和病态OS阻塞退出仍未认证。

## 审核修复（audit-fix-1）

Orca Run `run_f131406b8711` 累计创建 3 个子 agent，复用已有终端完成独立复核，没有新增第四个。修复方案切换后的统计/字节/错误归属、新建方案编辑丢失、持续周期入口、实际目录刷新和偏好恢复；补齐 TX 速率、独立截断计数、采集元信息、命令周期和定义明确的序号缺失率。连接配置、检查器、采集设置、诊断、命令/文件页和编辑对话框按设计重新对齐；显示样本导出和卷查询移到有界后台任务。

最新完整 Release CTest **3/3，零失败，65.37 秒**：网络 **9 组**、会话 **27**、UI **31** 个 Qt Test 结果。最终测试代码冻结后 UI 单套再通过 **31/31**；协调者独立审核探针 **13/13**，独立会话 **12/12**，Qt Test 数量均含初始化/清理。独立 UI 的完整原生/软件缩放结果见修复报告。截图和设计/修复前/修复后对照在 [audit-fix](../validation/audit-fix/comparison.html)，当前源输入见 `../validation/source-manifest.json`。

## 历史：后续 UI 设计细化（ui-refinement-2）

按设计原型完成原生组件细化并更新同版本运行目录和 ZIP。最新全量 CTest 为 **3/3、零失败、45.96 秒**；网络 **8 组**、会话 **19 Qt Test 结果**、UI **21 Qt Test 结果**，Windows 原生及 125%/150% 软件缩放各 **21/21**。新增重复选择/导航一致性和真实 24×1472B UDP 数据、8B 实际发送、默认字节检查器无水平溢出验证。最新记录见 [UI 设计细化验收](../reviews/ui-refinement.md)，截图在 `docs/screenshots/ui-refinement/`，源清单和发布清单已更新。连接配置已移除额外的“通信方式”下拉框，由选中的方案决定协议；标题右侧显示只读协议标识，新建/编辑方案仍可选择协议。

以下独立审查、19项 UI 与 48.21 秒测试结果保留为首版历史证据；最新 UI 修改由协调者实施与验证，没有创建新子 agent，也没有增加物理硬件验收结论。

## 范围与协调

本轮是 Windows/IPv4、Qt Widgets/C++17、Qt6.8.3/MinGW13.1.0 首版，实施 FR-001～022。增强功能（GBK、桥接、文件发送、多活动会话、脚本等）保持后续范围。

Orca Run：run_27bdb63ff03a。只创建3个Pi子agent；network/session/ui第一波开发后，在原有terminal复用做交叉审核和修复，没有第四个agent。最初未配置Codex launcher的preflight拒绝未创建Task/Dispatch。以下6个实际任务均成功结算；3个子agent terminal已释放并归档，协调者terminal保留。项目原本没有Git仓库，本轮未初始化仓库或创建提交。

| Orca任务 | 内容 | 最终证据 |
| --- | --- | --- |
| task_db0dd682bb98 | 网络实现 | network-implementation.md |
| task_186a15698a7e | 会话/串口/采集/配置 | session-implementation.md |
| task_862f978ccfad | 原生UI实现 | ui-implementation.md；保留初始15项历史证据 |
| task_a6e1e0d81012 | 独立数据审查及授权Windows UDP修复 | independent-data-review.md；含旧/新复现和18个当时快照哈希 |
| task_577b5d81fbe0 | 独立UI/网络/压力工具审查与复验 | independent-ui-review.md、independent-network-review.md；最终源哈希 |
| task_2ef307bcacad | 四项UI发现修复 | ui-fixes.md；包括恢复非零方案选择的精确保存回归 |

## 软件任务账本

| ID | 结果 | 证据 |
| --- | --- | --- |
| TASK-001 | 完成；版本固定、独立SerialPort、匹配工具链及CMake | scripts/build.ps1；依赖提交见THIRD_PARTY_NOTICES.md |
| TASK-002 | 完成；TCP客户端/多客户端服务器、UDP、取消/超时/背压 | src/network；8组真实回环/资源生命期测试及独立网络审查 |
| TASK-003 | 完成软件；串口线程、会话隔离、编码/有限周期/断线停止 | src/session；19项QtTest；真实不存在COM打开错误；成功硬件收发待测 |
| TASK-004 | 完成软件；原始二进制、后台写、限额/轮转、目录与可取消导出 | recorder；100条Windows完整性观察、TCP原字节重建、19项测试 |
| TASK-005 | 完成；版本化配置/命令、严格验证、原子写及精确字段保存 | configuration；核心/UI回归及完整JSON不变比较 |
| TASK-006 | 完成；Qt工作台、连接/发送、列表/详情 | 最终19项QtTest、原生深浅截图、独立focused8项 |
| TASK-007 | 完成；高速/诊断/采集/命令库、主题/快捷键 | 同上；125%/150%最终各19项 |
| TASK-008 | 完成；当前Release及全部CTest通过 | 3/3、48.21秒、0失败；日志在docs/validation及build/release |
| TASK-009 | 完成；独立收发/自回环、序号/校验与机器可读条件 | tools/network_bench.cpp；最终localhost JSON与独立真实注入 |
| TASK-010 | 完成；数据、UI及网络有效发现均修复且独立复验 | docs/reviews；固定旧/新复现和编译源哈希 |
| TASK-011 | 完成软件交付；便携部署、ZIP、说明/许可/证据 | dist/PortBridge；移除Qt环境的启动/500UDP检查；干净系统待测 |

## 功能及验收映射

“软件通过”仅指本文列出的功能证据，不将未测硬件或异常OS条件标为通过。

| 需求 | 软件实现/证据 | 边界 |
| --- | --- | --- |
| FR-001～003 | 真实QSerialPortInfo、可编辑端口/参数、独立QThread与可观察错误；不存在COM路径测试 | 成功硬件收发/占用/拔插/流控/高baud待测 |
| FR-004 | Asio异步IPv4解析/连接、deadline/cancel、拒绝测试；GUI不等待I/O | 外部DNS及病态OS阻塞退出未注入 |
| FR-005～006 | 4实际客户端定向/广播/选择性断开、稳定公共ID；UI选择即启用断开、移除即禁用 | 高速多连接累计负载未验收 |
| FR-007～008 | 实际本地端点/IPv4接口、多个UDP来源/零/大报文；Windows absent-peer保持绑定/接收/TX | 绑定不是peer在线；kernel/NIC drops未知 |
| FR-009～011 | 严格HEX/ASCII/UTF8/换行、字节预览、原字节模型、保留块连续解码 | 高速为抽样；实际ordinal缺口重置解码，重置统计不重置解码 |
| FR-012 | 时间/方向/来源/稳定ID、RX/TX/SYSTEM；控制事件UTF8 JSON可离线重建 | 软件时间；TCP/串口为stream chunks |
| FR-013～014 | 文本/HEX/source过滤、原字节复制/样本导出、自动滚动、暂停/清空独立于通信/记录 | 字节/记录上限与明确裁剪 |
| FR-015～016 | 实际收发/速率/队列、独立统计重置、有限/无限周期、冻结输入/target及断线停止 | 接受≠本地写出≠peer确认；已进栈的数据可能完成 |
| FR-017 | 50条/2MiB历史、命令CRUD/版本化导入导出、选择只加载 | 启动不自动发送 |
| FR-018 | PBCAP001/DATA/DONE、meta目录、原字节/来源/方向、轮转/时长、后台JSON进度/取消 | JSON非满速实时记录；无加密完整性/断电持久化认证 |
| FR-019 | QSettings偏好、原子JSON、版本/类型/大小拒绝、精确字节/全部范围保存、UDP改TCP规范化 | 启动不连接；导入失败保留原集合/文件 |
| FR-020 | generation/存活门禁、周期停止、后台退休/收尾、导出取消及有界3秒等待 | 病态OS DNS/文件I/O未证明线程实际退出；未完成不标完整 |
| FR-021 | 独立Asio/Serial I/O、50ms快照、有限原字节样本、后台记录 | 2.5G声明负载下响应/吞吐/CPU内存待两机验收 |
| FR-022 | 应用接收/UDP datagrams/drop/truncated/record/display分开；显式UDP uint64序号、wrap/窗口/eviction | kernel/NIC/持续磁盘未知；本地序号不证明远端交付 |

| 非功能/核心验收项 | 结果及证据 |
| --- | --- |
| NFR-001～005、012～013 | 软件功能通过：独立I/O、容量/描述符上限、TCP同块背压、UDP明确丢弃、完成才TX、独立身份/解码、Qt-free后端；实际声明负载待测 |
| NFR-006～008 | 软件功能通过：缺失设备、拒绝/超时、记录目录/写错、格式/旧输出保护、状态和配置；独立4项UI问题已闭环 |
| NFR-009 | Windows64首发；Linux/macOS/IPv6不作未经验证的兼容承诺 |
| NFR-010 | 本机隔离部署通过；干净Windows尚未通过验收 |
| NFR-011 | localhost功能基线通过；真实2.5G及后端对照/包长/PPS/突发/双向/记录条件未测 |
| AC-001 | 未完成物理验收：成功串口字节回环/热拔插需硬件；已验证真实缺失COM错误 |
| AC-002～004 | 软件通过：TCP任意切分及1,048,580字节采集复建，4客户端，UDP多个来源/空数据报 |
| AC-005～009 | 软件通过：编码/周期/暂停清空/损坏配置/记录恢复，独立UTF8/目标状态/精确值/保存回归；实物条件单列 |
| AC-010～011 | 部分/未完成：本机有界功能、退出/部署/缩放通过；声明负载/干净Windows/真实2.5G未验证 |
| AC-012 | 软件过载通过：消费者拒绝、TCP保持块/背压、UDP明确丢弃、录制/显示分开；真实持续磁盘饱和/满负载UI停顿未测 |

## 最终构建与测试

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Parallel 4 -SkipTests
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/deploy.ps1
```

环境：Qt C:/Qt/6.8.3/mingw_64；GCC C:/Qt/Tools/mingw1310_64；QtSerialPort prefix .deps/qtserialport-install；Asio1.30.2。共享头、源文件、依赖和验证入口均可在工作区检查。QtTest计数含初始化/清理，不称为同等数量的独立业务用例。

| 验证 | 最终结果 | 主要证据 |
| --- | --- | --- |
| root Release/CTest | 3/3，0失败，48.21秒；network8组、session19、UI19 | docs/validation/ctest-final.txt、session-final.txt、ui-final.txt、network-final.txt |
| 独立最终native UI | 19/19；13.117秒 | independent-ui-review.md；build/ui-independent/existing-ui-final-native.txt |
| 未修改独立focused | native8/8、3.198秒；offscreen8/8、1.278秒 | 同报告；focused-final-{native,offscreen}.txt |
| 最终源码offscreen125%/150% | 各19/19，7.372秒/7.240秒 | docs/validation/ui-scale-{1.25,1.5}.txt；build/final-ui-scale-*图片 |
| 独立最终网络/工具 | 8组全通过；5个短真实benchmark/注入；6代实际TCP旧池为0 | independent-network-review.md |
| 去Qt环境发布运行 | Windows native深/浅启动成功、UDP500/500、exe与Release一致 | docs/validation/package-smoke.json、package-udp.json |

核心捕获/序号独立审查的固定快照19项全通过。此后的UI/池修复补充由最终独立UI/网络报告和root全部CTest覆盖；旧报告哈希代表当时快照，最终source-manifest.json才表示本次全部软件输入。

## 修复闭环

1. Windows UDP10061曾关闭本地绑定并抑制完成TX。现在异步peer错误可见但非终止，重投接收；真实测试同绑定收到另一来源、完成64+13B TX并释放预算。
2. 捕获complete曾早于meta原子提交发布。修正收尾/flush/目录提交顺序，Windows固定观察：premature0、admitted100、recorded100、failures0。
3. uint64序号[MAX-1,MAX,0,2]现missing1/reordered0；65来源各[100,102]现RX130/missing65，淘汰先finalize。
4. recent catalog最多1024，后台流式候选加载；1028文件测试保留磁盘旧文件、支持旧文件导出和有界重载。
5. UI曾改写精确预算/timeout，现完整schema值按字节保持，未编辑关闭和恢复非零选中方案均不变；实际修改可保存。
6. UI以resettable omission计数判断UTF8连续性，现采用lifetime ordinal；重置统计保持E4 B8 AD为中，真正缺口仍重置并标注。
7. 服务端选择/消失客户端的Disconnect状态立即正确；sequence-enabled UDP改TCP先清理不适用flag再原子保存并可重启。
8. tiny payload曾强持有已退休的整个free pool。六代实际TCP旧代码用1536B样本钉住12,582,912B大块；weak pool deleter修复后每代旧大块为0、样本仍有效。永久回归覆盖多消费者期间不复用、释放后复用、free cache随owner释放及payload可独立存活。
9. 其他针对性修复包括容量计费、TX拒绝不重发、serial终止connecting、UDP-only分析、SYSTEM原记录、配置大小一致、大小写路径保护、后台可取消导出和有界teardown。

完整发现优先级、旧/新证据、锁/生命周期和哈希在docs/reviews。独立审查没有发现尚未闭环的具体软件阻塞缺陷；这不是对所有OS/硬件故障的证明。

## 最终localhost基线

--mode self-loop、--duration2、--drain-ms500、UI/recording=false：

| 传输/条件 | 完成TX/有效RX | 缺失/校验失败 | 接收负载率 | 证据 |
| --- | --- | --- | --- | --- |
| UDP1472B，请求20,000frames/s | 40,000/40,000 | 0/0 | 29.21 MB/s | docs/validation/localhost-udp.json |
| TCP16,384B，请求5,000frames/s | 10,000/10,000 | 0/0 | 81.33 MB/s | docs/validation/localhost-tcp.json |

这是本机功能基线，未找最大速率、没有物理网卡链路，未比较Qt/Asio/原生性能。JSON明确physical_2_5g_validated=false、kernel_or_nic_drops=null。请求速率、实际完成、admission retries、高水位和实际socket缓冲分开；真实硬件矩阵按10-high-throughput-plan.md执行，不将该数字推断为2.5G能力。

## 发布及尚待外部验证

运行目录dist/PortBridge，便携ZIP dist/PortBridge-0.1.0-windows-x64.zip；解压后双击PortBridge.exe，启动不自动连接/发送。README/第三方许可/设计与审查/验证文件随包提供。源码仍在工作区；包内历史开发日志路径若指向build/，须在工作区查看。

独立SerialPort通过项目deploy SDK overlay供windeployqt发现，不改全局Qt SDK。Qt/DLL/plugins/qt.conf与MinGW/GCC-runtime-exception等许可一起部署。最终程序在PATH只含Windows系统路径且删除QT*环境变量后启动深/浅截图成功，打包工具收到全部500条UDP；这是本机运行时隔离，不等于干净系统认证。ZIP/source/runtime哈希和内部清单便于检查。

原生Windows深浅截图：docs/screenshots/native-windows-{dark,light}.png。实际2-client TCP、小尺寸1100×760/1280×900和缩放图片在owner/reviewer/final-scale构建证据中；HTML继续作为模拟视觉参考，没有将原型虚构指标注入产品。

剩余外部验收：真实串口成功回环/拔插/占用/流控/高baud及1小时持续运行；两机2.5G不同payload/PPS/突发/方向和UI/记录条件；持续磁盘速度/饱和；干净Windows；真实多屏DPI变化；OS DNS/文件I/O异常阻塞和进程退出fault injection。正常软件路径及QT_SCALE_FACTOR缩放不能替代这些项目。Qt部署工具仍提示dxcompiler/dxil未发现；本机Widgets实际启动成功，干净Windows需另行验证。用户原Qt Socket丢失原因仍未定位，本轮不把换库或localhost零缺失当作原因分析。
