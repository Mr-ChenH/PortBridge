# PortBridge 审核修复：UI

本轮完成 UI 所有权范围内 A01–A07、G01–G04 和审核视觉/交互表的修复。修改文件为 `src/ui/main_window.cpp`、`src/ui/design_widgets.hpp`、`tests/test_ui.cpp`；本报告是唯一修改的评审文档。没有修改共享契约、session/network 实现或离线原型，没有使用 Git 或额外代理。生产 UI 和测试现已冻结，最终交付和全产品验收由协调器负责。

依据为 [原审核](design-function-audit.md)、项目需求/界面地图/UI 规范及离线 HTML/CSS/JS 原型。原审核及其失败证据保留，以下为新增修复证据。

## 功能修复与回归

| 审核项 | 修复后行为 | 主要正式回归 |
| --- | --- | --- |
| A01 / A07 | 构造、切换、新建、编辑、删除、导入统一走 `activateProfile()`，先调用 `selectConfiguration()`，再清理表格、文本解码/省略量、检查器、错误横幅/状态消息和趋势；选中方案同步到控制器，保持离线。 | `profileActivationClearsAllScopedStateAcrossCrud`：真实 UDP 100/102 序号与超长发送错误，逐条覆盖所有 CRUD/导入路径；统计和错误不归入新方案。 |
| A02 | 监听源/代理模型 reset、rowsRemoved、layoutChanged 和选择变化；过滤、清空、失选、缓存裁剪均清理独立字节区域，禁用两处复制。键盘复制也要求实际选中。 | `inspectorClearsOnFilterResetRemovalAndDeselection`；旧字节、解码、复制和有界缓存用例继续通过。 |
| A03 | 新建前保存旧方案有效编辑；新方案默认未连接，返回旧方案时有效参数仍在。 | 上述生命周期回归断言端口 34567 在控件和持久化方案中保留。 |
| A04 | 次数 0 原生显示“持续”，保留有限次数 1–1,000,000；次数/间隔完整可读，运行中锁定编辑，停止/断线/切换结束任务。 | `continuousPeriodicStopsOnManualDisconnectAndProfileSwitch` 使用真实 UDP 周期写出，核对停止后计数不再增长；原有限周期回归保留。 |
| A05 | 页面进入/手动刷新调用实际异步 `refreshCaptures(directory)`；UI 定时只读快照，内容未变不重建行，选择按路径保留。 | `captureRefreshSeesExternalFilesPreservesSelectionAndDeletes`：真实采集及 sidecar 复制到目录，刷新 1→2；外部删除后 2→1，原选择不丢失。 |
| A06 | 保存/恢复记录队列预算、高速模式、显示格式、自动滚动、间隔和次数，以及已有主题、窗口、分栏、方案和采集目录/策略；恢复时周期复选框关闭，不连接、不发送、不记录。 | `allPreferencesRestoreWithoutStartingWork`：运行中的连接/记录/周期关闭窗口后，77 MiB、333 ms、17 次等恢复，所有活动任务保持关闭。 |
| G01 | RX 指标下独立显示真实 TX MB/s；字节、待发送量仍分别显示。 | `txRateAndIndependentTruncationMetricFollowRealStatistics`：真实 50,000 B UDP 内容逐字节核对，并用短持续发送核对非零 TX 速率。 |
| G02 | 新增“接收截断”诊断行及完整说明，绑定 `receiveTruncatedDatagrams`，不混入应用队列丢弃。 | 同一指标回归通过现有 session 生产回调测试入口注入一次 `ReceiveTruncated`，确认界面 1 数据报、队列丢弃 0，切换后归零。该注入不是物理 UDP 截断证据。 |
| G03 | 采集行显示通信类型、格式化开始时间、时长、容量、条数、结束/失败状态和原始记录/元数据目录说明；元数据无效时类型/时长明确未知。每行导出和确认后后台删除，页头返回/刷新，更多菜单保留手动选择旧文件导出。 | 目录回归还覆盖无效 sidecar 的未知显示、行级真实导出和删除原文件/sidecar。 |
| G04 | 命令新增周期开关/间隔/次数编辑、CRUD、主动载入和 JSON 往返；命令载入只配置，不启动。完整保留 schema 间隔 1–86,400,000 ms 与次数范围。序号缺失率使用 `sequenceMissing / sequenceExpected`，仅 finalized positions 为分母，排除 pending window 和重复；分母 0 显示未知。 | `commandPeriodicCrudRowsChipsAndLocalizedDialogs`、`importedCommandPeriodicValuesPreserveFullSchemaRange`；序号 100/102、窗口 1 的 UI 回归确认分母 2、缺失率 50.0000%。 |

