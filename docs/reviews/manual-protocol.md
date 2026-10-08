# HTTP / WebSocket 手动调试 · http-ws-1

本轮在原生调试工作台加入HTTP与WebSocket独立入口、请求方案库和结果查看。原始串口/TCP/UDP连接方案保持原schema，工作流继续使用相同的真实协议客户端。实现与本轮审查由当前代理完成，没有新增独立代理审查。

## 完成范围

| 项目 | 实际行为 |
|---|---|
| M01 请求编辑 | 七种HTTP方法、启用/禁用参数与请求头、Basic/Bearer、原文/JSON/urlencoded；真实流量验证中文、emoji、`+`、`&`、重复头拒绝及编码往返。HEAD带Body在发送前拒绝。 |
| M02 HTTP结果 | 保留401/302、真实状态/耗时/Body大小；Body/JSON/Headers/HEX、有界历史、取消与晚回调隔离、容量失败后恢复；不自动跟随重定向。 |
| M03 WS握手/发送 | ws/wss配置、认证/自定义头/子协议、UTF-8/HEX、空消息；可离线准备消息，连接不自动发送；握手编辑在活动期间锁定。TX表示本地写出。 |
| M04 WS接收 | 完整消息类型与边界、主动推送、主动/对端关闭码及UTF-8原因、暂停显示仍接收、超限历史明确提示。 |
| M05 方案 | 独立版本1文件和方案库、原子导出、默认凭据遮蔽、带标记需补填、非法导入不修改当前编辑、覆盖草稿需确认。 |
| M06 活动切换 | 页面浏览维持活动；开始不兼容通信明确确认，并重新核对epoch/配置/工作流身份；确认取消和退出取消保留原活动。 |
| M07 原生界面 | 深浅主题、200DIP方案库、空态、可调分隔条；Windows原生Qt100/125/150%与1024紧凑窗口，部署启动另检查1280/1440请求尺寸。 |

生产HTTP使用cpr/libcurl/c-ares/OpenSSL，WS使用Boost.Beast；测试服务使用真实本地QTcpServer/QWebSocketServer。TLS仍校验证书链与主机名，显式CA保持原生产客户端策略，既有协议套件继续覆盖TLS、证书及主机名错误。未加入自动重连、自动周期发送、Socket.IO、SSE、OAuth交互登录、multipart或多活动资源。

## 发现与修复

- 本地主动关闭完成回调先将会话设为空闲，导致CLOSE被忽略：保留关闭等待状态，实际1000与4001/中文原因验证。
- 页面销毁时客户端回调访问已释放Impl：停止计时、取消、断开页面回调后再释放；初始访问异常和GDB栈保留在 `initial/`，连接页面销毁测试验证对端释放。
- 初始未修改的工作流示例阻碍退出夹具：仅跳过未修改、未运行示例的提示；真实编辑和活动仍确认。
- 未使用的HTTP子协议控件浮在页面上：限制为WS可见，增加控件无遮挡检查；顶部模式栏裁切改为明确36DIP，并检查按钮完整包含于父控件。
- 参数编码保存/载入丢失区别：保留 `%2B`、字面 `+`、`%252F`；使用实际服务器收到的URI校验。
- 草稿导入、持久化、消息容量和UTF-8：在修改/发送前完整校验，QSettings写入失败回滚，不虚报保存成功。
- 截断附近凭据可能只显示前缀：先限制处理预算并检查分页周围数据；数字/布尔/null/数组/对象/转义键/长值/大Body均验证默认视图、实际复制和方案遮蔽，原始结果保留。
- 无截图环境夹具未调整尺寸却断言1024×768：将实际resize置于必经路径。
- Windows150%窗口管理器把请求1000高限制到990：原始composer夹具按实际可用屏幕和边框计算尺寸，仍严格检查全部控制包含关系、96DIP输入最小高度、扩展与拖动行为。未以请求尺寸冒充实际尺寸。
- 原生剪贴板曾出现无法读取及末尾U+0000：保留失败记录，使用实际复制、短等待和有限重试；最终仍严格逐字比较，不去掉字符或替换实际剪贴板。

## 验证证据

完整9套件运行位于 `docs/validation/manual-protocol/final-before-native-fixture-fix/ctest-all.driver.txt`，全部通过。之后仅修改两个测试夹具；最终重跑UI与protocol_debug，并以生产源码及七个测试可执行文件SHA256不变为依据保留其余结果。复用范围和哈希见 [reused-validation.json](../validation/manual-protocol/final/reused-validation.json)，最终源冻结72项，见 [result.json](../validation/manual-protocol/final/result.json)。

| 套件 | 通过项数（含Qt初始化/清理） |
|---|---:|
| network | 11 |
| session | 27 |
| ui | 40 |
| workflow | 14 |
| workflow_ui | 28 |
| workflow_protocol | 17 |
| workflow_integration | 10 |
| workflow_e2e | 25 |
| protocol_debug | 38 |

原生协议38项在Qt软件100/125/150%运行，原始UI40项在100%运行；原始UI150%尝试时桌面可用区域小于既有1100×760最小窗口加边框，环境尺寸断言失败，记录保留且不记为通过。工作流UI28项150%和原生集成10项来源于同一生产源码/不变可执行文件的前一轮。真实服务验证请求方法/查询/认证/Body、关闭和消息、取消隔离、历史预算、活动互斥、退出取消及凭据实际复制。界面见 [交互截图浏览](../validation/manual-protocol/gallery.html)。失败阶段 `initial/`、`after-lifecycle-fix/`、`after-exit-and-budget-fix/`、`final-before-offline-editor-and-fixtures/`、`final-before-native-fixture-fix/` 保留。

部署位于 `dist/PortBridge-http-ws-1/PortBridge.exe`；最终包为 `dist/PortBridge-0.1.0-windows-x64.zip`。包与全部解压文件、构建EXE、72项来源逐项核对SHA256，部署及解压启动清除QT环境变量并仅使用Windows系统PATH，UDP/TCP各500帧本地往返。最终机器回执见 [package-receipt.json](../validation/manual-protocol/package-receipt.json)。上一workflow-ui-2目录保留，ZIP按旧SHA256备份到 `build/package-backups/workflow-ui-2/`。

这组验证覆盖本机原生窗口和真实本地服务，不覆盖干净Windows系统、物理串口、物理多屏DPI、双机2.5G或持续高负载。历史session连接等待失败的根因、原始Qt Socket丢包原因仍没有新证据可作结论。部署工具的dxcompiler/dxil查找提示如出现会保留在deploy.txt，不能替代干净系统验证。
