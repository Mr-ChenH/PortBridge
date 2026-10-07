# 更明显的收发配色与 UDP 直接发送（direct-send-1）

用户反馈上一版颜色不明显，UDP 还需应用客户端/目标，应用时又出现消息。本轮删除应用步骤，改为绑定本地端口后填写目标并直接发送，同时改为绿/橙配色、不同底色和明确方向标识。产品版本仍为 0.1.0；本修订取代 `udp-fix-1` 的“修改后必须应用才能发送”规则。由当前协调者完成，没有新增子 agent。

## 结果与原因

旧 `setUdpTarget()` 只解析目标，没有调用 UDP send；但成功时生成 `UdpTargetReady` SYSTEM 记录，进入样本和原始采集，容易让用户把配置消息当成发送。现在 UI 不再有“应用目标”，目标就绪只更新内部状态，不写入数据样本或原始采集。其他连接生命周期、错误和丢弃等 SYSTEM 记录仍保留，方向标识与真实 RX/TX 区分。

用户流程为：**绑定本地端口 → 填写目标 IP/域名、端口及内容 → 点击发送**。编辑目标、保存方案均不发送 UDP 数据报。UDP 不需要注册、应用或选择 TCP 客户端。点击后是有界队列接受请求，TX 仍只计实际本地写出完成的字节，不承诺远端在线或业务确认。

每条 UDP 请求保存自己的目标。即使旧请求仍排队，也可向新地址发下一条；旧请求不会跟随新地址改变去向。IPv4 直接写出，域名在 IO 线程按队首请求解析并使用单项缓存；失败仅产生错误、不发送探测包、不回退到先前目标，后续有效请求和接收仍继续。目标元数据纳入发送队列预算，原有描述符数量上限、代次隔离和停止取消保持有效。周期发送固定使用启动时的目标，运行中锁定相关编辑。底层旧默认目标 API 为既有调用方保留；绑定时原有默认目标解析仍可能发生 DNS 查询，这不代表向业务端口发送 UDP 数据。

收发配色：

| 方向 | 深色文字 / 底色 | 浅色文字 / 底色 | 其他区分 |
| --- | --- | --- | --- |
| RX 接收 | `#68ED9D` / `#142C20` | `#116B35` / `#E5F5EA` | `▼ RX 接收` |
| TX 发送 | `#FFAD5C` / `#392817` | `#9A4300` / `#FFF0DD` | `▲ TX 发送` |

颜色和底色作用于实际内容，方向标签及数据表的 RX/TX 徽标同步调整。格式切换和主题切换继续保留真实字节与方向。截图：[深色文本](../validation/direct-send/formatted-text-dark.png)、[浅色文本](../validation/direct-send/formatted-text-light.png)、[1100×760 UDP 工作台](../validation/direct-send/udp-target-small.png)。已实际打开检查。

## 验证

- 新增网络实收测试：绑定/配置目标后没有 UDP 包，包含零长度包的检测；紧接着向两个不同目标排队发送 0 B 和 3 B，逐个核对内容、源端口和 TX 元数据；无效 IPv4 解析之后的有效请求继续完成，失败请求不落入旧目标，队列预算释放，独立 RX 正常。
- 更新真实双对端 UI 测试：不存在应用按钮；目标有效时可直接发送，修改/保存目标后对端均无新包且 TX 统计不变；两次点击分别收到准确内容。绑定端口、会话代次、记录和后台接收继续保留。
- 对实际 QTextDocument 的 RX/TX 前景色、背景色及换主题后的颜色直接断言，保留 JSON/HEX 格式化、完整 ASCII 分页/末尾、全文复制、浏览会话保持等回归。
- 完整 CTest **3/3**：网络 **11** 组、会话 **27/27**、UI **34/34**；Qt 数量包含初始化/清理。构建无编译警告，测试前后源文件哈希一致。
- Windows 原生 100%、125%、150% 各 **6/6**：直接 UDP 发送、文本/完整 ASCII、后台绑定保持、原生布局四项综合测试加初始化/清理。软件缩放不等同真实多显示器 DPI 切换验收。

证据：[完整测试](../validation/direct-send/ctest.txt)、[验证结果](../validation/direct-send/result.json)、[源稳定性](../validation/direct-send/source-hashes.json)、[100%](../validation/direct-send/native-1.txt)、[125%](../validation/direct-send/native-1.25.txt)、[150%](../validation/direct-send/native-1.5.txt)。

## 交付

独立运行目录为 `dist/PortBridge-direct-send-1/PortBridge.exe`，不覆盖或结束用户打开的旧程序。新版 ZIP 为 `dist/PortBridge-0.1.0-windows-x64.zip`；前版归档于 `build/package-backups/interaction-fix-1/`。

打包核验 Release/部署/解压 EXE 一致、所有文件哈希、受限 PATH 启动及 UDP/TCP 各 500 帧回环。最终 ZIP 哈希与解压验证 JSON 生成于归档之后，保存在工作区 `docs/validation/direct-send-zip.json`，不将自引用哈希写入 ZIP。当前证据不增加物理串口、两机 2.5G、持续磁盘负载或干净 Windows 的验收结论。
