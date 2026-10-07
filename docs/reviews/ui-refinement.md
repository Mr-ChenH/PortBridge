# 原生 UI 设计细化验收

本次按 `docs/project-factory/04-ui-design.md` 和离线 HTML 原型逐块细化 Qt Widgets 界面。原型的模拟设备、吞吐和数据没有移入产品；截图中的通信数据由实际本地套接字生成。

## 连接配置修正（ui-refinement-2）

移除连接配置中的“通信方式”标签和下拉框。协议直接取自当前连接方案，标题右侧以只读 SERIAL / TCP CLIENT / TCP SERVER / UDP 标识显示，配置区直接从串口设备或本地地址开始。协议选择保留在新建/编辑方案对话框。实际测试覆盖四种方案的标识和字段可见性、方案编辑后的协议更新与持久化；确认侧栏不存在通信方式控件或标签。

## 界面调整

- 恢复 60 DIP 顶栏、54 DIP 工具轨、默认 232 DIP 连接侧栏、246 DIP 检查器和 28 DIP 状态栏。分隔器允许调整，已有用户布局偏好保留；小窗口的连接设置和检查器可滚动。
- 顶部采用下划线导航，工具轨同步选择；品牌、主题和操作菜单采用统一线性图标。搜索快捷键和从命令库载入也同步导航。
- 方案列表改为 64 DIP 双行条目，显示真实协议/地址或串口配置、图标、当前方案和实际连接状态。普通高度窗口完整展示四个默认方案。
- 标题旁放置连接状态；普通调试/高速采集改为紧凑分段按钮，保留原有控制接口。重复点击当前模式或数据页签保持选择一致。
- 四项指标采用横向分隔条、较大的数值和较小的单位/说明。吞吐曲线保留最近 60 秒真实采样；显示队列、记录队列和发送积压独立呈现。
- 数据表采用 32 DIP 行、分隔线、等宽字节、RX/TX/SYSTEM 徽标、选中强调和补零序号。工具条集中数据/文本/诊断页签、实际可见条数、搜索、格式和显示操作。
- 字节检查器采用记录标题、时间/来源元数据和紧凑的每行八字节 HEX，ASCII 单独分段。原始字节复制不受显示格式影响；保留 4096 字节预览上限及完整时间提示。
- 发送区采用左侧编辑器/校验说明、右侧格式/换行/周期设置及发送按钮。运行时使用简短“停止”按钮，发送次数保留在状态栏和提示中；UTF-8/ASCII 控件在文本格式时显示。
- 深浅主题统一下拉箭头、复选框、滚动条、焦点、菜单和进度条。诊断采用六层实际计数，完整说明可展开；采集文件/命令库采用一致标题、条目及空状态，导出进度仅在有任务时显示。

## 实际发现并修正

截图和新增交互验证发现了布局将模式框拉高、标题状态被推到最右侧、工具轨按钮因样式最小尺寸被压扁、工具条下拉文字截断、诊断滚动区出现白底，以及 246 DIP 字节检查器有 4 DIP 水平溢出。均已修正。八字节 HEX 行用更紧凑的偏移分隔，测试仍要求水平滚动范围为零。

## 验证结果

| 检查 | 结果 | 证据 |
| --- | --- | --- |
| 最终 Release 构建 | 通过，无编译器警告 | `.pi/ui-design-build.log` |
| 全量 CTest | 3/3，零失败，45.96 秒 | `docs/validation/ui-refinement-ctest.txt` |
| 网络 / 会话 / UI | 8 组 / 19 Qt Test 结果 / 21 Qt Test 结果 | 最终构建测试报告 |
| Windows 原生 UI | 21 通过、0 失败 | `docs/validation/ui-refinement-native.txt` |
| 125% / 150% 缩放 | 各 21 通过、0 失败 | `docs/validation/ui-refinement-scale-1.25.txt`、`ui-refinement-scale-1.5.txt` |
| 实际 UDP 数据表 | 实收 24 × 1472 字节，实发 8 字节并在对端核对；默认检查器无水平溢出 | `udpRecordInspectorFitsDesignedWidth()` |
| 导航、模式、诊断详情 | 鼠标、重复点击、Ctrl+K、展开/收起均通过，操作不自动发送 | `navigationAndModeControlsStayInSync()` |
| 布局截图 | 1440×1000 深浅主题；1440×960、1280×900、1100×760；TCP/UDP 实际回环、诊断、文件、命令页 | `docs/screenshots/ui-refinement/` |

Qt Test 总数包括初始化和清理结果。125%/150% 是软件缩放检查；这次工作没有增加物理串口、真实 2.5G、长时录制或多显示器切换的验收证据。

## 查看

- [深色启动](../screenshots/ui-refinement/native-dark-1440.png)
- [浅色启动](../screenshots/ui-refinement/native-light-1440.png)
- [实际 UDP 数据，深色](../screenshots/ui-refinement/udp-data-dark.png)
- [实际 UDP 数据，浅色](../screenshots/ui-refinement/udp-data-light.png)
- [实际 TCP 数据与所选字节](../screenshots/ui-refinement/loopback-server-dark.png)
- [诊断页](../screenshots/ui-refinement/diagnostics-page.png)
- [采集文件页](../screenshots/ui-refinement/captures-page.png)
- [命令库页](../screenshots/ui-refinement/commands-page.png)

软件仍显示真实通信状态和未知项。默认断开、不自动发送，配置、周期发送、UTF-8 连续解码、选择性断开客户端、原始捕获与导出取消等既有回归保持通过。
