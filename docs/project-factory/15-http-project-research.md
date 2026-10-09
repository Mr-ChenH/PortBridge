# HTTP 项目化与登录状态复用调研

## 需求与结论

现有HTTP工作台可保存独立请求，但没有项目/文件夹、环境上下文、认证继承或跨请求运行变量。登录后需手工复制token，地址和认证重复填写。本轮建议采用“本地项目 → 文件夹 → 请求 + 环境 + 运行变量”：一个项目管理一组接口，开发/测试环境分别维护base_url与登录状态，登录响应提取token后，由业务请求引用变量或继承项目认证。第一期以可视化提取规则完成常见联调，不需要编写JavaScript。

用户最初要求先调研，后续“继续实现”已授权进入实施。此文区分已查到的证据与PortBridge设计选择，不把他方能力视为已实现。

## 实际来源与比较

| 产品/来源 | 已核实的能力 | 适合借鉴的部分 |
|---|---|---|
| Postman官方变量/认证/脚本文档 | collection/environment/data/local等作用域；窄作用域覆盖宽作用域；双大括号变量；集合/文件夹认证可由请求继承；pm.environment.set和pm.response.json处理响应。 | 环境切换、认证来源可见、响应结果成为后续请求输入。 |
| Apifox官方项目/变量/提取/鉴权文档 | 项目、目录；本地/共享变量；临时>测试数据>环境>模块>项目>团队的优先级；可视化JSONPath提取到变量；接口认证可继承目录；token可供同目录调用。 | 本地项目分类、图形化后置提取、请求与环境在同一个操作界面。 |
| Bruno实际README、固定提交源码 | README明确本地文件夹、Bru文本、Git协作、离线取向。vars-runtime.js区分环境/运行/集合/文件夹/请求变量，执行后返回被修改的作用域；集合认证UI含Bearer等。 | 数据本地化、定义与运行变量分离、避免仅为变量复用引入云账户。 |
| Hoppscotch官方集合/环境文档 | 子集合、分类与导入导出；集合认证/请求头/变量继承；私有secret变量不导出值、不复制secret值，环境具有初始/当前值。 | 默认导出保留变量结构但不带真实token；环境状态本地隔离。 |
| Insomnia实际README | 官方仓库概述和产品定位已获取。 | 未成功取得本轮环境/脚本文档，不据此宣称深入审阅其实现。 |

Postman当前资料为v12，文档中的本地/共享变量表述及Vault可能与旧版本不同；研究引用实际下载内容，不以历史模型知识覆盖它。Bruno源码固定于 `efba9c2f13f3bff4e7b879123fdf5522d74189f9`，已阅读两份相关文件，不宣称审计全仓库。Bruno、Insomnia部分官方站点TLS握手超时；Brave搜索30秒超时且没有返回可用结果，改用直接官方文档和GitHub源码，TLS验证保持开启。

## 代表性原始链接

- [Postman变量](https://learning.postman.com/docs/sending-requests/variables/variables/)、[脚本变量API](https://learning.postman.com/docs/tests-and-scripts/write-scripts/postman-sandbox-reference/pm-variables/)、[响应API](https://learning.postman.com/docs/tests-and-scripts/write-scripts/postman-sandbox-reference/pm-response/)、[认证继承](https://learning.postman.com/docs/sending-requests/authorization/specifying-authorization-details/)。
- [Apifox项目](https://docs.apifox.com/create-api-project)、[环境](https://docs.apifox.com/environments-and-services)、[变量](https://docs.apifox.com/global-environment-session-variables)、[可视化提取](https://docs.apifox.com/extract-variables)、[鉴权继承](https://docs.apifox.com/use-security-schemes)。
- [Bruno固定提交运行变量源码](https://github.com/usebruno/bruno/blob/efba9c2f13f3bff4e7b879123fdf5522d74189f9/packages/bruno-js/src/runtime/vars-runtime.js)、[集合认证模式源码](https://github.com/usebruno/bruno/blob/efba9c2f13f3bff4e7b879123fdf5522d74189f9/packages/bruno-app/src/components/CollectionSettings/Auth/AuthMode/index.js)。
- [Hoppscotch集合](https://docs.hoppscotch.io/documentation/features/collections)、[环境与secret](https://docs.hoppscotch.io/documentation/features/environments)。
- [Bearer标准RFC6750](https://www.rfc-editor.org/rfc/rfc6750)、[Windows凭据API](https://learn.microsoft.com/en-us/windows/win32/api/wincred/nf-wincred-credwritew)、[DPAPI](https://learn.microsoft.com/en-us/windows/win32/api/dpapi/nf-dpapi-cryptprotectdata)：长期凭据持久化的备选，第一期不把自动提取的token写入项目文件。

下载状态、最终URL、字节数和SHA256记录于 `docs/validation/http-projects/research-sources.json`；原始HTML/文本与完整失败回执位于 `.pi/http-project-research/`。

## 仓库核对

- `ProtocolDebugPage::Impl` 将HTTP/WS分别存入 `manual/httpLibrary` / `manual/webSocketLibrary`，版本1扁平entries、64项/8MiB；每个请求已有UUID。
- `parameters()`直接读取URL、请求头与认证值，目前没有模板解析；保存调用相同参数构建/验证，因而新增变量后必须分离“保存模板”与“解析后发送”。
- `draft()`默认遮蔽认证、敏感查询与JSON字段；不能用遮蔽字符串代替运行变量，也不能把 `{{access_token}}` 当真实secret而覆盖模板引用。
- `ProtocolDebugSession`已有不可变快照、operationId/epoch、真实状态/Body/Headers、取消与晚回调隔离，适合绑定项目/环境提取上下文。
- 工作流 `lookup()`有受限点路径与数组数字段，extract节点支持JSON提取，变量在每次start重新初始化。该能力不等于手动页已有跨请求变量；第一期不直接共享运行器内部状态。

## PortBridge第一期建议

1. 项目与文件夹：创建/改名/删除、请求分类/移动/复制、搜索；旧请求迁入默认项目。
2. 环境：每个项目独立开发/测试等环境，base_url、普通变量、敏感运行变量分开；项目定义与运行token分离。
3. 请求模板：URL、query、headers、认证、Body使用 `{{变量名}}`；发送时统一解析并检查未定义变量，保留中文、加号和JSON类型/转义。
4. 公共认证：项目默认认证，请求选择继承/无认证/Bearer/Basic；登录请求可明确无认证，业务请求继承Bearer `{{access_token}}`。
5. 响应提取：JSON字段路径、响应头等可视规则；只对配置了提取的成功响应写入绑定项目/环境。取消、失败、路径缺失、过期操作不能伪报更新。
6. 查看与清除：显示变量来源、token已获取/缺失及更新时间，不显示明文；环境切换不会把测试token带入生产环境。
7. 本地项目导入导出：版本化JSON、默认不导出token/密码；原 `.pbhttp.json`兼容导入，启动/保存/切换保持静默。

任意JavaScript、自动刷新/自动重登、OAuth浏览器登录、团队云同步、完整Postman集合兼容、multipart/SSE及多活动资源留作独立后续范围。后续若需要保存长期token，应以Windows凭据存储为明确选项，不能简单将“本地变量”理解为可明文写入项目。
