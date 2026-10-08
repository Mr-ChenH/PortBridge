# 原生工作流整合、独立审核与验收

状态：实施、独立审核修复闭环和冻结整机回归已完成。最终 CTest **8/8**、Windows原生主窗口 **10/10**、原生工作流 UI **26/26** 在 Qt 软件100/125/150%均通过，冻结前后 **65个输入SHA256完全一致**。发布目录/压缩包的部署验收在下文单独记录。

## 实施范围

原生 Qt Widgets 第四“工作流”页使用 QtNodes 图模型、独立异步执行器、真实 cpr/libcurl HTTP 与 Boost.Beast WS；保留串口/TCP/UDP 既有工作台。16类节点、模板、保存导入、编辑历史、字段校验、条件/有限循环、可靠等待、暂停/停止、结构化结果/变量/日志全部连接真实后台。独立浏览器原型保持设计用途。启动、编辑和导入不执行业务。

一次一个流程、一个持久消息资源；借用/自建归属、显式 close、代际清理和活动连接替换计划在主窗口整合。通信发送与运行争用、后台方案浏览/非活动方案编辑、退出草稿取消/保存与活动清理均有真实主窗口测试。raw发送表示入队接受，WS发送表示本地写出；均不表示业务确认。

## 三个唯一子 agent 与独立性

Orca run `run_6ec448c3fc57` 始终复用3个唯一子terminal。A实现核心并审核/修复主窗口接入，随后独立审核C协议及真实组合流程；B实现原生UI并修复具体UI发现；C实现协议，随后独立审核A的核心/主窗口和B的UI。交叉审核所需最小自修明确记为自修，由另一作者复核。协调者处理根构建/部署、实际截图和最终冻结/包装。

| 对象 | 原作者 | 独立审核与报告 |
| --- | --- | --- |
| 原始观察/执行器/资源 | A | C，workflow-independent-review.md |
| 协议/模板组合 | C | A，workflow-protocol-independent.md |
| 原生UI/显示/导出 | B | C及协调者原生截图对照 |
| 主窗口接入 | 协调者，后交A修复 | A首次审核，C最终核心/主窗复审 |

## 已发现与修复/复审追踪

| 发现 | 触发/影响 | 修复与状态 |
| --- | --- | --- |
| QVariant临时提取优化诊断 | QtNodes/GCC产生array-bounds诊断 | 独立200000转换对照，const提取后零警告；6处哈希验证源补丁，未全局关闭诊断 |
| 本地资源/身份接入 | HTTP与raw占用、owned多方案切换、隐藏Ctrl+Enter、退出草稿 | A修复；native/offscreen真实主窗10/0，C复核 |
| 协议请求头合同 | 原型/模板逐行headers而后端最初只JSON | 支持明确格式，随后核心在变量替换前规范化 |
| Header变量CR/LF注入 | 返回token带换行可新增HTTP/WS请求头 | A实际HTTP/WS复现保留；规范化后实际无WS流量拒绝，C独立8/0合同复验 |
| 服务端主动WS关闭元数据丢失 | 4001+UTF-8原因在wait中变泛化断开 | 增加有界close元数据信号与核心结果保留，关闭先于state；实际错误出口保持推进 |
| QtWS关闭后的TCP/TLS teardown差异 | 已验证close帧后10053/10054误失败 | C限定仅已验证WS关闭时处理reset/aborted/truncated teardown；没有关闭帧的异常仍失败；QtWS6例加入原协议套件 |
| 协议错误漏中文翻译 | 真正cap/timeout/TLS失败向用户显示英文 | A补充消息映射，实际负例与C合同复验 |
| 浅色节点残留深色缓存/重阴影 | 切换浅主题仍灰卡片 | B失效每node缓存、明确不透明/flat白色，增加pixel断言 |
| 默认卡片字体/反向错误口重叠 | 85%字过小，第二行错误口盖详情 | B提高字号、左右gutter与baseline；C最终原生截图独立关闭 |
| 原生点击离屏fixture | 根首次workflow_ui键盘/点击测试13/1 | click/doubleclick/release完整事件修正；离屏及Windows复验通过，初次CTEST6/7失败保留 |
| 长凭据Base64显示/导出泄露 | 5000字符token被secret收集忽略，rawBase64可解码 | B有界结构/类型一致遮罩修复；C长token/数字/转义键/300k正文真实preview+实际导出10/0，最终UI26/0，独立关闭 |

