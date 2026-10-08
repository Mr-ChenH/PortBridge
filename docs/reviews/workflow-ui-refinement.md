# 工作流 UI 与交互设计再审核、优化

版本 `workflow-ui-2`。审查、整改及最终冻结验证已完成。此次重新对照 `12-workflow-ui-design.md`、可交互 HTML 原型以及整个原生应用，重点审核编辑、切换字段、验证定位、结果查看和日志折叠。此前 `workflow-1` 的功能回归通过，但对视觉层级和参数使用流程的符合程度判断过宽；本次把具体差距落实到原生页面。

| 编号 | 原生差距及使用影响 | 已实现的优化 |
| --- | --- | --- |
| U01 | 草稿状态离标题过远，工具条操作同样重边框，图编辑与文件操作层级不清 | 草稿徽标紧邻标题；轻量图编辑图标、分隔线、tooltip与辅助名称；模板、保存、运行保持主动作层级 |
| U02 | 节点库只有边框树列表，缺少分类色、图标、分组数量和拖动提示；搜索无结果时空白 | 原生 delegate 显示分类图标、数量、拖动点、选择/焦点反馈；增加搜索空态，保留搜索、键盘和拖入行为 |
| U03 | 画布节点缺少协议标识/步骤号；长路径占据摘要；比较摘要总显示等号 | 增加协议标识、分类图标背景、步骤号、右下运行状态；显示摘要压缩 URL scheme 与字段路径，按实际比较操作显示符号；缺省参数用于显示，持久参数不改写 |
| U04 | 参数栏缺少固定详情标题/关闭；HTTP 请求头把 Body 推到屏幕下方；TLS/超时/容量全部平铺 | 固定详情标题、关闭、节点图标/身份和完整 ID tooltip；Body 默认展示，请求头与高级选项独立折叠；验证错误自动展开并定位。软件150%整套用例测得实际990DIP窗口中Body底部裁切9DIP，HTTP Body高度上限改为128DIP，保留多行与内部滚动 |
| U05 | WS 发送表单仍展示 TCP/UDP 目标与分帧；所有匹配字段始终出现 | 按 raw/WS 资源和匹配方式展示对应字段；切换后异步重建表单，保留已存参数；raw 来源/分帧放到可折叠分组 |
| U06 | 上次结果只显示整段 JSON，HTTP 401 状态、响应和错误难以分别查看 | 状态/耗时/HTTP码与错误详情；Body、Headers、完整结果分开查看；失败颜色适配深浅主题；消息提供文本/有限HEX预览；新视图先限制预览预算再遮罩，避免选择大响应时全量处理；HEX只检查最多16,383B的有界前缀再展示最多4095B |
| U07 | 日志控制与页签占两行，空表没有指引 | 日志/变量页签与运行状态、暂停、停止同一行；空表显示操作指引；折叠后保留控制；紧凑窗口控制日志高度 |
| U08 | 关闭父窗口后，把子控件不可见误存为用户主动折叠日志，下次启动可能折叠并留空白 | 保存显式隐藏状态而非父窗口可见性；恢复/调整窗口时重新分配折叠尺寸；专项验证父窗口隐藏、再次打开和显式折叠恢复 |
| U09 | 切换到没有描述的流程时继续显示上一模板描述 | 每次更新流程身份时同步说明；没有描述时使用通用操作说明；用户标题/说明/结果详情按纯文本显示 |

实现集中于 `src/ui/workflow_page.cpp`、`workflow_canvas.hpp` 与新的 `workflow_presentation.hpp`。图编排、真实协议、runner、资源借用/占用、编辑锁和原始结果语义保留。HTTP/WS 新视图不替代后端原始数据；默认导出继续遮罩，二进制文本预览最多4095B。原型的模拟状态选择器不进入正式产品。

## 验证与证据

最终冻结结果见 [final/result.json](../validation/workflow-ui-refinement/final/result.json)。66项输入在最后一轮前后保持一致，编译无warning/error。最终相关UI套件4/4通过（UI40、workflowUI28、integration10、e2e25通过行；QtTest含初始化/清理）；最后预览调整涉及UI实现、UI测试和仅测试用QtWebSockets链接。其余network/session/workflow/workflow_protocol四套沿用上一轮冻结结果，确认后端源码与四个测试可执行文件哈希不变；复用依据在 `final/reused-validation.json`，不是将旧UI结果冒充本轮结果。

Windows原生主窗口集成10/0；工作流UI在受控Qt软件100/125/150%各28/0（30.630/26.657/26.692秒）。100%有一条Qt剪贴板重试warning，断言最终通过，未宣称全程warning-free。三组 `native-screen.json` 记录实际DPR/尺寸；整应用四张最新截图均退出0、stderr为空，尺寸回执见 `native-startup.json`。

