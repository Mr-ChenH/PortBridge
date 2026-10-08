# 工作流协议独立审核与真实组合验收

Task `task_b1b0be1425dc` / Dispatch `ctx_aed7b9768572`。本轮独立审查 C 的协议模块、公开协议 API、依赖配置/安装脚本及核心协议合同，并用真实 localhost 服务器验证组合流程。最终实际 Windows 原生套件 **25 passed / 0 failed / 0 skipped，8292 ms**，协议档案 SHA-256 为 `b40f12b238ec137143c63bde04ed5de134d229a2d96454bc43891c5d4ba04af0`。

## 独立性与范围

协议源代码、CMake 依赖模块和 setup 脚本由 C 维护，本 worker 没有编辑它们。审核覆盖 `src/protocol/*`、`include/portbridge/workflow_protocol.hpp`、`cmake/WorkflowDependencies.cmake`、`scripts/setup-workflow-deps.ps1`，包括 C 根据本轮发现修复后的最终 close 实现。

协调者另行明确授权本 worker 补正核心消费合同：接收 peer-close 元数据、在变量替换前规范化请求头、将协议错误转换为中文。这些 `workflow_runner.cpp` / `workflow_messages.hpp` 修改是**核心自审修复**，不是独立核心批准；C 后续负责独立核心/UI 审核。本报告对 C 的协议实现保持独立，对上述自写核心代码的独立性不作虚假声明。

新测试使用实际 WorkflowPage、runner、cpr/curl HTTP 与 Beast/OpenSSL WS/WSS 客户端。Qt Network/WebSockets/SSL 仅创建 localhost **服务器**及读取它们收到的请求，没有使用 Qt 客户端替代产品客户端，没有 protocol stub 或模拟成功回调。至少一条完整链从真实 WorkflowPage 的 Run/Pause/Resume 按钮信号进入执行，并核对结果面板、日志和变量表联动。

## 独立发现与处置

| 发现 | 真实复现 / 影响 | 修复 / 最终证据 |
| --- | --- | --- |
| 服务器主动 close 丢失 code/reason | 原 `ws_client.hpp` unsolicited close 分支只发 disconnected；实际 pending wait 得到泛化错误，无法保留 4001/UTF-8 reason | C 增加 `webSocketClosed(int,QString,bool)`，GUI 消费顺序为 metadata→state；本 worker 按授权在 pending wait result/log 保留 peerCode/peerReason，并先清除资源身份，使显式 error edge 可恢复。最终 failure/error-edge 两条真实 Qt 服务器路径通过 |
| Qt WS/WSS close 被 transport teardown 错误覆盖 | 即使初次 additive metadata 修复后，真实 QWebSocketServer 主动关闭仍只有 receive error10053/10054；可信 HTTPS→WSS 完成 ready 断言后 requested close 在 n7 失败。C 的原 raw-frame fixture 不覆盖此互操作 | C 仅在已有合法 received close frame/code/UTF-8 reason 且错误属于 reset/aborted/SSL stream_truncated 时认定 WS close 已完成，未关闭 TLS verification。最终真实 Qt peer-close 和 HTTPS/WSS close 成功；独立 WS/WSS 无 close-frame abort 对照仍失败且无假 close signal |
| canonical per-line Authorization 可被换行 token 注入 | HTTP token=`good\r\nX-Injected: yes` 或 LF 变体，先替换再分行，使真实 WS 服务器收到 authorization=`Bearer good` 及 `x-injected=yes`，流程仍完成 | 按授权先把 line / JSON-text 请求头解析成 typed object，再展开变量；控制字符留在单一 value，由 C 校验拒绝。最终两种换行都在 WS traffic 前失败，WSconnections=0；旧真实注入证据保留 |
| JSON-string headers 中引用/反斜杠变量破坏编码 | 同样的替换顺序将引号直接写入 JSON 文本，C 无法解析本应有效的 header value | 同一请求头规范化修复保持值语义；真实 JSON-text 引号值和 exact whole-object variable 两条正向路径通过；JSON body 原有 parse-before-substitute 的字符串/bool/number/object 类型也独立通过 |
| protocol errors 未被用户层中文映射 | HTTP cap、TLS、WS receive error 等原始英文穿过 `userMessage()`，出现在节点结果和日志 | 按授权补充已审协议字符串及带错误码的前缀映射，保留技术错误码；真实 body/header/WS cap 和证书/hostname 失败断言中文字符存在 |

