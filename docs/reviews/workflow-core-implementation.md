# 工作流核心实现主报告

任务：`task_ed46820cbfbe`，dispatch：`ctx_49b68b44d678`。此报告覆盖 worker A 的文档、静态验证、不可变运行计划、异步执行器以及独立原始接收观察；不代表 UI 或正式 HTTP/WebSocket 模块完成验收。

## 已实现

- `workflow_document.cpp` 实现 schemaVersion=1 的往返保存、16 MiB/16 层/条目限额解析、256 节点和 512 连线限额，以及全部 17 种节点定义：start/end/delay/branch/loop/raw/send/sendWait/wait/http/ws/close/extract/assert/variable/log。
- 静态校验覆盖未知节点/参数、端点和出口、重复 ID/出口、可达性、普通回路、循环 body 返回以及 done 出口形成回路。有限循环还受全流程最大步骤和时限限制。
- login、UDP、blank 模板只创建文档，无连接或发送副作用。blank 提供开始→结束，可直接检查和运行。
- 新增 `workflowConnectionConfigToJson/fromJson` 兼容方法保存明确配置快照，旧 profile v1 文件和其解析器保持兼容。owned 必须提供 `parameters.config`；borrow 绑定运行开始时提供的稳定 controller，可选 config 验证当前会话快照。profile 名称只作标签，不隐式查询名称或列表索引。
- `workflow_runner.cpp` 在 QObject 线程独立异步推进，启动时复制节点/参数/连线；runId 和 operationId 稳定唯一。停止/退休回调及排队推进受 generation 保护，暂停只阻止下一步骤，当前操作及其超时继续。
- 真实 true/false 分支、error 路径、变量、safe dotted JSON 字段路径、显式缺失字段、模板替换、断言和有限循环；禁止脚本执行。JSON 请求体先解析后替换，避免将 token 引号拼入 JSON 造成无效内容。
- 运行变量、保留节点结果、日志条目/文本、消息、观察队列、步骤与持续时间具有有限上限；溢出明确失败。运行与主要静态诊断采用中文。
- raw 与 WS 一次只能绑定一个持久资源，切换需要显式 close。borrow 释放时保留连接，owned 释放时只停止所属 epoch；手动替换后清理不会关闭用户的新会话。周期发送冲突在 start 前拒绝。
- HTTP 调用协议客户端并保留结构化 status/headers/body，非 2xx 仍保留变量及结果，expectedStatus 由 runner 判断。HTTP/WS 支持 `verifyTls` 和可选 `caFile` 参数；实际 TLS 行为由协议模块实现。
- raw send 明确报告“发送被本地有界队列接受”，结果含 admitted=true、localWriteCompleted=false、peerAcknowledged=false，没有把接受请求当本地写出或业务确认。WS send 的文案要求协议 operationFinished 表示真实本地写出，仍不代表对端确认。

## 原始接收观察

SessionController 新增 sessionEpoch、observeRaw、takeRawObservation、removeRawObserver。最多 4 个观察者，每个限额 256 字节–64 MiB、最多 4096 条数据/事件；共享 payload，容量收费包含 payload capacity 与记录、来源等元数据。

发布点位于实际 ingest 接受之后、显示暂停/高速抽样之前；recording backpressure 返回 false 时不发布重试块，因此同一接受块仅发布一次。采用有界轮询批次，未增加每包 Qt queued signal。溢出保留错误并清空/封禁该观察者，会话停止/切换也封禁观察者。

sendWait 在发送前注册观察者。UDP 使用数据报边界，可按 address/port 筛选；TCP server 等待必须指定客户端来源。TCP/串口实现 delimiter、fixed、lengthHeader 分帧，支持碎片和多个消息，并限制组装量和声明长度。观察 SendRejected、ReceiveTruncated、disconnect、相关 client removal 和 transport error。UDP 生命周期不进入旧 capture 数据日志的排除逻辑、请求级目标、采集归属及旧显示队列策略保持原实现。

## 实际验证

独立目录 `build/workflow-core-check`，Qt **6.8.3**、MinGW GCC **13.1.0**、C++17；SerialPort 使用 `.deps/qtserialport-install`。没有在共享 `build/release` 构建，没有修改 root CMake 或他人测试。

- 当前核心套件：**14 passed, 0 failed**，最新 2138 ms，证据 `build/workflow-core-check/workflow-test.txt`。
- 原有 SessionController 套件：**27 passed, 0 failed**，40522 ms，证据 `build/workflow-core-check/session-test.txt`。最后一次完整会话套件在 SessionController 修改完成后执行；随后仅工作流 caFile/参数验证改动，因此只重跑必要核心套件。
- 最新编译日志 `build/workflow-core-check/build.log` 无 warning/error；所有者范围 tracked diff whitespace check 通过。

新测试实际覆盖：

1. 三模板和 JSON 往返，拒绝原型文件、错误 schema、未知节点/参数、坏端点、普通环路、容量和深度。
2. 配置快照往返和非法端口。
3. 真 false 分支、3 次有限循环、不可变参数快照、最大步骤；JSON 字符串提取、字段缺失与 error 分支。
4. 当前延时可在暂停中完成，下一步暂停；停止后新运行隔离旧回调；总时限、变量容量。
5. 真实 localhost UDP 快回复，显示暂停、高速模式、来源筛选；真实 65 数据报 burst 在抽样丢弃情况下仍匹配，256 字节观察队列溢出明确失败。
6. 真实 localhost TCP delimiter/fixed/lengthHeader 分包和粘包，非匹配前一条消息、碎片后一条匹配消息，以及断开/组装超限。
7. 观察 payload 共享、按 capacity 超限、epoch 封禁、原始等待会话替换、借用停止保留连接、owned 完成释放和 owned 替换保留用户新会话。

## 明确限制和交接

这次隔离构建使用 `build/workflow-core-check/protocol_stub.cpp` 作为**仅链接用 seam**，所有核心网络验收来自现有真实 raw 核心；该 seam 不模拟成功响应，HTTP/WS 调用返回不可用错误。**未运行真实 HTTP/WS、HTTPS/WSS、TLS/caFile、协议取消或原生 UI 验收，也未声明 root 集成或所有既有网络/UI 测试完成**；协议模块和 root 冻结构建由 C/协调者后续集成验证。

协议合约要核对：HTTP result.status 为数字且 headers/body 保留；runner 传 maxResponseBytes/maxMessageBytes 及原型 timeout/connectTimeout 别名；WS message 为完整 text/binary 消息且线程归属 QObject；WS send operationFinished 的真实语义；close/stateChanged 顺序和 cancelAll 后错误回调隔离。runner 本身要求 explicit close 后切换持久资源。

本次未增加 tracked raw write-completion hook；发送仅报告接受请求。TCP/串口分帧是真实共用实现，但串口硬件分片未实际验证；TCP server clientId 过滤已实现，新增 localhost framing 验收使用 TCP client，后续独立审核可补 server 多来源证明。UI 中 profile 选择必须使用快照 helper 写 config，不能把显示标签当资源身份。

## 修改文件

- `include/portbridge/workflow.hpp`（新增兼容配置快照 helper，原 runner ABI 保持）
- `include/portbridge/session_controller.hpp`
- `src/session/session_controller.cpp`
- `src/workflow/workflow_document.cpp`
- `src/workflow/workflow_runner.cpp`
- `src/workflow/workflow_private.hpp`
- `src/workflow/workflow_messages.hpp`
- `tests/test_workflow.cpp`
- 本报告，以及忽略目录下 standalone check 的 CMake、链接 seam 和实际 build/test 日志。