## 当前实证及限制

A核心最初14/0，既有session27/0；A主窗口10/0同时在offscreen与Windows验证。C协议初次11/0且restricted-runtime11/0，新增QtWS互操作修复后17/0。A最终独立真实组合E2e **25/0**，包括默认WorkflowPage按钮、pause/results、HTTP→token→带Auth的WS→sendWait→assert→close、typedJSON、401any、binary、8MiB/cap负例、停止与迟回隔离、4001UTF8metadata/error出口、TLS信任/错误主机名、Unicode CA目录、异常无close控制。C补充真实TCP服务端多客户端来源及delimiter/fixed/lengthHeader三种分帧、UDP静默/代际归属/观察溢出，初次8/1仅长凭据遮罩失败。

模块和独立probe通过不替代协调者最后8-suite整机回归；以前模块使用的link seam被明确排除HTTP/WS证据，组合验收使用真实静态协议档案且记录前后哈希。真TLS证书由本地隔离CA生成，不修改系统根信任。部署应静态带入cpr/curl/c-ares/OpenSSL及许可，不新增这些协议DLL。

Windows原生与Qt软件缩放分别记录。实际DPR1/1.25/1.5；150%软件缩放下窗口管理器把请求高度768/1000夹到707逻辑像素，报告实际尺寸而不声称完整目标窗口能放进物理屏幕。浏览器原型63项是历史设计证据，不算原生验收。物理Windows显示设置变更、多显示器DPI、物理串口、两机2.5G、持续磁盘/长时间OS阻塞和原QtSocket丢包原因未验证。

## 最终冻结回归

当前固定完整构建在 `build/workflow-product`；`final/result.json` 全步骤exit0且source_stable=true。现有network11case、session27、UI40，新增workflow14、workflowUI26、protocol17、mainwindow10、e2e25。QtTest totals含各自init/cleanup，不能相加冒充独立需求数量。最终8个CTest套件全部通过，122.282秒；Windows主窗10/0、workflowUI26/0在100/125/150软件缩放分别15.087/14.794/14.958秒。编译没有产品或QtNodeswarning/error。

源变更守卫曾在第一次完整8/8及native全部通过后检测到最后P1修补重叠，整轮保留 `final-before-source-freeze/`，未当作有效冻结验收。明确冻结后的第一次回归session26/1在既有 `finalizedSequenceDenominatorAndSaturation()` 的UDP重启绑定等待未就绪；保留 `final-before-session-replay/`。不改源码的定向重放3/0、182ms，随后完整session27/0及全部8/8通过；没有宣称找到或修复该瞬态等待的根因。最终守卫前后65输入完全一致，记录 `final/{pre-source-hashes,source-hashes,source-manifest}.json`。

对照图 [comparison.html](../validation/workflow-implementation/comparison.html) 包含原型、修复前、最终1440/1280/1024原生页面与独立缩放标签状态。最终四组产品窗口真实启动全部exit0且stderr为空，未启动网络/运行任务。

## 部署及交付

独立审核A25/0与C最终批准均已结算，开放阻塞问题0。协议静态链接及许可证准备完成。部署 `dist/PortBridge-workflow-1`，保留旧ZIP/运行程序；最终发布校验包括restricted PATH四组主题/页面启动、UDP/TCP各500frame、压缩包解压全文件SHA256与实际EXE一致。最终部署验收已通过：Windows系统PATH-only且移除全部QT*环境，深浅主题/workspace/workflow四组直接启动及解压workflow启动全部exit0；UDP部署与TCP解压各完成并收到500frame，checksum/missing为0。压缩包testzip与解压全部文件哈希一致，EXE与冻结构建相同；QtNodes/协议静态依赖许可和无本机路径的依赖manifest已包含。receipt见 `docs/validation/workflow-{package,zip}.json`，`dist/PortBridge-workflow-1/PortBridge.exe`可直接运行；旧ZIP保存于 `build/package-backups/profile-picker-1/`，旧版本exe保留。最终归档的SHA256/字节数写在zip receipt，不递归把自身归档哈希放进包内。windeployqt提示缺dxcompiler/dxil，但实际Widgets/QtNodes原生启动通过；未声称干净Windows认证。
