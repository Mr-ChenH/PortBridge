# QtNodes / Qt 6.8 QVariant 警告调查

环境：Qt 6.8.3、MinGW GCC 13.1.0、C++17、Release `-O3`。固定 QtNodes 3.0.16 / `7c6341a66a8e46b8988140b9e60d892b6a3560b3`。

原研究日志的 `-Warray-bounds` 来自 `QVariant` 临时对象的 `value<NodeDataType>() &&` 移动提取路径，涉及两个 QString 组成的 48 字节类型。独立两翻译单元最小复现不使用 QtNodes：相同 Qt 库和编译选项下，临时对象提取产生 4 个警告；先保存到 const QVariant 再提取产生 0 个警告。两条路径各执行 200,000 次并核对完整字符串，均成功。这个结果定位了诊断路径，没有运行 UB/地址 sanitizer，也不构成所有内存行为正确的证明。

采用源码级调整：将原来的临时 QVariant 提取改为 const QVariant 值提取，保留端口类型、QString 内容、图模型和绘制接口。没有关闭编译诊断或修改全局优化选项。补丁包含 AbstractGraphModel 的两处模板 getter、DefaultConnectionPainter、DefaultNodePainter 和 DataFlowGraphModel，共 4 个文件、6 个 hunk；不修改自动数据传播或网络行为。

首轮修改三文件后，受影响代码已编译，但剩余 8 个警告来自 DataFlowGraphModel 的相同路径。补齐后 QtNodes target 成功链接；两次编译合计覆盖全部受影响翻译单元，修改后的路径不再产生警告。原始研究日志和第一轮剩余警告日志均保留。完整产品构建及原生拖动、连接、绘制行为仍须在后续集成验收中验证。

可复现补丁：`patches/qtnodes-qvariant-const.patch` 供审核，`.json` 包含原始/修改后 SHA256 和精确替换。`scripts/setup-qtnodes.ps1` 验证固定提交压缩包 SHA256 后调用 `scripts/patch-qtnodes.py`；后者只接受已知原文或已知修改后的文件，原子发布，遇未知源码保留文件并失败。对原始源码副本验证了首次应用与再次执行的幂等性。

证据：`docs/validation/workflow-implementation/qtnodes-warning/` 包含最小源码、编译输出、执行结果、第一轮剩余诊断、最终 target 输出和边界 receipt。构建目录位于忽略的 `build/workflow-warning-probe/`；没有覆写历史研究证据或产品可执行文件。
