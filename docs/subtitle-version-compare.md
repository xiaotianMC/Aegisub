# 字幕版本比较：阶段一

本阶段只有比较、读取和依赖分析，没有菜单、比较窗口、应用字幕、编辑锁或保存行为。
实现基准为 `e5db6862d765dd523da705c44ea76420c8f29681`。

## 接口与调用顺序

1. 宿主线程使用现有 `SubtitleFormat::GetReader` 选择读取器。
2. `subtitle_compare::ReadBaseline(reader, path, fps, encoding, cancel)` 读取独立文件。
   返回 Completed、Cancelled、NeedsEncoding 或 Failed；只有 Completed 携带文件。
   编码不明确时不自行弹窗，交由宿主提供编码后重试。
   TXT、MicroDVD、Matroska 仍使用已有导入参数/FPS/轨道选择流程，必须在宿主线程调用。
3. `ProjectDocument(const AssFile&, fields, cancel)` 生成不持有行指针和内部行 ID 的值模型。
   当前内存字幕使用 `All`，基线使用读取结果的字段能力；不能按当前保存扩展名抹掉内存编辑。
4. `agi::ass::compare::Compare` 接收两个 Document，返回全部显示项。
   Added 表示向当前添加基线行，Deleted 表示删除当前行；Modified 表示可对应的字段变化。
   Unchanged 可用于上下文显示，没有可应用变化。
5. `AnalyzeDependencies` 只分析指定基线行的样式、未知附加数据及绑定组成员数量。
6. `ResolveExtras` 严格校验 ID 并解析实际键值；`PlanExtraMapping` 只给出数字 ID 分配计划。
   此计划不授权导入未知数据，不决定部分绑定组的保留/解除/重建规则。

核心模型、匹配和附加数据计划在 `libaegisub/ass/version_compare.cpp`，只依赖标准 C++。
AssFile 适配和样式分析在 `src/subtitle_compare.cpp`；原始毫秒由 `Time::GetMilliseconds()` 提供。

## 比较规则

- 字段能力取双方交集。SRT/MicroDVD/TTXT 只有 Text、Start、End；TXT 为 Text、Actor、Comment。
  SSA 不将生成的 Layer 或缺乏可靠来源的 Extradata 当作待覆盖字段。
  不支持的字段不产生修改；阶段二应用时必须保留当前值。
- ASS Text 按原字符串比较，空格、换行转义及覆盖标签均是内容。
  样式分析使用临时标签解析结果，不重新生成原文。
- 相同边界序列可以直接显示对应，包括重复行，因为它们没有写入差异。
- 其余内容进行固定三轮分段扫描：唯一完整字段、唯一严格时间区间、唯一完整文本。
  每轮只扫描已定锚点间的不相交间隙。
- 交叉候选的双方都标记 Crossing，不任意选一条；时间和文本指向不同对象标记
  ConflictingEvidence；重复候选标记 Duplicate；无证据标记 NoReliableMatch。
- 匹配原因是可检查的规则，不是对历史身份的证明或概率。
  时间和文本同时变化且无证据时，保留新增与删除。
- 新增位置记录为原当前序列间隙 `[0, size]`。同一间隙按基线顺序输出。
  无可靠锚点时这个边界只是显示/规划信息，阶段二必须先确定产品定位策略。

设总行数为 N，输入字段编码总大小为 S。固定轮次索引期望 O(N+S)，锚点排序
O(N log N)，额外空间 O(N+S)；unordered_map 的恶意碰撞不具备该最坏时间保证。
不建立二维矩阵，不递归扫描不断缩小的区间。取消抛出 Cancelled，不返回半份结果。

## 读取与依赖边界

`SubtitleReadOptions` 默认保持原读取行为。比较读取启用 preserve_whitespace 和取消检查；
ASS/SRT/MicroDVD/TXT 保留正文空白，结构控制行仍按格式规则识别。
Matroska 比较读取保留毫秒，普通导入沿用原有精度；容器能力由实际轨道类型报告。
WebVTT 没有读取器，本阶段没有新增格式支持。

样式分析输出 Existing、Importable、Conflict、Missing、Ambiguous。
查找沿用现有不区分大小写规则；同名定义按解析字段比较，未使用可能过期的序列化缓存。
Importable 仅表示基线存在当前缺失的定义，不执行复制。

附加数据检测缺失/重复 ID、每行重复键。不同文件中的相同键值相等，ID 不参与比较。
未知键标记 UnknownDependency；绑定键标记 BindingPolicyRequired，并报告基线、所选和当前成员数。
不解析不透明 value 中的数字，不声称已验证跨行或外部依赖。

## 验证方式

构建目标：`gtest-main`、`subtitle-compare-tests` 和 Windows 应用程序。
核心 GTest 覆盖全部字段、重复字幕、1ms、中文空格、ASS 标签、交叉/冲突、空文件、
增删顺序、未知附加数据、严格 ID 校验和纯映射计划，并执行随机序列覆盖不变量检查。
性能测试输出 1000/5000/10000 行的单改动、重排、完全不同、全重复、批量文本修改耗时。

应用层测试使用生产 AssFile、ASS/SRT 读取器和样式解析器，验证独立加载、字段能力、
空白保留、取消/失败不发布文件、样式与部分绑定分析不修改输入。
宿主配置只在测试进程中提供，不启动编辑器。

本次 Windows x64 / MSVC Release 实际结果：核心及现有回归共 419 项全部通过，
其中新增核心测试 19 项；生产读取器及依赖分析集成测试 10 项全部通过。
Windows 程序构建及 Meson 的隔离集成测试入口通过。
Core 也通过不含 wxWidgets 的 GCC C++20 独立语法检查。

本机 CPU 为 Intel Core i5-12500H。以下为一次完整测试运行中的耗时（毫秒），
不是跨设备承诺或统计中位数；没有测量峰值内存。

| 行数 | 单处修改 | 全序重排 | 完全不同 | 全重复且相同 | 批量文本修改 |
|---|---:|---:|---:|---:|---:|
| 1000 | 1.71 | 6.39 | 8.18 | 2.76 | 11.01 |
| 5000 | 10.60 | 36.99 | 34.58 | 8.69 | 30.12 |
| 10000 | 17.65 | 82.91 | 78.35 | 19.76 | 87.52 |

交互式 TXT 导入、需要用户选 FPS 的 MicroDVD 和 Matroska 轨道选择路径已编译，
本次没有自动操作这些已有对话框；其他桌面平台仍需 CI 验证。

安全运行现有及新增测试：

```text
python tests/run_isolated.py build/tests/gtest-main.exe --output-root build/compare-regression
python tests/run_isolated.py build/tests/subtitle-compare-tests.exe --output-root build/compare-regression
```

每次新建目录并复制输入夹具；不运行旧 setup.bat/setup.sh，不删除或修改源码夹具。
也可通过 Meson 运行 `subtitle comparison integration`；该目标同样使用隔离目录。

## 阶段二前置条件

仍须确认部分绑定组、未知数据依赖、自动保存冲突、无锚点插入位置和性能验收设备。
然后实现工程级写入门禁、候选构建、受控原子发布、撤销/重做和精确选择恢复。
本阶段没有实现这些行为，也没有加入任何磁盘自动保存策略。
