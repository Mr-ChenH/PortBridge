# PortBridge

Qt/C++ 串口与 TCP/UDP 调试工作台。正式桌面界面采用 Qt Widgets，网络采用独立 Asio I/O 核心；原始记录、显示样本及统计分开处理。

## Windows 构建

准备 Qt 6.8.3 MinGW 64-bit、配套 MinGW 13.1.0、Ninja、CMake 3.24+ 和 Git。本机默认路径为 C:\Qt\6.8.3\mingw_64、C:\Qt\Tools\mingw1310_64、C:\Qt\Tools\Ninja。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1
```

脚本仅修改本进程 PATH，使用匹配工具链；首次获取固定 Asio，缺少 Qt SerialPort 时从匹配的 Qt 6.8.3 源码构建至 .deps/。网络不可用时提前准备依赖目录或已安装的 SerialPort 模块。

```powershell
scripts/build.ps1 -QtRoot C:\Qt\6.8.3\mingw_64 -MingwRoot C:\Qt\Tools\mingw1310_64 -NinjaRoot C:\Qt\Tools\Ninja
scripts/build.ps1 -Configuration Debug
```

构建后程序位于 build/release/PortBridge.exe。开发运行时 PATH 需包含 Qt、配套 MinGW 和 .deps/qtserialport-install/bin；部署版本可以直接运行。

## 部署

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/deploy.ps1
```

应用图标采用深色圆角底、薄荷绿桥接线和橙色端点，提供 16–256 像素九种 Windows ICO 尺寸，嵌入 EXE 文件资源及 Qt 窗口资源。资源管理器、快捷方式、标题栏和任务栏可使用同一图标，无需外部图片文件；图稿位于 `assets/app-icon/`，离线再生成可运行 `python scripts/generate-icon.py`（需要 PySide6、Pillow）。

修复版直接运行 `dist/PortBridge-profile-picker-1/PortBridge.exe`（原 `dist/PortBridge/PortBridge.exe` 运行时被占用，保留当前会话）。部署目录包含 Qt 运行时、平台插件、编译器运行库及必要的串口模块。实际可分发验证结果见验收报告。

## 操作

1. 点击左侧“当前方案”卡片，按名称、协议或地址搜索并选择串口、TCP 客户端、TCP 服务端或 UDP 方案；全部方案列表按需打开。检查本地绑定及目标参数后主动启动。浏览其他方案时，正在运行的连接继续收发、周期发送和记录，点击“返回运行方案”可回到其数据视图。新建与更多菜单保留方案管理功能。程序默认未连接，不自动发送。
2. HEX 或 ASCII/UTF-8 输入，换行由独立选项控制；检查编码后的字节数后发送。周期次数为 0 时持续发送，其余为有限次数；停止或明确断开会终止后续周期请求；浏览其他方案保持活动会话，主动启动另一方案时替换旧会话。命令可保存周期参数，载入后仍需主动发送。
3. 普通模式查看有限缓存，高速模式查看样本和统计。暂停/清空显示不等于停止接收或记录。
4. 选中记录检查时间、来源、长度。HEX/ASCII 均可查看完整内容，每页 4096 字节并可滚动，页码上方直接切换；复制按钮保留完整原始 HEX。字节详情另有 UTF-8 页签，中文发送/接收后可阅读原文；选中新记录的首屏包含有效 UTF-8 非 ASCII 字符时自动切换。ASCII 保持逐字节显示，可打印字符以外显示点；无效 UTF-8 明确提示，控制字节显示为 `\xNN`，不改变原始数据。UTF-8 分页让跨边界字符完整显示在起始字节所在页，不重复、不制造分页乱码。文本预览按 RX 绿色、TX 橙色显示，配合不同底色及 ▼接收/▲发送标识显示时间和端点，支持 UTF-8 原文、16 字节/行 HEX、有效 JSON 对象/数组缩进；无效或跨块 JSON 保留原文。TCP/串口记录是接收块，UDP 是数据报。浏览方案时各自显示独立，返回活动方案恢复缓存与持续接收的数据；仅显式断开、启动其他连接或替换活动配置时结束旧会话。
5. 配置目录、轮转和时长后开启原始二进制采集；磁盘/队列不足应通过状态和错误显示，不能假定记录完整。
6. 序号分析仅对明确配置的 64 位协议字段启用；缺失率使用已确认的序号位置为分母，尚在观察窗口内的位置和重复观察不计入分母。UDP 接收截断单独计数；网卡/内核丢失无法观测时显示未知。本地记录序号不能证明远端无丢包。

发送区在串口、TCP 客户端、TCP 服务端、UDP 四种模式下共用全宽多行编辑器。格式、编码、换行和周期选项位于编辑器下方；拖动接收区与发送区之间的分隔条可调整高度，右上“展开编辑 / 收起编辑”临时放大输入区，不改变通信和输入内容。下次启动恢复正常布局下保存的区域大小；小窗口和展开状态折叠吞吐趋势图，为编辑留出空间，实时指标与接收仍继续更新。

UDP 工作台把“本地接收绑定”和发送区的“UDP 发送目标”分开：绑定本地端口后，填写目标 IP/域名及端口，直接点击“发送”，无需应用目标或选择客户端。编辑目标、保存方案不发送 UDP 数据；每个发送请求保留点击时的目标，已排队数据不会被随后修改的地址重定向。域名按发送请求异步解析，解析失败显示错误，继续接收且不回退到旧目标。周期发送使用启动时的目标，运行中锁定编辑。目标就绪以及绑定/关闭端口不再作为 SYSTEM 消息写入 UDP 数据样本或原始采集，操作仅更新连接状态；这些操作不向业务对端发包，错误、拒绝和截断诊断仍保留。UDP 不显示 TCP 连接超时、客户端选择或客户端广播功能。

