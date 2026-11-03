# C1-C1c-9 Lua 隔离与事务化执行：正式设计

2026-11-03。本设计先于生产代码修改（C1-C1c-9a），基于对当前源码的实际调查，调查证据与入口清单见 [C1-C1c-9 迁移记录](subtitle-edit-migration-c1c9.md)。
本文件只规定架构和协议；9b–9e 实施时如需收紧，记入迁移记录，不在此回写成“已完成”。

## 1. 目标与非目标

目标：每次会修改字幕的 Lua 执行都只读写一个独立、完整、由该任务独占的工作模型；宿主在主线程对结果做身份、修订、结构和历史校验后，一次性发布到**同一文档**，失败、取消、异常、过期时活动工程完全不变；原有 Lua API、多 Undo 点历史和选择返回语义保持不变。

非目标：Lua 沙箱化（文件、进程、剪贴板、日志等外部副作用不随字幕事务回滚）；比较窗口、Apply Engine、Context::ass 最终私有化；修改 ASS 格式、Undo 历史格式、`signal.h`；增加第三方依赖；增加新的 Lua API。

## 2. 当前事实（设计依据，均来自源码）

1. `LuaAssFile` 的 `lines` 是指向**活动** AssFile 节点的指针表；Script Info 惰性复制；任何写入（`subs[i]=`、insert、append）都新建节点，**已存在节点在 Lua API 中从不被原地修改**。替换会使该行获得新的行 ID（`AssDialogue` 默认构造分配新 ID），这是现有可观察行为。
2. 宏 `run` 在 `DialogProgress` 的后台线程执行，活动模型在主线程事件循环继续存在期间被工作线程读取。
3. `LuaToAssEntry` 解析 dialogue 的 `extra` 时直接对**活动** AssFile 调用 `AddExtradata`——发生在工作线程、在 Validate/IsActive/配置等只读上下文的 `parse_karaoke_data` 中，并且在脚本失败或取消后不回退。
4. `set_undo_point` 仅在自上次以来有修改时记录一个检查点（指针表副本 + 修改类型 + 描述）；`ProcessingComplete` 对每个检查点依次“整表重排活动容器 + `AssFile::Commit`”，最后对剩余修改以宏显示名 Commit。没有任何写授权在执行前取得；Commit 内的 `OnCommit` 才请求 `TryWrite`，拒绝时已改写活动容器（部分写入）。
5. `OnCommit` 规则：描述为空且历史非空时不入栈也不推进 `commit_id`；Lua 的 amend id 恒为 -1，故不发生合并；每次入栈前对空 Styles/Events 补默认样式/行；超过 `Limits/Undo Levels` 时丢弃最旧；`App/Auto/Save on Every Change` 时可能每步保存。
6. 历史条目在 Commit 时捕获当时的选择（脚本运行前的选择）和文字选区；脚本返回后 `SetSelectionAndActiveIDs` 只更新栈顶条目的选择/活动行。
7. Lua 运行时（`lua_State`）按脚本常驻、跨调用复用；全局变量可保存 `subs` 对象。现有 `references` 计数与 `GetObjPointer(allow_expired=false)` 已让过期对象报错 “Subtitles object is no longer valid”，仅 `__gc` 允许过期访问。
8. 导出过滤器 `ProcessSubs` 作用于 `AssExporter::Export` 的局部副本，不修改活动模型；其配置函数在主线程以只读方式绑定活动模型。

## 3. 执行模型

### 3.1 三类绑定

| 绑定 | 入口 | 数据源 | 线程 | 写入 |
| --- | --- | --- | --- | --- |
| ReadOnlyLive | Validate、IsActive、过滤器配置 | `subtitleAccess->Read()` 的 const 活动模型，调用期间同步读取 | 主线程，同步 | 禁止。`parse_karaoke_data` 改用一次性暂存 Extradata 目标，不再写活动模型 |
| WorkModel | 宏 `run` | 本任务独占的完整工作模型 | 工作线程执行，主线程准备/发布 | 只写工作模型 |
| DetachedFile | 导出过滤器 `run` | 调用方传入的独立 AssFile（导出副本） | 工作线程 | 保持原行为：直接写该独立文件，无 Undo |

