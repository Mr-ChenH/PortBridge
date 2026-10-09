# RA-UI01 界面响应修复与验收

**RA-UI01/P2在本机声明负载范围内已关闭。** `ui-latency-fix-1` 修复GUI等待后台线程退出的问题，并减少重复布局/详情生成；UDP/TCP各5分钟实盘正常心跳最大152/43ms，均低于原200ms门槛。所有原始采集逐字节、DONE尾部和元数据校验通过，应用丢弃和记录失败0。原UDP434ms/TCP2364ms及其他超标记录完整保留，不提高门槛或用短跑替代长跑。

## 已证实的等待路径

旧实现每次空间查询单独创建线程。工作线程发布 `finished` 不代表原生线程已经退出，GUI的 `pollStorage()` 随后调用 `join()`，此处没有200ms预算。采集导出、样本导出和删除轮询也有同类模式。

独立子进程只拦截QtCore磁盘空间查询以识别工作线程，并让该线程的 `_endthreadex` 延迟1200ms。同一MainWindow接口、Qt/MinGW和显示设置下，旧实现心跳 **1163ms**；新实现复用常驻线程，任务轮询只读取已发布结果，心跳 **45ms**，窗口销毁 **30ms**。对照见 [修复前](../validation/ui-latency-fix/final/before-storage-exit.json)、[修复后](../validation/ui-latency-fix/final/after-storage-exit.json)。没有修改DLL文件、系统设置或部署中的旧进程。

该注入证明一个不受预算约束的生产等待及修复效果。历史自然停顿的全部OS外因无法逐一追溯，不宣称每次旧峰值都来自同一系统事件。旧对照使用修复前生产源码的仪表化副本，未冒称直接注入已交付EXE。副本TCP5分钟125ms、UDP1821ms的原始观测也在 `validation/ui-latency-fix/before/` 保留，不能用其中一个通过覆盖其他失败。

## 生产改动

- 四类文件后台任务分别使用按需启动的持久工作线程，每类至多一个执行中任务和一个待执行任务；任务只捕获独立状态、路径和不可变样本。GUI不持有需要join的原生线程。关闭时取消排队任务，每类等待任务循环最多1秒；超时由独立状态保持任务安全存活。
- 数据样本与任务完成轮询保持50ms。指标、诊断和页脚自动布局每250ms更新，显式操作和连接/记录状态变化立即刷新；统计来源与原始接收/记录没有改变。
- 诊断完整文本一次设置，避免先布局基本文本、再布局完整文本。相同路径的空间查询保留最近结果；路径变化立即显示查询状态，并拒绝迟到的旧路径结果。
- 空字节详情不随每次样本淘汰重复清空。选中记录使用持续索引、页码和主题识别已经显示的不可变内容；前面的记录淘汰不会反复生成同一HEX/UTF-8详情。选择、取消选择、过滤、清空、分页和主题变化继续更新。

没有修改收发核心、记录格式/预算、HTTP/WS库、请求断言或顺序联调语义。行为回归覆盖原始字节、复制、主题、分页、活动方案隔离、导出取消/原子保存与关闭。

## 实际持续负载

正常 `QApplication::exec()`、独立发送线程、生产MainWindow、真实二进制采集；记录队列16MiB、32MiB轮转，每7秒实际点击暂停/恢复显示，后台原子保存进度。UDP1472B/TCP4096B，各1000测试帧/秒、300秒，固定屏幕、顺序执行。