采集采用有版本的 .pbc 原始二进制和 .meta.json 目录元数据，完整状态在文件收尾及目录提交成功后发布。采集文件页显示协议、开始时间、时长和元数据状态；“刷新”异步扫描实际目录并反映文件增删。内存目录只保留最近 1024 个文件条目，旧文件保留在磁盘，可通过“更多 → 选择实际采集文件导出”访问。JSON 使用 Base64 保存实际采集字节；后台采集/显示样本导出有进度与取消，取消/失败保留原输出。磁盘空间查询也在后台执行。格式明细见 docs/reviews/session-implementation.md，最新修复和证据见 [审核修复验收](docs/reviews/audit-fix-acceptance.md)。

“接收诊断”保留实际接收、截断、应用队列丢弃、记录失败、显示省略计数；移除没有数据来源、一直“未知”的发送端总量与网卡/内核占位。普通文本通信无法直接判断网络丢包；报文包含连续 uint64 序号并显式启用分析后，才显示序号缺失率。

周期计数表示已接受的请求；TX 字节表示本地写出完成，均不保证业务确认。停止周期不再创建后续请求，已入系统/网络栈的数据可能完成；断开取消应用管理的旧连接队列。操作系统 DNS/文件调用若长时间阻塞，退出采用有界等待与独立状态保留，不把未完成记录标为完整。

## 测试与压力工具

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test.ps1 -Suite ui
```

Qt Test 日志写入 build/release/test-reports/，CTest 失败时直接展示断言与超时信息。网络测试使用实际 localhost sockets，界面测试使用真实 Widgets、实际回环数据与临时文件。

```powershell
build/release/portbridge_bench.exe --mode self-loop --protocol udp --payload 1472 --rate 20000 --duration 2
build/release/portbridge_bench.exe --mode receive --protocol udp --bind 0.0.0.0 --port 19090 --duration 15
build/release/portbridge_bench.exe --mode send --protocol udp --host 192.168.1.20 --port 19090 --payload 1472 --rate 10000 --duration 10
```

最后一条的 IP 是待替换的示例。TCP 使用 --protocol tcp。JSON 报告真实完成发送、有效接收、缺失/乱序/重复/校验、排队和系统缓冲；standalone receiver 需要 --expected 接收发送端实际完成总数，才能包含末尾缺失。self-loop 明确属于 localhost，录制与 UI 均未启用。

## 原生界面截图

```powershell
build/release/PortBridge.exe --review-settings build/review-settings/manual --theme dark --size 1440x960 --screenshot docs/screenshots/manual.png
```

在上述开发运行 PATH 条件下执行；--review-settings 使用独立配置目录，--screenshot 保存真实原生启动界面并退出。最新版本为 `app-icon-1`，图标设计与 EXE/窗口加载验证见 [应用图标报告](docs/reviews/app-icon.md)。此前 `text-diagnostics-1` 的中文 UTF-8 字节详情与接收诊断整理见 [文本与诊断修复报告](docs/reviews/text-diagnostics.md)。此前 `udp-lifecycle-1` 的 UDP 绑定/关闭不生成数据消息见 [生命周期消息修复报告](docs/reviews/udp-lifecycle.md)。此前 `composer-fix-1` 的四种通信方式全宽多行编辑、拖动调整与展开见 [发送区优化报告](docs/reviews/composer-fix.md)。此前 `direct-send-1` 的明显收发配色、UDP 无需应用直接发送见 [本轮修复报告](docs/reviews/direct-send.md)。此前会话浏览、文本格式化和完整 ASCII 查看见 [交互修复报告](docs/reviews/interaction-fix.md)。此前 UDP 配置修复与验证见 [UDP 修复报告](docs/reviews/udp-fix.md)。上一轮 `audit-fix-1` 的原生截图及可切换设计/修复前基准的 [对照页面](docs/validation/audit-fix/comparison.html) 位于 `docs/validation/audit-fix/`；完整结果见 [审核修复验收](docs/reviews/audit-fix-acceptance.md)。`docs/screenshots/ui-refinement/` 与 native-windows-* 图片保留为历史证据。

连接配置由选中的方案决定协议，标题右侧显示只读协议标识；更改协议通过新建/编辑方案完成。连接按钮紧跟基础参数，TCP 客户端优先显示目标地址/端口。检查器分开显示 OFFSET/HEX、范围及 ASCII；诊断按接收、截断、队列、记录和显示等位置区分实际计数。命令/采集页提供行内操作，更多功能保留在菜单；主题、显示偏好、队列预算及周期参数可恢复，启动始终不连接、不记录、不发送。

## 原型与设计

浏览器设计原型仍位于 docs/project-factory/prototype/index.html，全部流量为模拟，不是正式应用。正式组件与行为依据 docs/project-factory/02-requirements.md、04-ui-design.md、05-technical-design.md。

## 验证范围

本机自动回环及原生界面测试不等于真实 2.5G 网卡吞吐、真实串口拔插或干净系统认证。使用独立压力工具和真实设备按 docs/project-factory/10-high-throughput-plan.md 验证这些条件。第三方组件与固定版本见 THIRD_PARTY_NOTICES.md。
