# C1-C1c-8 Load / Close 完整业务迁移交付报告

2026-11-03。本阶段把剩余的文档生命周期操作（Close / 新建 / 启动空白文档）接入 C1-C1c-7 的宿主模型安装协议，并为 Load / Close 明确安装事务与提交后外围业务的边界。没有重新发明发布流程。本报告不宣布整个 C1-C1c 完成；交付后停止，等待复核，不进入 Lua 隔离、Owner 私有化、Apply Engine 或比较 UI。

## 1. 基线、环境与保护文件

- 分支 `codex/style-name-width`；起点 `29ac415b5072747ca58ccbc70dee0e1a43901812`，开始时本地与 fork 远端一致。
- **本阶段在 Linux（Ubuntu 26.04，GCC，wxGTK 3.2，Meson release）新克隆的工作区完成，不是 `O:/Aegisub-Native`。** 以往阶段的 Windows x64 Release 结果不适用于本阶段代码；本阶段 **没有执行 Windows 构建或测试**。
- 三个用户保护文件（`本版与原版差异清单.md`、`subprojects/checkasm.wrap`、`docs/subtitle-edit-migration-c1c1.md`）是 Windows 工作区中的未跟踪或本地修改文件，**不在本克隆中**，因此本阶段既无法也没有读取、修改或核对它们。SHA256 必须在 Windows 工作区拉取本阶段提交后按 C1-C1c-7 报告第 1 节所列值重新核对。本阶段提交不包含这三个路径。
- 没有 reset、clean、stash、强推或切换用户分支。

## 2. 调用链调查

| 入口 | 真实路径 | 迁移前状态 |
| --- | --- | --- |
| 打开字幕 / 最近文件 / 拖放 / 命令行 / 从视频打开 | `subtitle/open*`、`recent/subtitle`、`AegisubFileDropTarget`、`AegisubApp::OpenFiles` → `Project::LoadSubtitles` 或 `Project::LoadList` → `Project::DoLoadSubtitles` → `SubsController::Load` | 模型发布已走 C1-C1c-7 安装器；外围业务和错误分类未收口 |
| 新建字幕 / 启动空白文档 | `subtitle/new`、`FrameMain::FrameMain` → `Project::CloseSubtitles` → `SubsController::Close` | **未迁移**：直接 swap 活动 AssFile，清历史，`Commit(COMMIT_NEW)` |
| 关闭主窗口 | `FrameMain::OnCloseWindow` → `DeferHostClose` → `TryToClose` → `Destroy`；`subtitle/close` → `FrameMain::Close` | C1-C1c-7 已提供安装期间拒绝销毁 |
| 文件打开通知 | `SubsController::FileOpen` 监听者：`FrameMain::OnSubtitlesOpen`、`Automation4::LocalScriptManager::Reload` | 提交后外围 |

`src/` 内再无其他整体替换活动文档的路径（已检索 `ass->swap`、`swap(*c->ass)`、`LoadDefault`、`COMMIT_NEW` 的全部使用）。Undo/Redo 的 `UndoInfo::Apply` 是同一文档内的历史恢复，属于已收口的可信历史路径，不是文档替换。

迁移前发现的问题：

1. `SubsController::Close` 绕过安装器：没有文档 epoch 推进、没有逐观察者确认、旧模型不由事务保留。
2. `Project::DoLoadSubtitles` 把所有异常都显示为“Error loading file”并返回 false，包括 RecoveryBlocked 和模型已 Committed 之后的备份/MRU/FileOpen 失败；随后跳过关联音视频加载。
3. Load 成功后 `DoLoadSubtitles` 再次调用 `SetSelectionAndActiveIDs`，与安装器已安装的同一选择重复，选择观察者多收到一次通知。
4. Load 的外围业务按顺序直接执行：备份失败会使 `FileOpen` 不再执行，并以异常形式冒充加载失败。
5. `RetryModelInstallation` 固定调用 `SetFileName(filename)`；若 Close 接入安装器，重试会向 MRU 加入空路径。

## 3. 模型安装边界

属于安装事务（发布前全部准备，发布点一次性交换，失败前原工程不变）：