| 指标 | UDP | TCP |
|---|---:|---:|
| 原审核正常心跳最大值 | 434ms，失败 | 2364ms，失败 |
| 修复后正常心跳最大值，门槛<200ms | **152ms，通过** | **43ms，通过** |
| 最长GUI事件 | 24ms | 23ms |
| 实际完成且校验的测试帧 | 299989 | 299985 |
| 接收/记录/校验有效负载 | 441583808B | 1228738560B |
| 完整且独立校验的文件 | 14 | 38 |
| 应用丢弃 / 记录失败 / 测试帧缺失 / 重复 | 0 / 0 / 0 / 0 | 0 / 0 / 0 / 0 |
| 暂停/恢复点击最大同步耗时 | 5ms | 4ms |
| 记录队列高水位，预算16MiB | 91160B | 296760B |
| 显示队列峰值，预算2MiB | 91160B | 627212B |
| 工作集RSS峰值 | 158072832B | 155287552B |
| 停止记录调用 / 最终收尾 | 0 / 31ms | 0 / 15ms |

[持续负载回执](../validation/ui-latency-fix/ui-latency-soak-final/receipt.json)、[UDP逐字节核对](../validation/ui-latency-fix/ui-latency-soak-final/udp-capture-verification.json)、[TCP逐字节核对](../validation/ui-latency-fix/ui-latency-soak-final/tcp-capture-verification.json)。TCP245216个实际读取记录重组为299985个测试帧，未把读取块当报文。实际完成数没有替换成requested300000。所有payload字节、UDP边界、TCP跨块/跨文件重组、DONE尾部与元数据均检查。

## 跨屏与主动GUI停顿

本机三块实际显示器、现有100%/125%缩放，Qt缩放环境变量清空，每10秒移动自己的窗口。UDP/TCP各30秒，三个屏幕窗口DPR同步、可见/exposed检查全部通过，正常心跳49/34ms；整个套件均通过。[实际跨屏回执](../validation/ui-latency-fix/ui-latency-dpi-final/receipt.json)。不包含物理150%和逐控件像素审查。

另各30秒故意阻塞GUI500ms，实际515/512ms；UDP接收与落盘都继续758080B，TCP都继续2101248B，结束后全部采集校验通过。注入停顿单列，正常心跳26/31ms，两个套件均通过。[停顿回执](../validation/ui-latency-fix/ui-latency-overload-final/receipt.json)。没有把故意的500ms隐藏为正常响应通过。

新增可复用 `ui-exit` 入口实际编译/执行通过，心跳52ms、窗口销毁38ms、查询4次，运行期间查询线程不退出，通信保持静默。原始子进程结果为measured，驱动按明确门槛产生passed。[入口回执](../validation/ui-latency-fix/ui-latency-ui-exit-final/receipt.json)。

## 回归、冻结与交付

最终源码冻结后，完整CTest **10/10、167.724秒**；普通UI42/42、HTTP项目35通过/2受控截图跳过，其他套件零失败。原生Windows UI **42/42、86.107秒**，QtTest计数包含初始化/清理。队列边界、发布后阻塞、安全关闭和详情文档信号检查实际通过。[完整回执](../validation/ui-latency-fix/final/result.json)、[原生UI](../validation/ui-latency-fix/final/native-ui-results.txt)、[针对性回归](../validation/ui-latency-fix/final/focused-ui-results.txt)。初期检查点保留在 `validation/ui-latency-fix/check/`，扩展四类后台任务后重新完整构建和回归。

88项软件输入冻结，实际执行的探针/驱动版本与后续新增入口分别保存。总机器回执在 [receipt.json](../validation/ui-latency-fix/receipt.json)，新部署目录为 `dist/PortBridge-ui-latency-fix-1/`；独立包回执在 [package-receipt.json](../validation/ui-latency-fix/package-receipt.json)。旧 `http-sequence-fix-1` 部署、ZIP备份及全部失败记录保留；本次没有提交/推送Git。

本次是当前代理实际修复和本机验证，不是新独立代理审核。物理串口、两机2.5G、干净Windows、正式磁盘饱和和原Qt Socket调查继续受外部条件限制；五分钟通过不证明无限时长或任意系统压力下的响应保证。计划与入口见 [实施计划](../project-factory/20-ui-latency-fix-plan.md)、[验收指南](../remaining-acceptance-guide.md)。
