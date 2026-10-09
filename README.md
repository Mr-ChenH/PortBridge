# PortBridge

Qt/C++ 通信调试、HTTP/WebSocket 与原生工作流工作台。正式桌面界面采用 Qt Widgets；串口/TCP/UDP 使用既有独立收发核心，HTTP 使用 cpr/libcurl，WebSocket 使用 Boost.Beast。手动协议工作台和工作流复用真实协议实现，原始记录、显示样本及统计分开处理。

## Windows 构建

准备 Qt 6.8.3 MinGW 64-bit、配套 MinGW 13.1.0、Ninja、CMake 3.24+ 和 Git。本机默认路径为 C:\Qt\6.8.3\mingw_64、C:\Qt\Tools\mingw1310_64、C:\Qt\Tools\Ninja。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1
```

脚本仅修改本进程 PATH，使用匹配工具链；首次获取固定 Asio，缺少 Qt SerialPort 时从匹配的 Qt 6.8.3 源码构建至 .deps/。原生工作流另外准备固定 QtNodes、cpr/libcurl、Boost、OpenSSL 和 c-ares，下载校验 SHA256，静态构建协议依赖。QtNodes 的 const QVariant 提取补丁也按原始/修改后哈希验证，构建不关闭编译诊断。首次 OpenSSL 构建需要 Git for Windows bash/Perl 和脚本准备的本地构建模块；不修改系统 Perl。网络不可用时提前准备已校验的依赖缓存。

```powershell
scripts/build.ps1 -QtRoot C:\Qt\6.8.3\mingw_64 -MingwRoot C:\Qt\Tools\mingw1310_64 -NinjaRoot C:\Qt\Tools\Ninja
scripts/build.ps1 -Configuration Debug
scripts/build.ps1 -BuildDirectory build/workflow-product -Parallel 4
```

构建后程序位于 build/release/PortBridge.exe。开发运行时 PATH 需包含 Qt、配套 MinGW 和 .deps/qtserialport-install/bin；部署版本可以直接运行。

## 部署

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/deploy.ps1
```

应用图标采用深色圆角底、薄荷绿桥接线和橙色端点，提供 16–256 像素九种 Windows ICO 尺寸，嵌入 EXE 文件资源及 Qt 窗口资源。资源管理器、快捷方式、标题栏和任务栏可使用同一图标，无需外部图片文件；图稿位于 `assets/app-icon/`，离线再生成可运行 `python scripts/generate-icon.py`（需要 PySide6、Pillow）。

当前版本直接运行 `dist/PortBridge-http-clarity-fix-1/PortBridge.exe`。HTTP交互一致性审核修正10项：草稿/保存区别、项目归属、导入导出范围、分类空态和地址状态等；报告与清单见 [一致性审核](docs/reviews/http-clarity.md)，当前原生界面见 [图集](docs/validation/http-clarity/gallery.html)。上一 `dist/PortBridge-http-interaction-1/PortBridge.exe`。HTTP统一为项目/环境/请求层级：创建请求只填名称，地址引用环境；新建项目后直接配置环境；设置采用左侧分类、右侧编辑和固定保存区。见 [交互与布局验收](docs/reviews/http-interaction.md)、[实际界面](docs/validation/http-interaction/gallery.html) 与 [项目/环境/请求指南](docs/http-project-guide.md)。上一 `dist/PortBridge-browser-fingerprint-1/PortBridge.exe`。当前环境可离线随机生成并保存Chrome/Edge/Firefox浏览器请求头配置，手动发送和顺序联调稳定使用，手填同名头优先；完整10/10回归、三档原生软件缩放各9通过。见 [指南](docs/browser-fingerprint-guide.md)、[实施验收](docs/reviews/browser-fingerprint.md) 与 [原生界面](docs/validation/browser-fingerprint/gallery.html)。上一 `dist/PortBridge-http-history-1/PortBridge.exe`。该修订将HTTP发送状态和最终结果合并为同一条请求记录，失败/取消也更新原记录；完整10/10回归及原生软件100/125/150%各7项通过，见 [请求历史优化](docs/reviews/http-history.md) 与 [真实界面](docs/validation/http-history/gallery.html)。上一 `dist/PortBridge-http-configuration-1/PortBridge.exe` 统一项目、环境、共享变量、运行值及公共认证配置，环境创建后直接进入配置；完整10/10回归及原生软件100/125/150%各10项通过，见 [HTTP配置重构](docs/reviews/http-configuration.md) 与 [真实界面](docs/validation/http-configuration/gallery.html)。上一 `dist/PortBridge-ui-latency-fix-1/PortBridge.exe` 修复GUI等待后台线程退出与重复布局/详情刷新，保留当时UDP/TCP各5分钟152/43ms心跳证据，见 [界面响应修复](docs/reviews/ui-latency-fix.md)。上一 `dist/PortBridge-http-sequence-fix-1/PortBridge.exe`、`dist/PortBridge-http-sequence-1/PortBridge.exe`、`dist/PortBridge-http-project-1/PortBridge.exe`、`dist/PortBridge-connection-ui-1/PortBridge.exe`、`dist/PortBridge-http-ws-1/PortBridge.exe`、`dist/PortBridge-workflow-ui-2/PortBridge.exe`、`dist/PortBridge-workflow-1/PortBridge.exe`、`dist/PortBridge-profile-picker-1/PortBridge.exe` 和被用户占用的历史版本保持保留。部署目录包含 Qt 运行时、平台插件、编译器运行库及必要的串口模块。实际可分发验证结果见验收报告。

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