| 状态 | Load | Close |
| --- | --- | --- |
| 模型数据 | 独立解析后的候选深复制 | `LoadDefault(true, 默认样式目录)` 的空白候选 |
| 文档身份 | epoch +1、generation +1 | 同左 |
| Selection / Active | 候选按 `active_row` 计算，含绑定组扩展 | 空白行 |
| Undo/Redo 初态 | 新单条初态，Redo 清空 | 同左 |
| Metadata 与保存基线 | 候选 Properties；修订 +1；已保存 | 空白 Properties；修订 +1；已保存 |
| 当前路径 / `?script` | 文件路径 / 其目录 | 空 / 空 |
| 脏状态、自动保存代数 | 干净；旧 AutoSave lifetime 失效 | 同左 |

属于提交后外围业务（仅在 Committed 后执行）：

| 顺序 | Load | Close |
| --- | --- | --- |
| 1 | MRU 加入 + `Path/Last/Subtitles` | — |
| 2 | 原文件备份（`App/Auto/Backup`） | — |
| 3 | `FileOpen(path)`（标题刷新、本地 Automation 脚本重载） | `FileOpen("")` |
| Project 层之后 | 外围错误警告、网格滚动、关联音视频 `LoadUnloadFiles` | 外围错误警告、`LoadUnloadFiles` |

实现：`SubsController::InstallDocument` 在安装前保存 `PostCommitPlan`，`Install` 返回（Committed）后由 `RunPostCommitPlan` 执行。计划在执行前被取走，因此每次安装的外围业务最多执行一次。

## 4. 外围异常策略

- 每个外围步骤单独捕获异常并记入 `SubsController::LastPeripheralErrors()`，后续步骤继续执行；不向 Load / Close 调用者抛出。非空表示**文档已经安装**，只是后续工作失败。每次已提交安装开始外围业务时清空。
- `FileOpen` 仍是普通 Signal：某个监听者抛异常会中断后续 FileOpen 监听者（未修改 signal.h），该异常作为一个外围错误记录。
- `Project::DoLoadSubtitles`：外围错误显示警告（“The subtitles were opened, but some follow-up steps failed”），仍返回 true 并继续关联文件加载。
- 发布前失败（解析失败、文件不存在、授权拒绝、过期输入、准备失败/取消、错误线程）：沿用原错误提示，返回 false；原工程不变。
- RecoveryBlocked：`Install` 抛出新增的 `SubtitleModelRecoveryBlocked`（派生自 `std::runtime_error`，原测试语义不变）；Project 以安装状态而不是异常类型判定，单独提示“已安装但部分界面未能切换，编辑/保存/打开已禁用，关闭 Aegisub 以恢复”，不把它说成加载失败或未修改。
- `Project::CloseSubtitles` 不再让异常逃出命令：拒绝时提示并保持原文档；RecoveryBlocked 时给出同一恢复提示。

## 5. Close 生命周期处理

- `SubsController::Close` 现在是：`Begin` → 构建空白候选 → `InstallDocument(..., ClosedFile)`。授权、epoch、观察者逐个确认、旧模型保留和 RecoveryBlocked 与 Load 完全相同。
- `Begin` 拒绝安装进行中或 RecoveryBlocked，与原 Close 守卫相同（原测试 `Close` 抛 `logic_error` 继续通过）。比较会话或其他写事务活动时在授权阶段拒绝，原文档、历史、Metadata、路径、选择、修订全部不变。
- 旧任务、旧 WriteLease 和旧修订令牌在 Close 后与 Load 后一样失效；旧 AutoSave 完成回调因 lifetime 失效不会写入新文档的自动保存状态。
- 不在持有模型锁时等待 UI 或后台任务：Close 不同步等待任何后台队列；外围业务在安装完成、门禁解除后执行。
- `Project::CloseSubtitles` 删除了重复的 `?script` 设置和重复的选择通知（安装器已完成）。
- 宿主关闭：沿用 C1-C1c-7 的 `DeferHostClose`。RecoveryBlocked 时已发布的模型为干净初态，`TryToClose` 不要求保存，主窗口可以关闭。

## 6. 重复恢复与通知幂等

- `RetryModelInstallation` 改为执行该安装保存的 `PostCommitPlan`，不再固定调用 `SetFileName`：Close 的重试不加入 MRU 空路径，Load 的重试执行与正常 Load 相同的 MRU/备份/FileOpen（C1-C1c-7 中重试会跳过备份）。
- 失败的重试不执行外围业务；成功后执行一次；已完成后再次 Retry 抛 `logic_error`，FileOpen 不会重复。
- 重试重新派发固定名单时验证了：Metadata 修订与字幕修订不再推进（视频/翻译/样式观察者不产生写入）、不新增 Undo、不变脏、选择与活动行不变、编辑框内容正确，之后普通编辑与 Undo/Redo 正常。

