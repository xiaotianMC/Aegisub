# C1-C1c-12 比较会话约束项目状态（审计 C1）

本阶段修复 `docs/subtitle-edit-c1c-final-audit.md` 的 C1：比较会话期间，`SetMetadata`、`SetAutomationSetting`、`Save` 和 `RequestAutoSave` 不受约束，Save 还会移动 `filename`、`?script` 和已保存基线。这是 Apply Engine 和比较界面的前提条件。本阶段没有实现 Apply Engine 或比较界面。没有修改 `signal.h`、Undo/Redo 历史格式、Lua API 或保存格式。

## 1. 机制：`ProjectWriteLease`

`agi::SubtitleEditGate` 新增 `TryBeginProjectWrite()`，返回 `ProjectWriteLease`（只能移动，不能复制或默认构造）。它表示对**比较行之外的项目状态**的修改：元数据、自动化设置、保存目标与基线、媒体路径。

- 与 `WriteLease` 一样计入写者，在整个生命周期内排除比较会话。反过来，比较会话（包括安装与 Lua 发布使用的独占借用）或恢复重绑期间无法获取它。
- **不推进修订号**。行没有改变，已捕获的 `RevisionToken`、对话框与交互的基线都不会因此失效。
- **不是行写权限**。它不能转换为 `WriteLease`，也不能传给 `SubtitleProjectAccess::Edit`。这一点有 `static_assert`，还有两个新的真实编译失败用例（`project_lease_edits_rows`、`project_lease_from_gate_edits_rows`）。

`SubtitleProjectAccess::BeginProjectWrite()` 是业务入口：比较期间抛出 `SubtitleProjectBusy`（`agi::Exception`），重绑期间抛出 `logic_error`。

## 2. 各入口的行为

| 入口 | 比较会话期间 | 说明 |
|---|---|---|
| `SetMetadata`、`SetAutomationSetting` | 抛 `SubtitleProjectBusy`，什么都不改、不通知 | 注册覆盖整个通知过程，监听者内无法开始比较。值未改变（no-op）时不拒绝 |
| `SubsController::Save` | 抛 `SubtitleProjectBusy`，在写文件之前 | 注册覆盖整个保存过程（含 FileSave 监听者），保存中途无法开始比较。`subtitle/save` 命令以消息框报告 |
| `RequestAutoSave` | **推迟**：不写文件、不改自动保存基线 | 下一次计时器触发或提交时重试 |
| 媒体（`Load*`/`Close*`/`LoadUnloadFiles`/`ReloadVideo`） | 先报告“正在比较”并拒绝，无任何副作用 | 入口和内部 `Do*` 都在整个变更（包括进度对话框的模态循环）期间持有注册。`LoadList` 只在入口检查，因为列表可能包含字幕，而字幕安装需要独占借用；随后每一步各自注册 |
| 导出 | **允许** | 导出只读文档。记忆编码与过滤器设置改为尽力而为，比较期间跳过 |
| 样式管理器切换目录 | 目录照常显示 | 只是不把选择记入字幕 |
| 自动化脚本列表变化 | 脚本照常加载或卸载 | 列表未写入字幕时给出警告 |
| `UpdateUIState`（滚动、视频位置、缩放、AR） | **允许** | 有意保留：这是视图状态，不属于比较输入，也不应由 Apply 写入；比较界面需要它 |
| `WriteRecoveryCopy`（崩溃恢复） | 允许 | 只读快照，不改变任何项目状态 |

## 3. 测试

- `subtitle_edit_gate.project_write_excludes_comparison_without_changing_the_revision`，以及类型层面的 `static_assert`。
- `subtitle_project_access.metadata_and_automation_settings_are_refused_during_comparison`、`…comparison_cannot_begin_while_metadata_observers_run`。
- `document_lifecycle.comparison_session_freezes_save_target_metadata_and_media`：比较期间 Save、保存命令、AutoSave、加载与关闭音频都不改变文件名、`?script`、元数据、修订和脏状态；导出成功；比较结束后被推迟的 AutoSave 写出。
- `document_lifecycle.comparison_cannot_begin_while_a_save_or_media_change_is_in_progress`。
- 已有测试 `metadata_and_ui_have_separate_revisions_dirty_state_and_readonly_notifications` 原先断言“比较期间可写元数据”，即本阶段要修复的行为。现已改为：视图状态可写，元数据被拒绝，比较结束后其余断言不变。
- 所有者门禁：21 个编译失败用例（新增 2 个），控制片段包含 `BeginProjectWrite`。

另一个已有测试 `ordinary_edit.save_cleans_only_independent_copy_and_keeps_ass_format_precision` 原先在比较会话内保存。现改为：比较期间保存被拒绝且不产生文件，比较结束后再保存；原断言（快照保存不改变行修订、历史和活动 extradata）全部保留。

### 验证结果（Linux，GCC 15.2，wxGTK 3.2）

- `gtest-main`：434/434。
- `subtitle-compare-tests`：60/60。
- `ordinary-edit-tests`：逐项隔离运行，共 172 项。上述保存测试修正后单独通过。其余失败为：
  - 已知的 38 项 wxGTK 限制，与前两阶段相同；
  - 1 项间歇失败：`lua_macro.isolation_validate_parse_karaoke_does_not_write_live_extradata` 测试本身通过，但进程退出时偶发崩溃，5 次中 2 次（退出码 245）。gdb 回溯确认这是既有的 C9：libass `CacheFonts` 工作线程在日志接收器销毁后写日志。与本阶段无关。
- `luajit-52`、`live model owner access`：通过。
- ASan：新增的 2 项 ordinary 测试和 `subtitle-compare-tests`（60/60）均无 AddressSanitizer 错误，没有字幕模型或门禁泄漏。剩余报告来自 `DialogProgress` 创建时的 GTK/Pango 分配，计数与上一阶段相同。

## 4. 风险与未处理项

- 自动化脚本列表与样式目录在比较期间的变化不写入字幕，比较结束后不会自动补写，下一次相关变化时才写入。这是有意的保守选择，已在界面中以警告说明（样式目录除外）。
- Windows/MSVC 未构建。
- B6、B7、C2–C9 未处理，保持审计结论。

## 5. 保护文件

`本版与原版差异清单.md` 开始和结束时的 SHA256 均为 `6a922d69925ba61d4511fc29a712a3291e0034abdc2a39c8539b672bd0cf35d2`。提交只保存在本地。
