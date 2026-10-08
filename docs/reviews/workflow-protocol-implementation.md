# 工作流 HTTP / WebSocket 协议实现及验证

Worker C，Task `task_e0526fe83ed5` / Dispatch `ctx_6e2668e4ea54`。范围为预置 WorkflowProtocolClient ABI、协议源码、依赖模块/setup、协议测试和许可；没有修改 root CMake、UI、执行器、原有测试、发布目录或 raw 网络引擎。本报告覆盖独立协议模块，产品整体运行与发布由协调者负责。

## 实现与接入

`include/portbridge/workflow_protocol.hpp` 保留原有方法和信号签名；独立审核发现 server-initiated close 元数据丢失后，经协调增加 `webSocketClosed(int code, const QString& reason, bool peerInitiated)`。`cmake/WorkflowDependencies.cmake` 提供 `portbridge_setup_workflow_dependencies()`，创建 `portbridge_workflow_protocol` 静态 target，public include 为 `include/`，public link 为 `Qt6::Core`。其余链接 cpr、libcurl、c-ares、OpenSSL、Threads 和必要 Windows 系统库；Boost.Beast/Boost.Asio 头文件只进入协议 target 的 private include。模块自行启用 AUTOMOC，不向 standalone Asio 核心暴露 Boost 类型。

root 在找到 Qt6 Core 后 `include(cmake/WorkflowDependencies.cmake)` 并调用该函数即可。整个产品首次构建前运行 `scripts/setup-workflow-deps.ps1 -QtRoot C:\Qt\6.8.3\mingw_64 -MingwRoot C:\Qt\Tools\mingw1310_64 -Parallel 4`。脚本校验工具链版本与固定源归档 SHA256，构建静态 OpenSSL；CMake 配置/编译不联网。c-ares 使用独立 build-tree export，解决 curl `export(TARGETS)` 对该静态依赖的 export-set 要求。所有编译/安装都在 `.deps/workflow/` 和 `build/workflow-protocol/`。

HTTP 在专用协议 io_context 的线程中用 cpr 的 PrepareGet/Post/Put/Patch/Delete/Head/Options 准备真实 easy handle，再交给 libcurl multi 及 5 ms Asio timer 驱动；不调用 cpr 无界异步线程接口，也不在 Qt 线程执行网络请求。libcurl 使用 c-ares 异步 DNS，避免连续取消后仍留下逐请求 getaddrinfo 线程。

WS 使用 Beast 的异步 resolve/connect/TLS/upgrade/read/write/close；其 Boost.Asio 与 raw 核心的 standalone Asio 分开。专用协议 runtime 在进程中只有一个 I/O 线程，Beast 的 Boost.Asio resolver 使用其受控内部 DNS worker。每个 client 限制一个 HTTP operation、一个持久 WS、一个 write 和一个 close。写出成功才完成 send operation；close operation 在 disconnected state 通知之前完成。完成 close 输出请求 code/reason 和 peerCode/peerReason；主动 peer close 通过有界 `webSocketClosed` 保留 code 与 UTF-8 reason，先于 disconnected state 和任何待发送失败。public connected 状态在 Qt event delivery 更新，避免同一批次的旧 connect state 先报告泛化断开而掩盖 peer metadata。

Qt 拥有的 10 ms QTimer 从有限 mailbox 投递信号，I/O 从不访问 QObject。WS 完整消息（含碎片装配）投递后暂停下一次 read，直到 Qt 确认交付，GUI 停顿不会累积每条消息的 queued signal。输入命令也采用有限、单次调度 mailbox；cancel 清空未处理命令并递增 generation，退休的操作不能再发完成/消息信号。QObject cancel/destruction 只提交工作线程清理，不等待 DNS、TLS 或 socket 操作。

## 参数及结果

- HTTP：`url`、`method`、`headers`（object、JSON text 或 `Name: value` 每行文本）、`body`（UTF-8 text/object/array）、`timeoutMs`/`timeout`、`connectTimeoutMs`/`connectTimeout`、`maxResponseBytes`、`caFile`。
- WS：`url`、同格式 `headers`、`subprotocol`、超时别名、`maxMessageBytes`、`caFile`。
- 超时单位统一为 ms，范围 1..60000；连接超时不超过总超时；省略连接超时时默认 min(5000,total)。
- HTTP 非 2xx 是完成响应，`error` 为空；输出 `status`、小写 `headers`、`body`、`bodyText`、`bodyBase64`、`bodyBytes`、`elapsedMs`。不将 HTTP 401/500 当网络错误。
- body <=64 KiB 且合法 object/array 时 `body` 为解析 JSON，否则为 UTF-8 text；`bodyBase64` 始终保留原始字节。UTF-8 显示替换非法编码时原始字节仍可恢复。
- TLS 必须校验证书链和主机名，最低 TLS 1.2。默认读取 Windows ROOT 信任存储；局部 `caFile` 使用 PEM 证书，不修改系统根存储。错误只输出阶段及数字错误码/安全库 category，不回显 URL query、Authorization、CA 路径或 peer 提供的文本。