- 原生专项初轮15/0后，最终将九组凭据的检查扩展到Body/Headers/完整结果显示与真实默认复制，并加入Set-Cookie敏感头；实际文件导出与后端原值不变检查保留。完整工作流UI28/0包含这些用例（含初始化/清理）。
- 真实 localhost HTTP 返回401，Body包含合成token和错误正文；状态、失败详情、Body/Headers/完整结果可同时检查，token不出现在默认新视图中。响应截图来自真实工作流页测试窗口，非模拟结果；HTTP测试fixture的三节点位置分开，便于观察实际执行状态。
- 真实Qt WebSocket服务器接收并回传文本、18,020B二进制消息；二进制含0x00/0x01/0x02与预览外尾部标记。断言后端完整往返、HEX有限且没有尾部、预览不改写原结果。Qt仅作真实本地服务器；生产客户端仍为Beast。原生软件150%整套工作流用例28通过、0失败。多比例几何使用实际窗口尺寸，不把请求尺寸当实际尺寸，也不将软件缩放称为物理Windows DPI认证。
- 交互/视觉对照：[comparison.html](../validation/workflow-ui-refinement/comparison.html)。深浅主题、1440/1280/1024编辑器、真实HTTP Body/Headers结果分别提供；历史原型与workflow-1原生截图保留。编辑器图来自完整应用，响应图来自工作流页测试窗口，两者明确区分。

## 保留的失败与修复依据

1. 驱动首轮使用不存在的测试函数名，修正驱动后通过；`driver-before-function-name-fix.txt`保留。
2. 原生截图暴露日志隐藏状态保存错误，修复前截图保存在 `before-log-preference-fix/`，另保留紧凑间距修复前截图。
3. 完整回归首轮7/8套件通过，旧UI采集导出关闭测试与随后文件对话框测试失败；`final-before-capture-fixture-fix/`保留。未经产品修改的单独重放仍失败。测试创建第二窗口时没有设置实际采集目录，刷新扫描空目录，选中导出未启动，预定对话框回调残留并影响下一测试。仅修复测试fixture：第二窗口指定同一实际目录，并确认列表有实际文件。取消/完成/关闭不覆盖原文件与随后的方案操作复验4通过、0失败，记录 `old-ui-fixed-replay.txt`。没有修改生产采集导出逻辑，也没有降低断言或改成假导出。
4. 后续完整回归8/8、原生100/125%各28/0，但150%参数层级用例失败，记录 `final-before-150-body-check/`。单项复验3/0；加入几何诊断后完整150%再次出现实际`window=1440x990, viewport=280x438, body=(14,267,252,180), log=200`，确认Body底部超出9DIP。`native-150-geometry-replay.txt`保留。降低HTTP Body最大高度后完整150%28/0，记录 `body-150-fixed-replay.txt`；随后重新冻结源码并进行完整验证。

5. Body高度修复后的冻结整轮中再次出现历史 `SessionTest::finalizedSequenceDenominatorAndSaturation()` UDP连接等待失败。`final-before-session-replay/`保留；未经源码修改的单项3/0、会话整套27/0，以及其余七套当轮通过。本次未定位或修复其根因，不能将通过的重放称为UDP问题修复；这一后端版本随后用于有哈希核对的后端结果复用。
6. 新视图预算复查后，Body/消息改为先按既有32K字符/层数/条目预算截断再遮罩，HEX只检查有界前缀。扩展测试首轮误把既有32K预算当8K，且原小消息WS服务器fixture只支持125B以内帧；失败保存在 `final-before-preview-fixture-fix/`。纠正预算断言，改用真实QtWS服务器并仅给UI测试链接QtWebSockets；生产协议未替换。
7. 原生快速连续复制触发Windows OLE剪贴板占用，`final-before-native-clipboard-wait/`保留（offscreen相关4套通过，但原生9行复制失败）。测试让系统事件完成，并在占用时最多重新按Ctrl+C三次，仍校验实际系统剪贴板与显示完全一致；未使用假剪贴板或绕过遮罩断言。最后三比例各28/0；100%中保留的一条Qt重试warning如实记录。
8. 预览验证驱动初次误期待network的QtTest文本报告；network是通过CTest报告的自定义测试，修正后引用原CTest证据。`preview-driver-before-network-report-fix.txt`保留，属于驱动错误。

## 交付

新版运行目录 `dist/PortBridge-workflow-ui-2/PortBridge.exe`，便携包 `dist/PortBridge-0.1.0-windows-x64.zip`。`workflow-1`运行目录保留；其旧ZIP备份到 `build/package-backups/workflow-1/`并核对此前SHA256。部署、解压后启动、真实500帧UDP/TCP及ZIP/全文件哈希回执分别写入 `docs/validation/workflow-ui-refinement-package*.json` 和 `workflow-ui-refinement-zip*.json`，归档SHA256以外部`.zip.sha256`及交付回执为准，避免自包含哈希。

本轮是当前执行代理的审查、实现和实测，未冒称新增了独立代理审核。前轮独立协议/核心审核仍作为历史证据；本轮通过实际源冻结与当前回归核实兼容性。390像素浏览器原型仅为设计参考，正式Qt产品有自己的桌面最小窗口。未新增物理串口、两机2.5G、全新Windows或多显示器物理DPI验证。