## HTTP / WebSocket 手动调试

在工作台上方点击“新建调试项”，或使用原始连接侧栏加号，选择串口、TCP客户端、TCP服务端、UDP、HTTP或WebSocket卡片。HTTP只填写请求名称，保存到当前项目，初始地址为 `{{base_url}}/`；当前环境配置服务地址，请求编辑器补接口路径。WS填写连接名称和地址。创建不自动通信。当前界面见 [交互优化](docs/reviews/http-interaction.md) 与 [图集](docs/validation/http-interaction/gallery.html)，此前 [创建入口验证](docs/reviews/connection-creation.md) 保留为历史。

在“调试工作台”上方选择HTTP或WebSocket；“通信调试”继续使用原有串口/TCP/UDP方案。HTTP支持七种方法、启用/禁用参数与请求头、Bearer/Basic认证、原文/JSON/urlencoded Body，以及真实状态码、耗时、Body/JSON/Headers/HEX和有界历史。HEAD只读取响应头，不发送Body；3xx不自动跟随，4xx/5xx也保留真实响应。WebSocket支持ws/wss握手、请求头与子协议、UTF-8/HEX完整消息、主动推送、关闭码/原因、暂停显示和历史预算。

先编辑、保存或载入，再明确发送/连接。WebSocket消息可在离线时准备，连接不会自动发送它。开始另一类通信需要明确处理旧活动；拒绝或确认期间活动状态改变，会保留现状。默认方案、结果预览、复制与预览导出遮蔽凭据；原始结果不被改写。TLS保持证书/主机名校验。完整说明见 [操作指南](docs/manual-protocol-guide.md)、[设计与调研](docs/project-factory/14-manual-protocol-design.md)、[本轮实施验证](docs/reviews/manual-protocol.md) 和 [真实界面](docs/validation/manual-protocol/gallery.html)。本地请求示例在 `docs/examples/`，载入不会启动服务或通信。

HTTP项目功能支持项目/文件夹分类、独立环境、双大括号变量、项目公共认证和可视化响应提取。登录请求可把 `$.data.access_token` 写入当前环境运行变量，后续请求以 `{{access_token}}` 继承Bearer；模板可在登录前保存。敏感值只保留在运行期，环境/项目互相隔离，项目导出省略运行值。旧HTTP库迁入默认项目，WS与原始连接继续使用原方案。步骤见 [HTTP项目联调指南](docs/http-project-guide.md)，依据见 [调研](docs/project-factory/15-http-project-research.md) 与 [第一期要求](docs/project-factory/16-http-project-requirements.md)。

## HTTP响应断言与顺序联调

HTTP请求可保存状态码、响应头与JSON字段断言，按严格JSON类型比较。右上“顺序联调”选择已保存请求并排序，明确点击运行后逐项执行，当前步提取的token供下一步解析。支持遇错停止/继续、整段停止、逐步结果和省略值的JSON报告；执行期间锁定项目/环境，步骤间保持资源互斥。操作步骤见 [顺序联调指南](docs/http-sequence-guide.md)，示例 [登录→查询项目](docs/examples/login-sequence.pbhttp-project.json)，实现与验收见 [本轮审核](docs/reviews/http-sequence.md)。

