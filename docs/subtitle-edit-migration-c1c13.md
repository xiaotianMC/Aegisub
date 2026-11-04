# C1-C1c-13 模态对话框、保存失败、通知内打开与多步写入（审计 B6、C2、C4、C6）

本阶段修复 `docs/subtitle-edit-c1c-final-audit.md` 中进入 Apply Engine 之前建议处理的四项：B6、C2、C4、C6。每一项都有回归测试，且新测试在旧代码下失败（见第 4 节）。没有实现 Apply Engine 或比较界面。没有修改 `signal.h`、Undo/Redo 历史格式、Lua API 或保存格式，也没有新增公开可变 getter、`const_cast`、宽泛 friend 或全局锁。

## 1. 修复概览

| 项 | 修复 | 位置 |
|---|---|---|
| B6 | 模态对话框期间不再持有写租约。命令先试探一次 `TryWrite()`（比较期间等情况下在对话框之前拒绝，与原行为一致），随即释放；对话框关闭后才重新获取租约，并从活动模型重新暂存。颜色选择器的实时预览改为 `Interaction`，取消时只在自己的预览仍是最新修改时才撤销 | `command/edit.cpp`（颜色、字体、覆盖粘贴）、`command/tool.cpp`（分辨率重采样）、`dialog_style_manager.cpp`（导入、复制到当前、删除）、`dialog_style_editor.cpp`（应用）、`search_replace_engine.cpp`（全部替换）、`dialog_spellchecker.cpp`（检查完成） |
| C2 | ① 保存失败的回滚不再直接给 `Properties` 赋值，改为新的私有 `SubtitleProjectAccess::RestoreMetadata`：值确有变化时推进元数据修订号并通知监听者，且不抛出。② “每次修改都保存”在 `OnCommit` 中失败时不再中断提交：历史记录已经写入，提交照常宣告；`Save` 自身回滚，文档保持已修改；失败写入日志，并在提交结束后显示在状态栏 | `subtitle_project_access.{h,cpp}`、`subs_controller.{h,cpp}` |
| C4 | 观察者通知期间（`IsNotifying()`）拒绝开始文档安装（`Load`/`Close`，抛 `agi::InvalidInputException`）和媒体变更（入口报告后拒绝，内部 `Do*` 抛出）。场景：选择通知内打开的模态框期间，排队的拖放打开被分发 | `subs_controller.cpp`（`SubtitleModelInstallation::Begin`）、`project.cpp`（`BeginMediaChange`、`RequireMediaChange`） |
| C6 | ① 行值与“连接相邻行”合并为一次写入：`WriteAccess::UpdateRowsLinkingAdjacent`、`Interaction::UpdateRowsLinkingAdjacent`。所有校验（行目标、链接目标、Interaction 的完整目标集授权）都在第一次写入之前完成，被拒绝时活动模型不变。原来分开的 `LinkAdjacent` 已删除，调用者无法再走分步路径。② `combine_lines` 与 `recombine` 改为先提交再设置选择；`SubtitleRowListEdit::Select` 在提交之前调用会抛 `logic_error`，选择观察者不会再看到未提交的结构 | `subtitle_project_access.{h,cpp}`、`audio_edit_session.h`、`audio_timing_dialogue.cpp`、`subs_edit_box.cpp`、`command/time.cpp`、`subtitle_row_list_edit.h`、`command/edit.cpp` |

## 2. 各模态入口的行为（B6）

| 入口 | 对话框期间 | 对话框之后 |
|---|---|---|
| 颜色选择器（`edit/color/*`） | 持有 `Interaction`（目标为所选行），比较无法开始。这是有意的：实时预览就是一次交互，与视觉工具相同。每次预览都检查 `Current()`；其他处理函数的提交会使会话失效，此后的预览被忽略 | 取消时，只有会话仍为 `Current()`（自上次预览以来没有任何其他修订）才 `Undo()`，因此撤销的一定是预览本身。若期间有其他提交，预览保留，不撤销任何东西，用户可自行撤销 |
| 字体（`edit/font`） | 无租约 | `update_lines` 以自己的写作用域从活动模型重新暂存 |
| 覆盖粘贴（`edit/line/paste/over`） | 无租约。字段对话框在暂存之前显示（原来在第一行粘贴时才显示） | 选好字段后获取租约并暂存。**语义变化**：取消字段对话框即取消整次粘贴（上游会对每一行重复询问，并提交一条内容不变的“paste”记录） |
| 分辨率重采样（`tool/resampleres`） | 无租约 | 从活动模型重新暂存后计算 |
| 样式管理器：导入、复制到当前 | 无租约。重名确认在暂存之前、针对活动模型进行 | 只覆盖用户确认过的重名样式；对话框期间才出现的重名不会在未确认的情况下被覆盖（跳过） |
| 样式管理器：删除 | 无租约 | 重新读取列表选择和样式缓存；选择数量变化时放弃 |
| 样式编辑器：应用 | 无租约。“是否同步重命名脚本中的样式”在独立快照上判断并询问；各种错误提示框显示前先释放租约 | 被编辑的样式必须仍是同名的活动节点，否则提示“样式已不存在”，不会解析过期指针 |
| 查找替换：全部替换、拼写检查完成 | 提交后先释放租约，再显示结果消息框 | — |