## 7. 实际修改文件

功能族一（Linux 应用测试构建）：

- `src/main.cpp`：`wxIMPLEMENT_APP` 拆为等价的 `wxIMPLEMENT_WX_THEME_SUPPORT` + `wxIMPLEMENT_APP_NO_MAIN`。
- `src/main_entry.cpp`（新增）：`wxIMPLEMENT_WXWIN_MAIN`，仅链接进 aegisub 可执行文件。
- `meson.build`：编辑器加入 `main_entry.cpp`；`ordinary-edit-tests` 在所有平台构建。Windows 仍用 `extract_all_objects`（入口对象从 main.cpp 移到 main_entry.cpp，链接结果等价，但本阶段未在 Windows 验证），其他平台用 `extract_objects(aegisub_src)`，排除入口点。

功能族二（Load / Close 迁移）：

- `src/subtitle_model_installation.h`：`SubtitleModelRecoveryBlocked`。
- `src/subs_controller.h/.cpp`：`PostCommitPlan`、`InstallDocument`、`RunPostCommitPlan`、`LastPeripheralErrors`；Load / Close / Retry 接入。
- `src/project.h/.cpp`：`ReportRecoveryBlocked`、`ReportPeripheralErrors`；`DoLoadSubtitles`、`CloseSubtitles` 错误分类，删除重复选择通知；`subsGrid` 为空时不滚动。
- `tests/tests/ordinary_edit.cpp`：新增 `document_lifecycle` 11 项。

未修改：signal.h、Lua 实现、`Context::ass` 声明、第三方依赖、ASS 保存格式、Undo 历史格式、选择通知语义（只删除 Project 层的重复调用）。

## 8. 新增测试（`document_lifecycle`，11 项）

1. `production_close_installs_blank_document_with_new_identity_and_clean_history`：一次 COMMIT_NEW、Selection 先于 Active；epoch/generation 各 +1；旧令牌和旧任务写回拒绝；空白内容、Metadata、选择、干净历史、路径；FileOpen 恰好一次且为空路径；MRU 不变；之后编辑 Undo/Redo。
2. `production_close_denied_by_comparison_or_writer_preserves_complete_host_state`：比较会话和写事务期间 Close 拒绝，完整状态包不变，无通知、无 FileOpen，Redo 仍可用。
3. `close_recovery_blocked_runs_follow_up_work_exactly_once_after_explicit_retry`：Close 观察者失败 → `SubtitleModelRecoveryBlocked`，旧行存活、写/保存/Close/Load 封闭；失败重试不执行外围；成功重试 FileOpen 一次、MRU 不变；再次 Retry 拒绝。
4. `load_follow_up_failures_are_reported_after_commit_and_do_not_suppress_later_steps`：备份目录失败时 Load 不抛出，Committed，错误只含备份项，MRU / 路径 / `Path/Last/Subtitles` / FileOpen 仍执行；FileOpen 监听者再失败时两项错误、未封闭；错误按安装清空。
5. `project_entries_distinguish_parse_failure_recovery_blocked_and_follow_up_warnings`：经 `Project::LoadSubtitles`，未知格式和不存在文件提示且完整状态不变；RecoveryBlocked 给出恢复提示，`CloseSubtitles` 不抛出；外围失败给出“已打开”警告且文档已安装。
6. `project_close_reports_rejection_without_throwing_then_closes_once`：比较期间 `Project::CloseSubtitles` 提示且不变；之后关闭只有一次 Selection/Active 通知。
7. `project_load_announces_installed_selection_once`：经 Project 打开时选择通知各一次。
8. `open_close_reload_save_and_reopen_keep_path_mru_and_metadata_consistent`：打开、保存、关闭、重开、另存、再次关闭/重开，核对 MRU、`?script`、上次目录、Metadata 脏状态、Extradata 与 Comment。
9. `explicit_retry_with_real_grid_and_dialogs_repeats_rebinding_without_duplicate_side_effects`：真实 BaseGrid、样式助手、翻译对话框；失败/成功重试不推进修订、不重复 FileOpen/MRU、不产生 Undo。
10. `autosave_result_completing_after_close_cannot_touch_the_new_document`：Close 前排队的自动保存在 Close 后完成，不影响新文档内容和脏状态。
11. `lua_local_scripts_reload_is_post_commit_follow_up_not_isolated_work`：记录边界——Automation 本地脚本重载在 FileOpen 中、Committed 之后运行，未被本阶段隔离。

