# 工作流正式实现：范围、接口与三 agent 计划

用户已授权依据 11-workflow-research.md、12-workflow-ui-design.md 和 workflow-prototype 实施、审核、修复。沿用现有 02/05/06 文档，新增以下实施约定。旧原型和发布证据保留。

## 范围

正式 Qt 工作流页面：原生 QtNodes 画布、节点库、节点参数/上次结果、日志/变量、模板、文件保存导入、撤销/重做、真实异步执行、有限内存、停止/暂停、静态错误定位。实现开始/结束、延时、有限循环、条件、连接资源、发送/等待、变量提取/断言/日志，以及真实 HTTP 和 WebSocket 节点。子流程封装、多持久会话并行和完整独立 HTTP/WS 手动调试页面属于调研后续阶段，不阻挡本次工作流交付。

HTTP 使用 cpr/libcurl；WS 使用 Boost.Beast。不能用模拟数据或静默 Qt 后端替代模块。TLS 验证默认开启，非 2xx 保留响应；WS 保持文本/二进制完整消息边界。启动、导入、图修改、模板和浏览不会发送。一次运行一个流程、一个持久消息资源，顺序 HTTP→WS 可行；现有串口/TCP/UDP核心与已有配置保持兼容。

## 分工（最多三个唯一子 agent）

| agent | 主任务与可编辑范围 | 审核复用 |
| --- | --- | --- |
| A 执行器/可靠事件 | include/portbridge/workflow.hpp、src/workflow/*、SessionController 与必要网络追踪、tests/test_workflow.cpp；不得改 UI/CMake | 审核 C 的协议与内存/TLS/取消行为 |
| B 原生工作流 UI | src/ui/workflow_*、tests/test_workflow_ui.cpp；可使用 QtNodes，不能改 main_window/CMake | 修复 UI 审核问题；核对原型 |
| C 开源协议模块 | include/portbridge/workflow_protocol.hpp、src/protocol/*、cmake/WorkflowDependencies.cmake、scripts/setup-workflow-deps.ps1、tests/test_workflow_protocol.cpp、THIRD_PARTY_NOTICES.md | 对 A/B 做独立功能及设计审核 |

协调者拥有 CMakeLists、main_window 接入、构建/部署/打包、文档和跨模块集成。在同一已授权工作区分文件工作；不创建额外 agent 或未经必要性的分支。完成后用原有 agent 新 dispatch 审核/修复。所有构建在不同目录进行，最后统一冻结源码并验证。

## 公共约定

预置 workflow.hpp / workflow_protocol.hpp 是 ABI 合同。更改信号、公共方法或类型前发消息协调，允许补充兼容方法。UI 的 WorkflowPage 应提供构造、runner()、setDarkTheme(bool)、setSessionProvider(std::function<SessionController*()>), setProfilesProvider(std::function<QVector<ConnectionConfig>()>), setRunPreparation(std::function<bool(const WorkflowDocument&,QString*)>)。协调者用稳定活动 controller 绑定现有会话、阻止 UI 发送冲突，并明确替换活动会话。

工作流节点类型沿用原型的 start/end/delay/branch/loop/raw/send/sendWait/wait/http/ws/close/extract/assert/variable/log。参数沿用原型并可补充 resource、messageType、framing、sourceFilter、TCP clientId/UDP target；默认localhost。业务文件 schemaVersion=1，但禁止直接执行带 prototypeOnly 的设计文件。资源保存快照或绑定当前明确资源，不依赖永久列表索引。

节点完成状态区分接收入队、本地写出、业务确认；未实现可靠 write-completion 时发送节点只能明确报告“已接受”，不能谎称已确认。发送并等待先 arm 匹配器，再发送；原始事件有独立限额和 epoch，不能读取显示抽样。满队列或组装超限失败并留下证据。Qt queued signals 不允许按高速每包无限堆积。

循环 body/done 端口与普通 success/error 不同；循环回边只能经过有限循环节点，最大总步骤与截止时间限制。条件 true/false 为合法正常结果；普通错误不能当成功。停/超时/换会话后迟到回调不能推动新运行。暂停只阻止下一个节点，不声称冻结网络。

## 验收与审核

- 每个模块独立构建及真实回环：UDP快速回复/显示暂停、TCP分包粘包/来源、HTTP 200及401/500/大响应/取消、WS文本/二进制/握手失败/关闭；HTTPS/WSS本地TLS与证书验证。
- 按原型真实操作：拖入/拖线/移动/撤销、字段编辑、模板/文件、结果定位、日志分隔条、深浅主题、1024×768以及native125/150%。
- 审核以需求矩阵与实证为依据，写明确问题、触发步骤、代码位置与修复验证；由原有所有者修复后复审。
- 运行完整既有回归加新增测试，不重新执行无意义的模拟原型测试作为产品验收。
- 发布新 dist/PortBridge-workflow-1，不关闭用户旧程序；保留上一ZIP及源清单，更新部署哈希和restricted startup检查。
