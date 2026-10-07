# PortBridge 实施任务计划

用户已授权开发、审核及修复到完成。以下任务为本轮全部软件任务；硬件验证是显式验收边界，不能假称通过。

| ID | 目标/步骤 | 文件区域 | Owner | 依赖 | 验收 |
| --- | --- | --- | --- | --- | --- |
| TASK-001 | 建立固定共享接口、依赖和 CMake 构建 | include/、CMakeLists.txt、scripts/ | 协调者 | 无 | 匹配 Qt/MinGW，依赖可重现 |
| TASK-002 | 独立 Asio TCP 客户端/服务端与 UDP | src/network/ | worker-network | TASK-001 | 真回环、来源、空 UDP、多客户端、取消和停止测试 |
| TASK-003 | 会话与串口线程、发送调度、数据编码 | src/session/ | worker-session | TASK-001 | 参数/编码/有限周期/断线停止测试 |
| TASK-004 | 后台二进制采集、轮转/时长、索引导出 | src/session/ | worker-session | TASK-001 | 原始字节与来源可还原，溢出/写错可观察 |
| TASK-005 | 配置/命令持久化与合法导入导出 | src/session/ | worker-session | TASK-001 | 重启不连/发，损坏与版本拒绝 |
| TASK-006 | Qt 工作台、连接参数、列表与详情 | src/ui/ | worker-ui | TASK-001 | 与原型对应，四种真实通信状态 |
| TASK-007 | 高速/诊断/采集页、命令库、主题快捷键 | src/ui/ | worker-ui | TASK-001 | 未知统计不假造，任务状态可操作 |
| TASK-008 | 集成构建、UI 与端到端测试 | tests/、src/main.cpp | 协调者，owner 配合 | TASK-002～007 | Release + CTest + 原生截图 |
| TASK-009 | 独立网络压力工具与证据说明 | tools/ | worker-network | TASK-002 | 无 UI 收发、带序号和机器可读结果；不称回环为2.5G |
| TASK-010 | 独立审核并修复全部有效问题 | docs/reviews/、各 owner 区域 | 复用原3个worker交叉审核/修复 | TASK-008 | 问题均有修复/证据/明确边界 |
| TASK-011 | 部署、使用说明、最终任务验收 | README、scripts/、docs/ | 协调者 | TASK-010 | 发布包本机启动，任务/需求证据逐项记录 |

## 最终执行结果

TASK-001～011的软件实现/审核/修复/复验/部署全部完成，逐项证据和未测外部验收在08-acceptance-report.md。最终Release/CTest为3/3、0失败：network8组、session19、UI19；独立native UI19、固定focused8、网络8及实际TCP六代pool生命期复验均通过。只创建原3个子agent，在原terminal复用共6个Task后全部成功结算并释放。

## 工作方式

第一波三个 worker 并行开发，各自只修改所属区域和测试文件；共享头、顶层 CMake 归协调者，接口变更通过 Orca 消息协调。第一波完成后复用同一 terminal 交叉审核，必要时再派 owner 修复，最多总共创建三个子 agent。

每个任务的实际结果及验证命令记入 08-acceptance-report.md；P1/P2 不混入首版完成度。真实串口回环、拔插、高波特率、2.5G 两机链路和干净 Windows 测试如条件不具备标为未实测。
