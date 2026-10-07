# PortBridge 应用图标（app-icon-1）

用户要求添加并设计好看的应用图标。本轮设计原创桥接标识，嵌入 Windows EXE 图标资源及 Qt 应用窗口资源。版本仍为 0.1.0，交付修订 `app-icon-1`。当前协调者完成，没有新增子 agent。

## 设计

深色圆角底和轻微边缘高光，连接两端口的薄荷绿桥接线，接收端为绿色、发送端为橙色，与现有 RX/TX 配色一致。保留 PortBridge 导航标识的几何连接语言，不加小尺寸难读的文字。16/20/24/32 像素版本增加线宽，避免机械缩小后笔画变弱。

设计原稿为 SVG；图标生成脚本用 Qt SVG 渲染后抗锯齿采样，输出透明 PNG 和九种尺寸的 ICO：16、20、24、32、40、48、64、128、256。PySide6/Pillow 仅用于离线生成，成品运行无此依赖。

- [深浅背景及各尺寸预览](../../assets/app-icon/preview.png)
- [1024×1024 透明 PNG](../../assets/app-icon/portbridge.png)
- [SVG 原稿](../../assets/app-icon/portbridge.svg)
- [Windows ICO](../../assets/app-icon/portbridge.ico)

## 接入

CMake 在 Windows 构建中启用 RC，将 ICO 编译为 EXE 原生图标；Qt AUTORCC 将同一 ICO 放入应用内资源，QApplication 设置窗口图标。Windows 文件/快捷方式图标及窗口标题栏、任务栏使用同一艺术稿。即使外部 `assets/` 图片移走，EXE 图标和运行窗口图标仍来自嵌入资源；便携包另附原稿及预览供查看。

`src/main.cpp` 设置应用图标，`assets/app-icon/app-icon.qrc` 提供 Qt 资源，`app-icon.rc.in` 提供原生文件图标。脚本 `scripts/generate-icon.py` 可离线再生成，`scripts/deploy.ps1` 附带图稿。通信、发送、诊断和启动离线规则保持原有行为。

## 验证

- 构建无编译警告，完整 CTest **3/3**：网络 **11** 组、会话 **27/27**、UI **38/38**（Qt Test 含初始化/清理），源码/图标资源构建前后哈希一致。
- 使用 Windows API 读取构建后 EXE 的 RT_GROUP_ICON / RT_ICON，确认九种尺寸、32 位色深和完整 PNG 载荷。
- 用 Windows Shell 提取 EXE 的 16/32 像素图标，保存真实结果并与 ICO 图稿对比。
- 启动独立验收配置的原生窗口，用 WM_GETICON 获取实际小/大图标，保存真实像素。四种结果的全不透明区域最大 RGB 差均为 **0**，证明文件图标和运行窗口均加载了新图稿。验证进程按指定时限正常退出，未关闭用户旧程序。
- 原生检查脚本第一轮误把 ExtractIconEx 同时返回两个大小图标的数量当作必须等于 1，保留 `native-check-before-api-count-fix.txt`；修正检查为有返回且两个句柄均有效后通过，应用图稿和实现没有因此更改。

证据：[构建](../validation/app-icon/build.txt)、[CTest](../validation/app-icon/ctest.txt)、[结果](../validation/app-icon/result.json)、[源/资产哈希](../validation/app-icon/source-hashes.json)、[Windows 图标检查](../validation/app-icon/native-icons.json)、[EXE 小图标](../validation/app-icon/exe-small.png)、[窗口大图标](../validation/app-icon/window-large.png)。设计预览已实际打开检查。

## 交付

独立运行目录 `dist/PortBridge-app-icon-1/PortBridge.exe`，不覆盖正在运行的旧程序。新版 ZIP 为 `dist/PortBridge-0.1.0-windows-x64.zip`，上一版备份至 `build/package-backups/text-diagnostics-1/`。打包核验 Release/部署/解压 EXE 一致、全部文件哈希、受限 PATH 启动和 UDP/TCP 各 500 帧回环；最终归档证据位于工作区 `docs/validation/app-icon-zip.json`。

物理串口、双机 2.5G、持续磁盘负载、干净 Windows 和物理多显示器验收结论不变。