C 的协议互操作修复由本 worker 独立复核及重测；核心补正均已交 C 独立审核。没有为了全绿放宽 TLS 信任、改变负向用例、假造 close 或将突然断线说成成功关闭。

## 最终协议源码审查

| 关注点 | 审查结果与边界 |
| --- | --- |
| 身份、URL、时限 | operationId 1..128 UTF-16 字符；核心使用 runUUID/op/sequence，结果仍保留 nodeId。Strict QUrl 限 http/https/ws/wss、禁止 userInfo/fragment，元数据 URL≤4096；timeout 1..60000ms，connect≤total，实际默认 connect=min(5000,total)，核心传 canonical aliases。HTTP deadline 从创建开始计算，WS connect/handshake/send 和 close 有 timer |
| Headers / typed body | API 同时支持 object、JSON-text object、Name:value lines；supplied headers≤64条/16KiB，名称 tchar、值拒绝 CR/LF/NUL、禁止 Host/Content-Length/Upgrade 等覆盖；HTTP response header≤16KiB/128 callback lines。协议必需头、auto Content-Type 与 bounded subprotocol 是另外的固定/受限字段，未宣称完整 wire request 全部合计恰好16KiB。JSON body serializer 保留类型/引号；request body≤8MiB，JSON node/depth预算4096/32 |
| 完整消息与队列 | HTTP 累积 body、WS complete-message/read_message_max 与 send 上限8MiB；本轮有分帧 JSON/二进制和实际超限失败。HTTP解析JSON仅≤64KiB，较大body保留文本/base64；核心变量/结果预算独立，不能把协议8MiB上限当作所有workflow输出均可无限复制 |
| pending / cancel budget | Mailbox≤32 events、80MiB记账；HTTP完成按body×7+headers×4+1MiB估算，WS一条完整消息交付后须ack才重读。每client只准一个HTTP、一个connect、一个send、一个close；IoState coalesced wake每批4 commands，取消替换未执行队列，admission约束使commands≤5；request/write payload有独立8MiB上限。active async_write buffer保留到cancel callback，避免释放后引用；没有声称多个任意client实例的全进程工作集也被同一个80MiB预算覆盖 |
| DNS / threads | HTTP curl multi+5ms poll+c-ares异步DNS，关闭threaded resolver与c-ares threads；实际生成curl_config.h含 USE_ARES=1、USE_OPENSSL=1，USE_THREADS_POSIX/WIN32均undef。旧 CMakeCache ENABLE_ARES=OFF 由函数普通变量覆盖，判断实际生成配置而非只读cache。共享Runtime一个io thread；Boost WS resolver使用共享io resolver service，cancel/generation fencing不保证中断OS getaddrinfo本身，此轮未执行外部物理DNS黑洞压力场景 |
| Qt ownership / lifetime | I/O只访问shared mailbox，不访问QObject；timer在owner QObject thread逐个事件取出，generation过滤，signal后QPointer检查防reentrant destruction；ack捕获具体Ws weak_ptr且检查live，不会给新资源补旧read。新公共connected在Qt事件消费时按顺序更新。真实关闭signal thread/order、直接槽内删除client、Stop后重跑隔离通过 |
| close / state / errors | Requested close为 operationFinished→Closed(false)→state，core只对peerInitiated=true失败；spontaneous close metadata在pending-send错误/泛化state前交付。新completedClose只把已证实的合法WS关闭帧与有限teardown错误组合视为关闭；no-frame断线仍error。WSS TLS认证在连接阶段强制完成，互操作补正不构成跳过证书验证 |
| TLS / toolchain / deployment | HTTP强制VERIFYPEER=1、VERIFYHOST=2、TLS≥1.2，WS verify_peer+host_name_verification/TLS≥1.2，没有insecure模式。caFile支持，未提供时CURL native Windows CA / WS Windows ROOT；本轮验证显式可信CA、Unicode路径、不可信CA、两类错误hostname，不声称公网系统根CA正向连接或物理设备全场景。Boost与raw standalone Asio namespace/PRIVATE definitions隔离；实际客户端cpr/curl/c-ares/OpenSSL为static，无额外这些DLL直接导入 |
| 安装与复现 | setup下载源均固定版本/SHA256；OpenSSL限定Qt6.8.3/MinGW13.1.0/x86_64，no-shared/no-module，SOURCE_DATE_EPOCH=1759190400纳入stamp且强制重生成buildinf。manifest/许可证复制覆盖静态依赖。模块离线配置缺依赖时明确fatal，本轮未安装或重编译依赖 |

