# 浏览器请求头指纹：调研、要求与实施

用户授权研究并修改HTTP项目环境配置，允许随机生成浏览器指纹。本轮范围为浏览器HTTP请求头身份配置，保留现有cpr/libcurl/OpenSSL客户端和TLS链/主机名验证。

## 实际研究

Brave查询：`MDN User-Agent Client hints Sec-CH-UA Sec-CH-UA-Platform browser fingerprint HTTP TLS curl impersonate`；`site.chromium.org updates ua reduction Windows NT 10.0 macOS 10_15_7 user agent`；`site.github.com curl-impersonate TLS HTTP2 fingerprint Firefox Chrome`。技能脚本返回成功，查询输出、内容提取及SHA/URL另存research记录，不把搜索摘要日期当作当前版本保证。

| 原始资料 | 观察 |
|---|---|
| https://www.chromium.org/updates/ua-reduction/ | Chromium桌面UA冻结Windows NT10.0、macOS10_15_7、次版本0.0.0；UA与平台必须一致。 |
| https://developer.mozilla.org/en-US/docs/Web/HTTP/Reference/Headers/Sec-CH-UA | UA-CH低熵brand/version结构、GREASE品牌；支持浏览器的安全上下文特征。 |
| https://developer.mozilla.org/en-US/docs/Web/HTTP/Reference/Headers/User-Agent/Firefox | Gecko rv与Firefox版本相同，桌面Gecko/20100101格式，不添加Chromium标识。 |
| https://learn.microsoft.com/en-us/microsoft-edge/web-platform/user-agent-guidance | Edge Chromium UA追加Edg，同版本Chromium/Edge品牌，低熵UA-CH只发送HTTPS。 |
| https://github.com/lwthiker/curl-impersonate | TLS/HTTP2指纹需要修改curl/TLS库与参数，普通UA请求头不能证明完整浏览器身份。 |

选择格式一致的本地桌面预设（Chrome/Edge主版本131、138、143；Firefox128、135、143），不称为实时最新版或真实浏览器捕获指纹。支持Windows/macOS/Linux及语言偏好。随机离线生成，版本/品牌/平台绑定，保存后固定。只默认User-Agent、Accept-Language、Chromium HTTPS低熵Sec-CH-UA/Mobile/Platform。不生成依赖导航的Sec-Fetch/Origin/Referer，不宣称未验证压缩编码、高熵协商或网页脚本特征。

## 要求与界面

| ID | 实现与验收 |
|---|---|
| BF01 | 环境设置新增第五页“浏览器指纹”，保留原四页索引。选择生成范围、随机生成、启用自动头、查看实际头与变量说明；取消不改store。 |
| BF02 | 可选env.browserFingerprint版本1结构，旧schema2兼容。严格验证，保存/导入失败保留原状态。复制/导出保留非敏感配置，不复制运行值。 |
| BF03 | 可引用browser_user_agent/browser_accept_language/browser_platform/browser_name/browser_version/browser_fingerprint及Chromium的browser_sec_ch_ua/mobile/platform。来源为浏览器配置；优先级运行值 > 显式环境定义 > 环境指纹派生值 > 项目定义。关闭自动头仍保留变量，删除配置移除派生值。 |
| BF04 | 自动头在明确发送时解析，启用的显式同名请求头大小写不敏感优先；禁用行不覆盖默认。Firefox不自动生成UA-CH，普通HTTP不自动发送UA-CH，HTTPS补低熵CH。仍走原请求校验。 |
| BF05 | 手动与顺序联调共用resolver；可未登录保存模板。原revision确认及整段冻结保持，发送不随机生成。 |
| BF06 | 真实HTTP与可信本地HTTPS检查头/优先级/环境隔离；持久化、导入/导出、取消/错误、顺序联调；深浅及软件缩放截图、完整回归、独立打包。 |

## 技术与任务

1. http_browser_fingerprint.hpp/.cpp负责生成、校验、派生变量/头模板；合法属性有界，不接受任意头，防止配置暗藏敏感值。
2. Store校验、原子配置和effectiveVariables合并；旧调用未提供字段时保留当前配置。
3. 统一窗口追加页签、预览/清除/随机按钮及派生来源，沿用原子保存和上下文围栏。
4. resolver先读手填头，补默认，HTTPS限定CH；不引入自动探测、重试或重登，作用域限手动HTTP项目。
5. 测试、机器回执、用户指南、源冻结、独立部署；保留http-history-1与其尚未提交改动。

当前代理实施/自审。用户明确要求研究后修改，无新增批准问题，不使用未经明确请求的多代理workflow。
