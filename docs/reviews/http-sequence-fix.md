# HTTP 断言审核问题修复

目标版本 `http-sequence-fix-1`。用户明确要求修复 [完成度审核](http-sequence-completion-audit.md) 的HA01、HA02。当前代理实现和自审，沿用原HTTP客户端、项目环境与顺序控制器。

## 修复

| 审核项 | 实现与验收 |
|---|---|
| HA01 / HS01 | 断言使用局部、有长度限制的路径语法和查找器，支持 `$[0].id`、`[0].id`、嵌套和连续数组索引；原点路径/数字段、完整根值与JSON字符串内字段保持可用。不修改工作流和一期响应提取的路径语义。拒绝负数、小数、通配符、空索引、尾随文本/换行及超512字符路径；越界/超整数范围索引返回字段不存在。 |
| HA02 / HS01、HS03/HS04 | `HttpAssertions::resolve()` 展开完成后再次调用同一规则校验器；超出64KiB有界JSON估算预算或深度限制时返回错误和空规则。首步在占用资源和确认旧活动前拒绝；手动发送及后续步骤复用同一解析函数，后续超限步骤按既有停止/继续策略处理，无该步骤网络请求。 |

## 验证

新增5个回归槽：

- `assertionRootArrayPathsAndInvalidIndices`：根数组、相对根数组、嵌套/连续索引、旧点数字段、整根等于、越界与整数溢出、非法索引、512字符边界。
- `realRootArrayAssertionsPersistAndRun`：真实HTTP数组响应，保存无通信，手动断言通过，随后两个顺序请求断言通过。
- `expandedAssertionsRevalidateScalarAndNestedBudget`：合法字符串与对象展开，以及字符串/对象/数组内部变量展开后的超限拒绝。
- `expandedAssertionFailurePreservesRawBeforeConfirmation`：正在绑定的UDP保持连接与原epoch，手动与顺序首步均拒绝，无确认弹窗、服务端0请求。
- `expandedAssertionFailureInLaterStepHonorsPolicies`：第二步超限时默认停止并跳过第三步，或选择继续并执行第三步；两种情况下均不发送第二步，不在报告中写入展开值。

原完成度探针使用同一实际构建库复跑，修复前证据位于 `../validation/http-sequence/audit/`，修复后见 [修复后探针](../validation/http-sequence-fix/audit/probe-result.json)。第一轮新增测试编译有初始化器括号遗漏，构建失败后停止，未运行旧测试程序；记录保留 `before-test-initializer-fix/`。

最终完整回归、原生三档与输入/应用哈希见 [最终回执](../validation/http-sequence-fix/final/result.json)：冻结87个输入且前后稳定；完整10/10套件通过，HTTP项目普通35通过/2原生截图槽跳过，100/125/150%原生专项各37通过/0跳过，原HTTP/WS43通过，创建入口各7通过。修复后探针确认根数组断言通过、超限规则运行前拒绝、服务端0请求。HA01、HA02已关闭。

## 交付

独立部署 `dist/PortBridge-http-sequence-fix-1/PortBridge.exe`。原 `http-sequence-1`、`http-project-1`、此前失败检查点和审核记录保留；标准ZIP替换前按原SHA256备份到 `build/package-backups/http-sequence-1/`。独立解压和系统PATH启动、文件哈希及TCP/UDP回环以 [包回执](../validation/http-sequence-fix/package-receipt.json) 为准。

本轮只修复两处已复现问题，不扩大到脚本、自动刷新、并发或共享变量。10分钟期限继续使用已有定时器实现，本轮没有实等10分钟；本地合成服务和软件缩放不代表生产账号、物理多屏或干净Windows验证。
