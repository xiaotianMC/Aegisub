# C1-C1c 最终隔离审计

日期：2026-11-03。审计基线：`33d43ced5`（C1-C1c-10 Windows 复核之后，与 `origin/codex/style-name-width` 一致）。本审计**只读**：未修改生产代码与测试；运行时探针为临时追加的测试，运行后用备份逐字节恢复（`git diff` 为空）并重新构建。

## 结论

1. **授权边界：通过。** 不存在未解释的活动模型写入旁路。活动 AssFile 只能从四个私有根到达（C1-C1c-10，编译期 + API 扫描锁定，Linux/Windows 均验证）；逐个读过的全部写入方法都在首次修改前完成授权与数据校验；后台线程不触及活动模型或 Selection；Lua、自动保存、字体收集、导出只处理独立副本。
2. **失败路径：不通过。** 审计发现 4 个高严重度缺陷：授权正确，但在**失败或 RecoveryBlocked 状态下**会造成已释放内存访问、进程终止或恢复保存丢失（第 3 节 A1–A4）。其中 A1 直接位于将来 Apply Engine 要使用的“提交 → 通知观察者”发布路径上。
3. 因此**尚不具备进入 Apply Engine 的条件**。建议先做一个收口修复阶段（第 6 节），再复审本报告的 A/B 项。

## 1. 方法

隔离边界在 C1-C1c-10 之后由类型系统保证，因此审计问题变为：

| 问题 | 负责 | 结果节 |
| --- | --- | --- |
| 四个私有根（Context/SubsController、SubtitleProjectAccess、SelectionController、安装/Lua 发布）与门禁的每个写入是否先授权、先校验 | 本人逐行阅读 | 2、3 |
| 写能力（WriteScope/WriteAccess/Interaction、暂存辅助类）的持有者、持有时长、线程 | 独立只读子代理 A，结论经本人核实 | 4 |
| 观察者与缓存的行/样式指针在提交、替换文档后是否可能悬空 | 独立只读子代理 B，经本人核实 | 3、4 |
| 异步/后台任务；模型之外的文档状态（Metadata、UI 状态、历史、文件名、磁盘文件）是否绕过门禁 | 独立只读子代理 C，经本人核实 | 3、4、5 |

子代理只拿到源码路径和规则，不拿本人的结论；它们的每条“已确认”都由本人对照源码复核，关键项另加运行时探针。被复核推翻或降级的条目列在第 5 节。

## 2. 门禁与私有根（通过项）

| 组件 | 结论 |
| --- | --- |
| `SubtitleEditGate` | 有写登记时拒绝比较；比较开始或文档纪元变化使租约失效；令牌为弱引用；重绑定失败使门禁保持封闭直至关闭；所有状态转换在短互斥区内，不在锁内回调 |
| `WriteAccess`：`SetDialogueField`、`UpdateRows`、`LinkAdjacent`、`Commit`、`ReplaceRows`、`UpdateValues` | 线程、存活、非通知期、租约令牌检查后才借用；`ReplaceRows`、`UpdateValues` 在第一次写入前完成全部比对与分配，修改循环只做 flyweight 句柄复制和侵入式链表重链 |
| `Interaction`：`UpdateRows`、`UpdateLayout`、`InsertBefore`、`LinkAdjacent` | 先校验全部目标与修订再写；`LinkAdjacent` 要求全文档 ID 集合等于授权目标 |
| `SelectionController` | 只写 ID 集合、通知标志与 UI 的 `active_row`；安装/重绑定期间拒绝 |
| `SubtitleModelInstallation`（Load/Close） | 按 C1-C1c-7/8 协议：ComparisonSession 独占、文档身份 + Metadata 修订校验、逐观察者隔离与确认、失败进入 RecoveryBlocked 并保留旧节点 |
| Lua 发布 | 令牌 + ComparisonSession 借用；提交点后无分配；观察者失败时退役节点停放到控制器，不释放 |
| AutoSave / 字体收集 / 导出 / 视频渲染 | 只处理快照或独立副本；AutoSave 完成回调检查生命周期凭据 |
| 比较引擎输入 | `subtitle_compare.cpp` 只读 Events、Styles、Extradata，不读 Properties/Metadata |