## 原生工作流

上一工作流UI修订位于 `dist/PortBridge-workflow-ui-2/PortBridge.exe`，本轮按交互设计重新整理节点库、工具条、参数常用/高级分层、HTTP响应、日志与紧凑布局，修复日志折叠误保存、无描述流程残留旧说明及Body裁切。新增结果视图保持有界预览与凭据遮罩，原生工作流UI28/28在Qt软件100/125/150%通过；最终修改复验4个相关UI套件，4个未改后端套件依据此前冻结回归及源码/可执行文件哈希复用。此前 `workflow-1` 已完成正式协议/核心实施与交叉独立审核，其历史证据保持保留。点击第四导航“工作流”，从节点库拖入或双击添加节点，拖动端口连接，在右侧填写协议参数；检查后主动运行。编辑、模板替换、导入和程序启动都不建立连接或发送业务数据。

支持原始连接/发送/等待、HTTP 请求、WS 连接与完整文本/二进制消息、JSON 提取、断言、变量、日志、真实条件分支及有限循环。保存/导入采用版本化 `.pbflow.json`；设计原型导出带 `prototypeOnly`，正式程序拒绝作为运行文档加载。源方案使用明确配置快照，流程不永久绑定列表下标或模糊名称。

运行前显示必要资源替换计划。借用已活动连接保留连接和记录，周期发送须明确停止；流程自建资源在完成/停止后只清理所属代际。raw/HTTP/WS 顺序使用，持久资源切换先显式关闭。暂停只阻止下一步骤，当前操作及超时继续；停止不能撤回系统或对端已经接受的数据。raw 发送节点报告入队接受，WS 发送报告本地写出，两者都不代表对端业务确认。

原始回复匹配使用独立有界观察，显示暂停/高速抽样不影响它；UDP按数据报匹配，TCP/串口等待需要明确分帧，TCP服务端还需指定来源客户端。HTTP收到401/500时保留状态/响应头/内容，期待状态和下游断言决定是否失败。超限、取消、证书错误及关闭原因在结果与日志中体现。当前原始 `.pbc` 采集仍记录串口/TCP/UDP；HTTP/WS结果由流程结果与日志提供。

图模型最多256节点/512边；HTTP/WS单消息或响应内容上限8 MiB，操作超时上限60秒，运行还有步骤、变量、日志、总时长与观察队列限额。参数和大型结果采用有界预览，历史有字节预算。子流程封装和多持久会话并行为后续范围。HTTP/WS手动调试已在本轮独立工作台实现。实现依据见 [实施计划](docs/project-factory/13-workflow-implementation-plan.md)，UI设计见 [工作流设计](docs/project-factory/12-workflow-ui-design.md)。

## 测试与压力工具

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test.ps1 -Suite ui
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test.ps1 -BuildDirectory build/workflow-product -Suite protocol_debug
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test.ps1 -BuildDirectory build/workflow-product -Suite http_projects
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test.ps1 -BuildDirectory build/workflow-product -Suite workflow_e2e
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

