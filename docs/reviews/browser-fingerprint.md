# 浏览器请求头指纹配置：实施与验证

本轮用户要求研究后在项目环境变量中增加随机浏览器指纹。当前代理实施和自审，交付browser-fingerprint-1。原http-history-1及其尚未提交代码、验证和部署保留；无新独立代理审计。

## 结果与边界

HTTP项目当前环境增加浏览器指纹页和菜单。随机生成Chrome/Edge/Firefox与Windows/macOS/Linux的桌面组合，保留语言偏好、版本/品牌/平台一致性。配置为env.browserFingerprint版本1；旧schema2无该字段维持原行为。生成/编辑/保存不通信，取消保留旧配置；只有显式保存发布草稿。

User-Agent、Accept-Language为默认头；Chromium低熵Sec-CH-UA/Mobile/Platform仅自动用于HTTPS，Firefox不自动加这些头。启用的手填同名头忽略大小写优先，禁用行不覆盖默认。变量优先级为运行值 > 显式环境定义 > 环境指纹派生值 > 项目定义。关闭自动头保留变量，删除配置移除派生值。复制/导入/导出保留非敏感配置，运行秘密仍隔离并省略。

http_browser_fingerprint负责有界生成、严格结构校验、变量/头模板。环境store沿用原子saveConfiguration，旧调用省略字段保留指纹。resolver在发送冻结前补默认头，沿用原模板/头安全校验、revision确认和顺序联调。发送、切换、读取均不重新随机。Firefox macOS格式使用Mac OS X10.15；Chromium采用其冻结平台格式。

本地固定版本预设不代表实时最新版或真实完整捕获。TLS/HTTP2握手参数、Canvas、字体、WebGL、屏幕及JS执行环境未改变，未宣称完整浏览器模拟。HTTPS检验服务端实际收到的请求头与可信CA验证，不证明完整浏览器指纹。物理串口、第二机器2.5G、干净Windows及所有物理DPI组合验收仍未建立。

## 验证

最终源冻结期间无代码改变，build33.442s，focused2.221s。完整CTest10/10通过174.18s（驱动174.436s），HTTP项目47通过/0失败/4个受控截图跳过，协议调试46通过/0失败/1个受控跳过。编译日志无compiler warning/error。

新增六个功能槽覆盖：

- 生成不重复版本/ID、Chrome与Firefox头差异、环境复制/删除/恢复隔离。
- 实际HTTP捕获User-Agent/语言、普通HTTP无CH、启用的手填UA覆盖及编辑器保留原始行。
- 对话框随机后取消、清除后保存、新生成并保存，以及QSettings重新加载一致。
- 临时RSA证书含localhost SAN和CA约束，cpr显式caFile且保留链/主机名校验。真实HTTPS依次捕获Chrome/Edge/Firefox，独立核对UA主版本、Gecko rv、Chromium品牌版本、平台/mobile值；关闭自动头后不发送语言和CH。
- malformed版本/字段类型/小数主版本/任意headers导入被拒，原project/runtime/revision保持；持久化/导出导入；四层优先级；注入控制字符的UA在请求解析中拒绝。
- 实际顺序联调两步收到同一UA且环境配置不变。

原生Qt Windows软件因子1/1.25/1.5各9通过/0失败/0跳过，3.866/3.874/3.945s，每档Chrome/Edge/Firefox深浅六图，共18图。截图控件位置检查通过，查看深浅及不同缩放的代表图未发现裁切或对比问题；未把这转换为任意物理显示器组合认证。offscreen propagateSizeHints警告来自测试平台，不是失败。

首轮编译和focused通过；扩展HTTPS/持久化/顺序检查通过，最终完整回归通过，无新增失败检查点。计划Markdown被误送clang-format后已完整恢复为正常Markdown，未进入产品代码或最终部署。

最终日志、源hash、机器回执、原生截图在docs/validation/browser-fingerprint/final/。调研来自MDN、Chromium、Microsoft及curl-impersonate；6份查询/提取保留原输出、来源与SHA，工具不提供HTTP状态码则不编造状态。

部署、ZIP完整性与解压系统PATH启动/TCP/UDP复测详见package-receipt.json。历史版本的部署/压缩包保留于build/package-backups/http-history-1。源、文档和测试完成；本轮未收到新增提交/推送要求。