## 3. 缺陷（按严重度）

标注：**运行时确认** = 临时探针实测；**阅读确认** = 本人对照源码确认机制；**疑似** = 机制成立但触发链或后果未完全证明。

### A. 高

**A1 普通提交时观察者抛异常 → 已删除节点被释放，而未收到通知的观察者仍持有指针（已释放内存访问）。** 运行时确认机制，阅读确认后果。

- `SubtitleModelObservers::operator()`（`subtitle_model_observers.h:136`）按注册**逆序**调用且不隔离异常。较新的观察者（对话框、工具）先被调用；一旦它抛出，较老的 BaseGrid、SubsEditBox、VideoController、SelectionController 都不会被调用。
- 被删除的节点由 `SubtitleRowListEdit::retired`、`SubtitleValueEdit::retired`、`UndoInfo::Apply` 的局部 `old` 持有，异常展开时随之释放。
- 门禁不封闭，普通写入立即恢复；`OnExceptionInMainLoop` 弹出的消息框会运行事件循环，于是 `BaseGrid::OnPaint` 会读 `index_line_map`（`base_grid.cpp:378`）里已释放的行。样式变体：SubsEditBox 的 `active_style`、样式管理器的 `styleMap`。
- 探针（真实 `edit/line/delete` + 一个较老的计数观察者 + 一个较新的抛异常观察者）：删除已提交（剩 1 行，Undo 描述为 “delete lines”），较老观察者调用 0 次，`NeedsRebinding=0`，`TryWrite` 成功。
- 与安装、Lua 发布的处理不一致。C1-C1c-9 曾把“通知后观察者异常不升级为 RecoveryBlocked，与普通编辑一致”列为已接受行为，但没有记录其内存后果。**Apply Engine 若沿用普通提交发布，会继承此缺陷。**

**A2 RecoveryBlocked 状态下加载音频 → 旧音频提供者已释放，AudioController 仍持有其裸指针。** 运行时确认。

- `Project::DoLoadAudio`（`project.cpp:365`）先替换 `audio_provider`（旧对象被销毁），再 `SetPath → UpdateRelativePaths → SetMetadata`；后者在 `NeedsRebinding()` 时抛出，于是 `AnnounceAudioProviderModified` 不执行。
- 探针：进入 RecoveryBlocked 后调用 `LoadAudio`，抛出 “Metadata is sealed during installation”，`providerReplaced=1`，`announced=0`。
- 同一模式还会让视频、时间码、关键帧加载停在半途：`?audio/?video` 令牌和 MRU 已改，通知未发（阅读确认）。没有命令检查 RecoveryBlocked 状态。

**A3 RecoveryBlocked 状态下关闭导出对话框 → `std::terminate`。** 阅读确认。`DialogExport::~DialogExport`（`dialog_export.cpp:186`）调用会抛出的 `SetMetadata`，而析构函数隐式 `noexcept`。打开导出对话框的命令（`command/tool.cpp:64`）不检查状态。

**A4 崩溃恢复保存在异常文档状态下失效。** 阅读确认。`AegisubApp::UnhandledException`（`main.cpp:439-450`）对每个窗口调用 `Save` 且没有 try/catch；`Save` 在安装中或 RecoveryBlocked 时抛出（`subs_controller.cpp:411`），后续窗口的恢复保存全部丢失——恰好是最需要恢复的时刻。若处理函数在工作线程触发，`Snapshot()` 的线程检查也会抛出（疑似，取决于 wx 在哪个线程调用）。

### B. 中

