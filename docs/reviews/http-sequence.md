# HTTP 响应断言与顺序联调实施审核

目标修订 `http-sequence-1`，延续已交付HTTP项目化一期。用户明确选择“请求断言与项目顺序联调”。范围、资源身份和失败语义见 [第二期计划](../project-factory/18-http-sequence-plan.md)，使用见 [指南](../http-sequence-guide.md)。本期由当前代理实现与自审，未新增独立代理审核。

后续完成度复核发现两处已复现的验收遗留：顶层JSON数组路径与断言展开后的发送前预算校验，原复现见 [完成度审核](http-sequence-completion-audit.md)。用户授权修复后，`http-sequence-fix-1` 已完成修复与回归，两项已关闭；最新状态见 [审核修复](http-sequence-fix.md)。以下实施与回归记录保留其原始版本和证据。

## 变更

- `http_assertions`验证/解析/计算最多32行状态码、响应头、JSON断言，equals严格类型，exists与字符串contains。结果只含序号、是否通过与一般原因，不含实际/预期值。
- `http_assertions_editor`原生配置表、启用/来源/路径/关系/JSON预期值；单请求保存/导入与项目存储校验新增可选assertions，旧文件兼容。敏感路径与运行secret的实际预期值遮蔽，引用保留。
- `http_request_resolver`提供无UI副作用的解析，手动HTTP发送与顺序请求共同使用；URL/query/form编码、JSON转义/类型、重复Authorization、请求边界与遮蔽标记检查保留。
- `http_sequence_runner`在同一个HTTP会话上执行冻结模板，最多128步/8MiB/10分钟。当前步响应提取之后再解析下一步；项目/环境/revision/epoch和运行ID隔离，外部修改终止。失败默认停止或显式继续，停止取消当前操作并标记剩余跳过。
- HTTP会话在步骤间保留sequence身份，原始通信、WS、工作流和退出/Esc继续识别活动；保留原资源确认并在结束旧活动前核对上下文。嵌套确认期间等待标记，避免排队信号提前计算完成。
- 勾选/排序对话框、停止按钮、断言页签、联调结果和省略值JSON报告。计划/结果不自动持久化，编辑/保存/导入/选择不通信。

## 验证记录

最终输入清单、应用及测试程序哈希见 [最终回执](../validation/http-sequence/final/result.json)。新的本地真实TCP服务用例覆盖严格断言类型、手动保存无通信、登录→提取→Bearer查询、失败停止/继续、取消晚响应、外部环境变更、确认期间旧UDP保留、接受资源确认、步骤间资源占有与停止。报告核对无实际token。

检查点完整回归中HTTP项目28通过/2截图跳过；原协议剪贴板9个数据行失败，原因是新增联调页签含外层容器，既有复制预览未寻找内部编辑器。修复入口并保留逐页实际剪贴板完全相等检查，最终协议43通过。最终HTTP项目普通运行30/0，两个截图槽跳过；windows平台100/125/150%原生专项每档32/0且没有跳过，创建入口每档7/0。冻结87个输入。

取消边界修复前完整运行有9/10套件通过，session出现采集等待超时（报告15000ms等待不够，本次18900ms可完成）及历史connected-wait失败。原始失败保留，同源码和程序哈希不变的两个用例集中复跑4/0、完整session27/0；这些属于修复前检查点证据。最终修复后重新冻结87输入并完整运行10/10通过，不声称偶发失败原因已消除。

补齐步骤间的排队取消边界：第一步完成后先排队Esc取消，再处理已排队的下一步；下一步发起前复核sequence身份，过期sequence参数被HTTP会话拒绝。真实服务核对只收到第一条请求，剩余步骤跳过。修复前部署、ZIP与全部验证保留于独立before-between-step-cancel-fix检查点。

失败编译/测试记录保留于 `before-assertion-editor-include-fix/`、`before-fixture-wire-receipt-fix/`、`checkpoint/` 和 `before-between-step-cancel-fix/session-before-replay.txt`。一次测试驱动在失败构建完成前运行了上一期测试二进制，该记录在原目录明确标注无效，不计入第二期通过。

## 界面和交付

实际原生图集见 [界面浏览](../validation/http-sequence/gallery.html)。断言、选择/排序、实际联调结果、项目浅色与提取配置提供三档软件缩放记录。结果截图由明确运行后的本地合成HTTP服务生成；选择对话框取消核对服务器0请求。

独立目标 `dist/PortBridge-http-sequence-1/PortBridge.exe`，上一 `http-project-1`保留。系统PATH启动、解压哈希、最终TCP/UDP回环与ZIP以 [包回执](../validation/http-sequence/package-receipt.json) 为准。包内应用回执省略最终ZIP自身哈希避免自引用；发布ZIP旁sha256文件与工作区完整回执提供最终校验。

未增加自动刷新、OAuth、脚本、循环或并发。只验证本机软件缩放与本地合成服务，不代表物理多屏DPI、生产账号、串口/2.5G硬件、长期压力或干净Windows认证。