body 多种表达都占用调用者的变量预算；例如接近 8 MiB 的响应若保留全部结果，需要相应变量容量，执行器可另行拒绝超过其预算的结果。协议没有替执行器做状态断言、变量替换或 raw resource lease。

## 明确限额

| 内容 | 限额 / 生效位置 |
| --- | --- |
| HTTP request body | 最大 8 MiB；JSON 预先计算保守编码预算，最多 4096 值、32 层 |
| HTTP response body | 1..8 MiB，默认 8 MiB；write callback 在追加每块之前检查 |
| HTTP response headers | 累计 16 KiB / 128 行；header callback 在累积之前检查，含中间 status/空行 |
| 用户 request headers | 最多 64 字段、合计 16 KiB；禁止 CR/LF 注入和覆盖保留协议字段 |
| URL / CA path / operationId | 4096 字符 / 4096 字符 / 128 字符 |
| WS message / send | 1..8 MiB，默认 8 MiB；read_message_max 和受限 flat_buffer，发送入队之前检查 |
| WS close reason | 最多 123 UTF-8 bytes；只允许合法 RFC close codes |
| Beast upgrade response | 上游 parser 有限默认：header 8 KiB / response body 8 MiB；压缩扩展关闭 |
| Qt mailbox | 32 events / 80 MiB 加权预算；可容纳一个最大 HTTP 结构化结果及一个最大 WS handoff |
| WS handoff / write | 每种至多一个在途；控制帧由 Beast read/close 机制处理 |

HTTP/WS 默认不自动重试或重连。HTTP redirects、环境代理、持久 Cookie、HTTP/2/3 与压缩扩展未启用；自带 client 范围不包含服务器功能。关闭连接的 peer reason 通过有界 close result / close metadata signal 返回，不混入协议错误日志。取消不能撤回已经被对端接收的业务数据。WS 操作可在 DNS 阶段按应用 deadline 失败，但正在进行的系统 getaddrinfo 可能继续在唯一的 Boost.Asio resolver worker 上完成；QObject 停止/销毁不等待它。

## 固定依赖与许可

固定 cpr 1.14.2、libcurl 8.16.0、c-ares 1.34.5、Boost 1.89.0、OpenSSL 3.5.4。全部下载 URL、归档 SHA256、许可证见 `THIRD_PARTY_NOTICES.md` 与 setup 脚本。OpenSSL 实际选项：`mingw64 no-shared no-tests no-apps no-docs no-module`，固定 `SOURCE_DATE_EPOCH=1759190400`（3.5.4 发布日 2025-09-30 00:00 UTC）以免 build-info 每次嵌入当前日期。c-ares 静态、无 event thread；libcurl 显式使用 HTTP/HTTPS、OpenSSL、c-ares，关闭 libpsl（无需 Meson）、Cookie、HTTP/2/3、libssh2、IDN2、zlib/brotli/zstd 与 curl WS。

主组件许可原文已纳入 `src/protocol/licenses/`，setup 复制到 `.deps/workflow/licenses/` 供发布。cpr 主体 MIT，upstream test 子目录是 GPLv3，其许可单独保留；应用没有构建/链接 cpr upstream tests、Mongoose 或 GoogleTest。构建辅助纯 Perl 模块及 Strawberry Pod 源归档也固定 SHA256，只进入 .deps，不部署。

## 原生验收

测试在 Windows 11、Qt 6.8.3、MinGW GCC 13.1.0、C++17 Release 下实际编译并连接 localhost。`tests/test_workflow_protocol.cpp` 使用 QTcpServer / QSslSocket 作为测试服务端，手工生成 RFC6455 真实握手和帧；客户端实际是 cpr/libcurl/Beast。每次测试用 OpenSSL CLI 生成两天有效、SAN=localhost 的隔离 CA/server 证书，只写临时目录。

