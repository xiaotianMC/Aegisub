# C1-C1c-10 活动工程 Owner 私有化与可变访问收口

状态：**C1-C1c-10a–10d 代码与 Linux 验证完成；Windows x64 Release 未执行，人工验收未执行。** 因此完成条件 13（Windows 构建）尚未满足，不能宣布 C1-C1c-10 完成。第 1–7 节为 10a 调查记录（实施前源码），第 8 节起为实施结果。

## 1. 基线、环境与保护文件

- 2026-11-03，Linux 工作区 `/home/bily/project/Aegisub`，分支 `codex/style-name-width`。
- HEAD `678d98586590df7ef9cd22950e81aa827e635d05`（docs(compare): report C1-C1c-9 lua isolation and transactional execution），与 `origin/codex/style-name-width` 完全一致（0 ahead / 0 behind）。远端名为 `origin`，任务中的 `fork` 在本克隆中不存在。
- `684b4a3`（C1-C1c-8 Windows 复核）和 `678d985`（C1-C1c-9）均在当前分支历史中。
- 暂存区为空；工作区无修改、无未跟踪文件（.gitignore 之外）；无 stash。
- 构建：Ubuntu 26.04，GCC 15.2.0，Meson 1.10.1，Ninja，wxGTK 3.2，`build/` 为 release 配置；`ninja -C build` 显示与 HEAD 一致无需重编。
- 历史文档实际名称：`subtitle-edit-migration-c1c1a/b/c.md`；任务列出的 `c1c1.md`、`c1c.md` 在本仓库不存在，未创建替代文件。已阅读 isolation、c1a、c1b、c1b1–3、c1c1a–c、c1c1c8、c1c1c8-windows、model-installation(-design)、lua-isolation-design、c1c9。

保护文件（开始时）：

| 文件 | 状态 | SHA256 |
| --- | --- | --- |
| `本版与原版差异清单.md` | 已跟踪，与 HEAD 一致 | `6a922d69925ba61d4511fc29a712a3291e0034abdc2a39c8539b672bd0cf35d2` |
| `subprojects/checkasm.wrap` | **本克隆不存在**（仅 Windows 工作区） | 无法计算 |
| `docs/subtitle-edit-migration-c1c1.md` | **本克隆不存在**（仅 Windows 工作区） | 无法计算 |

### 1.1 开始前的实测测试基线（HEAD 678d985，Linux）

每个 ordinary 测试独立进程、`tests/run_isolated.py` 隔离目录，与 C1-C1c-9 方法一致：

| 程序 | 结果 |
| --- | --- |
| gtest-main | 433/433 |
| subtitle-compare-tests | 55/55 |
| ordinary-edit-tests | 123 通过 / 162；38 项为已记录 wxGTK 限制（37 项 `CaptureMouse` 中止 + 1 项 GTK 文件对话框 `real_style_import_and_delete…`）；**1 项间歇失败** |

间歇失败：`lua_macro.isolation_comparison_session_rejects_the_whole_result_without_partial_write` 断言全部通过，但进程约 1/3 概率在退出时 SIGSEGV（6 次重跑 2 次）。gdb 回溯：

```
agi::log::LogSink::Log  <- agi::log::Message::~Message  <- libass msg_callback
<- ass_set_fonts <- libass::CacheFonts()::lambda <- agi::dispatch::Queue::Async 工作线程
```

即 libass 字体缓存后台任务在进程退出、日志 sink 已销毁后仍在记录日志，属于既有测试进程退出竞态，与字幕模型所有权无关（该测试本身运行仅约 9 ms，最容易在 CacheFonts 完成前退出）。本阶段不修复，不计入通过数，在最终结果中单列。

## 2. Context::ass 原始架构

```cpp
struct Context {
    SubtitleEditGate subtitleEditGate;
    // Legacy mutable access: migrate business entry points before enabling comparison.
    std::unique_ptr<AssFile> ass;                       // 公开、可变
    std::unique_ptr<SubtitleProjectAccess> subtitleAccess;
    ...
};
```

- 所有权：`Context` 构造时 `make_unique<AssFile>()`，析构时最后释放；从不 reset/重新赋值。Load/Close/Undo 均对同一对象 `swap` 或重建内容，因此**活动 AssFile 的对象身份等于其所属 Context 的身份**。
- `SubtitleProjectAccess(*ass, gate)` 与 `SelectionController(*c->ass, gate)` 在 Context 构造时绑定同一对象，内部私有保存 `AssFile*`。
- 活动 AssFile 附带 `edit_gate`（只在活动对象上设置，拷贝/快照不复制），据此 `AddUndoManager` 对活动模型拒绝。

## 3. Context::ass 全量引用清单（生产代码）

