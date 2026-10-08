# 主窗口工作流资源接入审核与修复

Task `task_d0061569e26f` / Dispatch `ctx_1c0b3bd93ebd`。结论：本次主窗口接入审核、修复和对应真实原生集成测试完成；offscreen 与真实 Windows 平台均 **10 passed / 0 failed**。本报告不代表全产品冻结回归、发布验收或 WS/HTTPS/WSS 全协议验收。

## 审核与所有权边界

worker A 独立审核协调者新增的 `src/ui/main_window.cpp` 接入，依据工作流研究、UI 设计、实施计划及原型。协调者交接该文件及必要 `main_window.hpp` 维护权；本轮未编辑 B 的 workflow UI、C 的协议模块、根 CMake 或他人测试。

原核心作者补正 HTTP/WS 资源守卫及协议参数合同是**自审修复**，协调者已授权 compact operationId 和实际容量/时限对齐；最终核心独立复审仍由 C 完成，不能将本报告当作核心独立复审通过。

## 缺陷、触发条件与修复

| 缺陷 | 触发条件 / 原行为 | 修复与证据 |
| --- | --- | --- |
| 隐藏工作台快捷发送 | 工作流页面未运行时，全窗口 Ctrl+Enter 调用 send()，可能发送隐藏编辑器内容 | send() 在 page index=3 明确拒绝工作台发送；真实绑定 UDP 下空闲流程快捷键零数据报、TX=0 |
| HTTP/WS 重叠 raw 会话 | HTTP 不检查 lease；WS 只检查 lease，borrow close 后原连接仍活动 | 两种协议节点同时检查当前 lease 和 provided controller connected/connecting；协议期间外部启动 raw 也终止流程。真实 UDP 下 direct、borrow、borrow-close 六种组合均在调用协议前拒绝且保留原连接 |
| 后续 owned 连接身份错误 | prepare 只选择第一个 owned 配置，第二资源已开始但 UI profile/model/document 仍属于第一资源 | resourceChanged 按明确快照同步前台或 parked profile 身份；清除旧资源显示队列、模型、文本和序号，后台浏览不改变捕获 controller。真实两次 UDP owned 连接、发送、close/切换、后台浏览/返回及 cleanup 测试通过 |
| 确认期间会话变更 | QMessageBox 事件循环中会话被替换，原逻辑确认后无条件 stop 捕获 controller | 确认后核对 activity controller、epoch 和配置快照；不匹配则保留新任务并要求重新运行。此分支为源码审核及修复，未单独人为注入弹窗期间替换测试 |
| 借用快照错误先停止周期 | 不匹配在 runner 绑定时才发现，周期可能已经被停止 | 在准备确认及 stop 之前验证 borrowed 配置快照，不一致拒绝并保留任务。此分支源码检查，正常借用确认行为有真实测试 |
| 方案保护过宽及索引风险 | 最初 guard 对所有方案修改一律拒绝，违反后台可编辑非活动方案要求 | 收窄至活动/预留方案与整表导入；非活动编辑、删除、新建保留。删除非活动方案沿用 parkedProfile 位置调整，不影响 runner 快照。真实暂停中的 borrowed UDP＋recording，修改/删除/创建其他方案后 epoch、绑定、记录及运行状态不变且零配置流量 |
| 父窗口关闭缺少草稿确认 | main 无 closeEvent，不能依靠子页 closeEvent 提示 | main closeEvent 先调用 page.confirmLeave；取消不停止，接受后才停止 runner 与 parked/current raw、记录/周期。真实 Cancel/Discard 和 Stop+Save 的路径取消/接受均通过 |
| 合法长 nodeId 导致 operationId 超限 | 原 runUUID/nodeId/sequence 可超过协议允许的 128 字符 | 改 runUUID/op/序号，原 nodeId 在结果与日志保持。真实 128 字符 HTTP nodeId 收到 200 |
| 协议容量/时限合同不一致 | 核心可配置 16 MiB/长 timeout，真实后端为 8 MiB/60000 ms；短 WS timeout 会继承后端 5000 ms connect timeout | HTTP/WS 静态校验 8 MiB、60000 ms、connect≤total；runner 传 canonical timeoutMs/connectTimeoutMs，短 WS timeout 限制默认 connect。针对静态边界的测试通过；短 WS 实际连接仍属于 C 后续协议验证 |

逐行请求头与 C parser 的初始差异已通过 Orca 发送给协议所有者，未越界修改协议源码。B 对 active confirmLeave 的 Save 动作改为“停止并保存”，先获得保存路径，选择 Cancel 或取消路径不停止，再在肯定决定后停止并保存；本轮通过主窗口端到端验证该实现。

## 已核对的接入行为

第四主导航及工具轨存在且同步，进入工作流隐藏旧连接侧栏、保存通信 splitter 状态，离开恢复，退出保存通信布局而非隐藏布局。主题传至 WorkflowPage。provider 返回 parkedController（如果存在）而非当前离线浏览 controller；runner 启动后绑定稳定对象，简单导航无确认、无停止、无自动发送。

borrow raw-only 运行保留已有连接和记录；已有周期发送须明确确认后停止。HTTP-only/WS/owned 在 raw 占用时显示替换计划，取消保留任务；确认同意停止连接、周期和记录，然后 runner 建立明确配置快照资源。借用 close 只释放 lease，不授权协议重叠或建立 owned 资源；owned 要明确 close 后切换。

命令“载入”不发送，实际手动、载入命令后的发送和周期发送均由集中 send() active 守卫保护。发送接受请求的诚实语义保持，没有将入队称为本地写出或对端确认。

