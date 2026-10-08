# 工作流独立复审（C）

状态：**本轮所审 core/session、main-window、native UI 范围批准；P1/P2 已在冻结源码上独立闭环**。最终协调者冻结通知为 `msg_7617d7407a9c`，覆盖 B 补齐的 26 行 UI 源码/测试；替代较早的冻结通知。C 自己的协议修复自测不作为独立协议批准，协议批准另由 A 的证据支持。root 最终整套回归、产品窗口/部署验收仍由 coordinator 完成。

本报告对应独立复审 dispatch `ctx_b2b905a7003f` / task `task_ac1d5ed55189`，与已结算的初始协议实现 dispatch 分开。匹配环境为 Windows 11、MinGW GCC 13.1.0、Qt 6.8.3、C++17。所有 C 复审构建在 `build/workflow-independent-review`，编译真实 core/session/UI/main-window 源码，静态链接 QtNodes 与真正的 cpr/libcurl/Beast protocol archive；没有 fake protocol、Qt HTTP/WebSocket 替代客户端，也没有构建或修改其他 worker 的目录。

## 本人执行的证据

证据路径基于 `docs/validation/workflow-implementation/independent/`；QtTest totals 包含 init/cleanup，不应相加解释为不重复的业务需求数量。

| 范围 | 结果 | 证据 |
|---|---:|---|
| A 核心/session 原生套件独立构建重放 | 14 passed / 0 failed / 0 skipped | `core-replay.txt` |
| B 冻结原生 UI 全套，受 P1 修复影响后重新构建 | **26 / 0 / 0** | `ui-final.txt`（较早基线 `ui-replay.txt` 为 17/0/0） |
| coordinator 主窗口集成套件独立构建重放 | 10 / 0 / 0 | `main-window-replay.txt` |
| C 自编附加真 TCP/UDP/实际 UI 探针，修复前 | 8 passed / 1 failed | `additional-before.txt` |
| C 补充实际长 token 结果导出，修复前 | 2 passed / 2 failed（preview / actual-export） | `credential-export-before.txt` |
| C 最终实际 preview/export：长 token、数字、转义键、大 JSON | **10 / 0 / 0** | `credential-final.txt`（八业务行 + lifecycle） |
| A 冻结核心 typed headers / close metadata 修复，定向独立重放 | 8 / 0 / 0 | `core-contract-final.txt` |
| Windows 原生可见 GUI：URL 编辑、检查、撤销、RX/TX0 | 实际撤销回原始 URL，检查未触发连接/发送 | `product-dark-1024.png`；原始 accessibility JSON 在 own build/gui-* |
| Qt scale 1.25 / 1.5 最终冻结原生截图 | 各 **3 / 0 / 0**；DPR=1.25/1.5、zoom=0.85、Stop 可见、canvas 非空 | `native-{125,150}-final.txt`、`native-layout-{1.25,1.5}.json`、theme/size/scale PNG |

附加探针实际启动 localhost TCP client/server、UDP peer 和真实 HTTP peer。覆盖 delimiter/fixed/lengthHeader 的拆分与合并、TCP-server selected-client 过滤、其他 client 断开、显示暂停/high-speed 下可靠读取、静默 UDP lifecycle、send admission 语义、borrowed stop、owned epoch 被人工替换后保留人工连接，以及 observer overflow 明确失败且不产生 message。

`core-contract-final.txt` 实际执行 JSON header text 中的引号、整个 object variable、CRLF/LF token 注入拒绝，以及真实 Qt WS peer 4001/UTF-8 关闭后的 terminal failure 和显式 error-edge。验证的是 A 的 core 整合；协议 archive 是 C 本人实现的模块，协议独立性另由 A 提供。

## 发现与关闭状态

### P1：Base64 preview 和实际结果导出的凭据遮蔽绕过（已独立关闭）

真实 HTTP JSON 返回 `token` 为 5000 个字符加尾标记，body 小于 64 KiB。旧 `collectSecrets` 忽略长度超过 4096 的字符串，旧 `redact` 只有 secrets 非空才处理 `*Base64`。`workflowResult` 展示完整编码；实际点击 `workflowResultExport` 并通过真实 Qt 文件对话框写出的 JSON 也保留完整 `bodyBase64`，可解码恢复凭据。

