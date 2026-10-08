# HTTP / WebSocket 手动调试设计与实施

## 目标与入口

在原生“调试工作台”内提供“通信调试 / HTTP / WebSocket”三个任务入口。通信调试继续由原有连接方案选择串口、TCP客户端/服务端、UDP；HTTP请求与WS握手拥有独立、版本化请求方案，不把HTTP或WS冒充原始TCP，也不修改旧连接方案schema。页面切换、载入、编辑、保存、启动都不自动通信。

## 调研证据

- Postman官方WebSocket概述：https://learning.postman.com/docs/sending-requests/websocket/websocket-overview/ 。实际下载200；明确持久双向连接，并将建立请求、消息处理、保存配置分成独立功能。
- Hoppscotch官方实时API调试：https://docs.hoppscotch.io/documentation/features/realtime-api-testing 。实际下载200；持续消息/事件日志与HTTP请求响应的交互不同。
- HTTPie实际客户端源码：https://github.com/httpie/cli/blob/master/httpie/client.py 。实际下载200；准备请求与发送配置分离，方法/URL/请求头/请求体/认证组合后发送，响应与重定向显式处理，离线准备不发请求。
- websocat实际源码：https://github.com/vi/websocat/blob/master/src/main.rs 。实际下载200；区分文本/二进制消息，支持自定义握手头、子协议及连接生命周期。PortBridge保留证书与主机名校验，不照搬其不安全TLS选项。

下载与失败回执在 `.pi/manual-protocol-research/`。Brave检索失败，GitHub目录API及若干猜测源码路径失败，未宣称审阅了Insomnia或Hoppscotch的实现。已读取HTTPie客户端与websocat的选项/入口源码；不把其他项目的技术栈直接移入Qt产品。

## 功能与交互

- M01 HTTP：GET/POST/PUT/PATCH/DELETE/HEAD/OPTIONS，完整URL，可启用/禁用的query/header键值行；None/Bearer/Basic认证；无Body、原文、JSON、urlencoded。请求先本地验证，然后明确发送/取消。
- M02 HTTP响应：真实状态码、耗时、正文大小、Body/JSON/Headers/HEX、有界请求历史。4xx/5xx是收到的HTTP响应；网络/TLS/超时单独显示。重定向维持现有客户端不自动跟随，保留3xx和Location。
- M03 WS：ws/wss完整URL、握手头、子协议、认证、超时/消息限额/自定义CA；明确连接、取消、断开。已连接时握手配置锁定；消息编辑器支持UTF-8文本与HEX二进制。发送完成仅表示本地写出，接收保留完整消息边界与文本/二进制类型。
- M04 WS时间线：连接、TX、RX、关闭码/原因、错误；选择查看完整保留字节的分页视图。暂停显示不停止接收；清空不发送、不关闭。丢弃的显示历史单独提示，不能当网络丢失。
- M05 请求方案：独立HTTP/WS库，显式保存/载入/删除；JSON版本与大小限制。保存/默认导出遮罩凭据，载入带遮罩标记的配置要求补填后才能发送；不持久化响应或自动重连。
- M06 单活动资源：浏览任何工作台/工作流保持活动；开始另一类通信前明确处理旧活动，取消确认保留旧任务。HTTP与WS、原始会话、工作流之间互斥；确认后核对活动epoch再停止旧活动。退出存在协议活动时确认取消/关闭。
- M07 UI：遵循现有深浅主题、轻量工具条、200DIP方案库、可调请求/响应分隔条、明确空态；在1024/1280/1440及Qt软件缩放下检查实际窗口，不仅检查功能断言。

## 技术与边界

手动协议控制器复用真实 `WorkflowProtocolClient`（cpr/libcurl/c-ares/OpenSSL与Boost.Beast），独立于图执行器。控制器负责操作ID、epoch、状态、字节有界历史；widget负责编辑/预览。生产协议客户端仍不是Qt HTTP/WS客户端；Qt Network/WebSockets仅用于真实本地服务器测试。

网络请求/消息最多8MiB，默认1MiB；操作1–60000ms；表单Body/payload1Mi字符、query/header各64行；方案最多64个/文件8MiB；WS显示历史最多500条/4MiB，HTTP结果历史按内存预算保留；预览分页/32K字符，凭据处理先限制预算。完整原始结果可用于验证，默认UI/复制/导出遮罩。TLS始终校验，不增加忽略证书、自动重连、自动周期发送、Socket.IO、SSE、OAuth登录、multipart文件上传或多活动会话。

## 实施与验收

1. 提取已验证的共享遮罩/预览工具，不改变原工作流政策；实现独立控制器与请求方案持久化。
2. 实现协议专属工作台、集成主窗口模式/资源确认/快捷键/退出。
3. 实际HTTP服务器验证请求语义、401/302/取消/晚回调、限额、凭据；实际WS服务器验证文本/二进制/空消息/主动推送/关闭/错误/有界历史。原始会话切换和工作流互斥用主窗口实测。
4. 构建、相关回归、原生截图/软件缩放、独立输出目录部署、解压启动及源/包哈希核对；保留旧workflow-ui-2交付。