## 实际构建与测试

工具链：Qt **6.8.3**、MinGW GCC **13.1.0**、C++17；SerialPort `.deps/qtserialport-install`。没有使用或修改共享 `build/release`。独立目录为 `build/workflow-integration/native`。

为避免重复 C 的 Beast/依赖编译，standalone CMake 从根源码构建真实 core、main、WorkflowPage/QtNodes 和测试，并只读链接 C 实际静态档案。`protocol_archive_anchor.cpp` 只是让根 target 保持正常可设置编译属性的空符号；所有协议方法、QObject 元对象、I/O 都来自真实 C archive，**没有 protocol stub、模拟响应或 Qt 协议替代**。

| 检查 | 结果 | 证据 |
| --- | --- | --- |
| 主窗口真实集成，Qt offscreen 平台 | 10 passed / 0 failed / 0 skipped，5532 ms | `build/workflow-integration/integration-test.txt` |
| 同一集成套件，真实 Qt Windows 平台 | 10 passed / 0 failed / 0 skipped，5027 ms | `build/workflow-integration/integration-windows-test.txt` |
| 更新的独立 core 守卫/合同套件 | 4 passed / 0 failed（含 init/cleanup） | `build/workflow-core-check/integration-core-test.txt` |
| 必要 workflow 核心回归 | 14 passed / 0 failed | `build/workflow-core-check/workflow-test.txt` |
| 最新真实集成编译 | 成功，无 compiler warning/error | `build/workflow-integration/build-native.log` |

core-check 两套依旧是明确 protocol link seam；仅它们的 raw/静态检查属于该 seam。主窗口两套真实集成使用上表所述真实协议档案。

实际 native 测试的 8 个业务 slot（另含 init/cleanup）：

1. 协议容量/时限合同、长节点 ID 静态校验。
2. HTTP/WS 对已有 raw 的六种直接 API 重叠拒绝，连接保留。
3. 第四导航、侧栏、splitter 保存恢复、主题、Ctrl+Enter 空闲流程零发送、退出/再启动无自动连接或运行。
4. borrow 周期确认 Cancel/Accept、记录/连接保留、命令发送争用保护、非活动方案编辑/删除/新建、后台收包与返回。
5. **真实 localhost HTTP 200**：HTTP-only 替换确认取消时保持 raw/periodic/recording；肯定后停止旧任务再收到响应，包含 128 字符 nodeId。
6. 主窗口 close Cancel 保留暂停运行/连接/草稿，Discard 接受后停止并关闭。
7. Stop+Save：取消文件目标不停止；选定目标后停止、保存真实可读取 schemaVersion1 文档并清理连接。
8. 两个真实 owned UDP 会话顺序 close/重建、不同方案身份、parked 浏览和返回、显示模型隔离、stop cleanup。

第一轮曾出现一个失败，是返回按钮的可见文本会按宽度省略方案名称，测试错误要求包含完整名称；现检查完整 tooltip 和返回后的真实当前方案/模型。新增非活动编辑以及 Save 语义后最终两平台均全通过。

## 实际链接档案指纹

指纹文件 `build/workflow-integration/protocol-artifacts.sha256` 在最终链接之前记录，并在两平台测试之后 `sha256sum -c` 全部验证一致。测试 EXE 指纹另存 `build/workflow-integration/test-executable.sha256`。

| 实际档案 | SHA-256 |
| --- | --- |
| `build/workflow-protocol/native/libportbridge_workflow_protocol.a` | `eb3552d3a2d46e0a0a464b74e8df08ce85ddd43f17748686cb0769aa8c7b9ffa` |
| `.../workflow-deps/cpr/cpr/libcpr.a` | `9e5a675c0db2cab0877f1209c5c29189044401528adab3f2c3e4eedbfdc87617` |
| `.../workflow-deps/curl/lib/libcurl.a` | `cac367ce186f8e90bdd10fb7115e271d749e4a4a4abcadff8fba014430a967d8` |
| `.../workflow-deps/cares/src/lib/libcares.a` | `9d92c02642394449a845a079e66c16fb637c1498c377281f1eeaee8ec57fb629` |
| `.deps/workflow/openssl-install/lib64/libssl.a` | `c78f840687f2cca3077363cb66c0c85ad785ff1250305370719b81305cb9576e` |
| `.deps/workflow/openssl-install/lib64/libcrypto.a` | `c4afeef58c5d471cd5a7bcf0e25efd5e572b40f8db81c9bd9781ffc50ead2237` |

## 后续边界

本轮未声明实际 WS/HTTPS/WSS、TLS trust/CA、完整 HTTP 登录→WS 模板、协议取消 DNS、DPI 截图或全部旧网络/UI 套件通过；这些由 C/B 和协调者按各自所有权继续验证。主窗口最终编译复用的实际协议档案已锁定指纹，但不是全产品冻结的源清单；协调者仍需 root 冻结回归、整合 B/C 最新修复与发布验证。

C 后续独立复审需关注本轮 core 自审的资源守卫、compact operationId、参数容量/时限，以及 owned cleanup 和手动 replacement epoch 行为。原 borrowed API 无可靠 raw tracked-write completion，本轮维持“入队已接受”的正确表述。

修改：`src/ui/main_window.cpp`、`src/ui/main_window.hpp`、`tests/test_workflow_integration.cpp`、`src/workflow/workflow_runner.cpp`、`src/workflow/workflow_document.cpp`、`src/workflow/workflow_messages.hpp`、本报告；另有忽略的 standalone CMake/空 archive anchor/构建日志和指纹证据。
