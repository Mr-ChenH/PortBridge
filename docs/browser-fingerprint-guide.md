# 浏览器指纹配置（HTTP 请求头）

此功能为当前环境生成一致的浏览器请求头身份，用于HTTP接口联调。它控制User-Agent、语言偏好及Chromium的低熵Client Hints；TLS握手、HTTP/2参数、Canvas、字体、WebGL、屏幕或JavaScript环境并不会改变，也不是完整浏览器模拟。

## 使用

1. 在HTTP工作台选择项目和当前环境。
2. 打开“项目设置 → 浏览器指纹”，或“管理 → 浏览器指纹（当前环境）”。
3. 选择下一次生成的浏览器范围（随机/Chrome/Edge/Firefox）、桌面平台（随机/Windows/macOS/Linux）和语言。
4. 点击“随机生成一组”，查看预览。生成是本地操作，不登录、不发送请求。启用“自动使用当前环境的浏览器请求头”并点击“保存配置”。
5. 明确发送HTTP请求。同一环境固定使用保存的配置，发送、加载、环境切换、程序启动都不会重新随机；再次点击生成并保存才变更。

取消窗口放弃草稿；“清除配置”先清除草稿，保存后才移除。关闭自动请求头保留生成值，方便手动引用。复制环境保留非敏感指纹配置并保持身份；希望新身份时在副本中重新生成。旧项目默认没有指纹配置，维持原行为。

## 请求头与覆盖

| 条件 | 自动添加 |
|---|---|
| 配置启用且目标为HTTP/HTTPS | User-Agent、Accept-Language。 |
| Chrome/Edge且目标为HTTPS | 额外Sec-CH-UA、Sec-CH-UA-Mobile、Sec-CH-UA-Platform。 |
| Firefox | 不自动添加Chromium UA-CH。 |

启用的手填同名请求头优先，比较忽略大小写；例如手填User-Agent就覆盖环境默认。禁用的手填行不覆盖默认。生成不会自动添加Cookie、Authorization、Origin、Referer或依赖页面上下文的Sec-Fetch；不会声明不支持的压缩格式。网络、证书、主机名校验和响应预算继续生效。

Chrome/Edge预设主版本131/138/143，Firefox128/135/143，表示固定测试格式，不是实时最新版或完整真实浏览器采样。Windows桌面UA按官方规则使用Windows NT10.0；Chrome/Edge的次版本冻结0.0.0，Firefox的Gecko rv和Firefox版本相同。版本、平台与品牌头一致生成，不独立乱拼。

## 变量

在“生效变量”可查看浏览器配置来源；这些值可在URL、请求头或JSON等模板中引用。

| 变量 | 含义 |
|---|---|
| browser_user_agent | UA字符串。 |
| browser_accept_language | 带权重的语言头。 |
| browser_platform | Windows/macOS/Linux。 |
| browser_name | chrome/edge/firefox。 |
| browser_version | 主版本文本。 |
| browser_fingerprint | 包含配置元数据、scope=http-request-headers及预设头的JSON对象。 |
| browser_sec_ch_ua | Chromium品牌/主版本列表，仅Chrome/Edge生成。 |
| browser_sec_ch_ua_mobile | 桌面值?0，仅Chrome/Edge生成。 |
| browser_sec_ch_ua_platform | 带双引号的平台，仅Chrome/Edge生成。 |

例如手填请求头 `User-Agent: {{browser_user_agent}}`，或JSON Body使用 `{"browser":{{browser_fingerprint}}}`。运行值 > 显式环境变量 > 当前环境浏览器派生值 > 项目默认变量；显式覆盖可改变生成的一致组合，因此测试浏览器识别时应核对最终头。browser_fingerprint描述保存的预设配置，不反映手填头或单个变量的覆盖。

自动头只应用于HTTP项目手动发送和顺序联调。WS方案及流程运行变量保持各自作用域。配置可持久化、复制、导入/导出；敏感/登录运行值仍省略。旧schema2保留，可选browserFingerprint子结构版本1经过严格校验。

研究依据和范围见 [计划](project-factory/23-browser-fingerprint-plan.md)，验证见 [报告](reviews/browser-fingerprint.md)。