统计重置仍保留原始显示/跨块解码连续性，清空显示仍不停止通信/记录，原有 UTF-8、多客户端定向/广播、严格 HEX、精确非 MiB 配置和采集导出不覆盖原文件等回归继续保留。

## 视觉和交互证据

对比原 [UDP 工作台](../validation/design-audit/native-udp.png)、[TCP 客户端](../validation/design-audit/native-tcp-client.png)、[命令库](../validation/design-audit/native-commands.png)、[采集文件](../validation/design-audit/native-captures.png) 以及 [设计原型](../project-factory/prototype/preview-dark.png)，人工打开复核以下 Windows 原生 Qt 截图。截图使用受控临时配置、真实 localhost 字节及真实采集文件；测试命令只由测试 fixture 保存，不是产品预装指令。

| 区域 | 修复后证据与变化 |
| --- | --- |
| 连接侧栏 | [UDP](../../build/audit-ui/screenshots/audit-native-udp.png)、[TCP](../../build/audit-ui/screenshots/audit-native-tcp-client.png)：动作紧接基础字段并位于高级参数上方；TCP 为目标地址→目标端口→本地地址，本地端口保留在高级参数。CRUD/导入导出收入更多/上下文菜单，保留只读协议标记；无 `transportKind` 或侧栏通信方式字段。 |
| 标题与数据工具条 | 同图：状态徽标压缩，数字/单位/辅助文字分层并补小标签；HEX/文本位于显示提示行，保留搜索类型、文本流、暂停、统计重置和快捷键。 |
| 字节详情 | 同图：OFFSET、HEX、范围、ASCII 独立分区；仅格式化前 32 B，前 4 B 只有视觉强调，明确未做协议解析。1474 B 真实记录的 ASCII 在 1440×1000 默认可见，复制/样本导出保留完整字节。1472 B 完整复制由正式回归核对。 |
| 采集设置 | [采集设置](../../build/audit-ui/screenshots/audit-native-capture-settings.png)：纵向标签，目录可编辑/选择，轮转与时长并列，队列预算保留，容量估算有底色；实际空间有来源，持续写入能力保持未知。 |
| 诊断 | [诊断](../../build/audit-ui/screenshots/audit-native-diagnostics.png)：七个紧凑分隔行，未知与零区分；1440×1000 七层及详情入口可见。 |
| 发送与库页面 | [命令库](../../build/audit-ui/screenshots/audit-native-commands.png)、[采集页](../../build/audit-ui/screenshots/audit-native-captures.png)：真实可点击快捷 chips、更多/历史，持续次数、“无”换行；命令行提供载入/编辑/导出/删除，新增在页头右侧；采集行导出/删除，页头返回。修复自定义行和 delegate 重叠绘制。 |
| 对话框 | [命令](../../build/audit-ui/screenshots/audit-native-command-dialog.png)、[方案](../../build/audit-ui/screenshots/audit-native-profile-dialog.png)：中文保存/取消，纵向字段、可见内容标签和 placeholder；HEX 隐藏编码，文本显示编码；周期设置只配置，不自动运行。 |
| 小窗口与缩放 | [1100×760](../../build/audit-ui/screenshots/audit-native-1100x760.png)、[125% UDP](../../build/audit-ui/screenshots-125/audit-native-udp.png)、[150% UDP](../../build/audit-ui/screenshots-150/audit-native-udp.png)、[150% 小窗口](../../build/audit-ui/screenshots-150/audit-native-1100x760.png)：短窗缩小方案列表并允许滚动，连接和发送动作完整处于窗口及所属滚动 viewport 内。 |

