# PortBridge 正式实现技术设计

## 已授权范围

按需求 FR-001～022 和 UI 设计实现首版桌面应用，完成软件审核、修复和复验。P1/P2 仍为后续范围，不在本轮隐式扩张。实物串口、两台机器的真实 2.5G 链路及干净系统验证必须单列证据，不用回环或模拟替代。

## 技术栈与环境

Qt 6.8.3、C++17、Qt Widgets、Qt SerialPort、standalone Asio 1.30.2、CMake、Ninja。本机使用 C:/Qt/6.8.3/mingw_64 和配套 C:/Qt/Tools/mingw1310_64。Qt SerialPort 未预装，使用与 Qt 对齐的 v6.8.3 源码独立构建至项目依赖目录。第三方源码不纳入产品源码，固定版本并保留原许可。

## 目录与分层

- include/portbridge/types.hpp：不依赖 Qt 的传输、记录、统计、采集契约。
- include/portbridge/network_engine.hpp、src/network/：Asio TCP/UDP 核心，独立 I/O 线程、连接/取消、服务端身份、有限发送队列与接收缓冲。
- include/portbridge/session_controller.hpp、src/session/：Qt 会话控制、串口工作线程、定时发送、有界显示管线、后台采集、配置和编解码。
- src/ui/：Qt Widgets 主窗口、表格模型、字节详情、吞吐图、命令和采集页面；只消费有限批次/统计快照。
- src/main.cpp：应用启动与命令行截图/自动退出支持。
- tests/：网络回环、会话/编解码/采集、Qt UI 自动检查。
- tools/：独立收发压力工具。
- scripts/：匹配工具链的构建、独立测试与 Windows 发布脚本；cmake/ 保存 Qt Test 日志适配。

## 共享接口

共享头文件由协调者先建立，子 agent 根据边界实现。接口变更先报告，避免并行修改造成 UI 与通信契约不一致。NetworkEngine 回调在 I/O 线程运行；SessionController 把事件排入有限队列，由 GUI 定时泵处理，不逐报文发送 Qt queued signal。

DataRecord 保存原始共享字节、微秒软件时间戳、内部记录序号、稳定连接 ID、来源和方向。内部序号不是远端协议序号。UDP 保留报文边界；TCP/串口明确是接收块。原始数据只在显示/导出时转换成 HEX。

## 生命周期及过载

- 启动不自动连接或发送；默认方案参数使用本地回环安全示例，不填造真实设备与吞吐。
- 切换方案停止连接、周期、记录；页面切换保留通信。
- 网络 async connect/read/write，禁止 GUI 等待联网。TCP 记录队列不足时保留待交付块并停止继续读取/背压；UDP 丢弃可计数。
- 发送队列按字节限额；部分/失败发送不能误记完整 TX。服务端目标 ID 或广播明确。
- 日志后台有界队列、分块二进制格式、来源/长度/时间索引、轮转与时长限制。错误可观察，停止完整回收。
- 普通模式显示缓存也有记录和字节上限；高速模式仅有限样本，独立计算未显示计数。
- 序号分析默认关闭，首版仅支持显式配置的 UDP uint64 字段、偏移/端序/有限窗口；未分帧的 TCP/串口不启用该分析，系统不可观测统计保持未知。

## 实施与审查后的约束

- 网络保留缓冲按 0/256/2048/8192/65536 字节分级复用，每级free最多32；payload deleter只弱引用池，退休engine释放全部空闲缓存而保留payload有效。队列与显示计费包含 vector capacity、来源和记录开销；空报文还受到描述符数量上限约束。
- UDP 本地绑定独立于发送目标。Windows ICMP 导致的拒绝/重置/不可达作为可观察的非终止错误，保持本地接收；描述符/绑定等实际终止错误仍断开。
- 会话回调包含固定 generation 和存活门禁；旧引擎在最多四代的后台清理队列退休，不让取消后的 Windows DNS 阻塞方案切换。退出等候清理最多三秒，异常 OS 延迟保留独立状态且不再访问 GUI。
- 生命周期/错误是独立 SYSTEM 记录，原始 UTF-8 JSON 包含类别/端点/消息，不增加 RX/TX 流量。
- .pbc 使用 DATA 原始记录及强制 DONE 页尾；目录初始 incomplete，只有文件收尾/flush 和原子目录提交均成功才发布 complete。内存近期目录最多1024条，旧文件不删除，后台流式扫描。
- exportCapture 可选进度回调返回false取消；UI 后台仅运行一个导出，50ms进度快照，不访问 QWidget。失败/取消以 QSaveFile 保留旧目标；关闭最多等候三秒，阻塞 OS I/O 下使用工作线程独立状态，不强制终止线程。
- Qt Widgets 已经实现；截图和压力工具不往正式应用注入模拟流量。具体格式、线程/计费边界与实测结果见 docs/reviews/ 及 08-acceptance-report.md。

## 配置、操作与界面

QSettings 保存偏好，版本化 JSON 保存连接方案/命令并使用 QSaveFile。合法性检查先于覆盖旧数据。HEX 严格校验；ASCII/UTF-8、换行、字节数明确；正常流模式有状态解码保持跨块 UTF-8。Qt UI 采用设计颜色和 QSplitter/QTableView/QPlainTextEdit，自绘有限吞吐曲线。主题、键盘、焦点和 DPI 不依赖 HTML 嵌入。

## 验证及发布

先构建 Release 和执行 CTest。网络测试覆盖 TCP 切分/合并、多客户端定向/广播、UDP 空报文与来源、退出/取消、发送背压。会话覆盖编码、周期次数与断线停止、配置损坏、日志恢复/轮转、过载计数。UI 用 Qt Test/offscreen 检查真实控件状态并生成原生截图。

独立审核完成后将问题回派给对应 owner，复验后更新 08-acceptance-report.md。发布目录用 windeployqt 部署运行时并提供启动说明；本机部署运行不等于干净系统认证。2.5G 能力以工具和记录格式提供测量路径，本轮不能虚构硬件吞吐结果。