队列、generation 和线程审查是源码/实际生成配置证据；没有把本轮25行通过替换成独立8MiB最大边界、外部DNS故障、数百取消压力或长期进程工作集测量。C 自有相关检查属于其报告，未照抄为本轮独立通过数字。

## 原生组合验收

实际 Qt Windows 平台，Qt6.8.3、MinGW GCC13.1.0、C++17，服务器全为真实localhost TCP/WS/TLS。默认登录模板仅替换临时端口/地址，保留默认 method/headers/body/type/path/output；客户端实测 POST application/json→httpResponse.body.token→Authorization:Bearer token→sendWait subscribe→完整分帧JSON→ready断言→close→end。

| 独立覆盖 | 行数（不含init/cleanup） | 实际断言 |
| --- | ---: | --- |
| WorkflowPage默认组合、Pause/Resume、结果日志变量 | 1 | 不运行时零连接，按钮Run后真实请求；pause时当前I/O完成而n6仍pending，resume才继续；typed状态/headers/body/base64，完整8节点success、8日志，结果面板ready和表model行数，服务端归零/协议QObject释放 |
| JSON body替换 | 1 | 引号、反斜杠、换行、中文、bool/number/object正确序列化，真实服务器读到对应typed JSON |
| JSON-text headers / whole-object variable | 2 | 请求头引号值保持；`${requestHeaders}` typed object可用，非递归动态求值 |
| CRLF/LF token | 2 | 请求头值拒绝，WS handshake前失败、服务端连接计数零 |
| HTTP401 any→缺少token | 1 | HTTP节点success且body/error/bool/base64保留；extract token失败，WS skipped且零连接 |
| 二进制sendWait | 1 | 含00/FF字节真实往返，binary标识/length/base64无损 |
| HTTP body/header与WS超限 | 3 | 节点失败、中文diagnostic、无完整业务结果、资源释放 |
| 页面Stop与旧排队回复/新run隔离 | 1 | 向旧连接发送实际seq101并停止Qt消费40ms时触发Stop；Beast I/O仍独立运行，但测试不读取私有mailbox来断言具体入队时刻。新run仅保留seq202及新runId，旧回复未污染，服务端两次连接后归零 |
| Peer4001UTF8与error-edge | 2 | failed wait output/log保留peerCode/peerReason；正常terminal failure及显式log→end恢复路径分别正确 |
| Close signal顺序、Qt线程、reentrant delete | 1 | Closed-before-State、GUI thread，message槽直接delete client安全释放 |
| 无close frame突然断线 | 2 | WS/WSS都protocolError且无假Closed signal，connected=false |
| HTTPS→WSS完整组合与TLS负向 | 6 | trusted CA/Unicode CA两条success含requested close；不可信HTTP/WSS、错误HTTP/WSS hostname四条fail |

业务data rows共23，加init/cleanup共 **25 passed / 0 failed / 0 skipped，8292ms**。证据 `docs/validation/workflow-implementation/e2e/windows-test-final.txt`，结构化UTF-8结果在 `e2e/final/`。证书私钥仅临时目录生成并随测试清理；所有token/设备数据均是fixture合成数据，未与真实账号/设备通信。