| # | 缺陷 | 证据 | 状态 |
| --- | --- | --- | --- |
| B1 | 每次 Undo/Redo 泄漏全部样式节点：侵入式链表 hook 为 `auto_unlink`，`Styles.clear()` 只解链不释放（对照 `~AssFile` 使用 `clear_and_dispose`），被替换的节点无其他所有者 | `subs_controller.cpp:130`、`ass_entry.h:50` | 阅读确认 |
| B2 | Undo/Redo 不具异常安全：先 splice 历史栈、改 `commit_id`，再进入 `Apply`；`Apply` 先清空活动容器再逐个 `new`，`bad_alloc` 会留下未提交的半成品文档；观察者异常时与 A1 相同 | `subs_controller.cpp:120-160、618-643` | 阅读确认；观察者变体运行时探针：模型与历史栈一致，门禁未封闭 |
| B3 | `Interaction::Commit` 在 `file->Commit()` 之后才做嵌套修订检查，会在提交已成功的情况下抛出；调用者（AudioEditSession、SubtitleDialogEdit）随即把成功当作失败 | `subtitle_project_access.cpp:176-179` | 阅读确认，触发条件疑似 |
| B4 | Lua 导出过滤器在工作线程运行时没有设置 `SetRunning`，工具栏 `OnIdle` 的 Validate/IsActive 可能在主线程并发使用同一个 `lua_State`（C1-C1c-9 #6 对导出路径未修复） | `auto4_lua.cpp:1150`；`SetRunning` 只出现在宏路径 `:967-977` | 阅读确认 |
| B5 | `AsyncVideoProvider::GetSubtitles` 在主线程使用工作线程私有的 `subs`/libass 轨道，没有 `worker->Sync`（`GetFrame` 有） | `async_video_provider.cpp:97-109` | 阅读确认；独立副本上的数据竞争，与活动模型无关 |
| B6 | 写租约跨模态对话框持有且未使用 Interaction：颜色选择器（在模态循环内写入）、字体、覆盖粘贴、分辨率重采样、样式导入，以及若干短消息框。失败方式是关闭的（打开期间拒绝开始比较，不会写入过期数据）。颜色选择器取消时无条件 `Undo()`：由于 `TryWrite` 会把同一个活动租约交给任何调用者，若模态期间有其他处理函数提交，取消会撤销那一次提交 | `command/edit.cpp:357-414`、`:536`、`:966`；`command/tool.cpp:100`；`dialog_style_manager.cpp:692` | 持有：阅读确认；取消撤销错项：疑似（上游逻辑） |
| B7 | Lua 工作线程辅助函数未同步地读取宿主的 `filename`、`c->path`、视频/音频控制器；导出过滤器调用 `project_properties()` 会在工作线程触发线程检查而失败 | `auto4_lua.cpp:134-367` | 阅读确认（功能性）；竞争为疑似 |

### C. 低 / 设计前提

| # | 内容 |
| --- | --- |
| C1 | `SetMetadata`、`SetAutomationSetting`、`UpdateUIState`、`Save`、`RequestAutoSave` 不受比较会话约束。当前比较引擎不读 Properties，因此不影响比较输入；但这是 **Apply Engine 和比较界面的前提条件**：会话期间 Save 会移动 `filename`、`?script` 和已保存基线 |
| C2 | Save 失败回滚直接赋值 `Properties`，Metadata 修订与监听者不同步；“每次修改都保存”的 I/O 失败从 `OnCommit` 抛出时，历史已更新但 `AnnounceCommit` 被跳过 |
| C3 | `UpdateRows`/`ReplaceRows` 不校验 Style 是否存在、ExtradataIds 能否解析；`UpdateValues` 仅在含 `COMMIT_EXTRADATA` 时校验新引用 |
| C4 | `SubtitleModelInstallation::Begin` 不检查 `IsNotifying()`：选择通知内的模态框期间，排队的拖放打开可以启动 Load。未发现解引用链 |
| C5 | RecoveryBlocked 期间新增订阅抛出（打开样式管理器、样式助手、翻译助手、切换视觉工具等会失败），这是健壮性问题 |
| C6 | 多步写入跨多次授权调用不是原子的（音频 `Update → LinkAdjacent → Commit`）；`combine_lines`/`recombine` 在 Commit 前先 `Select()`，观察者会先看到未提交的结构 |
| C7 | 多个命令对 `ReadActiveLine()` 不做空检查（继承自上游） |
| C8 | 手动保存后，自动保存失败会把自动保存基线回滚到更早版本，只导致一次多余的自动保存 |
| C9 | 既有：libass CacheFonts 进程退出竞态（Linux 间歇，Windows 5/5 未复现） |