“授权”列：Lease = WriteLease/WriteScope；HR = 私有 HistoryRestore；Inst = SubtitleModelInstallation 会话；Lua = Lua 发布借用（ComparisonSession）。

| # | 文件:行 | 函数 | 类型 | 访问形式 | 授权 | 生命周期 | 风险 | 迁移 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | context.h:50 | 声明 | — | 公开 `unique_ptr<AssFile>` | — | Context | **任何包含 context.h 的代码可取得可变引用** | 私有化 |
| 2 | context.cpp:37–38 | Context 构造 | 创建 | 所有权 | Owner | Context | 无 | 保留（成员） |
| 3 | context.cpp:43 → selection_controller.cpp:30 | `SelectionController(Context*)` | 绑定 | `*c->ass` | Owner 装配 | Context | 公开构造函数读取 Owner | Context 直接传 `*ass`，删除 Context* 构造 |
| 4 | main.cpp:441 | `UnhandledException` | 读指针 | `!c->ass` | 无 | 同步 | 只判空（恒非空） | 删除判空 |
| 5 | project.cpp:224 | `CloseSubtitles` 后续 | 读 | `Properties` 拷贝 | 无 | 同步 | 只读但经可变指针 | `Read()` |
| 6 | project.cpp:410 | `DoLoadVideo` | 读 | `GetYCbCrMatrix()` | 无 | 同步 | 同上 | `Read()` |
| 7 | project.cpp:426 | `DoLoadVideo` | 读 | `LoadSubtitles(const AssFile*)`（提供者自行深复制） | 无 | 同步 | 同上 | `&Read()` |
| 8 | dialog_export.cpp:119、150 | 导出对话框构造 | 读 | `Properties.export_*` | 无 | 同步 | 同上 | `Read()` |
| 9 | dialog_style_manager.cpp:457 | 样式管理器 | 读 | `Properties.style_storage` | 无 | 同步 | 同上 | `Read()` |
| 10 | dialog_selection.cpp:196 | 选择行对话框 | 读 | `AssFile const*` 传给 `process` | 无 | 同步 | 同上 | `&Read()` |
| 11 | subs_controller.cpp:87、102–113 | `UndoInfo` 构造 | 读 | 容器拷贝 | Owner 内部 | 同步 | 无 | Owner 内部访问器 |
| 12 | subs_controller.cpp:122–153 | `UndoInfo::Apply` | 写（重建全部容器 + Commit） | 引用 | HR + 精确修订 + 非通知 | 同步 | 无（C1-C1c 第三批已收口） | Owner 内部访问器 |
| 13 | subs_controller.cpp:194 | `Installation::Pending` | 私有观察者准备 | `AnnounceCommit` | Inst | 安装期 | 无 | 同上 |
| 14 | subs_controller.cpp:233–296 | `Abort/Install/Confirm/Retry` | 写（installing/notifying 标志、`swap`） | 引用 | Inst（ComparisonSession + 文档身份 + Metadata 修订） | 安装期 | 无（C1-C1c-7/8） | 同上 |
| 15 | subs_controller.cpp:306 | SubsController 构造 | 私有历史连接 | `PushState` | Owner | 控制器 | 无 | 同上 |
| 16 | subs_controller.cpp:441 | `Save` 失败回滚 | 写 `Properties` | 赋值 | Owner 内部（恢复保存前值） | 同步 | 无 | 同上 |
| 17 | subs_controller.cpp:535 | `CanSave` | 读 | `AssFile*`→`CanSave(const AssFile*)` | 无 | 同步 | 无 | `Read()` |
| 18 | subs_controller.cpp:577–581 | `OnCommit` | 写（补默认样式/行） | 容器 | 已在 Commit 授权内（OnCommit 先检查 TryWrite） | 同步 | 无 | Owner 内部访问器 |
| 19 | subs_controller.cpp:614、630 | `Undo/Redo` | 读 `IsNotifying` | — | — | 同步 | 无 | 同上 |
| 20 | subs_controller.cpp:740、917 | `PrepareLuaPublication/PublishLua` | 读/写（发布交换） | 引用 | Lua（令牌 + ComparisonSession 借用） | 同步 | 无（C1-C1c-9） | 同上 |

结论：**私有化前，`Context::ass` 外部（非 SubsController）生产使用全部是只读**（#3–#10），但它们通过可变指针读取，且 `context.h` 让任何代码都能写入。

## 4. 其他可变访问路径调查

### 4.1 能持有活动 AssFile 非 const 引用/指针的根

