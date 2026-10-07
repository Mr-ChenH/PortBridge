# PortBridge 第三方组件

- Qt 6.8.3（Core、Gui、Widgets、Network、SerialPort、Test）：https://www.qt.io/ 。使用 Qt 开源发行的适用许可；Qt Test 仅用于验证。Qt SerialPort 按 v6.8.3 构建，提交 704c70cde4d3153d2871e39d5bc10ccbd270f850，源码 https://github.com/qt/qtserialport 。许可证见对应源码 LICENSES/。
- standalone Asio 1.30.2：提交 12e0ce9e0500bf0f247dbd1ae894272656456079，https://github.com/chriskohlhoff/asio ，Boost Software License 1.0；原文见 .deps/asio/asio/LICENSE_1_0.txt。
- MinGW 13.1.0 配套运行库由 Qt MinGW SDK 提供；部署使用 windeployqt 的 compiler-runtime 处理，包含 GCC GPLv3/GCC Runtime Library Exception 3.1、winpthreads 和 MinGW-w64 原许可，见发布目录 third-party-licenses/MinGW/。

Qt 动态链接发行所用 LGPLv3 原文保存在 third-party-licenses/Qt-LGPL-3.0.txt，SerialPort 原许可集合另存。Qt 6.8.3 原版源码可从 https://download.qt.io/archive/qt/6.8/6.8.3/submodules/ 获取对应 qtbase/qtserialport 源码包；Qtbase源码及第三方归属信息另见 https://github.com/qt/qtbase/tree/v6.8.3/src/3rdparty 。Qt运行时DLL可替换为ABI兼容的用户构建，应用未静态链接Qt。实际链接模块以部署DLL和源码构建条件为准。

第三方源码位于被忽略的 .deps/，构建脚本固定下载版本。应用动态链接 Qt，发布脚本保留可用的组件许可文本。本文件不为 PortBridge 自身选择发布许可证。