| 测试 | 实际覆盖 |
| --- | --- |
| httpStatusAndBody | 200 JSON、401/500 body 保留；逐行 Authorization/Content-Type 头实际转发；lossless body 字节 |
| httpCapsDeadlinesCancelAndReuse | body/header cap 中止；100 ms 总截止；取消后新请求完成且旧 callback 静默；HTTP 销毁 <100 ms |
| webSocketTextBinaryFragmentAndClose | text/binary 实际回环、fragment 装配、交错 ping/pong、subprotocol、write-completion byte count、close 顺序及双方 code/reason；DirectConnection 检查信号线程 |
| webSocketFailuresAndLimits | 非 upgrade 响应、握手 timeout、oversize、保留 opcode 的 malformed peer、主动正常 peer close |
| unsolicitedPeerCloseMetadataBeforeState | 实际服务端 4001/UTF-8 令牌过期原因；close metadata 先于 disconnected State，DirectConnection 验证 QObject 线程 |
| boundedHandoffCancelAndReuse | 1000 条真实 WS 消息，Qt 暂停处理时没有无界投递；cancel 清除旧消息；同 client 重连/关闭 |
| maximumHttpAndWebSocketPayloads | 同 client 最大 8 MiB HTTP 与 8 MiB WS 消息完成、类型和原始字节保留 |
| cancellationAdmissionAndConfigurationBudgets | 200 次 cancel/reuse、握手取消、待发送取消、WS 销毁 <100 ms、无效头/超大限额/JSON 节点预算拒绝 |
| httpsAndWssTrustAndHostname | HTTPS/WSS 局部 CA 成功及 WSS echo；未提供信任 CA 和 wrong host 两种证书失败分别拒绝 |

完整原生 suite 为 **11 passed, 0 failed, 0 skipped**（含 init/cleanup），包括最终诊断、信号线程、server-initiated close metadata、peer close metadata 和最大容量检查。最终 warning-enabled 编译未产生 warning/error；最后原生运行 6071 ms。开发测试日志 `build/workflow-protocol/test-results.txt`、JUnit `.xml`；编译日志 `build/workflow-protocol/build-peer-close.log`，SOURCE_DATE_EPOCH SDK 记录 `build/workflow-protocol/setup-final.log`。修复过 Windows localhost IPv6 首尝试吞掉连接截止的问题，当前优先 IPv4 并保留 IPv6 fallback；没有全局禁用诊断。

## 部署验证

协议依赖都静态链接，**没有新增 cpr/curl/c-ares/OpenSSL DLL**。PE import 只增加标准 Windows 系统库（ADVAPI32、bcrypt、CRYPT32、IPHLPAPI、KERNEL32、msvcrt、USER32、WS2_32）；应用仍需 Qt6Core 和已有 MinGW runtime。独立测试 executable 另需 Qt6Network/Qt6Test 与 Schannel plugin，仅因为它承载 Qt TLS 测试服务器。

已将测试 executable、Qt/MinGW DLL、Schannel 测试 plugin、局部 qt.conf 复制到 `build/workflow-protocol/portable/`。将 PATH 限制为 Windows System32/Windows，清除 QT_PLUGIN_PATH/OPENSSL_CONF，再实际执行完整 HTTP/WS/HTTPS/WSS suite，最终结果 **11 passed, 0 failed, 0 skipped**，6200 ms，native 和 copied executable 的 SHA256 一致。OpenSSL CLI 仅通过 Git 安装目录的绝对路径生成测试证书，其邻接 DLL 为该 CLI 自身运行环境；协议客户端没有动态依赖 OpenSSL DLL。这是实际 TLS 请求验收，不是检查 Qt SSL backend 是否可加载。

`.deps/workflow/openssl-install/portbridge-build.json` 记录源选项、发布 epoch 和 compiler hash；`.deps/workflow/deployment-manifest.json` 记录 static TLS 产物和 compiler runtime 的 SHA256。最终产品应复制 `.deps/workflow/licenses/` 并由协调者的发布脚本生成最终 executable/DLL 清单及移除本机绝对路径。本 worker 的 copied runtime 校验不代替完整 PortBridge 产品打包和 UI 验收。