| 根 | 可见性 | 用途 | 是否外泄 |
| --- | --- | --- | --- |
| `Context::ass` | 公开 | 见第 3 节 | **是（本阶段关闭）** |
| `SubtitleProjectAccess::State::file` | 私有嵌套结构 | WriteAccess/Interaction 受控写；`Read()` 只返回 `AssFile const&`；`Snapshot()` 返回独立深复制 | 否：`WriteAccess::Borrow`、`Check()` 均私有；公开 API 只收值类型 |
| `SelectionController::file` | 私有 | 选择校验（const 遍历）、通知阶段标志、`Properties.active_row`（UI 状态） | 否：只返回 `AssDialogue const*`；`PreparedSelection::owner` 私有 |
| `SubsController`（经 `context->ass`） | 私有成员函数 | 第 3 节 #11–#20 | 否：公开 API 只返回值、令牌、事务对象、`ProjectProperties` 值 |
| `SubtitleModelInstallation`（经 `owner.context->ass`） | 内部类 | 安装/恢复 | 否 |
| `LuaSubtitlePublication::Data`（SubsController 内部） | 内部 | Lua 发布 | 否 |

### 4.2 逐项排查（任务 4.2 / 4.3）

| 检查项 | 结果 |
| --- | --- |
| `AssFile&` / `AssFile*` 形参函数 | `ResampleResolution`、`update_*`（视频属性）、`StyleRenamer`、`SetInfoIfDifferent`、`AssStyleStorage::ReplaceIntoFile`、`SetBinding`、导出过滤器 `ProcessSubs`、`MatroskaWrapper::GetSubtitles`、`SubtitlesProvider::LoadSubtitles`：全部调用点传入的是 `staged.file`（`SubtitleValueEdit`/`SubtitleDialogEdit` 的独立副本）、`AssFile calculated(Read())`、`Snapshot()`、导出副本、预览 `sub_file` 或视频提供者内部副本。**没有任何调用点传入活动模型。** |
| `shared_ptr/unique_ptr<AssFile>` | `Snapshot()`（独立）、自动保存 `subs_copy`、字体收集 `shared_ptr<AssFile const>`、`SubtitleModelCandidate::file`（安装前独立、`Read()` const）、`LuaSubtitleTransaction::work`（Lua 工作模型，`Work()` 返回 `AssFile&` 是**独立模型的合法可变访问**）、`AsyncVideoProvider::subs`、`SubtitlesPreview::sub_file`、安装 `retired`。均非活动模型。 |
| 公共 mutable getter | 只有 `Context::ass`；`SubtitleProjectAccess::Read()` 返回 const；无其他。 |
| 隐式转换操作符 | `src/` 中无到 `AssFile&/*`、`AssDialogue*` 的转换操作符。 |
| friend | `AssFile` 的 friend：SubtitleProjectAccess、SubsController、SelectionController、SubtitleModelInstallation（访问私有通知/安装标志与历史信号）。Context 无 friend。 |
| 继承暴露 | `SelectionController`、`SubsController` 的 `protected` 只含 `model_observer_lifetime`；无派生类暴露模型。 |
| const_cast | `src/` 中 4 处，均与字幕模型无关（libass 内存、选项、wx 字符串、DialogManager）。 |
| Selection 可变行 | 已在 C1-C1b 收口：`ReadActiveLine/ReadSelectedSet/ReadSortedSelection` 只返回 const；`Selection = std::set<AssDialogue const*>`；旧 `GetActiveLine/GetSelectedSet` 不存在。 |
| `RowsForWrite` 等旧写接口 | 全仓库无此名称；已由 WriteAccess/Interaction 值接口替代。 |
| Signal 参数 | Commit 通知 `(int, AssDialogue const*)`；`AssFileCommit::single_line` 为 const；选择/活动行通知无载荷；Metadata 通知无载荷。 |
| `mutable` 成员 | 仅 `AssDialogueBlockOverride` 解析缓存（`ass_override.h:71`，派生解析块，不是模型数据）。 |
| 全局缓存/长生命周期对象 | 字幕相关缓存均以行 ID/修订令牌保存（C1-B 迁移）；无全局 `AssFile*`/可变行指针。 |
| Lua/后台任务 | C1-C1c-9 后 Lua 只拿工作模型或 const 源；`auto4_*` 中无 `c->ass`。自动保存、字体收集只拿快照。 |
| 历史恢复 | 私有 `HistoryRestore`，`UndoInfo::Apply` 校验控制器/工程/授权/精确修订/非通知；凭据业务代码无法命名。 |
| 保存清理 | `CleanExtradata` 只作用于 `Snapshot()`。 |
| 模型安装 | C1-C1c-7/8 安装协议，未改。 |

**由类型系统推出的结论**：私有化 `Context::ass` 后，非 const 活动模型只能从 4.1 的私有根取得；除 `const_cast`（不存在，且将由 API 检查禁止）外，业务代码无法得到活动 AssFile、活动容器或活动行的可变引用。剩余的活动写入全部位于 SubsController 内部受信路径和 SubtitleProjectAccess 的受控接口。

### 4.3 测试代码

