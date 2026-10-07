# PortBridge Agent 协作计划

## 协调方式

明确使用用户指定的 /orchestration：Orca 原生 Run/Task/Dispatch。CLI 固定为 `orca`。Run：`run_27bdb63ff03a`，coordinator：`term_86c2d11e-b45c-4fef-b578-5a3ec5e5e569`。

最多创建三个子 agent，并在完成/审核阶段复用这三个 terminal。禁止子 agent 再启动其他 worker。当前项目是有效 folder workspace，不需要为协调强制初始化 Git。

## 所有权

| Worker | 第一波范围 | 可编辑文件 | 不得编辑 |
| --- | --- | --- | --- |
| network | Asio 核心、回环测试、压力工具 | src/network/**、tests/test_network.cpp、tools/network_bench.cpp、其报告 | 共享头、session、UI、CMake |
| session | Qt 会话、串口、编解码、配置、采集 | src/session/**、tests/test_session.cpp、其报告 | 共享头、network、UI、CMake |
| ui | Qt Widgets 主窗口/模型/自绘图/页面 | src/ui/**、tests/test_ui.cpp、其报告 | 共享头、session、network、CMake |

协调者维护共享头、构建、main、部署、技术设计、验收与集成；审核发现的问题回派相应 owner 修复。所有 worker 以同一文件夹为上下文，按目录独占减少冲突，接口由统一头文件约束。

## 最终执行与清理

实际只创建3个Pi子agent。network原terminal复用审核session/采集，并授权修复Windows UDP absent-peer；session原terminal复用做独立UI及network/benchmark审查；ui原terminal复用修复4个UI发现，协调者修复并提供旧池weak-owner永久回归。原计划的交叉审核角色据实际发现调整，但独立审查与最多3个terminal约束保持。实际6个Task/Dispatch全部succeeded，最后3个terminal均worker-release并归档，之后才ack对应worker_done。权威最终worker-list在.pi/workers-final.json；释放回执为.pi/data-review-release.json、.pi/ui-fix-release.json、.pi/ui-network-review-release.json（该证据属于工作区，不是运行时依赖）。

## 阶段与清理

第一波全部启动后再等待。处理问题消息时回复明确接口或范围；每个 worker_done 验证 Task/Dispatch 和实际文件/证据。结算后立即复用、显式 retain 或 release，之后才 ack。计划立即复用各 worker 做交叉审核/修复，最后 release，关闭所有 reclaimable terminal。

独立审核：session owner 审核 network；network owner 审核 session/采集；UI owner 在自身 UI 测试之外参与整体验收，协调者核查原生截图。有效问题全部回派修复，复验失败继续同一 owner，不创建额外 agent。

不以心跳、缺少消息或超时当作失败；只按 Orca 正向退出/结算证据处理生命周期。最终验收区分可运行软件、自动验证与实物/2.5G 未实测事项。