保留 `additional-before.txt` 和 `credential-export-before.txt`；第二份证据是实际文件导出失败，不是只检查 helper 的推断。第一轮 B 修复后，5000 字符 token 的 preview 和 actual-export 已独立通过，但扩大同一策略检查发现 `{"token":1234567890123}`、Unicode 转义键 `to\\u006Ben` 对应的数字 token、以及 300000 字符 padding 后的 `clientCredential` 仍有可恢复的 Base64。`credential-key-checkpoint.txt` 为 5 passed / 5 failed：前两种各有 preview/export 失败，大响应 preview 被截断但实际导出仍失败。

原因是字符串 sample collection 与只匹配引号 credential value 的 regex 没有覆盖与 `sensitive(key)` 相同的结构语义。已将三种真实 HTTP/实际文件导出复现一次性报 B/coordinator；C 不修改 B 产品文件。

最终 B 修复按 bounded chunks 检查编码字节：遇到 object/array 语法或无法安全检查时隐藏该编码表示；可读结构中敏感 key 按实际 JSON 类型遮蔽。超过 262144 字符的结构文本保守隐藏，并通过有界 sample 检查避免大结果中的凭据转入 free-text logs。后端 raw output 保持不变。

在最终 26 行源码/测试冻结版本上，C 自编八业务行实际 preview/export **10/0/0**，包含 long token、numeric、Unicode-escaped key、300000 padding 后 clientCredential；B 九个实际 HTTP credential 类型/尺寸测试由 C 整套独立重放，连同 clipboard/log/variables/raw-integrity 等原生 UI 总计 **26/0/0**。证据 `credential-final.txt`、`ui-final.txt`；原失败文件完整保留。修复会同时保守隐藏一些不含凭据的 JSON/含结构符号的 Base64 以及大型结构文本，这是安全检查能力的显式边界，可读小结构与后端原始值仍保留。

### P2：翻转第二排节点的错误标签与第二行摘要重叠（已独立关闭）

旧 `native/current-dark.png` 的原图已另存 `independent/p2-label-before.png`， close/assert 第二排左侧“错误”标签覆盖“流程完成”/“明确通过/不通过”。旧 Painter 只保留右侧 label gutter，且摘要 baseline 接近端口文字。B 修改摘要位置、反向节点左侧空间后，C 重新构建真实原生 UI 并将视口平移至第二排；`labels-dark-1440x1000-scale1.25.png` 显示 close/assert/sendWait 的错误标签和摘要分离，保留左侧空间。此缺陷已独立关闭，最终源指纹已随完整冻结记录；最终 1.5 scale 的 `labels-dark-1440x1000-scale1.5.png` 也显示相同分离。

### 协议：Qt peer close 后 Windows/TLS teardown 10053（C 自修，独立协议复审另由 A）

修复前实际 Qt WS/WSS 定向结果 5 passed / 3 failed，证据 `protocol-qt-close-before.txt`。授权的最小修复仅接受已验证 WebSocket close 帧/合法 code 和 UTF-8 reason 后的 TCP reset/abort/TLS stream_truncated，突然无 close 断线仍失败，TLS chain/hostname 策略未改变。

C 修复后协议原生 17/0/0 和 restricted-PATH 17/0/0 是自测，证据 `protocol-qt-close-after.txt`、`protocol-restricted-final.txt`、`protocol-restricted-manifest.json`。独立批准由 A 的另一个 dispatch 提供：`docs/reviews/workflow-protocol-independent.md` 及 `e2e/windows-test-final.txt` **25/0/0**，实际组合涵盖 trusted HTTPS→WSS requested close、peer 4001/UTF-8、WS/WSS 无 close 突断、不可信 CA/错误 hostname、Unicode CA path。C 已核对 `e2e/source-review.sha256` 所列全部 12 个当前源文件均匹配，证据 `independent/a-source-freeze-check.txt`；这是对 A 独立证据的审核，不冒充 C 亲自执行 25 行。

当前 archive SHA256 `b40f12b238ec137143c63bde04ed5de134d229a2d96454bc43891c5d4ba04af0` 与 A 独立 relink 所用档案匹配；测试新增 `Qt6WebSockets.dll` 仅用于真正 Qt peer，不是生产 runtime 依赖。协议不再阻塞本轮批准。

## 设计与集成判断