`tests/tests/ordinary_edit.cpp`（425 处）与 `lua_isolation.cpp`（61 处）直接使用 `c->ass`：约 430 处只读断言，约 97 处写入，分两类：

1. 夹具准备（`LoadDefault`、追加行、`AddExtradata`、`InsertAttachment` 后 `Commit`），在 `TryWrite()` 作用域内执行；
2. **模拟“其他写入者”或旧旁路**（例如在比较会话/Interaction 期间直接改字段并 Commit，验证运行时门禁、过期令牌和 Commit 拒绝）。

第 2 类正是运行时防线的回归测试，必须保留原始可变访问才能表达。`tests/native_preview_regression.cpp` 不在任何构建目标中（手工工具），只读使用 `c.ass`。

## 5. 独立 AssFile 与活动模型的判定

判定依据是**所有权**，不是类型或地址比较：

- 活动模型：恰好是某个 `agi::Context` 构造时创建并私有持有的那个 AssFile（附带 `edit_gate`）。它只能经该 Context 的私有成员到达。
- 独立模型：其余所有 AssFile——`AssFile x;`、`AssFile x(Read())`、`Snapshot()`、候选、Lua 工作模型、导出副本、预览、读取器目标、Undo 内部的值、测试中自建的模型。它们的公开容器、字段、`Commit`、`AddUndoManager`、`Sort`、`swap` 等可变能力保持不变。

因此不需要修改 AssFile 类型语义，也不需要“活动/独立”两套类型。

## 6. 最小 Owner 私有化方案（10b/10c）

1. `Context::ass` 移入 `private`。friend 仅两项：
   - `::SubsController`——活动文档的生命周期 Owner（历史、安装/恢复、Lua 发布、保存回滚都已在其中）；
   - `::LiveSubtitleModelTestAccess`——**仅测试**的 attorney，只在 `tests/support/` 定义。理由见 4.3：运行时门禁回归需要模拟非法写入者。生产代码命名该类型由 API 检查拒绝（第 7 节）。
   不提供任何公开 getter、转换操作符或 `LegacyMutable`。
2. SubsController 内新增**私有静态**访问器 `Live(agi::Context*)`，`UndoInfo`、`SubtitleModelInstallation`（SubsController 的 friend）、Lua 发布统一经它访问；全部原有运行时检查不变。
3. Context 构造直接 `SelectionController(*ass, subtitleEditGate)`；删除 `SelectionController(agi::Context*)`。
4. 第 3 节 #4–#10、#17 改为 `subtitleAccess->Read()`（只读、宿主线程、未关闭检查）。
5. 测试：只读断言改用 `Read()`；仅编译器指出的写入点改用测试 attorney，测试语义不变。
6. 不修改：`signal.h`、Undo 历史格式、Lua API、AssFile 类型语义、安装协议、Selection、WriteAccess/Interaction。

## 7. 编译期防绕过设计

1. **类型约束测试**（`tests/tests/subtitle_owner_access.cpp`，进入 subtitle-compare-tests）：用 C++20 requires 表达式在非 friend 上下文检测，访问违规即为替换失败：
   - `Context::ass` 不可访问；`Context` 无 `Ass()/GetAss()/Model()/File()` 等 getter；
   - `Context`、`SubtitleProjectAccess`、`SelectionController`、`SubsController` 不能转换为 `AssFile&/AssFile*`；
   - `Read()` 返回 `AssFile const&`，容器元素与 `GetStyle` 结果为 const；
   - Selection 读取只返回 const 行，无 `GetActiveLine/GetSelectedSet/RowsForWrite`；
   - `WriteScope/Interaction/WriteAccess/PreparedSelection` 不可默认构造、不可从外部构造，`WriteAccess` 不暴露 `Check/Borrow`；
   - 独立 `AssFile` 仍可写容器、字段、`Commit`、`AddUndoManager`、`swap`，`Snapshot()` 结果可变。
2. **真实编译失败测试**（`tests/owner_access_compile_fail.py`，meson test）：用项目编译器和包含目录逐个编译负例片段，要求失败；同一命令先编译一个对照正例，要求成功，排除因参数错误“假失败”。负例：直接访问 `c.ass`、经 `Read()` 写字段/容器、经 Selection 写行、外部构造 `WriteAccess`/`WriteScope`、调用私有 `SubsController::Live`。（在生产代码中自行定义测试 attorney 类型能编译通过，因此由第 3 项 API 检查拒绝。）
3. **构建期 API 检查**（同一脚本）：扫描 `src/`，要求 `->ass`/`.ass` 成员访问只出现在 `context.cpp` 与 `subs_controller.cpp` 的 Owner 访问器中；`LiveSubtitleModelTestAccess` 只能在 `context.h` 中以 friend 声明出现；`src/` 中不得出现 `const_cast<AssFile` / `const_cast<AssDialogue` / `const_cast<AssStyle`。