只读绑定不复制模型，避免 Toolbar/菜单每次空闲刷新都深复制整个文件；它只能在主线程同步调用期间存在，调用结束即过期（沿用 references 机制）。

### 3.2 工作模型

宏开始时，主线程依次：

1. 检查宿主线程、非安装/非 RecoveryBlocked、无活动比较；预检是否存在与 Lua 冲突的写任务/Interaction（存在则直接拒绝启动，避免浪费执行后再丢弃）。
2. 捕获 `RevisionToken`（含 Project Identity、Document Epoch、Generation）。
3. `subtitleAccess->Snapshot()` 取得深复制：Info/Styles/Events/Attachments/Extradata/`next_extradata_id`/Properties，行 ID 保持。
4. 同步构建 `work 节点 → live 节点` 映射（Styles 按位置，Events 按位置并校验 ID 相等），供发布时保留未修改节点身份。
5. 以 ID 记录原选择与活动行，并在工作模型上换算成脚本使用的行号（与原算法相同的 `Info.size()+Styles.size()+Row+1`）。
6. 捕获 `aegisub.file_name()`、`aegisub.project_properties()` 所需的值（工作模型已含 Properties；文件名另存），工作线程不再读取活动 AssFile/SubsController。

工作模型及其全部节点、检查点、暂存删除节点由一个 `LuaMacroJob` 拥有。`LuaAssFile` 只借用它，并在完成/取消时把内部模型指针置空；Lua 的过期访问仍先由 references 检查拒绝，即使将来遗漏检查也不会触及已释放内存。

不复用 `AssFile` 复制构造的 ID 重新分配语义，也不共享任何可变节点：工作模型与活动模型之间只有“映射表”这一只读关联，映射只在主线程发布时、且修订令牌仍有效时使用。

### 3.3 Lua API 兼容

字段名、类型、默认值、索引、`n`/`#`/ipairs、delete/deleterange/insert/append、`script_resolution`、`parse_karaoke_data`、`set_undo_point`、错误文本、选择/活动行返回值、进度和取消接口全部不变。实现上只把 `LuaAssFile` 的数据源从 `AssFile*`（活动）换成工作模型或 const 活动模型，算法原样保留，包括：

- 替换行获得新 ID、未改行保留 ID；
- `extra` 中非字符串键被忽略、同行重复键由 Lua 表语义决定；
- Script Info 惰性复制、跨组插入位置算法；
- 允许悬空样式引用、允许改名样式而不改引用（原 API 不校验，宿主不得新增拒绝）。

## 4. 检查点与历史

### 4.1 检查点

`set_undo_point(desc)` 记录 `{desc, modification_type, lines 指针表, info 副本状态}`，与原实现相同的“无修改不记录”规则。因 Lua 不原地修改节点，指针表可共享不可变节点，代价为 O(n) 指针而非深复制。

### 4.2 发布前历史准备

脚本成功返回后，在主线程按原 `ProcessingComplete` + `OnCommit` 的顺序**模拟**每一步，只生成数据，不碰活动模型：

| 原步骤 | 准备结果 |
| --- | --- |
| 检查点 k 的整表重排 | 状态 S_k：Info、Styles、Events（值）、Attachments（不变）、Extradata（最终工作模型值，与原实现中工作线程早已写入活动模型的完整 Extradata 一致） |
| OnCommit：描述为空且历史非空 | 不生成历史条目，不推进 commit_id |
| OnCommit：补默认样式/行 | 对 S_k 补默认节点（新 ID，与原实现每次 Commit 新分配一致） |
| OnCommit：入栈 | 用现有 `UndoInfo` 字段格式生成条目：描述、commit_id、S_k、选择=脚本前选择 ID、活动行、文字选区 |
| Undo Levels 截断 | 预先计算需要从栈底退役的条目 |
| 最终剩余修改 | 以宏显示名生成最后一步，规则同上 |

