# 四种通信方式统一扩大发送编辑区（composer-fix-1）

用户指出操作页面发送数据窗口偏小，并补充所有模块相同。本轮研究共享 Widgets 布局后发现，串口、TCP 客户端、TCP 服务端、UDP 均调用同一个 `buildComposer()`：输入框被固定为 56 DIP 高度，右侧格式/周期设置又占用 320–340 DIP 宽度，增加主窗口尺寸也不能增加编辑高度。UDP 另外多一行发送目标，进一步限制小窗口可用空间。

当前协调者直接修复共用组件，没有新增 worker。产品版本仍为 0.1.0，交付修订为 `composer-fix-1`；此前 UDP 直接发送、保留活动绑定、完整 ASCII 和 RX/TX 配色规则继续适用。

## 修改后的操作

1. **全宽多行编辑。** 格式、文本编码、换行、周期/间隔/次数及发送按钮统一移到输入框下方，不再占输入框右侧宽度。输入框取消固定高度，可随发送区扩展，最低 96 DIP；编辑区与验证提示的最小高度约束防止父容器裁切。
2. **拖动调整高度。** 接收/检查器与发送区之间加入 7 DIP 分隔条，可拖动给发送区更多空间；发送区不会被拖成不可见，记录表/检查器可折叠。分隔条提供 hover 提示及可访问名称。
3. **展开编辑 / 收起编辑。** 发送区右上按钮临时占用原数据区并收起趋势图；收起后恢复之前的空间分配。输入内容、格式、连接、记录和周期任务不变，不触发发送。焦点回到编辑框，发送按钮继续可访问。
4. **小窗口布局。** 窗口高度小于 850 DIP 时折叠趋势图及同一行队列概览，为接收/发送腾出空间；顶部实时指标、状态、统计和通信持续更新，增高窗口恢复图表。后台趋势采样继续运行。
5. **保存区域大小。** 正常布局下的 splitter 状态随其他 UI 偏好保存；关闭时将临时展开还原后保存，下次启动保持接收区可见，不自动连接或发送。

四种模式使用同一套调整。UDP 目标仍位于编辑器上方；TCP 服务端保留客户端/广播控件；各协议专用项继续按模式显示。

## 尺寸与截图

真实 Windows 原生 100% 测试记录的初次默认布局（单位 DIP）：

| 窗口 | 串口输入框 | TCP 客户端/服务端输入框 | UDP 输入框 |
| --- | --- | --- | --- |
| 1440×1000 默认 | 1109×205 | 1109×204 | 1109×168 |
| 1440×1000 展开 | 1109×514 | 1109×513 | 1109×477 |
| 1100×760 默认 | 769×114 | 769×113 | 769×96 |
| 1100×760 展开 | 769×274 | 769×273 | 769×237 |

尺寸随用户拖动、协议专用行、保存的偏好和字体略有变化；96 DIP 是下限，不是固定高度。

截图：[默认大窗口](../validation/composer-fix/composer-default-1440x1000.png)、[默认小窗口](../validation/composer-fix/composer-default-1100x760.png)、[小窗口展开 UDP](../validation/composer-fix/composer-expanded-3-1100x760.png)、[串口展开](../validation/composer-fix/composer-expanded-0-1440x1000.png)、[TCP 服务端展开](../validation/composer-fix/composer-expanded-2-1440x1000.png)、[浅色多行输入](../validation/composer-fix/composer-multiline-light.png)。已实际打开检查。

## 验证

新增一项综合回归，覆盖四种通信方式、两个窗口尺寸：窗口实际尺寸一致，编辑器至少 96 DIP 高、宽度超过发送区 85%；各可见格式/周期/发送控件逐级检查父容器边界。展开增加实际高度并保留多行原文，收起恢复分配；模拟真实拖动分隔条后高度实际增加。真实 UDP 绑定期间展开不发包，主动发送后对端逐字节收到多行内容，收起后绑定仍在。另一项回归验证关闭展开窗口后保存正常区域尺寸、重开恢复大小且不恢复临时展开、不自动连接/周期发送。

最终完整 CTest **3/3**：网络 **11** 组、会话 **27/27**、UI **36/36**（Qt 数量包含初始化/清理）。证据：[构建](../validation/composer-fix/build.txt)、[CTest](../validation/composer-fix/ctest.txt)、[结果](../validation/composer-fix/result.json)、[源哈希](../validation/composer-fix/source-hashes.json)。

Windows 原生 [100%](../validation/composer-fix/native-1.txt)、[125%](../validation/composer-fix/native-1.25.txt)、[150%](../validation/composer-fix/native-1.5.txt) 各 **6/6**：多行/拖动/四协议展开、尺寸偏好恢复、UDP 直接发送及原生页面布局四项综合测试加初始化/清理。源码冻结后执行，前后哈希相同。软件缩放并不等同真实多显示器 DPI 切换。

第一轮曾复现小窗口父容器裁切，保留 `ctest-before-size-constraints.txt` / `ui-before-size-constraints.txt`；补上最小布局约束和小窗口趋势折叠后消除。125% 时物理屏幕高度限制使 1440×1000 偏好恢复测试得到不同实际窗口高度，保留 `native-1.25-before-preference-fixture.txt`；尺寸恢复测试改为 1100×760，并新增实际窗口尺寸相等断言，保证同一尺寸下比较 splitter 恢复，不削弱控件裁切、内容或通信断言。

## 交付

独立运行目录 `dist/PortBridge-composer-fix-1/PortBridge.exe`。保留用户打开的旧进程及此前各目录，上一版 ZIP 与 source manifest 备份至 `build/package-backups/direct-send-1/` 和 `docs/validation/direct-send/source-manifest.json`。新版 ZIP 仍为 `dist/PortBridge-0.1.0-windows-x64.zip`。

打包核验 Release/部署/解压 EXE 一致、全部文件哈希、受限运行环境启动以及真实 UDP/TCP 各 500 帧回环；最终归档证据位于工作区 `docs/validation/composer-fix-zip.json`，生成于归档后。当前工作不增加物理串口、两机 2.5G、持续磁盘负载、干净 Windows 或病态 OS 阻塞退出的验收结论。