运行时检查（工程/文档身份、修订令牌、WriteLease、Interaction、RecoveryBlocked、线程/关闭检查）全部保留，不由编译期约束替代。

## 8. 实施结果（10b/10c/10d）

调查没有发现需要改变 Undo/Redo、Lua API、AssFile 类型语义或宿主所有权架构的阻断，因此按第 6 节方案直接实施。

### 8.1 所有权调整与 Owner 私有化

```cpp
struct Context {
    SubtitleEditGate subtitleEditGate;
private:
    friend class ::SubsController;                 // 文档生命周期 Owner
    friend struct ::LiveSubtitleModelTestAccess;   // 仅测试 attorney
    std::unique_ptr<AssFile> ass;
public:
    std::unique_ptr<SubtitleProjectAccess> subtitleAccess;
    ...
```

- 所有权不变：活动 AssFile 仍由 Context 构造创建、析构释放，成员声明顺序不变，初始化/析构顺序不变。
- `SubsController` 新增**私有** `static AssFile& Live(agi::Context const*)` / `AssFile& Live() const`，是 `src/` 中唯一一处 `c->ass` 访问（由 API 检查锁定为恰好一处）。`UndoInfo`（嵌套类）、`SubtitleModelInstallation`、`LuaSubtitlePublication`（SubsController 的既有 friend）全部经它访问；原有运行时检查逐字保留。
- Context 构造直接以 `SelectionController(*ass, subtitleEditGate)` 装配；删除 `SelectionController(agi::Context*)` 公开构造函数（它是 Owner 之外第二个读取 `c->ass` 的入口）。
- 没有新增公开 getter、转换操作符、`LegacyMutable`、全局锁或 `const_cast`。

### 8.2 只读接口

业务代码唯一的读取入口仍是 `subtitleAccess->Read()` / `Read(RevisionToken)`：返回 `AssFile const&`，容器、行、样式、Info、Extradata、Attachments、Properties 均为 const，`GetStyle` 返回 const 指针、`iterator_to` 返回 const_iterator，并检查宿主线程与访问对象存活。由编译期测试逐项断言（第 9 节）。Read 不创建写入权限，不推进修订。

迁移到 `Read()` 的生产调用点：

| 文件 | 原代码 | 现代码 |
| --- | --- | --- |
| `src/project.cpp` | `context->ass->Properties`、`->GetYCbCrMatrix()`、`LoadSubtitles(context->ass.get())` | `subtitleAccess->Read()…`、`LoadSubtitles(&Read())`（提供者原本就深复制 const 输入） |
| `src/dialog_export.cpp` | `c->ass->Properties.export_filters/encoding`（前者绑定 const 引用） | `Read().Properties…`，导出过滤器名改为值拷贝，不跨调用持有引用 |
| `src/dialog_style_manager.cpp` | `c->ass->Properties.style_storage` | `Read()` |
| `src/dialog_selection.cpp` | `process(…, con->ass.get())` | `process(…, &Read())`（形参原本即 `AssFile const*`） |
| `src/subs_controller.cpp` | `CanSave(context->ass.get())` | `CanSave(&Read())` |
| `src/main.cpp` | 崩溃保存前 `!c->ass` 判空 | 删除（恒非空；后续 `Save` 已经走快照） |

### 8.3 受控写入接口（未改动，逐项复核）

| 写入能力 | 获得方式 | 失效 | 工程身份 | 文档身份 | 修订 | 异常安全 |
| --- | --- | --- | --- | --- | --- | --- |
| `WriteScope` / `WriteAccess`（普通编辑） | `TryWrite()`：通知阶段拒绝、比较会话拒绝；`Edit(lease)` 校验租约归属 | 租约释放或 `Close()` 后再次调用抛出 | 租约绑定工程 | Load/Close 推进修订使旧令牌失效 | `UpdateValues/ReplaceRows` 需令牌 | RAII 释放；值接口在改动前完成校验 |
| `Interaction`（音频/视觉/对话框） | `TryBeginInteraction(targets)` | `End()`、析构、令牌过期 | 同上 | 同上 | 每次写入 `Current()` | RAII |
| `HistoryRestore`（Undo/Redo） | 仅 SubsController 在已取得写授权和 `history_action` 后私有构造 | 单次动作 | 校验控制器/工程 | 精确修订 | 精确修订 | 失败不移动历史 |
| `SubtitleModelInstallation`（Load/Close） | SubsController 内部；需要 ComparisonSession 独占 | Committed / RecoveryBlocked | 文档身份 + Metadata 修订 | 有 | 有 | 发布前失败不改活动模型；发布后失败进入 RecoveryBlocked（不是 Commit 成功） |
| Lua 发布 | `BeginLuaTransaction`→`Prepare`（ComparisonSession 独占借用 + 令牌复核）→`Publish` | 发布或析构 | 有 | 有 | 有 | 提交点之后无分配、不抛出 |
| `OnCommit` 补默认样式/行、`Save` 失败回滚 Properties、选择 `active_row` | Owner/SelectionController 内部维护 | 同步 | — | — | — | 已有 |

