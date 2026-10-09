# 剩余验收执行指南

本轮用户确认目前只有本机。验收按可用条件推进：本机固定负载、实际磁盘、生产顺序计时器、三块现有显示器，以及子进程内受控阻塞注入。物理串口、两机2.5G、干净Windows和原Qt Socket程序对照需要额外条件。具体结果以 [补充验收报告](reviews/remaining-acceptance.md) 和机器回执为准。

## 本机复验

工具链接 `build/workflow-product` 中 `test_http_projects` 使用的同一套生产静态库，生成独立探针，不重新实现通信、断言或顺序运行器。需要已构建的Qt6.8.3/MinGW版本、Python3.11及以上。长测试是显式入口，不加入普通CTest。

```powershell
# 全部顺序执行：实际10分钟截止、UDP/TCP各5分钟、跨屏、UI停顿、受控DNS/文件写入阻塞、等待诊断和GUI后台线程退出延迟。
python scripts/validate-local-acceptance.py --suite all

# 分项执行。
python scripts/validate-local-acceptance.py --suite deadline
python scripts/validate-local-acceptance.py --suite soak --duration-seconds 300
python scripts/validate-local-acceptance.py --suite dpi
python scripts/validate-local-acceptance.py --suite overload
python scripts/validate-local-acceptance.py --suite faults
python scripts/validate-local-acceptance.py --suite waits
python scripts/validate-local-acceptance.py --suite ui-exit
```

默认在 `build/local-acceptance/<时间>/` 保存日志和JSON，探针EXE/对象及采集保存在 `build/acceptance-probes/<输出目录名>/`，都保留旧证据；指定输出目录必须为空。TCP和UDP顺序运行，避免把并行诊断负载混入基线。5分钟两项采集需要约1.7GB有效负载空间，另有记录和元数据开销。运行前检查空间；长时间复验按时长增加容量。

UDP为1472字节×1000报/秒，TCP为4096字节×1000测试帧/秒，全部开启真实工作台及二进制记录，每32MiB轮转。TCP接收块保留实际边界，文件校验器在跨块/跨文件流上重组测试帧。每个测试帧包含大端序号和确定性字节模式，独立读取所有采集文件核对每一字节、UDP边界、DONE尾部及元数据。

发送器在独立线程运行，GUI使用正常的 `QApplication::exec()`，进度由后台线程原子写入。持续负载固定在当前屏幕；跨屏专项每10秒把自己的工作台窗口移动至下一块实际显示器，并记录窗口与屏幕的DPR；Qt缩放环境变量清空。它不改Windows显示设置。UI事件循环用10ms心跳观测，并每7秒实际点击一次暂停/恢复显示，工具采用最大延迟200ms作为本机基线门槛；这不等于测量所有用户操作延迟。记录暂停/停止的函数调用与最终收尾时间分开报告。RSS按秒记录，显示与记录队列检查预算。

主动UI停顿专项在GUI线程暂停500ms，检查独立发送、原始接收和实盘记录是否继续推进，最终仍逐字节校验采集。注入的500ms故障单列报告；正常心跳在注入结束后重新开始计时。

断言路径和截止计时仍调用生产实现。截止探针安排128步、每次真实HTTP响应延后47秒，生产 `600000ms` 计时器在请求中途停止；再观察15秒，检查晚到回调、报告稳定性及资源释放。Qt普通计时器有调度精度，回执记录实际墙钟值。

DNS与文件阻塞探针只修改其独立子进程的导入地址表：分别拦截指定测试主机的 `getaddrinfo` 和QtCore对 `PBCAP001` 文件的 `WriteFile`，等待事件直到显式释放。观察销毁3秒预算、释放后的安全收尾，以及不释放阻塞工作线程时子进程是否仍在15秒内退出。这是可重复的依赖API阻塞注入，不是制造真实系统永久故障。

GUI线程退出专项 `ui-exit` 只在自己的子进程识别空间查询工作线程，并拦截其 `_endthreadex`，延迟1200ms。工作台观察5秒，检查线程在持续运行期间复用、GUI最大心跳与窗口销毁均小于200ms、查询确实执行、没有通信启动；再让注入的线程退出完成。原始探针保留 `measured`，驱动按明确条件产生passed/failed。源码与最初旧/新生产实现对照见 [RA-UI01修复报告](reviews/ui-latency-fix.md)。该项不是异常真实OS永久故障模拟，也不修改部署DLL或其他进程。

## 物理串口（需要设备）

先接入有明确规格的USB串口或本机串口，准备对端/回环和硬件流控所需接线。记录设备、驱动、串口号、接线、支持的波特率及流控。用发送含序号与校验的已知二进制数据验证TX/RX一致性，分别检查端口占用、断开/重新接入、停止、切换、错误可见性及设备支持的高波特率。115200bps连续运行1小时；对照原始采集字节而非显示行。实际拔插/流控不能由无COM设备的错误测试替代。对应FR-001～003、AC-001。

## 两机2.5G（需要第二台机器及链路）

执行 [高速矩阵](project-factory/10-high-throughput-plan.md)。记录两端CPU/驱动/网卡链路速度、MTU/offload、电源、磁盘、包长/PPS、方向、UI/记录条件。先用既有 `portbridge_bench.exe --help` 确认参数。

接收机先启动，绑定真实网卡IP并留出启动与排空窗口；发送机使用接收机真实IP。以下仅是单项命令模板，IP需替换，速率需按实际矩阵设定；未接好设备时不执行。

```powershell
# 接收机：总时长覆盖发送与尾部排空。
.\portbridge_bench.exe --mode receive --protocol udp --bind <接收机IP> --port 19090 --duration 90 --payload 1472 > receiver.json
# 发送机：接收端显示ready后启动。
.\portbridge_bench.exe --mode send --protocol udp --host <接收机IP> --port 19090 --duration 60 --rate 10000 --payload 1472 > sender.json
```

一发送端测试使用 `sender.json` 的实际 `tx_completed_frames` 对照接收端 `rx_unique_frames`，同时核对checksum/format、late-window、乱序/重复、截断、传输错误和排空。接收端未提供 `--expected` 时，单份报告只能判断已观察范围内缺口，不能判断末尾丢失；不能把requested rate或admitted frames当实际完成。TCP换协议并验证业务帧重组。全部UI/记录、多客户端与双向条件另按矩阵运行。headless工具声明UI和记录关闭，其结果不能代替工作台持续录制验收。

## 干净Windows（需要独立干净环境）

在独立干净Windows环境复制当前ZIP与SHA256，核对后解压；不安装Qt开发环境，不借用开发机PATH。启动HTTP/WS/工作流和原始通信页，完成真实本地服务往返、退出与再次启动，检查平台插件/串口模块/运行库、中文路径及权限错误。记录系统版本、哈希、进程退出码和截图。当前本机Windows Sandbox入口不存在；VMware虚拟网卡存在也不能证明已提供可用的干净Windows虚拟机。

## 原Qt Socket丢失及历史偶发等待

需要原程序源码/版本、发送端负载与实际完成数、两端日志及抓包，按TCP业务帧与UDP数据报分别对照。历史采集测试曾在15000ms后完成至约18900ms；其1028次微小文件轮转涉及文件创建、刷新与原子元数据提交。新诊断会记录完整轮转时间，不把一次复跑通过当作历史根因已证实。

快速重连诊断故意连续执行1000次start/stop，观测有界清理队列的明确拒绝和清理后的恢复。历史connected失败未保存当时lastError，因此可复现的保护路径仍是根因候选，不能据此断言原失败已定位或已修复。