用脚本扫描 `src/` 中所有 `TryWrite()`/`TryBeginInteraction` 作用域内的模态调用，扫描结果只剩颜色选择器的 `Interaction`，以及上面那些“试探后立即释放”的行。

## 3. 语义变化（有意）

- **B6 颜色选择器**：期间有其他提交时，取消不再撤销（旧代码会撤销掉那次其他提交）。预览会保留下来。
- **B6 覆盖粘贴**：取消字段对话框就取消整次粘贴。
- **B6 样式导入/复制**：确认之后才出现的重名样式会被跳过。
- **C2**：“每次修改都保存”失败不再让提交抛出。失败只记录日志并显示在状态栏，不弹出模态框：这条路径每次修改都会触发，而且发生在提交内部。
- **C2**：保存失败回滚时，元数据确有变化就会推进元数据修订号。下一次自动保存会把它视为已修改，最多多出一次自动保存。
- **C4**：观察者通知期间打开或关闭文件会被拒绝，并提示用户重试。
- **C6**：`WriteAccess::LinkAdjacent` 和 `Interaction::LinkAdjacent` 被 `UpdateRowsLinkingAdjacent` 取代（C1 内部 API，不是 Lua API）。

## 4. 测试

新增（`tests/tests/ordinary_edit.cpp`）：

| 测试 | 覆盖 | 旧代码 |
|---|---|---|
| `ordinary_edit.color_picker_preview_is_an_interaction_and_cancel_undoes_it` | 预览写入活动模型；对话框期间比较被拒绝；取消后文本和撤销栈复原 | 通过（行为保持测试） |
| `ordinary_edit.color_picker_cancel_never_undoes_a_commit_made_while_it_was_open` | 对话框期间另一个处理函数提交，之后的预览被忽略；取消不撤销那次提交 | **失败**（撤销了其他提交） |
| `ordinary_edit.font_resample_and_style_manager_questions_hold_no_write_lease` | 字体、重采样、样式删除确认、样式导入文件框打开期间都可以开始比较；取消后模型和历史不变 | **失败** |
| `ordinary_edit.joined_lines_are_committed_before_selection_observers_see_them` | 合并行时，选择观察者看到的撤销描述已经是“join lines”，行数为 1 | **失败** |
| `document_lifecycle.failed_save_restores_metadata_through_its_listeners` | FileSave 监听者改元数据后抛出：值复原，监听者看到复原后的值，修订号前进 2，文件名和脏状态不变 | **失败** |
| `document_lifecycle.failed_save_on_every_change_still_announces_the_recorded_commit` | 保存失败时提交不抛出，观察者被调用，撤销描述正确，文档保持已修改；恢复后正常保存 | **失败**（提交抛出） |
| `document_lifecycle.opening_or_closing_inside_an_observer_notification_is_refused_without_side_effects` | 选择通知内 `Load`、`Close`、`LoadSubtitles`、`LoadList`、`LoadAudio` 都被拒绝，模型、文件名和音频不变；通知之外可以正常打开 | **失败** |

修改：`audio_edit.linked_neighbor_is_authorized_and_restored_by_undo` 改用新 API，并额外断言：链接被拒绝时，请求的行值没有残留在活动模型中，会话已结束。

“旧代码”一列的做法：把 `src/` 换回 HEAD 版本，测试文件为 HEAD 版本加上新增测试，构建后运行；随后恢复修改。

### 验证结果（Linux，GCC 15.2，wxGTK 3.2）

- `gtest-main`：434/434。
- `subtitle-compare-tests`：60/60。
- `ordinary-edit-tests`：逐项隔离运行，共 179 项，141 通过、38 失败。失败集合与 C1-C1c-11 **完全相同**：37 项 CaptureMouse（GTK `window` 断言），以及 `real_style_import_and_delete_preserve_unrelated_data_and_history` 的 GTK 文件选择框问题（`SetPath` 在 GTK 原生对话框上不生效，断言位置相同）。新增的 7 项和修改的 1 项全部通过。本次运行没有出现 C9 退出崩溃。
- `luajit-52`、`live model owner access`：通过。
- ASan（独立的 `-Db_sanitize=address` 构建）：上述 8 项均无 AddressSanitizer 错误。泄漏报告的分配点都在 wxSTC、GTK、Pango、fontconfig 内部，模型代码只作为调用栈中的调用者出现（例如 Undo 之后的 `TextSelectionController::SetSelection` 进入 wxSTC），没有任何 `Ass*`、`UndoInfo`、`SubsController` 或 `SubtitleProjectAccess` 对象泄漏。

## 5. 未处理项与风险

- **B7、C3、C5、C7、C8、C9** 未处理，保持审计结论。C7 只顺带给本阶段改动的 `update_lines`、颜色选择器和字体命令加了活动行空检查。
- 颜色选择器在对话框期间仍会阻止比较（`Interaction`）。样式助手和音频编辑会话也一样（审计第 4 节），都属于比较界面的设计输入。
- Windows/MSVC 仍未构建（C1-C1c-11、12、13 均未验证）。

## 6. 保护文件

`本版与原版差异清单.md` 开始和结束时的 SHA256 均为 `6a922d69925ba61d4511fc29a712a3291e0034abdc2a39c8539b672bd0cf35d2`。