### 8.4 独立 AssFile 兼容性

未改变 `AssFile` 的任何声明。独立模型（`AssFile x`、`AssFile x(Read())`、`Snapshot()`、候选、Lua 工作模型 `Work()`、导出副本、预览、读取器目标）仍可写全部容器与字段、`Commit`、`AddExtradata`、`SetScriptInfo`、`swap`、`LoadDefault`、`Sort`、`AddUndoManager`。编译期测试以同一组 concept 断言“独立模型全部为真、活动视图全部为假”；运行时测试验证快照/副本修改不影响活动模型与修订令牌。

### 8.5 迁移文件清单

生产代码：`src/include/aegisub/context.h`、`src/context.cpp`、`src/subs_controller.h`、`src/subs_controller.cpp`、`src/selection_controller.h`、`src/selection_controller.cpp`、`src/project.cpp`、`src/dialog_export.cpp`、`src/dialog_style_manager.cpp`、`src/dialog_selection.cpp`、`src/main.cpp`、`src/subtitle_project_access.h`（注释）。

测试与构建：新增 `tests/support/live_model_test_access.h`、`tests/tests/subtitle_owner_access.cpp`、`tests/owner_access_check.py`；修改 `tests/tests/ordinary_edit.cpp`、`tests/tests/lua_isolation.cpp`、`tests/native_preview_regression.cpp`（不在构建目标中，已用生产编译参数 `-fsyntax-only` 验证）、`tests/meson.build`、`meson.build`。

测试迁移方法：先把全部 `c->ass->` 机械替换为生产只读路径 `LiveRead(*c)`，再**只把编译器报错的行**改为 `LiveWriteForTest(*c)`。结果：HEAD 中含 `c->ass` 的 470 行里，378 行只读、改走 `Read()`；92 行含写入（夹具准备与模拟非法写入者），改走测试 attorney；两者之和等于原行数，无遗漏。测试断言与语义不变。

未修改：`signal.h`、Undo 历史格式与合并规则、ASS 格式、Lua API、Selection、WriteAccess/Interaction、安装与恢复协议、第三方依赖。没有实现 Apply Engine 或比较窗口。

## 9. 编译期防绕过测试

### 9.1 类型约束（`tests/tests/subtitle_owner_access.cpp`，subtitle-compare-tests）

在非 friend 上下文用 requires 表达式/类型特征断言：

- `Context::ass` 不可访问（含 const Context）；Context、SubtitleProjectAccess、SubsController 无 `Ass/GetAss/Model/LiveModel/File/Subtitles/Mutable/LegacyMutable`；`SubsController::Live` 静态与成员形式均不可访问。
- Context、SubtitleProjectAccess、SubsController、SelectionController、WriteAccess、WriteScope、Interaction、SubtitleModelCandidate 均不能转换/构造为 `AssFile&`/`AssFile*`。
- `Read()`/`Read(token)` 返回 `AssFile const&`；Events/Styles/Info/Extradata/Attachments/Properties 均为 const；`GetStyle`、`iterator_to` 为 const 版本。
- 七组修改能力（改行字段、改结构、改 Properties/Extradata、Commit、AddExtradata/SetScriptInfo、swap/LoadDefault/Sort、AddUndoManager）对活动视图**全部不成立**，对独立 `AssFile` **全部成立**；`Snapshot()` 返回可变 `unique_ptr<AssFile>`。
- Selection 只返回 const 行，无 `GetActiveLine/GetSelectedSet/SetActiveLine`。
- `WriteAccess/WriteScope/Interaction/PreparedSelection` 不可凭空构造，`Interaction` 不可复制，`WriteAccess::Check` 不可调用，无 `RowsForWrite/MutableRows`，候选 `Read()` 不可写。

### 9.2 真实编译失败与 API 检查（`tests/owner_access_check.py`，meson test `live model owner access`）

从 `compile_commands.json` 借用 `src/subs_controller.cpp` 的生产编译命令（同编译器、同包含目录、同宏、同 PCH），对 19 个负例逐个 `-fsyntax-only`（MSVC 为 `/Zs`），要求全部失败；同一命令下的对照正例（`Read()`、独立副本写入与 Commit、快照写入、Selection 读取、`TryWrite`+`Edit` 写字段）必须编译通过，以排除参数错误造成的“假失败”。