| 运行库 / 实际产物 | SHA256 |
| --- | --- |
| 最终协议 libportbridge_workflow_protocol.a | `dc3e0b6a7d81809774a3570657b7b806d81de3525ef33691a1504b567550ccd1` |
| 最终 native/copied test_workflow_protocol.exe | `8e761e7f9ec7a623f1b59bf75818505d47c105105d2cc6fb579042526e42e11e` |
| Qt6Core.dll | `06d84a2e85bda1a651c65f7d159b63bee2458a8b50274da52b70c9e3e9188e97` |
| libgcc_s_seh-1.dll | `5e27589147caa6d7a30f4cc058a14c455dc62233901ffff9a1797ede425e9f86` |
| libstdc++-6.dll | `8013488c5528bad7966ca07f3ea2e7a9b743cacb258fe76b46a326f821cc83b0` |
| libwinpthread-1.dll | `c7c7dced65fff71c7bb61f80c771c562f533d26d72722d9d6091129a3b03d5ce` |
| OpenSSL libssl.a | `ba31e94f6dfa49b4b4870ca57cf0cc0fe9c306369fee3fe9231aef5d45647c65` |
| OpenSSL libcrypto.a | `86df25d84d82c83349bff5e0b609d280fb1bfa739eb92bb353c64096d3140862` |
| Native cpr libcpr.a | `0f7cde60ac33d244a1562e2f02b5a8a211493403033b7b4fa6e7cb41e30b4d9e` |
| Native curl libcurl.a | `e7843da1d0c1c0d85fd59a4adb52de1bd9c611ea999b446c4eb2d7aebf3457d0` |
| Native c-ares libcares.a | `9d92c02642394449a845a079e66c16fb637c1498c377281f1eeaee8ec57fb629` |

## 独立重跑

在空的忽略 build 子目录创建如下 CMakeLists.txt，将 ROOT 换为仓库绝对路径；不会改产品 root CMake。

```cmake
cmake_minimum_required(VERSION 3.24)
project(WorkflowProtocolProbe LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Network Test WebSockets)
set(ROOT "C:/Users/threeTeeth/workSpace/github/PortBridge")
include("${ROOT}/cmake/WorkflowDependencies.cmake")
portbridge_setup_workflow_dependencies()
add_executable(test_workflow_protocol "${ROOT}/tests/test_workflow_protocol.cpp")
target_link_libraries(test_workflow_protocol PRIVATE portbridge_workflow_protocol Qt6::Network Qt6::Test Qt6::WebSockets)
```

先运行 setup，再用 Ninja、Release、Qt prefix 和匹配 gcc/g++ 配置/构建；执行 `test_workflow_protocol.exe -o results.txt,txt -o results.xml,junitxml`。测试可用 `PORTBRIDGE_TEST_OPENSSL` 指定生成临时证书的 CLI 路径；没有该变量时搜索 PATH，然后使用 Git 的 openssl.exe 绝对路径。Qt TLS 测试服务端需要标准 TLS plugin；客户端本身不需要它。

## 独立审查期间的 Qt 服务端关闭兼容性修复

原始交付的 11/0 是历史基线。在新授权的独立审查期间，真实 `QWebSocketServer` 在 Windows 上的 WS/WSS 关闭复现了 10053：peer 已发送合法 close 后，TCP reset/abort 或缺少 TLS `close_notify` 使 Beast teardown 返回错误。修复只在已验证 close frame 或已验证 close reason 的前提下接受这些 teardown 错误；没有 close 的 abrupt disconnect 仍失败，证书链/hostname 验证未放宽。

新增六行真实 Qt WS/WSS 请求关闭、4001/UTF-8 peer 关闭、无 close 突然断线控制。C 本人修复前定向测试 5/3，修复后全套 **17/0/0**；这些是 C 自测，独立协议批准由 A 的另一个 dispatch 提供。证据保留在 `docs/validation/workflow-implementation/independent/protocol-qt-close-{before,after}.txt`。

重新复制新二进制、只提供 Qt/MinGW runtime 和 Qt Schannel 测试服务端 plugin，在仅有 Windows System32/Windows 的 PATH 下执行 **17/0/0**。新测试专用 runtime 多一个 `Qt6WebSockets.dll`；生产客户端仍是 cpr/Beast，未引入 Qt WebSocket 客户端或 protocol runtime DLL。证据/完整 manifest：`independent/protocol-restricted-final.txt`、`independent/protocol-restricted-manifest.json`；原始 restricted 11/0 文件未覆盖。

当前协议 archive SHA256：`b40f12b238ec137143c63bde04ed5de134d229a2d96454bc43891c5d4ba04af0`；更新后 native/copied test executable SHA256：`0e79063decaaa6d63912c7831d28f265d61afcf3eb727be8d597a478c9034c3f`。上文旧 test hash 是原始交付的历史指纹。
