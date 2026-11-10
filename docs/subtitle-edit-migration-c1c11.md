# C1-C1c-11 失败路径修复（审计 A1–A4、B1–B5）

本阶段修复 `docs/subtitle-edit-c1c-final-audit.md` 中的全部高严重度项（A1–A4）和中严重度项 B1–B5。每一项都有由审计探针改写而成的回归测试。没有启动 Apply Engine 或比较界面。没有修改 `signal.h`、Undo/Redo 历史格式、Lua API 或保存格式，也没有新增公开可变 getter、`const_cast`、宽泛 friend 或全局锁。

## 1. 修复概览

| 项 | 修复 | 位置 | 回归测试 |
|---|---|---|---|
| A1 | ① `SubtitleModelObservers` 普通分发对每个订阅者单独 try/catch，所有订阅者都调用完后再重抛第一个异常；顺序仍为由新到旧。② 退役节点不再交给调用者，由项目访问层托管。`ReplaceRows`/`UpdateValues` 把节点放入 `pending`（容量预留，转移不抛）。提交成功后节点进入当前最外层写作用域的 `settled`，作用域结束时才释放；提交失败则转入 `parked`，随项目访问层析构释放。`WriteAccess::Commit` 和 `Interaction::Commit` 都经过 `CommitRetiring` | `subtitle_model_observers.h`、`subtitle_project_access.{h,cpp}`、`subtitle_row_list_edit.h`、`subtitle_value_edit.h` | `subtitle_project_access.throwing_newer_observer_still_notifies_older_ones_and_parks_retired_rows`、`…observer_dispatch_rethrows_first_exception_after_all_observers_ran`、`ordinary_edit.throwing_newer_commit_observer_still_reaches_older_observers_and_keeps_deleted_rows_alive` |
| B1 | Undo/Redo 中被替换的样式不再只解链不释放：新状态在独立 `AssFile` 中构建，与活动容器整体交换，旧内容随该 `AssFile` 析构（`clear_and_dispose`）释放。顺带修复了同类泄漏：`Info.push_back(*new AssInfo)` 会在每次 Undo/Redo 时泄漏全部 Script Info，`AssParser::ParseScriptInfoLine` 会在每次读取文件时泄漏全部 Script Info（`Info` 是 `std::vector<AssInfo>`，原写法复制后泄漏原对象） | `subs_controller.cpp`、`ass_parser.cpp` | ASan 下 Undo/Redo 测试无任何 `Ass*` 泄漏帧 |
| B2 | `UndoInfo::Apply` 拆为 `Prepare` 和 `Install`。`Prepare` 完成全部分配与授权检查，失败时模型、历史栈和 `commit_id` 都不变。`Undo`/`Redo` 在 `Prepare` 成功后才 splice。`Install` 只做不抛的容器交换，然后宣告；宣告失败时旧内容停放到 `retired_after_failure`（由 `lua_retired_after_failure` 推广而来，容量预留）。Lua 发布路径的停放也改为预先分配，不再在 catch 中分配 | `subs_controller.{h,cpp}` | `ordinary_edit.undo_with_throwing_commit_observer_keeps_model_history_and_replaced_rows_consistent` |
| A2 | `Project` 的媒体入口在 `IsInstalling()` 或 `NeedsRebinding()` 时先拒绝并报告，不产生任何副作用。覆盖的入口：`LoadAudio/Video/Timecodes/Keyframes`、对应的 `Close*`、`LoadList`、`LoadUnloadFiles`、`ReloadVideo`。内部 `Do*` 和 `SetSubtitlesFilename` 以抛出方式执行同一前置条件，因此 `DoLoadSubtitles` 先尝试时间码/关键帧的路径也不会先替换再失败 | `project.{h,cpp}` | `document_lifecycle.media_changes_are_refused_without_side_effects_while_recovery_is_blocked` |
| A3 | `~DialogExport` 在封存状态下跳过记忆过滤器选择，其余异常记录日志后吞掉；析构函数不再抛出 | `dialog_export.cpp` | `document_lifecycle.export_dialog_closes_without_terminating_when_metadata_cannot_be_stored`（旧代码下复现 `terminate`） |
| A4 | 新增 `SubsController::WriteRecoveryCopy`（`noexcept`）：写出独立快照，不改变文件名、脏状态、元数据和历史，在任何安装状态下可用。崩溃处理改用它，并对每个窗口单独 try/catch | `subs_controller.{h,cpp}`、`main.cpp` | `document_lifecycle.recovery_copy_is_written_in_recovery_blocked_without_touching_document_state` |
| B3 | `Interaction::Commit` 在提交成功后发现意外嵌套修订时不再抛出：返回提交 ID 并结束会话（`Current()` 为 false） | `subtitle_project_access.cpp` | `subtitle_interaction.own_commit_does_not_adopt_unexpected_nested_revision`（已改为新语义） |
| B4 | `LuaExportFilter::ProcessSubs` 使用与宏相同的按脚本运行标志：脚本忙时拒绝（`agi::InvalidInputException`，由导出对话框报告），运行期间宏的 Validate/IsActive/重载被拒绝。`GenerateConfigDialog` 在脚本运行时不再进入 Lua 状态 | `auto4_lua.cpp` | `lua_macro.isolation_export_filter_and_macro_of_one_script_never_share_the_running_lua_state` |
| B5 | `AsyncVideoProvider::GetSubtitles` 整体在 `worker->Sync` 内执行，排在所有已排队的字幕更新之后 | `async_video_provider.cpp` | `visual_edit.subtitle_overlay_is_rendered_on_the_worker_after_queued_updates`（旧代码 3 次运行中 2 次失败；新代码 9 次全部通过） |