负例：`c.ass`、`c.ass.get()`、`AssFile& = Read()`、经 Read 改行字段/清容器/Commit/AddExtradata/改 Properties/改样式、经 Selection 改活动行/选中行、旧 `GetActiveLine`、凭空构造 WriteAccess/WriteScope/Interaction、调用 `SubsController::Live`（静态/成员）、`RowsForWrite`、对活动视图注册历史。

API 扫描（去除注释与字符串字面量后）：`src/` 中 `->ass`/`.ass` 成员访问只允许 `subs_controller.cpp` 的一处 Owner 访问器，且该访问器形式固定；Context 的 friend 必须恰为 `::SubsController` 与 `::LiveSubtitleModelTestAccess`；`ass` 必须位于 private 段；测试 attorney 在 `src/` 中只能出现在 context.h，且只在 `tests/support/live_model_test_access.h` 定义一次；`src/` 中禁止对任何字幕模型类型 `const_cast`（`const_cast` 是合法 C++，无法靠编译失败拦截，因此由扫描负责）。

**守卫自身的有效性验证**（变异测试，验证后已恢复，`cmp` 确认字节一致）：

1. 在 context.h 中把 `ass` 移回 `public`：扫描报 “not declared in a private section”，`context_ass_member`、`context_ass_pointer` 两个负例“意外编译通过”，脚本退出码 1。
2. 临时在 `src/` 放入含 `c->ass->Events.clear()`、`const_cast<AssFile&>`、测试 attorney 名称与 `"%s.ass"` 字符串的探针文件：三条规则全部报错，字符串字面量未误报。

## 10. 运行时授权测试

保留全部既有运行时门禁测试（租约越界、异工程、跨线程、过期令牌、通知期重入、比较会话拒绝、安装 RecoveryBlocked、Lua 过期/拒绝等）。新增 `ordinary_edit.owner_privatization_keeps_one_live_document_read_only_path_and_detached_copies`（真实 Context + 真实编辑框事件）：

- `Read()` 返回的正是 Owner 持有的同一对象；Undo/Redo/Close 前后对象身份不变；
- 快照与副本（含副本 Commit）修改后，活动文本、行 ID、Undo 描述不变，且之前捕获的修订令牌仍有效（独立修改不推进活动修订）；
- 工作线程调用 `Read()` 抛出；
- Undo/Redo 经私有历史恢复正常，且使旧令牌失效；Close 使旧令牌失效并替换内容。

## 11. 测试结果（Linux，Ubuntu 26.04，GCC 15.2，wxGTK 3.2，Meson release）

方法同 1.1 节：每个 ordinary 测试独立进程，`tests/run_isolated.py` 隔离目录；最终一次完整构建 `ninja -C build` 0 错误。

| 程序 | 开始前基线（HEAD 678d985） | 本阶段完成后 |
| --- | --- | --- |
| gtest-main | 433/433 | 433/433 |
| subtitle-compare-tests | 55/55 | **56/56**（+1 `subtitle_owner_access` 运行时用例；其类型约束在编译期全部成立） |
| ordinary-edit-tests | 123 通过 + 1 间歇退出崩溃 + 38 已知限制 | **125 通过** + 38 已知限制（+1 新 Owner 运行时测试；间歇项本次通过） |
| luajit-52 | 通过 | 通过 |
| live model owner access（meson test） | — | 通过：对照正例编译、19 个负例全部被编译器拒绝、API 扫描通过 |
| 合计（实际运行并通过的 gtest 用例） | 611 | **614** |

- 通过/失败集合与基线逐项比对：除新增两项外完全一致；38 项不可运行项与基线、C1-C1c-9 记录相同（37 项 `CaptureMouse` 断言中止 + 1 项 GTK 文件对话框），未计入通过数。
- 27 项 `lua_macro` 全部通过。
- 1.1 节的间歇退出崩溃是既有问题，本次恰好通过；它**不能**作为稳定通过计入，Windows/再次运行时可能复现。
- 日志在会话临时目录，未纳入 Git。

## 12. Undo/Redo、Save/Load/Close 与 Lua 兼容性

- Undo/Redo：`UndoInfo` 与 `HistoryRestore` 代码仅把 `c->ass->` 换为 `SubsController::Live(c).`，检查顺序、历史格式、合并规则未改；既有普通编辑、多步、绑定组、模型安装、关闭重开、Undo/Redo 后继续编辑测试全部通过（第 11 节）。
- Save/AutoSave/CleanExtradata：仍只作用于 `Snapshot()`；Save 失败回滚改经 Owner 访问器。Load/Close：安装协议原样，仅访问器替换。保存后重开与 Metadata 往返测试通过。
- Lua：`auto4_*` 未修改；27 项 `lua_macro` 测试（多 Undo 点、userdata 过期、同文档发布、比较期拒绝、运行中 Close、7 个自带脚本加载与 strip-tags）全部通过。

## 13. 完整旁路审计（实施后）