`UndoInfo` 只增加一个由已准备值构造（含描述和文字选区）的内部构造函数，字段与恢复逻辑不变。Undo/Redo 恢复、合并规则和其他编辑入口不改。最终选择/活动行在发布后写入栈顶条目，等价于原来 `OnSelectionChanged/OnActiveLineChanged` 只更新栈顶。

### 4.3 与原行为一致性和需确认差异

完全一致：步数、每步描述、每步恢复的完整数据、空/重复 Undo 点、取消或失败不增加历史、Redo 顺序、脏状态。

有意差异（需负责人复核后才实施，见第 8 节）：

1. 通知次数：原实现每步一次 `AnnounceCommit`，新实现发布后一次（类型为各步掩码按位或；有 COMMIT_NEW 时为 COMMIT_NEW）。观察者只看到最终模型，不再看到中间状态。
2. `Save on Every Change`：原实现可能每步保存；新实现在全部历史发布后按同一条件最多保存一次，文件内容等于最终状态。
3. 宏显示名为空且有修改：原实现改写活动容器却不 Commit（无通知、无历史、行号未重排）。新实现仍不生成历史条目，但必须发出通知并重排行号，否则观察者会继续引用已退役节点。

## 5. 同文档受控发布协议

Lua 发布不是模型安装：不推进 document_epoch，不重建工程身份，不清 Undo/Redo 以外的状态（Redo 按原 Commit 规则清空），不走 FileOpen/MRU。由 `SubsController` 内的私有 `LuaPublication`（不可由业务构造）执行。

### 5.1 状态

| 状态 | 说明 | 退出 |
| --- | --- | --- |
| Running | 工作线程执行 Lua；不持 WriteLease、不持门禁/模型互斥锁 | Completed / Cancelled / Failed |
| Completed | 主线程读取返回值，准备历史、最终节点表、选择、退役列表；全部可能分配/抛出的工作在此完成 | Validating / Discarded |
| Validating | 身份与结构校验（5.2） | Publishing / Rejected |
| Publishing | 唯一提交点：只做 noexcept 交换/splice/整数赋值 | Notifying |
| Notifying | 普通 Commit 语义的一次通知、选择安装、栈顶选择更新、可选保存 | Done |
| Rejected / Discarded / Cancelled / Failed | 活动工程完全不变；释放工作模型、检查点、暂存节点、Lua 引用 | 终止 |

### 5.2 发布前校验

宿主线程；Project Identity、Document Epoch、Generation 与启动时令牌完全相同（不允许以新令牌替换）；无安装/RecoveryBlocked；无 ComparisonSession；无其他写登记或 Interaction；最终行 ID 唯一且 > 0；每个节点组别与所在容器一致、无重复节点；映射复用的 live 节点 ID 与工作节点一致；Extradata 以活动 Extradata 为前缀、ID 严格递增且唯一、`next_extradata_id` 不回退、所有行的 ExtradataIds 均存在；选择/活动行 ID 存在于最终模型。样式引用不作为拒绝条件（原 API 允许悬空引用）。

授权必须原子：门禁新增最小内部能力“仅当修订等于期望令牌、且无其他写/会话登记时取得独占写登记”，避免“先检查再取得”之间被其他线程插入登记。该能力只给可信宿主组件，不公开为普通业务接口；取得自身登记不得使期望令牌失效（在同一临界区内完成比较与登记）。

### 5.3 提交点与通知

提交点之前已准备：最终 Info 向量、Styles/Events 节点序列（未修改者复用 live 节点，新节点来自工作模型并从中摘下）、Extradata 向量、历史条目列表、需退役的 live 节点和历史条目、Redo 退役列表、行号、选择 ID 集合、代数推进量（预先检查耗尽）。

提交点：推进 Generation 一次 → swap Info/Extradata/`next_extradata_id` → 重建 intrusive Styles/Events 链表 → splice 历史 → 更新 commit_id 系列 → 重排 Row。全部 noexcept；退役节点由 `LuaPublication` 持有到通知与选择安装结束。

