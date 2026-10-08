# 统一新建连接与对话框优化 · connection-ui-1

原“新建连接方案”仍只有串口/TCP客户端/TCP服务端/UDP，造成HTTP/WS工作台与用户创建入口脱节。本轮将新建入口统一为六类，创建HTTP/WS请求后保存到对应独立方案库并打开工作台，创建不建立连接、不发送业务数据。串口/TCP/UDP继续保存到原连接方案文件；不改变原TransportKind或文件schema。

对话框改为双列类型卡片、图标、用途说明与选中状态，统一名称、协议地址和底部动作区。选择HTTP/WS时显示地址；字段校验就地反馈，修改后清除旧错误。使用深浅主题的面板、边框和强调色，明确“创建并打开”与不自动通信的提示。原始工作台侧栏的加号，以及工作台上方常驻“新建连接”使用同一入口；已有原始方案编辑保持四类原始通信。

HTTP/WS创建先检查名称、地址和现有活动，再确认覆盖未保存编辑。拒绝覆盖或目标协议活动尚在时保留原状；不借创建动作替换活动连接。原始方案创建仍保留此前未保存的有效连接编辑。重选同一卡片维持唯一选中状态。

双列卡片由独立尺寸样式保持82DIP高度，避免旧全局按钮样式压矮图标/说明；实际高度和完整包含关系纳入断言。压矮前的截图与已通过功能结果保留在 `before-card-height-fix/`。

本轮复跑ui、workflow_ui、workflow_integration、workflow_e2e、protocol_debug；未修改的四个后端套件依据生产源码和测试可执行文件SHA256复用。统一创建在真实本地HTTP/WS服务旁验证，但创建期间请求数/握手数保持0；确认对应方案库新增、原始库未改变、目标页面打开，实际发送由用户随后操作。原生深浅主题和Qt软件100/125/150%截图保存在 `docs/validation/connection-creation/final/native-*/`，包括非法地址与重选卡片、取消覆盖和活动保护。

最终结果及73项冻结输入见 [验证记录](../validation/connection-creation/final/result.json)，实际对话框见 [界面浏览](../validation/connection-creation/gallery.html)。初始编译中QWidget::font名称遮蔽导致错误，已明确调用design::font；原失败保留在 `before-font-qualification-fix/`。

程序位于 `dist/PortBridge-connection-ui-1/PortBridge.exe`；标准便携ZIP更新，上一http-ws-1目录与ZIP备份保留。部署及解压采用仅Windows系统PATH启动，文件/来源SHA256逐项核对，本地TCP/UDP各500帧。最终包回执在工作区 `docs/validation/connection-creation/package-receipt.json`，该回执在ZIP生成后写入；包内已有release-manifest.json与部署启动证据。

本轮是当前代理的实现与验证，没有新增独立代理审核。软件缩放和本地服务结果不代替物理多屏DPI、干净Windows、物理串口或双机2.5G验证；此前未定位问题和原生环境限制记录继续保留。