在上述开发运行 PATH 条件下执行；--review-settings 使用独立配置目录，--screenshot 保存真实原生启动界面并退出。最新版本为 `http-clarity-fix-1`，审核发现与修正见 [一致性审核](docs/reviews/http-clarity.md)；上一 `http-interaction-1`，清晰的HTTP项目/环境/请求和设置布局见 [交互优化](docs/reviews/http-interaction.md)；上一 `browser-fingerprint-1`，HTTP环境随机浏览器请求头配置见 [指南](docs/browser-fingerprint-guide.md) 和 [验收](docs/reviews/browser-fingerprint.md)；上一 `http-history-1`，一请求一记录的交互及验证见 [请求历史优化](docs/reviews/http-history.md)；此前 `http-configuration-1` 的统一配置及验证见 [HTTP配置重构](docs/reviews/http-configuration.md)；历史 `http-sequence-fix-1` 验证见 [断言审核修复](docs/reviews/http-sequence-fix.md)；第二期原始记录见 [HTTP顺序联调审核](docs/reviews/http-sequence.md) 与 [界面图集](docs/validation/http-sequence/gallery.html)；开发构建可用 `build/workflow-product/PortBridge.exe --page http` 或 `--page websocket` 静默打开协议工作台。原生工作流开发构建可用 `build/workflow-product/PortBridge.exe --page workflow --review-settings build/review-settings/workflow --theme dark --size 1440x1000 --screenshot docs/screenshots/workflow.png` 查看。`--page` 只选择页面，不运行任务。此前 `app-icon-1` 的图标设计与 EXE/窗口加载验证见 [应用图标报告](docs/reviews/app-icon.md)。此前 `text-diagnostics-1` 的中文 UTF-8 字节详情与接收诊断整理见 [文本与诊断修复报告](docs/reviews/text-diagnostics.md)。此前 `udp-lifecycle-1` 的 UDP 绑定/关闭不生成数据消息见 [生命周期消息修复报告](docs/reviews/udp-lifecycle.md)。此前 `composer-fix-1` 的四种通信方式全宽多行编辑、拖动调整与展开见 [发送区优化报告](docs/reviews/composer-fix.md)。此前 `direct-send-1` 的明显收发配色、UDP 无需应用直接发送见 [本轮修复报告](docs/reviews/direct-send.md)。此前会话浏览、文本格式化和完整 ASCII 查看见 [交互修复报告](docs/reviews/interaction-fix.md)。此前 UDP 配置修复与验证见 [UDP 修复报告](docs/reviews/udp-fix.md)。上一轮 `audit-fix-1` 的原生截图及可切换设计/修复前基准的 [对照页面](docs/validation/audit-fix/comparison.html) 位于 `docs/validation/audit-fix/`；完整结果见 [审核修复验收](docs/reviews/audit-fix-acceptance.md)。`docs/screenshots/ui-refinement/` 与 native-windows-* 图片保留为历史证据。

连接配置由选中的方案决定协议，标题右侧显示只读协议标识；更改协议通过新建/编辑方案完成。连接按钮紧跟基础参数，TCP 客户端优先显示目标地址/端口。检查器分开显示 OFFSET/HEX、范围及 ASCII；诊断按接收、截断、队列、记录和显示等位置区分实际计数。命令/采集页提供行内操作，更多功能保留在菜单；主题、显示偏好、队列预算及周期参数可恢复，启动始终不连接、不记录、不发送。

## 原型与设计

浏览器设计原型仍位于 docs/project-factory/prototype/index.html，全部流量为模拟，不是正式应用。正式组件与行为依据 docs/project-factory/02-requirements.md、04-ui-design.md、05-technical-design.md。

工作流设计/模拟原型位于 `docs/project-factory/workflow-prototype/`，真实产品依据 [原生工作流设计](docs/project-factory/12-workflow-ui-design.md) 和 [实施计划](docs/project-factory/13-workflow-implementation-plan.md)。[本轮设计/优化前/优化后对照](docs/validation/workflow-ui-refinement/comparison.html) 和 [UI再审核报告](docs/reviews/workflow-ui-refinement.md) 记录当前界面、真实HTTP/WS结果、凭据遮罩、失败复验及源码冻结范围。[上一版原生实现对照](docs/validation/workflow-implementation/comparison.html) 可直接离线打开；此前 [整体验收](docs/reviews/workflow-acceptance.md)、[独立核心/UI审核](docs/reviews/workflow-independent-review.md)、[独立协议组合审核](docs/reviews/workflow-protocol-independent.md) 记录范围与未验证条件。

## 验证范围

新增可选的长时间验收入口 `python scripts/validate-local-acceptance.py --suite all`，默认使用 `build/workflow-product`；分项可选deadline/soak/dpi/overload/faults/waits，soak支持固定UDP或TCP及30～3600秒。实际10分钟截止、阻塞退出、指定负载实盘与跨屏/停顿证据见 [补充验收](docs/reviews/remaining-acceptance.md)，操作与设备条件见 [剩余验收指南](docs/remaining-acceptance-guide.md)。本机5分钟GUI心跳200ms门槛未通过（RA-UI01保持打开）；完整采集字节校验通过不替代界面响应验收。

本机自动回环及原生界面测试不等于真实 2.5G 网卡吞吐、真实串口拔插或干净系统认证。使用独立压力工具和真实设备按 docs/project-factory/10-high-throughput-plan.md 验证这些条件。第三方组件与固定版本见 THIRD_PARTY_NOTICES.md。