通知：设置 notifying，按普通 Commit 语义发一次 `AnnounceCommit`；然后 `SetSelectionAndActiveIDs`。普通 Commit 观察者异常按现有 Signal 语义传播（与 C1-A 至今所有普通编辑一致）：此时模型与历史已一致发布，不是部分写入；不承诺回滚观察者副作用，也不把它升级为 RecoveryBlocked（RecoveryBlocked 仅用于文档安装）。退役节点在异常展开后才释放，保证未刷新的观察者没有悬空节点。

## 6. 生命周期、并发与取消

- 同一 `LuaScript` 同时只能有一个运行中的任务：运行期间同一 `lua_State` 的 Validate/IsActive 不得在主线程重入（现有 Toolbar 空闲刷新可能触发，属于既有并发风险），返回“不可用/未激活”而不触碰 Lua；脚本 Reload/Destroy 在任务结束前拒绝或推迟。
- 运行期间发生 Load/Close/New/工程切换/其他写入：令牌失效，结果被拒绝；宿主关闭时任务在模态循环结束后丢弃结果，不访问已销毁 Context（任务只持有弱引用凭据）。
- AutoSave 只读取活动模型快照，工作线程不再访问活动模型，二者无数据竞争。
- 取消：开始前取消不运行；运行中合作式取消（`aegisub.progress.is_cancelled`/`aegisub.cancel()`）；脚本完成但用户已点取消时丢弃结果；均不留 Undo、不改活动模型。不检查进度的死循环仍无法中断——不强杀线程，也不中断 Lua，作为限制记录。
- C++/Lua 边界：保留 `exception_wrapper`；对未包装的闭包（delete/insert/append 等）在实现批次补齐包装，使 C++ 异常不跨越 Lua C 边界，错误信息保持原文。
- 外部副作用（文件、剪贴板、日志、状态栏、对话框）不属于字幕事务，执行即生效，不回滚。

## 7. 测试与性能要求

先在**未改生产代码**时用真实 Lua 运行时、真实 `LuaCommand::operator()`、SubsController 和 Selection 编写行为刻画测试（多 Undo 点、空/重复 Undo 点、ID 变化、Extradata、选择返回），锁定旧实现的期望；迁移后同一组测试必须通过，第 4.3 节的有意差异单独断言。其余场景（异常、取消、过期、并发生命周期、过期 userdata、保存重开、仓库自带 Automation 脚本加载与运行）按迁移记录的测试计划执行。性能分阶段计时（准备/执行/转换/校验/发布）并记录峰值与历史内存，规模 1000/5000/10000 行及大量 Extradata/Styles。

## 8. 负责人确认与实施结果

2026-11-03 负责人以“continue”批准进入 9b，以下四项按推荐方案实施：

1. 第 4.3 节三项有意差异（单次通知、按最终状态最多保存一次、空显示名时仍通知并重排行号）。
2. 运行期间同一脚本的 Validate/IsActive 返回不可用，Reload 拒绝并记录错误。
3. 启动时存在写登记、Interaction 或比较会话即拒绝运行；发布时在独占借用内再次校验令牌。
4. 只读上下文中 `parse_karaoke_data` 使用一次性暂存文件，不再写活动 Extradata。

实施中按真实行为又确认三处旧缺陷，作为修复而非兼容保留（详见迁移记录）：在首次修改 Script Info 之前设置的 Undo 点，撤销后 Script Info 全部丢失；失败/取消后 Extradata 残留；字幕对象方法抛出的 C++ 异常在脚本中只显示为“C++ exception”。

授权实现采用已有能力而非新增门禁接口：发布前先取得 C0 ComparisonSession 独占借用（它不推进代数，且排斥任何写登记），再核对启动时令牌；提交点用该会话的 `AdvanceRevision` 推进一次代数。第 5.2 节“新增原子能力”因此不需要。