全仓库重新检索（`src/` 与 `tests/`）：

| # | 审计项 | 结果 |
| --- | --- | --- |
| 1 | 外部普通业务代码取得活动模型可变访问 | 无：`src/` 中 `->ass` 仅 Owner 访问器一处（编译期 + 扫描双重锁定） |
| 2 | 旧 mutable getter | 无（`RowsForWrite/LegacyMutable/GetActiveLine/GetSelectedSet` 均不存在；同名的 `GetActiveLineRange` 是音频时间范围值） |
| 3 | 隐藏可变别名 | 返回非 const `AssFile&` 的函数只有私有 `SubsController::Live` 与 Lua 独立工作模型 `Work()` |
| 4 | 全局活动 AssFile 可变指针 | 无 |
| 5 | 未登记后台写入 | 无：自动保存、字体收集只拿快照；Lua 工作线程只写独立工作模型 |
| 6 | Lua 直接访问活动模型 | 无（`auto4_*` 中无 `ass` 成员访问；Validate 只拿 const 源） |
| 7 | Selection 可变行 | 无（编译期负例） |
| 8 | 历史恢复 | 仅私有 `HistoryRestore` |
| 9 | 保存清理 | 只清理快照 |
| 10 | 模型安装 | 既有协议，未改 |
| 11 | 独立 AssFile 可变 | 是（编译期 + 运行时） |
| 12 | 合法受控编辑入口 | 全部回归通过 |

仍存在的活动模型可变访问点及其授权：见 8.3 表；它们全部位于 `SubsController`（经私有 `Live`）、`SubtitleProjectAccess`（私有 `State::file`，经 WriteAccess/Interaction）、`SelectionController`（私有 `file`，仅通知标志与 UI 的 `active_row`）、`Context` 构造。测试 attorney 只存在于测试目标。

## 14. 保护文件、Git 与远端

- 保护文件：`本版与原版差异清单.md` 开始与结束 SHA256 均为 `6a922d69925ba61d4511fc29a712a3291e0034abdc2a39c8539b672bd0cf35d2`，未修改、未暂存；`subprojects/checkasm.wrap`、`docs/subtitle-edit-migration-c1c1.md` 在本克隆中不存在，无法核对，本阶段提交不含这两个路径。
- 本阶段本地提交（基于 `678d98586`）：
  - `ef9b66baa` docs(compare): investigate C1-C1c-10 owner access and plan privatization
  - `0cab1df1c` refactor(compare): make the live subtitle model private to its owner
  - `3ebeabd0c` test(compare): guard live model owner access at compile time
  - 本报告的最终提交（docs(compare): report C1-C1c-10 owner privatization）——哈希见交付消息，避免自引用。
- 远端：**未推送**。`origin/codex/style-name-width` 仍为 `678d98586`；本地领先 4 个提交。未 reset/clean/stash/rebase/amend/force push。

## 15. 剩余风险与人工验收

剩余风险：

1. **Windows 未验证**：本阶段只在 Linux 实际构建和运行。`owner_access_check.py` 的 MSVC 分支（`/Zs`、去掉 `/Fo /Yu /Fp`、Windows 引号处理）未实际执行；若参数处理有误，对照正例会失败并让测试明确失败，不会产生“假通过”。
2. 测试 attorney 是一个具名 friend。它不能被生产代码合法使用（API 检查），但若有人删除检查脚本，编译器本身不会阻止在生产代码中定义它。
3. `SelectionController` 与 `SubtitleProjectAccess` 仍各持有一个私有可变指针（UI 状态 `active_row`、通知标志、受控写入），由类封装而非 Owner friend 约束；这是既有内部协作者，未扩大。
4. 既有间歇性退出崩溃（1.1 节，libass CacheFonts 日志竞态）未修复。
5. wxGTK 下 38 项 ordinary 测试不可运行（既有限制），这些路径在 Linux 上只做了编译验证。

人工验收（均未执行）：Windows x64 Release 构建与全量测试（含 `live model owner access` 在 MSVC 下）；桌面编辑、音频/视觉拖动、对话框、Lua 宏、Save/Close/重开；崩溃恢复保存（main.cpp 判空删除涉及的路径）。

## 16. 状态区分

| 类别 | 内容 |
| --- | --- |
| 已实现 | Owner 私有化、访问器收口、只读迁移、测试 attorney、编译期/扫描守卫、运行时 Owner 测试 |
| 已测试（Linux 实际运行） | 第 11 节所列；守卫变异测试 |
| 静态审查 | 第 13 节审计；Windows 编译参数处理 |
| 尚未验证 | Windows x64 Release；wxGTK 不可运行的 38 项；人工桌面验收 |
| 不在本阶段 | Apply Engine、比较窗口 UI、C1-C1c 最终隔离审计、libass 退出竞态修复 |