已对照 `11-workflow-research.md`、`12-workflow-ui-design.md`、`13-workflow-implementation-plan.md` 及 prototype 的 index/app/styles，检查 native current dark/light 和本人原生窗口。原生 UI 保留节点库、参数面板、底部运行记录/变量、主题、清晰的运行/停止、资源状态、错误出口和图操作，主要信息结构及视觉层次吻合设计。缩小窗口通过紧凑面板/折叠保留画布与停止入口，而不是自动改变 85% 初始 zoom；大图通过平移、缩放、概览查看。

主窗口源码和独立 integration 结果证明指定活动/后台资源提供者稳定、workflow 请求期阻止竞争发送、编辑/模板/检查不触发网络、owned 清理按 epoch、borrowed stop 保留连接、已替换的人工任务不会被旧 workflow 清理误关。核心基线覆盖严格 schema、变量/分支/有限循环/普通 cycle 拒绝、失败/error edge、暂停边界、deadline/step/variable/observer/message 限制、新 run 不受旧 callback 污染。

receive observer 与显示队列分开，显示暂停或采样不影响可靠判定。`onData` 在 recorder admission 重试返回之前不发布 observer record、只在接纳之后发布一次；本人网络探针覆盖可靠读取和 overflow，尚未通过受控 instrumentation 强制命中每个 recorder retry 分支。发送结果明确为 `admitted=true`、`localWriteCompleted=false`、`peerAcknowledged=false`，不把 admission 宣称为 peer 成功。

本人 1.5 scale 原生窗口受实际屏幕工作区约束：请求 1440×1000 得到 1283×707，请求 1024×768 得到 1024×707；实际尺寸、DPR、96 logical DPI、画布尺寸/zoom 写入 JSON。此项是 Qt application scale 验证，不能称为物理 Windows 125%/150% 设置验证。低尺寸画布不会自动容纳整个八节点图，需使用平移或显式概览。

## 验证边界

没有物理串口、两机 2.5G 或原 QtSocket packet-loss 实测；localhost 软件 peer 与原生 Qt UI 不替代这些验收。没有修改系统 ROOT、放宽 TLS、不运行脚本型节点、未改旧应用实例或 `build/release`。可见产品窗口由本复审独立启动、已关闭以释放 root exe。

最初一次 GUI 撤销调用由于 stale element index 未执行；随后刷新 accessibility tree、再次点击撤销，实际确认 URL 从 `/independent-review` 返回 `/api/token`，RX/TX 仍为 0。失败原始 JSON 保留，不把失败调用计作成功。

## 最终冻结与结论

最终冻结来源：coordinator `msg_7617d7407a9c`、B `msg_708ab7fbc71e`；`final-receipt.json` 记录 **48 个实际 product/source/test/setup 源码 hash、13 个真实 archive/EXE hash**、toolchain 和各证据结果。检查 A 协议独立审阅的 12 个 source hash 全部吻合；`b-final-freeze-check.txt` 逐项核对 B 最后给出的 canvas/page/test 三个 SHA256，全部 MATCH。冻结核对发现先前 UI 构建仍需更新，C 实际重新编译后执行最终 UI26、credential10、native scale 各3；最终 receipt 记录这些产物，未以较早24行产物替代。

源码/依赖层审查确认 QtNodes 的 const QVariant 提取是固定补丁，没有全局禁用诊断；C 独立构建保留 `-Wall -Wextra -Wpedantic`，受影响最后编译日志没有 warning/error/FAILED。root 实际冻结编译及整套 CTest 由 coordinator 的独立产品 build 继续执行，本报告不冒称本人运行了 root 最终 suite。

本轮批准仅覆盖所审 core/session、main-window 资源/生命周期和 native UI/设计范围，以及修复 P1/P2 的实际闭环。未改变的 core14/main10 基线不重复，core header/peer-close 独立8行和 A 协议25行的证据边界清楚。root 最终 fullCTest、native product 与 deployment 收尾仍属协调者验收，物理/公网/DPI边界如上。

附加复现源码保存在 `independent/independent-probe.cpp` 和 `probe-CMakeLists.txt`。恢复到 `build/workflow-independent-review/probe/{independent.cpp,CMakeLists.txt}`，使用匹配 Qt/MinGW 配置并预先准备固定依赖/真正 protocol archive 后可重复构建；不在 application build 时获取新依赖。最终测试与 receipt 输入全部是实际模块。