变异验证（临时修改后恢复，未提交）：恢复 Project 层重复选择调用 → 测试 7 失败；让外围异常向上传播 → 测试 4、5 失败；恢复旧 Close 实现 → 测试 1、2 失败且测试 3 因旧行被释放而进程崩溃。

## 9. 构建与测试结果

Linux x64 release（Meson buildtype=release，GCC，wxGTK 3.2）构建成功，`aegisub` 与全部测试目标链接。

| 测试程序 | 定义 | 运行 | 通过 |
| --- | --- | --- | --- |
| ordinary-edit-tests | 135（124 原有 + 11 新增） | 97 | **97/97** |
| subtitle-compare-tests | 55 | 55 | **55/55** |
| gtest-main | 433（Linux；Windows 为 436） | 433 | **433/433** |
| 合计（Linux 实际运行） | | 585 | **585/585** |

`ordinary-edit-tests` 中 **38 项原有测试在 Linux 无法运行，未计入通过**，在本阶段改动前后结果相同：

- 37 项（`visual_edit` 38 项中的 29 项、`audio_edit` 24 项中的 5 项拖动、`selection_ui` 6 项中的 3 项）在 wxGTK 上触发 `CaptureMouse` 断言（`src/gtk/window.cpp:6327 "window"`）：测试窗口从未显示，GTK 不会为其创建本地窗口，而 Win32 总会创建。其中包括 C1-C1c-7 的 `visual_edit.production_load_rebinds_real_visual_cache_and_renderer_before_next_gesture`。
- 1 项 `ordinary_edit.real_style_import_and_delete_preserve_unrelated_data_and_history`：GTK 文件对话框路径未经过测试的对话框钩子。

这是测试夹具与平台的差异，不是本阶段产品代码问题；修复它需要改动 GTK 下的测试窗口策略，超出本阶段范围。Linux 运行使用固定的排除列表，原有 86 项可运行测试在改动前后均全部通过。

真实编辑器冒烟（临时 stderr 跟踪，已移除）：使用隔离的 HOME，以命令行字幕文件启动 `build/aegisub`。完整 `FrameMain` 构造中的启动 `CloseSubtitles` 达到 Committed；随后命令行 Load 达到 Committed，无 RecoveryBlocked、无外围错误，MRU 与 `smoke.ORIGINAL.ass` 备份均已生成。这不是人工 UI 验收。

**Windows x64 Release：本阶段未执行。** 预期 Windows 总数为 135 + 55 + 436 = 626，必须在 Windows 工作区实际构建运行后才能报告。Linux 结果不能代表 Windows 或 macOS；GitHub Actions 未对本分支运行。

## 10. Git 提交与推送

按功能族分别提交：Linux 应用测试构建、Load / Close 迁移（含测试）、本报告。提交哈希和远端核对结果记录在交付消息中，不在报告内自引用。本机初始没有 git 作者身份和 GitHub 推送凭据；推送状态以交付消息为准。

## 11. 剩余旁路与人工验收

未在本阶段处理，留待后续授权：

- Lua：独立工作模型、多 Undo 点结果、受控发布均未实现；`LocalScriptManager::Reload` 仍在 FileOpen 中读取活动文档。不能声称 Lua 已隔离。
- `Context::ass` 仍为公开 Owner；任意代码直接修改或销毁 Context/FrameMain 仍是阶段外旁路。
- 普通 Commit 的观察者异常仍保持原 Signal 语义；RecoveryBlocked 仅覆盖文档安装（Load / Close）。
- RecoveryBlocked 仍只有内部 `RetryModelInstallation`，没有面向用户的重试按钮；用户提示为关闭 Aegisub。
- `FileOpen` 监听者异常会中断后续监听者（未改 signal.h），已作为外围错误报告。

人工验收清单（未执行）：

- Windows：构建、626 项全量测试、三个保护文件 SHA256。
- 桌面实测：新建、打开、最近文件、拖放、从视频打开、关闭窗口时保存提示；备份目录不可写时的警告；关联音视频提示。
- 故障注入下的 RecoveryBlocked 提示与关闭。
- Linux 上 38 项受限测试需要可显示的测试窗口策略后再验证；macOS 未验证。
