# PortBridge 连接方案呈现优化（profile-picker-1）

用户反馈：所有连接方案无论是否使用都常驻显示，占据侧栏，显得不自然。本轮根据现有单活动会话和配置使用流程，改成当前方案卡片、按需搜索列表与后台会话返回入口。产品版本仍为 0.1.0，没有新增子 agent。

## 取舍与最终交互

| 呈现方式 | 对现有工作流程的影响 |
| --- | --- |
| 常驻全部方案列表 | 每次调试都占用 130–260 DIP 高度；不活动方案与连接参数同时争夺空间。 |
| 可折叠列表 | 收起后需要另找当前方案，展开后仍占用参数区域；折叠状态还需管理。 |
| 当前卡片 + 按需搜索 | 当前身份持续可见；切换时显示全部方案，平时保留空间给参数。采用此方案。 |

侧栏常驻 64 DIP 卡片，显示当前方案名称、协议/端点摘要和活动指示，长名称及摘要省略并保留完整工具提示。连接/绑定后展示实际本地端口；新建入口和更多管理菜单保留。协议由方案确定，现有只读协议标签继续显示。

点击卡片打开搜索弹窗，按名称、协议或地址匹配。最多六行后滚动，匹配结果减少时窗口随之收缩；无匹配时显示说明。真实点击选择后收起；搜索框 Enter 接受当前匹配项或首个匹配，Escape 关闭。打开时清除旧查询并聚焦搜索，搜索本身不选择或启动通信。深浅主题同步到弹窗和绘制委托。

浏览其他方案时，卡片下方按需出现“返回运行方案 · 名称”。点击即可返回原会话及其缓存。所有浏览仍遵守一个活动会话规则，启动另一方案继续采用原有显式替换行为。

## 实现

- `src/ui/design_widgets.hpp`：自绘 `ProfilePicker`，保持焦点、主题、悬停反馈与名称省略。
- `src/ui/main_window.cpp`：搜索弹窗、计数驱动高度和返回入口。`QMenu` 缓存 widget-action 尺寸，因此筛选后激活内容布局并通过 action 数据变化刷新尺寸。
- 保留方案列表模型及现有管理动作；同样数量刷新时复用列表项，避免真实点击期间清空/重建选中项。
- 本轮未修改网络或会话实现。方案卡片读取现有配置及实际端点，不产生通信请求。

## 验证

最终完整 CTest **3/3**：网络 **11** 组、会话 **27/27**、UI **40/40**（Qt Test 含初始化/清理），构建及测试前后源码/图标资产哈希稳定。

新增两项行为验证：

1. 真实点击卡片和搜索结果，启动 localhost UDP 后捕获 6 B；搜索无匹配并 Escape 不改变当前方案；切换查看 TCP 后继续接收 4 B，原绑定端口、会话 generation、记录状态保持。返回后恢复 UDP 视图和接收样本，总 RX 10 B；业务对端没有额外收到数据报。卡片显示实际临时绑定端口。
2. 加载 30 个长名称方案，列表限制六行并可滚动，过滤后弹窗收缩；键盘 Enter 选择最后一个方案。1100×760 窗口未被长名称撑大，卡片保留完整工具提示，未建立连接或发送数据。

原生 Windows 在 **100% / 125% / 150%** 分别运行上述两项、启动/可访问性与完整布局截图验证，每轮 **6/6**，源码稳定。深浅主题、搜索弹窗、多方案、长名称、后台返回及 1100×760/1280×900/1440×1000 状态均保存，实际打开截图复核。

首轮功能测试通过后，截图暴露筛选后弹窗留白偏多；修复尺寸缓存后增加收缩断言，并重新通过完整测试及三种缩放。之前证据保留在 `before-popup-size/`。最终验证驱动曾尝试复制不存在的 `network.txt`：网络是普通 CTest 可执行程序，日志在 `LastTest.log`，当时三个测试已全部通过；保留收据复制错误，修正复制逻辑并补存完整 CTest 日志，核对同一源码哈希，无需重复已通过的全套测试。

证据：[最终结果](../validation/profile-picker/result.json)、[CTest](../validation/profile-picker/ctest.txt)、[完整日志](../validation/profile-picker/ctest-details.txt)、[UI](../validation/profile-picker/ui.txt)、[原生缩放](../validation/profile-picker/native-result.json)、[源/资产哈希](../validation/profile-picker/source-hashes.json)。

预览：[当前卡片](../validation/profile-picker/profile-picker-active-dark.png)、[搜索结果](../validation/profile-picker/profile-picker-search.png)、[浅色弹窗](../validation/profile-picker/profile-picker-search-light.png)、[后台会话返回](../validation/profile-picker/profile-picker-background-session.png)、[多方案滚动](../validation/profile-picker/profile-picker-many.png)。

## 交付

独立运行目录 `dist/PortBridge-profile-picker-1/PortBridge.exe`，原程序与会话保留。最新 ZIP 为 `dist/PortBridge-0.1.0-windows-x64.zip`，上一版 `app-icon-1` 归档和校验备份至 `build/package-backups/app-icon-1/`，源清单保存至其历史验证目录。

核验 Release/部署/解压 EXE 一致、归档完整性、全部文件哈希、移除 Qt 环境变量且仅 Windows 系统 PATH 的独立启动，以及 UDP/TCP 各 500 帧回环。最终归档收据位于工作区 `docs/validation/profile-picker-zip.json`。物理设备、2.5G、干净 Windows 等历史验收边界保持原有结论。