## 前后证据与构建来源

- 初轮真实suite：`windows-test-initial.txt` **14 pass / 5 fail**，体现Qt close互操作欠缺。
- focused pre-fix：`windows-test-repro.txt` **2 pass / 7 fail**，覆盖真实CRLF/LF注入及close/TLS失败；`repro/token-*-server.json`保存额外头的服务端实证，`repro/peer-close-protocol.json`保存0 close signals及10053。
- 核心header/中文补正后对原协议快照focused检查：`windows-core-repair-test.txt` **16 pass / 0 fail**；`core-repair/`保留修复阶段结果，不能将此阶段说成全部TLS close已通过。
- 最终C corrected artifact独立检查：`windows-test-final.txt` **25 pass / 0 fail**；`final/`保留typed结果、headers错误拒绝、peer-error恢复、TLS与abrupt对照。

构建隔离于 `build/workflow-protocol-review`，只编译本worker授权core与test；只读复制既有root真实UI/session/network/QtNodes archive及C实际协议依赖archive，再在独立目录链接。未争用共享product build，未安装/重编译协议依赖。UI archive身份锁定，而不是假称对所有root最新源码做完整产品重编译；协调者另跑冻结root全套。

最终链接10个真实archive全部通过 `sha256sum -c linked-artifacts.sha256`；初期archive/source指纹另存 `*-initial.sha256`，未覆盖失败历史。`source-review.sha256`记录最终审阅C source/头、cmake/setup、授权core和test；EXE指纹在 `test-executable.sha256`。SOURCE_DATE_EPOCH后最终OpenSSL stamp=1759190400。

关键最终archive SHA-256：

| 档案 | SHA-256 |
| --- | --- |
| protocol | `b40f12b238ec137143c63bde04ed5de134d229a2d96454bc43891c5d4ba04af0` |
| cpr | `0f7cde60ac33d244a1562e2f02b5a8a211493403033b7b4fa6e7cb41e30b4d9e` |
| curl | `e7843da1d0c1c0d85fd59a4adb52de1bd9c611ea999b446c4eb2d7aebf3457d0` |
| c-ares | `9d92c02642394449a845a079e66c16fb637c1498c377281f1eeaee8ec57fb629` |
| ssl | `ba31e94f6dfa49b4b4870ca57cf0cc0fe9c306369fee3fe9231aef5d45647c65` |
| crypto | `86df25d84d82c83349bff5e0b609d280fb1bfa739eb92bb353c64096d3140862` |
| real UI | `a5712ec57aa73b95e0a99198c82628cbaa32a019a51fc9272f0e59887e88bd20` |

PE直接导入检查 `pe-imports.txt` 没有cpr/curl/c-ares/libssl/libcrypto DLL；Qt6WebSockets.dll/Qt6Test.dll是服务器fixture/测试依赖。没有替协调者宣称整套部署或restrictedPATH测试重复通过，本轮仅独立核对实际测试EXE import table，C自己的部署证据仍属于C。

## 冻结与剩余工作

本worker `workflow_runner.cpp`、`workflow_messages.hpp`、`test_workflow_e2e.cpp` 在最终25行通过后通知协调者冻结，无进一步源编辑。协议本轮发现均已修复且对应真实独立回归通过；不阻塞所审协议组合范围。

剩余C独立核心/UI复审、协调者最新root全8suite与native产品验收不属于本报告的完成宣称。未验证公网服务、物理设备、系统CA正向公网链、外部DNS长期故障、所有平台、所有DPI/发布安装。前main集成及旧core14范围由前报告解释，本轮没有无理由重跑全库旧回归。

修改：`tests/test_workflow_e2e.cpp`、授权 `src/workflow/workflow_runner.cpp` / `src/workflow/workflow_messages.hpp`、本报告、`docs/validation/workflow-implementation/e2e/` 证据/索引；另有忽略的独立CMake、archive快照与构建日志。没有修改C协议源、UI/main或root CMake。
