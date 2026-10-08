# 独立 HTTP/WS 组合验收证据

所属 Task `task_b1b0be1425dc` / Dispatch `ctx_aed7b9768572`。独立审核报告：[workflow-protocol-independent.md](../../../reviews/workflow-protocol-independent.md)。

最终实际 Qt **Windows** 平台：**25 passed / 0 failed / 0 skipped，8292 ms**。这是实际 cpr/curl/Beast/OpenSSL 客户端、真实 WorkflowPage/runner 和 localhost Qt HTTP/WS/TLS 服务器，服务器fixture没有替代客户端。本目录所有 token、设备状态与业务值都是合成fixture数据；不包含生成的证书私钥，也没有真实账号/设备通信。

## 结果与失败历史

| 文件 / 目录 | 含义 |
| --- | --- |
| [windows-test-final.txt](windows-test-final.txt) | 最终完整25行Windows独立验收 |
| [final/](final/) | 最终UTF-8结构化结果/变量/日志、401、规范化headers、peer4001错误恢复、TLS和abrupt对照 |
| [windows-test-initial.txt](windows-test-initial.txt) | 初轮14 pass / 5 fail，真实Qt WS/WSS close问题 |
| [windows-test-repro.txt](windows-test-repro.txt) | 修复前focused2 pass / 7 fail，包括CRLF/LF额外请求头实证 |
| [repro/](repro/) | 初期真实注入及关闭错误，`token-*-server.json`可见x-injected=yes和WSconnections=1；`peer-close-protocol.json`为0close signals及10053 |
| [windows-core-repair-test.txt](windows-core-repair-test.txt) | 核心header/中文补正对原协议快照16 pass / 0 fail，阶段结果不能当作全部TLSclose成功 |
| [core-repair/](core-repair/) | 规范化后token拒绝，JSON/typed header、中文错误证据 |
| [linked-artifacts.sha256](linked-artifacts.sha256) | 最终10个实际archive快照，测试后逐个sha256sum -c一致 |
| [source-review.sha256](source-review.sha256) | 最终审阅C source/API/cmake/setup、授权核心与测试源码；最终逐个核对一致 |
| [linked-artifacts-initial.sha256](linked-artifacts-initial.sha256) / [source-review-initial.sha256](source-review-initial.sha256) | 初始审阅阶段指纹，保留历史，不是最终批准指纹 |
| [test-executable.sha256](test-executable.sha256) | 实际最终测试EXE指纹 |
| [pe-imports.txt](pe-imports.txt) | 没有cpr/curl/c-ares/libssl/libcrypto额外直接DLL导入；QtWebSockets/Test是fixture/test依赖 |

最终协议 SHA-256：`b40f12b238ec137143c63bde04ed5de134d229a2d96454bc43891c5d4ba04af0`。

archive原始来源为 `build/workflow-protocol/native`（C的protocol/cpr/curl/c-ares）、`.deps/workflow/openssl-install/lib64`（最终SOURCE_DATE_EPOCH=1759190400）、`build/workflow-product`（既有真实UI/session/network/QtNodes）。本worker只读复制至 `build/workflow-protocol-review/artifacts`，编译授权核心/test并独立链接；未安装/编译依赖或写入共享产品build。

## 重现

协调者已把 `test_workflow_e2e.cpp` 纳入root suite。此轮实际使用的独立CMake及日志位于被忽略的 `build/workflow-protocol-review`。最终native运行使用Qt6.8.3、MinGW13.1.0、匹配SerialPort运行目录及 `QT_QPA_PLATFORM=windows`，可设置 `PORTBRIDGE_E2E_EVIDENCE` 到已存在目录以保存结构化JSON。

```text
build/workflow-protocol-review/native/test_workflow_e2e.exe -o docs/validation/workflow-implementation/e2e/windows-test-final.txt,txt
```

Qt仅用于服务器；已有OpenSSL CLI只用于生成临时localhostSAN自签名证书，程序实际客户端使用C的static OpenSSL。TLS负向分别验证HTTP/WSS不可信CA与错误hostname，正向验证HTTPS→WSS完整组合和Unicode caFile路径。此轮不是公网/物理设备、系统根CA正向公网链或所有DPI验收。

证据JSON中的RunState：0=Idle、1=Running、2=Paused、3=Completed、4=Failed、5=Stopped；NodeState：0=Pending、1=Running、2=Succeeded、3=Failed、4=Skipped。结果helper列出固定诊断ID n1..n8/recovery；不在该计划中的ID返回默认Pending，不应将其理解成图中实际存在的节点。

核心header规范化/中文映射/peer metadata消费由此worker按协调者授权编写，属于自审修复，C独立核心/UI复审和协调者root冻结回归继续。C协议互操作修复在本轮由此worker独立复核及25行真实回归验证。
