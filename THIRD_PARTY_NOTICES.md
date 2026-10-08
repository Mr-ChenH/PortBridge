# PortBridge 第三方组件

- Qt 6.8.3（Core、Gui、Widgets、Network、SerialPort、Test）：https://www.qt.io/ 。使用 Qt 开源发行的适用许可；Qt Test 仅用于验证。Qt SerialPort 按 v6.8.3 构建，提交 704c70cde4d3153d2871e39d5bc10ccbd270f850，源码 https://github.com/qt/qtserialport 。许可证见对应源码 LICENSES/。
- standalone Asio 1.30.2：提交 12e0ce9e0500bf0f247dbd1ae894272656456079，https://github.com/chriskohlhoff/asio ，Boost Software License 1.0；原文见 .deps/asio/asio/LICENSE_1_0.txt。
- QtNodes 3.0.16：提交 7c6341a66a8e46b8988140b9e60d892b6a3560b3，https://github.com/paceholder/nodeeditor ，BSD-3-Clause；原文见固定源码 LICENSE.rst，发布由协调者复制到 third-party-licenses。工作流 UI 静态链接该组件；受控兼容补丁及其校验由 scripts/setup-qtnodes.ps1 和发布源码清单记录。
- MinGW 13.1.0 配套运行库由 Qt MinGW SDK 提供；部署使用 windeployqt 的 compiler-runtime 处理，包含 GCC GPLv3/GCC Runtime Library Exception 3.1、winpthreads 和 MinGW-w64 原许可，见发布目录 third-party-licenses/MinGW/。

Qt 动态链接发行所用 LGPLv3 原文保存在 third-party-licenses/Qt-LGPL-3.0.txt，SerialPort 原许可集合另存。Qt 6.8.3 原版源码可从 https://download.qt.io/archive/qt/6.8/6.8.3/submodules/ 获取对应 qtbase/qtserialport 源码包；Qtbase源码及第三方归属信息另见 https://github.com/qt/qtbase/tree/v6.8.3/src/3rdparty 。Qt运行时DLL可替换为ABI兼容的用户构建，应用未静态链接Qt。实际链接模块以部署DLL和源码构建条件为准。

第三方源码位于被忽略的 .deps/，构建脚本固定下载版本。应用动态链接 Qt，发布脚本保留可用的组件许可文本。本文件不为 PortBridge 自身选择发布许可证。

## 工作流协议组件

`scripts/setup-workflow-deps.ps1` 验证下列源归档 SHA256 后解包；CMake 只使用本地源，不在配置或构建期间联网。协议使用静态 cpr/libcurl/c-ares/OpenSSL 和 Boost.Beast 头文件，Qt 仅为 QObject 信号桥及测试服务端。Boost.Asio 与原始网络核心的 standalone Asio 分别运行。

| 组件 | 固定版本 | 许可及来源 | 源归档 SHA256 |
| --- | --- | --- | --- |
| cpr | 1.14.2 | 主体 MIT；[上游](https://github.com/libcpr/cpr/tree/1.14.2) | `b9b529b47083bfe80bba855ca5308d12d767ae7c7b629aef5ef018c4343cf62b` |
| libcurl | 8.16.0 | curl 许可；[源包](https://curl.se/download/curl-8.16.0.tar.xz) | `40c8cddbcb6cc6251c03dea423a472a6cea4037be654ba5cf5dec6eb2d22ff1d` |
| c-ares | 1.34.5 | MIT；[上游](https://github.com/c-ares/c-ares/tree/v1.34.5) | `7d935790e9af081c25c495fd13c2cfcda4792983418e96358ef6e7320ee06346` |
| Boost / Beast / Asio | 1.89.0 | BSL-1.0；[源包](https://archives.boost.io/release/1.89.0/source/boost_1_89_0.zip) | `77bee48e32cabab96a3fd2589ec3ab9a17798d330220fdd8bde6ff5611b4ccde` |
| OpenSSL | 3.5.4 | Apache-2.0；[上游](https://github.com/openssl/openssl/tree/openssl-3.5.4) | `967311f84955316969bdb1d8d4b983718ef42338639c621ec4c34fddef355e99` |

许可原文作为受版本控制文件保存在 `src/protocol/licenses/`，setup 同时生成 `.deps/workflow/licenses/` 供发布脚本复制。cpr 主许可证明确排除 test 子目录；该目录采用 GPLv3，许可保留于 `cpr-test-license.txt`。应用不构建或链接 cpr 上游测试/Mongoose/GoogleTest。

libcurl 显式启用 HTTP/HTTPS、OpenSSL 和 c-ares；关闭 libpsl、Cookie 持久化、压缩库、HTTP/2/3、libssh2、IDN2 和 curl WebSocket。这样不需要 Meson，也不通过禁用 TLS 验证省略依赖。HTTP 和 WSS 默认验证证书链及主机名，读取 Windows ROOT 存储；`caFile` 可使用局部 PEM 信任文件，不修改系统根存储。WS 使用 Beast，禁用压缩。应用层 body 与 WS 完整消息有独立大小上限。

构建辅助依赖只存在 `.deps/workflow/`，不链接或随程序部署：Git for Windows 的 MSYS Perl；Locale::Maketext::Simple 0.21（Perl 许可，归档 `b009ff51f4fb108d19961a523e99b4373ccf958d37ca35bf1583215908dca9a9`）；ExtUtils::MakeMaker 7.76（Perl 许可，归档 `30bcfd75fec4d512e9081c792f7cb590009d9de2fe285ffa8eec1be35a5ae7ca`）；Strawberry Perl 5.40.4.1 portable 中的纯 Perl Pod 模块（各上游 Perl 许可，归档 `aa9052ac082c8a8a0f952823b3f4f0bf9c1d0f84bbe0b3dfe0aa94291d6b1045`）。脚本固定并核验这些构建源，不向系统安装 CPAN 模块。

协议组件没有新增部署 DLL：仍需 Qt6Core 及 MinGW 的 `libgcc_s_seh-1.dll`、`libstdc++-6.dll`、`libwinpthread-1.dll`。实际静态 TLS 产物与运行库哈希由 `.deps/workflow/deployment-manifest.json` 生成；发布副本应采用发布脚本的路径清理规则。Qt Network 与 SSL 后端只用于独立回环验证，协议客户端不依赖 Qt TLS 后端或开发 PATH 下的 OpenSSL DLL。