尺寸按 Qt logical DIP 验证；1440×1000 的 125%/150% 截图为 1800×1250 / 2160×1500 物理像素。fixture 在 `show()` 后显式调整并断言尺寸，避免 Windows 初始摆放把窗口缩为屏幕可用高度。较小窗的完整字节/采集说明允许独立滚动；不承诺 1100×760 **物理像素** 在 150% 下等同 1100×760 DIP，也没有验证跨不同真实显示器的 DPI 迁移。

## GUI 文件操作

显示样本导出、空间查询、已有采集导出和新增采集删除各最多一个独立后台任务。样本导出在 GUI 上只复制有界模型中的不可变 payload handle/元数据，转换和分条 JSON 写盘均在 worker；QSaveFile 原子提交，转换/写出之间和提交前检查取消。目录查询每窗最多一个任务，合并最新路径，无效的旧路径结果不覆盖最新结果；父目录遍历最多 64 层。采集页内容未变时不重建列表。

任务捕获独立共享状态及路径，不持有 QWidget/控制器；关闭时设置取消，每条线程最多等待 1 秒，未返回的 OS 调用可安全分离。`sampleExportCancellationImmutableSnapshotAndClose` 核对取消/关闭不覆盖已有输出，导出开始后清空模型仍导出正确不可变快照，并核对最新目录结果；原采集后台导出原子取消与安全有限等待测试继续通过。

受控 completion stall 测试证明等待和独立状态寿命有界，不等同真实 SMB/坏盘故障注入。OS 文件调用无法被硬中断；安全分离保证 UI 不无限等它，不能保证故障 I/O 最终完成。持续磁盘吞吐、磁盘饱和及 2.5G 负载仍待物理验证。

## 最终验证

使用隔离 `build/audit-ui`，Qt 6.8.3 / MinGW 13.1、SerialPort 本地安装，`QT_QPA_FONTDIR=C:/Windows/Fonts`。最终生产编译无新增警告。

- [最终完整 UI 日志](../../build/audit-ui/final-ui.txt)：31 通过、0 失败，19.877 s，含 init/cleanup；共 29 个业务/验证函数，截图函数在未设置截图目录的 offscreen 全套中只执行入口。
- [Windows 原生最终子集](../../build/audit-ui/native-ui.txt)：6 通过、0 失败，覆盖原生取证/布局、schema 边界往返、真实持续停止及真实 TX/独立截断可观测性，含 init/cleanup。
- [125%](../../build/audit-ui/scale-125-ui.txt)、[150%](../../build/audit-ui/scale-150-ui.txt)：各 5 通过、0 失败，覆盖原生取证/布局、schema 边界宽度及持续发送，含 init/cleanup。
- 旧全套 Windows 原生运行 29/29 通过，见 [旧完整原生日志](../../build/audit-ui/final-native-ui.txt)；该日志早于最后两个新增用例，不作为最终 31 个结果的替代证据。
- 协调器另报最终全产品 CTest 3/3 通过（65.37 s），其中 root UI 31/31；其最终来源守卫、其他组件回归及发布包由协调器报告。

Qt offscreen 对话框的 `propagateSizeHints` 提示仍出现在相应日志；125%/150% 的 Qt 原生日志有一次 Fixedsys DirectWrite fallback 提示。已人工检查实际中文/等宽字段截图，未见缺字或行动控件裁剪，未据此宣称任意字体/显示器组合完成验证。

UDP “已绑定”仍只代表接收端绑定；目标解析/发送 admission 异步完成，不能等同对端在线。新增 TX fixture 对初始发送使用 guarded admission retry，仅成功提交一个初始数据报；随后短持续发送避免独立 50 ms 统计/显示时钟遗漏单次瞬时速率。截断回归使用受控 session 回调注入，物理截断、真实串口、两机 2.5G、持续坏盘/网络盘和干净 Windows 不在本轮证据范围。采集目录保留最近 1024 条范围及手动选择历史文件入口，数据检查器保留明确 32 B 预览边界，不添加假解析或原型模拟默认值。