## 2. 语义变化（有意）

- **普通观察者分发**：抛异常的观察者不再阻断较旧观察者。异常仍会传播，顺序不变。旧测试 `model_installation_preparation.normal_observer_order_and_exceptions_are_preserved` 断言的是旧的阻断行为，已更新为新语义。
- **退役节点的生命周期**：从调用者对象的生命周期改为“通知成功且最外层写作用域结束”。如果执行了 `ReplaceRows` 但从未提交，节点会保持 pending，直到下一次成功提交或项目析构。这比旧行为（随辅助对象析构立即释放）更保守。
- **`ReplaceRows`/`UpdateValues` 返回 `void`**：原返回值只供调用者持有节点使用。新增只读诊断 `SubtitleProjectAccess::RetainedRetiredNodes()`，返回计数，不暴露任何节点。
- **B3**：调用者（`AudioEditSession`、`SubtitleDialogEdit`、视觉工具）不再把成功当作失败。后续操作会照常因 `Stale` 被拒绝，或重新开始交互。

## 3. 复核（审计 A/B 项）

| 项 | 状态 |
|---|---|
| A1 | 已关闭。普通提交、Interaction 提交、Undo/Redo、Lua 发布四条宣告路径上，所有观察者都会被调用；任何可能仍被引用的退役节点都会存活到宣告成功之后，宣告失败时存活到项目销毁 |
| A2 | 已关闭。媒体入口在副作用之前被拒绝；内部路径由前置条件兜底 |
| A3 | 已关闭 |
| A4 | 已关闭。残余：如果 wx 在非宿主线程调用崩溃处理，`Snapshot()` 的线程检查会使恢复副本失败，此时返回 false 而不是中止（不写恢复文件，与“不崩溃但无副本”一致） |
| B1 | 已关闭，另外修复了两处 Script Info 泄漏 |
| B2 | 已关闭 |
| B3 | 已关闭 |
| B4 | 已关闭。B7（工作线程辅助函数读取宿主状态）不在本阶段范围内 |
| B5 | 已关闭 |
| B6、B7、C1–C9 | 未处理，保持审计结论。其中 **C1**（Save/Metadata 不受比较会话约束）仍是进入 Apply Engine 的前提条件 |

## 4. 验证（Linux，GCC 15.2，wxGTK 3.2）

- `gtest-main`：433/433 通过。
- `subtitle-compare-tests`：58/58 通过（新增 2 项）。
- `ordinary-edit-tests`：逐项隔离运行，170 项中 132 通过、38 失败。失败集合与 C1-C1c-10 最终运行**完全相同**，都是已知的 wxGTK 限制：37 项 CaptureMouse（GTK `window` 断言）和 1 项 GTK 文件对话框。新增的 7 项全部通过。
- `luajit-52`、`live model owner access`（API 扫描加 19 个编译失败用例）：通过。
- **ASan/LSan**：单独的 `-Db_sanitize=address` 构建位于临时目录，不在仓库内。
  - 新增 7 项 ordinary 回归测试：无 AddressSanitizer 错误，泄漏报告中无任何 `Ass*`、`UndoInfo`、`SubsController` 或 `SubtitleProjectAccess` 帧（剩余泄漏来自 GTK/Pango/fontconfig）。
  - `subtitle-compare-tests`：58/58 通过，零泄漏。这次运行还发现并修复了 C1-C1c-10 测试代码中的两处 `Events.clear()` 泄漏（`subtitle_owner_access.cpp`、`ordinary_edit.cpp`），只涉及测试代码。
- 反证：A3 测试在旧析构函数下触发 `terminate`；B5 测试在旧代码下间歇失败。

## 5. 未验证 / 风险

- Windows/MSVC 未构建。改动只用了标准 C++ 和已有的 boost 设施，但 `project.cpp`、`dialog_export.cpp` 新增的 `LOG_W` 依赖 `libaegisub/log.h`，需要在 Windows 侧确认。
- 崩溃处理（`UnhandledException`）本身无法在测试中触发，只测试了它调用的 `WriteRecoveryCopy`。
- 既有的 libass `CacheFonts` 退出竞态（C9）未处理。

## 6. 保护文件

`本版与原版差异清单.md` 开始和结束时的 SHA256 均为 `6a922d69925ba61d4511fc29a712a3291e0034abdc2a39c8539b672bd0cf35d2`。`subprojects/checkasm.wrap` 和 `docs/subtitle-edit-migration-c1c1.md` 在本克隆中不存在。所有提交只保存在本地，未推送。
