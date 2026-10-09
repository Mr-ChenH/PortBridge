# HTTP 项目工作台实施与审核

修订目标：`http-project-1`。在原生HTTP工作台中支持项目/分类、环境变量、登录响应token复用、公共认证和项目文件，使用原cpr/libcurl生产客户端。

## 实现范围

- `HttpProjectStore`统一保存版本2项目与HTTP请求；迁移原版本1库至默认项目，保留原请求ID和原旧库，损坏/未知版本原数据不被覆盖。项目/环境/请求身份独立；分类改名同步更新归属，删除分类保留请求。
- 项目、分类、环境管理与变量编辑；环境创建/复制/重命名/删除。普通项目/环境变量可持久化，敏感与响应值进入有界的环境运行状态，复制和导出省略敏感值。
- 保存未解析模板；发送解析URL、query、header、Bearer/Basic、原文/JSON/urlencoded Body。URL路径/query分别编码，JSON转义及类型保持；变量缺失、循环、超限、非法URI或CR/LF注入在网络前拒绝。
- 项目公共认证、请求继承/无认证/显式认证；登录可禁用继承。重复Authorization拒绝，不静默覆盖。
- 成功响应从权威JSON/headers提取，全部规则成功再提交；身份/代际、项目、环境和变量版本不匹配，非2xx、取消或路径缺失不更新旧值。选择历史不会重新提取。原始响应不被预览遮蔽改写。
- 资源确认后在结束旧活动前复核项目上下文；真实UDP活动在确认期间环境变更时保留，新HTTP没有发送。

界面和实际步骤见 [操作指南](../http-project-guide.md)，样例 [登录项目](../examples/login-reuse.pbhttp-project.json)。研究资料的下载回执见 [来源记录](../validation/http-projects/research-sources.json)。

## 验证与证据

最终构建、完整CTest、原生缩放结果和79个稳定输入见 [最终回执](../validation/http-projects/final/result.json)。完整回归涵盖network、session、ui、workflow、workflow_ui、workflow_protocol、workflow_integration、workflow_e2e、protocol_debug、http_projects共10个套件；新增HTTP项目功能使用真实本地TCP HTTP服务核对URI、Authorization与JSON/form Body。

初版新增测试12/0、原HTTP/WS43/0；项目管理补齐后19/0，单个原生截图槽在普通无截图环境按约定跳过。最终普通HTTP项目测试21/0、截图槽另在windows平台100/125/150%软件缩放执行，22/0且没有跳过；创建入口回归每个缩放7/0。截图槽跳过不代表功能跳过，原生结果保存独立日志。

原生图集见 [截图浏览](../validation/http-projects/gallery.html)。原有主窗口背景已以真实EXE额外检查；独立截图页补齐宿主背景。默认分类调整为“全部请求”，避免保存到分类后看似消失。项目公共认证Token默认隐藏，含不完整双大括号的真实字面凭据也会遮蔽，不以“出现{{”错误认定为模板。

失败和检查点均保留：`before-combo-data-fix/`、`before-panel-metaobject-fix/`、`before-runtime-remove-fix/`、`before-default-filter-and-screenshot-background-fix/`、`before-project-auth-masking-fix/`、`before-extraction-editor-budget-fix/`。前两次编译分别修复QJsonValueRef到QVariant的显式转换和测试查找所需metaobject；QJsonObject::remove返回void的编译问题已修复。背景问题属于独立截图准备；正式主窗口截图保存于检查点。提取编辑器最多32行，禁用行也计入编辑预算；保存/导入一致，避免超额禁用行可以保存却不能重新载入。项目token遮蔽的后续修复重新冻结和验证，不沿用修改前通过作为最终结果。

## 交付与边界

独立部署目标 `dist/PortBridge-http-project-1/PortBridge.exe`，标准Windows ZIP位于dist；部署、仅系统PATH启动、压缩完整性、全部解压哈希和TCP/UDP各500帧复验以 [包回执](../validation/http-projects/package-receipt.json) 为准。上一connection-ui-1部署目录、ZIP备份和输入清单保留。

本轮由当前代理实现与自审，未新增独立多代理审核。验证使用本地合成凭据和真实客户端，未对真实生产账号登录。软件缩放不等于物理多屏DPI，回环不等于物理串口、2.5G网卡、长时间磁盘/负载或干净Windows验证。历史connected-wait偶发失败原始记录保留，不能由本次通过推断原因已消除。

本期不提供脚本引擎、自动重登/刷新、OAuth浏览器登录、团队同步、完整Postman集合导入、multipart/SSE或多个同时活动的资源。HTTP项目变量与工作流运行变量、WS方案保持独立。