## 4. 写能力持有者与观察者概览

- 约 106 个 `TryWrite` 调用点（21 个文件）：命令全部是块内局部持有；未发现被保存到成员、lambda 或异步续体中的 WriteScope。长期能力只有 Interaction，位于 VisualToolBase、AudioEditSession、SubtitleDialogEdit。
- 所有生产观察者都使用带弱生命周期的具名注册，只在 `ticket.Current()` 时确认；安装期间的确认行为全部正确。`AddInstallationObserver` 在 `src/` 中没有调用者。
- 缓存的活动节点指针：BaseGrid `index_line_map`、SubsEditBox `active_style`、样式管理器 `styleMap`。正常路径下它们都在节点释放前刷新；唯一的悬空途径是 A1（漏掉通知）。其余缓存保存的都是值、ID 或独立快照。
- 子代理 A 记录了另外两处持有方式：样式助手（非模态）在空闲时保持 Interaction，会无限期阻止比较；`time/*` 命令启动的音频编辑在命令返回后仍保持租约。两者都是关闭失败（拒绝比较），列为比较界面的设计输入。

## 5. 经复核推翻或降级的条目

| 子代理结论 | 复核结果 |
| --- | --- |
| 颜色选择器在回调中解引用活动选择集合 | `ReadSelectedSet()` 返回的是副本；每次解引用前都有 `revision.Current()` 检查，而节点只会经由推进修订的提交释放 → 不成立；只保留“取消时撤销错项”（B6） |
| 样式管理器 `styleMap` 会悬空或命中地址复用 | 任何替换样式节点的提交都带 `COMMIT_STYLES` 或 `COMMIT_NEW`；Lua 发布复用未改样式的节点；`UpdateLayout` 原地交换数据 → 正常路径不可达，只在 A1 下成立 |
| Undo 观察者失败导致历史栈错位 | 探针显示提交先于异常发生，历史栈与模型一致；剩余问题归入 A1 与 B2 |

## 6. 建议的修复阶段（进入 Apply Engine 之前）

1. **统一提交发布的失败语义（A1、B2）**：普通 Commit 与 Undo/Redo 在通知结束前保持退役节点存活（同 Lua 的停放方式，或交给门禁的 `retired`）；观察者失败时进入与安装相同的可恢复封闭状态，而不是立即重新开放写入。`Apply` 改为“先准备、后交换”，并释放样式节点。
2. **RecoveryBlocked 的外围（A2、A3、C5）**：Project 的 Load*/Close* 先检查状态再改动成员；`SetMetadata` 失败不能留下半更新状态；析构函数中不再抛出；在 UI 命令层统一禁用有副作用的命令。
3. **崩溃恢复（A4）**：逐窗口 try/catch；安装中或 RecoveryBlocked 时改为保存安装前的保留数据或快照。
4. B3–B7 逐项修复；C1 作为 Apply Engine 设计约束写入设计文档。
5. 每一项都配一个回归测试（本审计的探针可直接转为正式测试）。

## 7. 环境与保护

- Linux：Ubuntu 26.04 / GCC 15.2；守卫 `owner_access_check.py` 在 `33d43ce` 上重跑通过。
- 运行时探针 3 个（Undo + 抛异常观察者；RecoveryBlocked + 加载音频；普通删除 + 抛异常观察者），均为临时代码，未提交。
- `本版与原版差异清单.md` SHA256 `6a922d69…cf35d2`，未改变；另外两个保护文件不在本克隆中。
- 本报告为本地提交，未推送。
