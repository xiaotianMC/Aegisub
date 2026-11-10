# C1-C1b Selection 接口收口

日期：2026-11-02。以 C1-C1a a2fa1c6865f06588755a8cf9a78fc73b0304807a 为实际基线，分支 codex/style-name-width，本地/跟踪/实时远端一致。账号 xiaotianMC，fork push=true，upstream push=false，fork PR=0；已检查上游 PR，不合入。暂存区空。用户差异清单、checkasm.wrap 及上一轮未跟踪 c1c1 调查报告保持原样，不纳入提交。

## 调查与实施边界

| 调用链 | 实际作用及问题 | 本阶段方案 |
| --- | --- | --- |
| SelectionController getters/state/notifications | private selection 和 active 均存 live mutable pointers；getter 即使 const 仍泄漏写能力；通知 payload mutable；setters 没有成员/线程验证 | 状态存稳定 IDs，只读同步查询；Active 通知不带行指针，观察者即时读取当前状态；普通 UI 不申请 WriteLease |
| BaseGrid SelectRow/鼠标/菜单/键盘 -> RowsForWrite -> setters | 纯选择也申请写 scope，构造 mutable 全行表；跨回调范围锚点是 Row 位置 | 移除 RowsForWrite 和选择分支 lease；只给注释字段编辑保留授权；选择/范围锚点使用 IDs |
| commands/audio/video、编辑框、补全、查找、拼写、Select Lines、样式窗口 | 多数只读或 UI 选择，C1-C1a 字段写已通过值 API；旧 getters/setters 仍穿透 | const/ID 读取、ID setters；不修改业务字幕算法 |
| SubtitleRowListEdit / BindSelectedSubtitles / VisualTool 选择桥接 | 为调用旧 setter 获取 mutable 当前行，真实模型写已受控 | 移除 live pointer 桥接，按结果 ID 安装选择；视觉手势本身的 Interaction 校验保留 |
| B1/B2/B3 观察者 | 音频/视觉/对话框已持独立编辑快照或 IDs，部分选择回调签名仍借行；SubsEditBox 长期存 const 当前行 | 通知回调现读；编辑框和选择锚点跨回调持 IDs；不破坏原编辑/预览/取消 |
| UndoInfo::Apply | 在 Undo/Redo 已取得 scope 后重建模型；COMMIT_NEW 先于最终选择恢复；历史中的 selection/active 已是 IDs | 仅收口选择快照/恢复，保持历史格式、原 Commit 与选择通知相对顺序、旧节点保留 |
| SubsController::OnCommit / AddUndoManager | PushState 先于 AnnounceCommit；OnCommit 授权补默认行/样式并维护 Undo；公开 Undo payload/注册仍是当前模型入口 | 本阶段核对并测试 selection 通知不得重入历史/写入；受信历史模型操作、AddUndoManager 权限封闭归 C1-C1c |
| Project / Lua | 旧接口调用涉及当前选择，但模型事务属于后续阶段 | 仅必要的只读/ID Selection 适配；不迁移 Lua/Load/Close/Save，不改 Context owner |

保持实际行为：组合安装先 Selection 通知、后 Active；相同选择仍发 Selection；活动 ID 不变不额外发 Active（模型恢复重绑仍通知）；单独设置 Active 不修改选择，允许活动行不在选择集。绑定扩展可关闭，silent 安装不扩展、不通知。普通 UI 选择不推进字幕修订，也不创建 Undo 记录。
通知是同步只读模型阶段：复用每 AssFile 的 notifying 标记，受控字幕写与 Undo/Redo 在选择通知期间拒绝；允许合法的嵌套选择导航（翻译助手跳过不可译块需要它）。Active 通知没有保存的行 payload，各观察者读取当前选择，避免嵌套导航后继续收到过期行。

功能族顺序：1）稳定 ID 内部状态、只读查询/通知与观察者适配，构建测试后提交推送；2）ID setters、网格/工具/历史恢复桥接、缓存生命周期及编译期保护，完整回归后提交推送。中间的旧 const 输入适配仅为分批构建，最终删除，不建立永久 LegacyMutable。
不修改 signal.h、不增加依赖、不修改历史格式/ASS 持久化、Context owner 或 Lua/I/O 模型事务。C1-C1b 完成即停止，不进入 c。

## 基线完整调用索引

以下为基线源码的逐行调用/定义索引，结合上表调用链分类，不将每个文本命中都计为字幕写入。脱离工程的 RowListEdit selection 属独立可变值，保留正常可变能力。

| 文件 | 所有定位及表达式 |
| --- | --- |
| src/audio_karaoke.cpp | 64: `, active_line_changed(c->selectionController->AddActiveLineListener(&AudioKaraoke::OnActiveLineChanged, this))` |
| src/audio_timing_dialogue.cpp | 432: `, active_line_connection(c->selectionController->AddActiveLineListener(&AudioTimingControllerDialogue::Revert, this))`<br>763: `auto const& sel = context->selectionController->GetSelectedSet();`<br>829: `auto const& selected = context->selectionController->GetSelectedSet();` |
| src/auto4_lua.cpp | 819: `auto const& sel = c->selectionController->GetSelectedSet();`<br>842: `if (auto active_line = c->selectionController->GetActiveLine())`<br>881: `if (auto active_line = c->selectionController->GetActiveLine())`<br>937: `AssDialogue *new_active = c->selectionController->GetActiveLine();`<br>942: `c->selectionController->SetSelectionAndActive(std::move(sel), new_active);`<br>967: `c->selectionController->SetSelectionAndActive(std::move(new_sel), new_active);`<br>985: `if (auto active_line = c->selectionController->GetActiveLine())` |
| src/base_grid.cpp | 96: `context->selectionController->AddActiveLineListener(&BaseGrid::OnActiveLineChanged, this),`<br>254: `auto rows = RowsForWrite(edit_scope->Lease());`<br>258: `context->selectionController->SetSelectedSet(Selection{line});`<br>262: `bool selected = !!context->selectionController->GetSelectedSet().count(line);`<br>264: `auto selection = context->selectionController->GetSelectedSet();`<br>269: `context->selectionController->SetSelectedSet(std::move(selection));`<br>370: `const auto active_line = context->selectionController->GetActiveLine();`<br>371: `auto const& mutable_selection = context->selectionController->GetSelectedSet();`<br>526: `auto rows = RowsForWrite(edit_scope->Lease());`<br>534: `context->selectionController->SetActiveLine(target);`<br>538: `auto const& selection = context->selectionController->GetSelectedSet();`<br>545: `context->selectionController->SetSelectionAndActive({target}, target, false);`<br>571: `context->selectionController->SetSelectionAndActive({target}, target, false);`<br>589: `context->selectionController->SetSelectedSet(std::move(newsel));`<br>617: `auto rows = RowsForWrite(edit_scope->Lease());`<br>620: `if (context->selectionController->GetSelectedSet().count(line))`<br>621: `context->selectionController->SetActiveLine(line);`<br>623: `context->selectionController->SetSelectionAndActive({line}, line);`<br>702: `std::vector<AssDialogue*> BaseGrid::RowsForWrite(agi::SubtitleEditGate::WriteLease const& lease) const {`<br>775: `auto rows = RowsForWrite(edit_scope->Lease());`<br>777: `auto active_line = context->selectionController->GetActiveLine();`<br>778: `if (rows.empty()) { context->selectionController->SetActiveLine(nullptr); return; }`<br>781: `context->selectionController->SetActiveLine(rows.at(next));`<br>807: `context->selectionController->SetSelectedSet(std::move(newsel));` |
| src/base_grid.h | 92: `std::vector<AssDialogue*> RowsForWrite(agi::SubtitleEditGate::WriteLease const&) const;` |
| src/command/audio.cpp | 172: `return c->project->AudioProvider() && !c->selectionController->GetSelectedSet().empty();`<br>176: `auto const& sel = c->selectionController->GetSelectedSet();` |
| src/command/edit.cpp | 98: `return c->selectionController->GetSelectedSet().size() > 0;`<br>105: `return c->project->VideoProvider() && !c->selectionController->GetSelectedSet().empty();`<br>112: `return c->selectionController->GetSelectedSet().size() > 1;`<br>319: `const auto active_line = c->selectionController->GetActiveLine();`<br>332: `auto const& sel = c->selectionController->GetSelectedSet();`<br>362: `const auto active_line = c->selectionController->GetActiveLine();`<br>367: `auto const& sel = c->selectionController->GetSelectedSet();`<br>538: `const parsed_line active(c->selectionController->GetActiveLine());`<br>600: `SetClipboard(join(c->selectionController->GetSortedSelection()`<br>956: `bool can_paste = !c->selectionController->GetSelectedSet().empty();`<br>1328: `return reverse_move_paths(c->selectionController->GetActiveLine(), false);`<br>1349: `auto const& selection = c->selectionController->GetSelectedSet();`<br>1372: `auto const& selection = c->selectionController->GetSelectedSet();` |
| src/command/grid.cpp | 75: `AssDialogue *cur = c->selectionController->GetActiveLine();`<br>77: `if (cur == c->selectionController->GetActiveLine()) {`<br>122: `return c->selectionController->GetSelectedSet().size() > 1;`<br>394: `return c->selectionController->GetSelectedSet().size() != 0;`<br>414: `return c->selectionController->GetSelectedSet().size() != 0;`<br>435: `return c->selectionController->GetSelectedSet().size() == 2;` |
| src/command/subtitle.cpp | 67: `return !c->selectionController->GetSelectedSet().empty();`<br>74: `return c->project->VideoProvider() && !c->selectionController->GetSelectedSet().empty();`<br>415: `c->selectionController->SetSelectedSet(std::move(sel));`<br>438: `c->selectionController->SetActiveLine(&diag);`<br>443: `c->selectionController->SetSelectedSet(std::move(new_selection));` |
| src/command/time.cpp | 65: `size_t sel_size = c->selectionController->GetSelectedSet().size();`<br>69: `auto sel = c->selectionController->GetSortedSelection();`<br>145: `auto const& sel = c->selectionController->GetSelectedSet();`<br>146: `const auto active_line = c->selectionController->GetActiveLine();`<br>178: `auto const& sel = c->selectionController->GetSelectedSet();` |
| src/command/video.cpp | 350: `AssDialogue *active_line = c->selectionController->GetActiveLine();`<br>366: `AssDialogue *new_line = c->selectionController->GetActiveLine();`<br>417: `AssDialogue *active_line = c->selectionController->GetActiveLine();`<br>433: `AssDialogue *new_line = c->selectionController->GetActiveLine();`<br>567: `if (auto active_line = c->selectionController->GetActiveLine())`<br>580: `if (auto active_line = c->selectionController->GetActiveLine())` |
| src/dialog_rectangle.cpp | 134: `agi::signal::Connection selection_changed=c->selectionController->AddActiveLineListener([&]{edit.Invalidate();});` |
| src/dialog_selection.cpp | 207: `old_sel = con->selectionController->GetSelectedSet();`<br>244: `AssDialogue *new_active = con->selectionController->GetActiveLine();`<br>247: `con->selectionController->SetSelectionAndActive(std::move(new_sel), new_active);` |
| src/dialog_shift_times.cpp | 265: `if (context->selectionController->GetSelectedSet().empty()) {`<br>378: `auto const& sel = context->selectionController->GetSelectedSet();` |
| src/dialog_spellchecker.cpp | 250: `AssDialogue *real_active_line = context->selectionController->GetActiveLine();`<br>328: `context->selectionController->SetSelectionAndActive({ &*current }, &*current);`<br>344: `AssDialogue *active_line = context->selectionController->GetActiveLine();` |
| src/dialog_style_manager.cpp | 265: `, active_line_connection(c->selectionController->AddActiveLineListener(&DialogStyleManager::OnActiveLineChanged, this))`<br>400: `AssDialogue *dia = c->selectionController->GetActiveLine();` |
| src/dialog_styling_assistant.cpp | 59: `, active_line_connection(context->selectionController->AddActiveLineListener(&DialogStyling::OnActiveLineChanged, this))`<br>156: `OnActiveLineChanged(c->selectionController->GetActiveLine());` |
| src/dialog_translation.cpp | 70: `, active_line_connection(c->selectionController->AddActiveLineListener(&DialogTranslation::OnActiveLineChanged, this))` |
| src/initial_line_state.cpp | 22: `: active_line_connection(c->selectionController->AddActiveLineListener(&InitialLineState::OnActiveLineChanged, this))`<br>24: `OnActiveLineChanged(c->selectionController->GetActiveLine());` |
| src/project.cpp | 157: `context->selectionController->SetSelectionAndActive(std::move(sel), active_line);`<br>184: `context->selectionController->SetSelectionAndActive({line}, line);` |
| src/search_replace_engine.cpp | 197: `AssDialogue *line = context->selectionController->GetActiveLine();`<br>235: `auto const& sel = context->selectionController->GetSelectedSet();`<br>245: `context->selectionController->SetActiveLine(&*it);`<br>247: `context->selectionController->SetSelectionAndActive({ &*it }, &*it);` |
| src/selection_controller.cpp | 83: `void SelectionController::SetSelectedSet(Selection new_selection, bool expand_bound_groups) {`<br>90: `void SelectionController::SetActiveLine(AssDialogue *new_line) {`<br>99: `void SelectionController::SetSelectionAndActive(Selection new_selection, AssDialogue *new_line, bool expand_bound_groups) {`<br>113: `std::vector<AssDialogue *> SelectionController::GetSortedSelection() const {`<br>124: `SetSelectionAndActive({&*it}, &*it);`<br>132: `SetSelectionAndActive({&*it}, &*it);` |
| src/selection_controller.h | 94: `void SetActiveLine(AssDialogue *new_line);`<br>98: `AssDialogue *GetActiveLine() const { return active_line;  }`<br>112: `void SetSelectedSet(Selection new_selection, bool expand_bound_groups = true);`<br>116: `Selection const& GetSelectedSet() const { return selection; }`<br>119: `std::vector<AssDialogue *> GetSortedSelection() const;`<br>130: `void SetSelectionAndActive(Selection new_selection, AssDialogue *new_line, bool expand_bound_groups = true);` |
| src/subs_controller.cpp | 121: `c->selectionController->SetSelectionAndActive(std::move(new_sel), active_line);`<br>128: `auto line = c->selectionController->GetActiveLine();`<br>134: `auto const& sel = c->selectionController->GetSelectedSet();`<br>166: `active_line_connection = selection_controller->AddActiveLineListener(&SubsController::OnActiveLineChanged, this);` |
| src/subs_edit_box.cpp | 259: `context->selectionController->AddActiveLineListener(&SubsEditBox::OnActiveLineChanged, this),`<br>516: `auto const& sel = c->selectionController->GetSelectedSet();` |
| src/subs_edit_ctrl.cpp | 242: `auto diag = context ? context->selectionController->GetActiveLine() : nullptr;`<br>316: `auto diag = context ? context->selectionController->GetActiveLine() : nullptr;`<br>523: `AssDialogue *diag = context ? context->selectionController->GetActiveLine() : nullptr;`<br>566: `auto diag = context ? context->selectionController->GetActiveLine() : nullptr;` |
| src/subtitle_binding.cpp | 66: `auto selection = c->selectionController->GetSelectedSet();`<br>99: `c->selectionController->SetSelectedSet(std::move(expanded));` |
| src/subtitle_row_list_edit.h | 60: `c->selectionController->SetSelectionAndActive(std::move(live), active_live, expand);` |
| src/video_box.cpp | 130: `AssDialogue *active_line = context->selectionController->GetActiveLine();` |
| src/video_controller.cpp | 56: `context->selectionController->AddActiveLineListener(&VideoController::OnActiveLineChanged, this),`<br>156: `AssDialogue *curline = context->selectionController->GetActiveLine();`<br>162: `int startFrame = FrameAtTime(context->selectionController->GetActiveLine()->Start, agi::vfr::START);`<br>164: `end_frame = FrameAtTime(context->selectionController->GetActiveLine()->End, agi::vfr::END) + 1;` |
| src/visual_tool.cpp | 65: `connections.push_back(c->selectionController->AddActiveLineListener(&VisualToolBase::OnActiveLineChanged, this));`<br>89: `auto const& selected = c->selectionController->GetSelectedSet();`<br>283: `for (auto& row : c->ass->Events) if (row.Id == id) { c->selectionController->SetActiveLine(&row); return; }`<br>289: `auto selected = clear ? Selection{} : c->selectionController->GetSelectedSet();`<br>291: `selected.insert(&row); c->selectionController->SetSelectedSet(std::move(selected)); return;`<br>296: `auto selected = c->selectionController->GetSelectedSet();`<br>301: `auto active = c->selectionController->GetActiveLine();`<br>303: `c->selectionController->SetSelectionAndActive(std::move(selected), active);`<br>314: `c->selectionController->SetSelectionAndActive({&row}, &row); break;` |

## 第一批：稳定状态、只读接口和通知

- 状态只存行 ID；移除三个 mutable getters，Selection 改为 const 行集合。Active 通知无行指针，观察者即时读取当前状态，避免嵌套导航后使用旧 payload。临时保留 const pointer 输入 setters 仅用于分批构建，第二批全部移除。
- 修正头文件：组合安装先 Selection 后 Active；相同选择仍发 Selection。活动行独立于选择集；COMMIT_NEW 重建同 ID 行后仍发必要的 Active 重绑定通知；空活动行不会凭空通知。
- 普通选择不申请 WriteLease，不推进字幕修订。选择通知复用现有模型通知阶段：TryWrite、既有 WriteAccess 及 Undo/Redo 均不能从回调重入模型写入，但允许翻译助手需要的同步嵌套选择导航。
- PrepareSilent/InstallSilent 保存 IDs，检查 owner/thread/session/revision 及模型身份顺序，在无分配交换后静默安装，不展开绑定组。
- const AssFile::iterator_to 支持只读查找/拼写遍历；独立 AssFile 的 mutable 版本保留。字体解析使用独立行副本；结构算法的可变指针仅指向已隔离的副本。
- Windows x64 Release 全构建成功；真实 wx 应用测试 98/98（普通37、音频24、视觉37）、模型测试45/45、基础436/436，共579/579。新增实际编辑框刷新、无 WriteLease、通知次序/重复、同 ID 重建、异常恢复、线程/关闭/缺失 ID、回调已有授权写入拒绝、嵌套导航和静默安装身份变更覆盖。嵌套导航测试首轮对现有 observer 次序的假设错误，已按实际 signal 行为修正测试并复验，signal.h 未改。
- 证据：build/c1c1b-state-build.log、c1c1b-state-app.log、c1c1b-state-model.log、c1c1b-state-base.log。git diff --check 通过，两个受保护文件 SHA256 与开始时一致。
- 剩余本阶段事项：const pointer 输入 setters、RowsForWrite/UI 选择 lease、编辑框/拼写/网格跨回调位置、结构和视觉工具选择桥接，第二批处理。
- 剩余阶段外绕过：Context::ass、Lua 当前模型事务、Load/Close/Save、UndoInfo::Apply/OnCommit 的可信历史模型重建、AddUndoManager 可变提交载荷。此批仅迁移只读选择使用和通知防重入，完整所有权隔离留 C1-C1c。

实际修改文件（另含本报告）：

- `src/ass_file.cpp`
- `src/ass_file.h`
- `src/audio_karaoke.cpp`
- `src/audio_karaoke.h`
- `src/audio_timing_dialogue.cpp`
- `src/auto4_lua.cpp`
- `src/base_grid.cpp`
- `src/base_grid.h`
- `src/command/audio.cpp`
- `src/command/edit.cpp`
- `src/command/grid.cpp`
- `src/command/subtitle.cpp`
- `src/command/time.cpp`
- `src/command/video.cpp`
- `src/dialog_selection.cpp`
- `src/dialog_shift_times.cpp`
- `src/dialog_spellchecker.cpp`
- `src/dialog_style_manager.cpp`
- `src/dialog_styling_assistant.cpp`
- `src/dialog_styling_assistant.h`
- `src/dialog_translation.cpp`
- `src/dialog_translation.h`
- `src/initial_line_state.cpp`
- `src/initial_line_state.h`
- `src/search_replace_engine.cpp`
- `src/selection_controller.cpp`
- `src/selection_controller.h`
- `src/subs_controller.cpp`
- `src/subs_edit_box.cpp`
- `src/subs_edit_box.h`
- `src/subs_edit_ctrl.cpp`
- `src/subtitle_binding.cpp`
- `src/subtitle_binding.h`
- `src/subtitle_row_list_edit.h`
- `src/video_box.cpp`
- `src/video_controller.cpp`
- `src/video_controller.h`
- `src/visual_tool.cpp`
- `src/visual_tool.h`
- `tests/native_preview_regression.cpp`
- `tests/tests/ordinary_edit.cpp`
- `tests/tests/subtitle_selection.cpp`

## 第二批：ID 调用链、跨回调状态与网格

恢复工作时重新检查：账号 xiaotianMC，fork push=true/upstream=false；fork 开放 PR=0，上游开放 PR 已逐项列出检查，不合入；实时远端仍为424d70baa60645410630d8e6a2ac1e27832a3880。两个保护文件 SHA256 与开始时一致。暂停期间的第二批未提交源代码保持在原工作区继续完成。

完成内容：

- 删除三个旧 pointer-input setters。普通选择仅接受 IDs；没有 LegacyMutable、返回 mutable live 行的替代 getter 或永久兼容入口。每个旧 getter/setter 都有单独的编译期不存在检查，getter 返回 const 类型、SelectedIDs 返回值集合也通过类型检查。
- BaseGrid 移除 RowsForWrite；SelectRow、鼠标选择、上下文菜单选行、键盘导航均不申请 WriteLease。只有号码列修改 Comment 保留受控写入。绑定组展开与 Ctrl/Alt 临时单行选择不变。
- 网格范围锚点存行 ID，排序后按身份恢复；删除锚点时，在移动活动行前解析起点，退回移动前活动行。真实回归首次暴露移动后回退会缩成单行，已修复并复验。
- 编辑框存 active_id，各同步回调重新取得 const 行；拼写扫描的当前行及循环起点存 IDs，起点消失时重新开始。InitialLineState 已使用 ID；音频、视觉和 B3 的长期行数据是已有独立快照，不改回 live 指针。
- SubtitleRowListEdit 在独立副本上保留可变操作能力，结果直接按 IDs 安装选择，删除扫描当前工程取 mutable 行的桥接。Binding 与视觉特征选择也直接使用 IDs。
- Select Lines、选择全部/可见、查找、历史恢复及项目最终选择安装迁移 IDs。Lua 只改 getter 和最终选择安装；Lua 对脚本公开的行号、后台处理和模型事务仍属 C1-C1c，没有提前迁移。
- UndoInfo 的历史字段仍为原有 vector<int>/active_line_id；Apply 的模型重建、旧节点保留、Commit 和恢复文字选区次序未改，仅改选择恢复输入。保留原有空 Active 不覆盖历史 active_line_id 的行为。OnCommit 和 AddUndoManager 权限未扩大。

实际修改文件（另含本报告）：

- `src/auto4_lua.cpp`
- `src/base_grid.cpp`
- `src/base_grid.h`
- `src/command/grid.cpp`
- `src/command/subtitle.cpp`
- `src/dialog_selection.cpp`
- `src/dialog_spellchecker.cpp`
- `src/project.cpp`
- `src/search_replace_engine.cpp`
- `src/selection_controller.cpp`
- `src/selection_controller.h`
- `src/subs_controller.cpp`
- `src/subs_edit_box.cpp`
- `src/subs_edit_box.h`
- `src/subtitle_binding.cpp`
- `src/subtitle_row_list_edit.h`
- `src/visual_tool.cpp`
- `tests/native_preview_regression.cpp`
- `tests/tests/ordinary_edit.cpp`
- `tests/tests/subtitle_selection.cpp`

验证结果：

| 检查 | 实际结果 |
| --- | --- |
| Windows x64 Release 主程序及测试目标 | 成功，build/c1c1b-callers-build.log |
| 全部应用生产入口回归 | 104/104：普通37、音频24、视觉37、Selection UI6；build/c1c1b-callers-app.log |
| 模型、比较、Selection、访问边界 | 45/45；build/c1c1b-callers-model.log |
| 基础回归 | 436/436；build/c1c1b-callers-base.log |
| 总计 | 585/585 |
| 旧 getters/setters、RowsForWrite 生产声明/调用 | src 中无匹配；每个旧公共方法另有独立 C++ requires/static_assert 检查 |
| 差异检查 | git diff --check 通过 |

六个新增真实 Selection UI 测试使用生产 wx 键鼠/编辑框/查找/命令入口，验证比较会话内无需写租约的选择、绑定展开/修饰键/重复通知、排序与删除后的范围锚点、Undo/Redo 同 ID 重建后编辑框和网格恢复、已有授权的选择回调不能写入或重入历史、只读查找导航。最初新增测试误用私有 FindReplace，已改为公开 FindNext。自动审批曾拒绝可能误删测试尾部的编辑命令，命令未执行；改用精确补丁，全部原有测试保留。

## 最终边界与交付

第一批提交：424d70baa60645410630d8e6a2ac1e27832a3880，`refactor(compare): make selection state and observers read only`，579/579。第二批提交名称：`refactor(compare): route selection callers through stable row ids`，585/585。每批使用普通 push，提交哈希与远端的最终核对由交付时实际执行并在回复给出。

本阶段已关闭 Selection 这条公开可变访问路径。普通选择是 UI 状态，不推进字幕修订、不新增 Undo；active_row 仍兼容写回原有 Properties UI 字段。持久化 Metadata 的独立修订/脏状态和完整 Properties 分离属于 C1-C1c，本阶段没有实现。

仍存在、明确留给 C1-C1c 的路径：

1. Context::ass 当前仍为公开 mutable owner。直接调用它可以绕过受控值 API；不能据本阶段宣布整个工程所有写入已被封闭。
2. UndoInfo::Apply 在 Undo/Redo 已有 WriteScope 下执行可信模型重建；本阶段仅收口选择读取/恢复。其原始容器操作的内部 capability 及 owner 私有化留 c。
3. OnCommit 经 TryWrite 后做可信历史维护，位于 PushState 而非普通通知阶段；保存联动与补默认行/样式仍用旧路径。AddUndoManager 的注册与 AssFileCommit.single_line 可变 payload 尚未最终私有化，不能把公共历史回调说成已安全封闭。
4. 选择通知期间复用 IsNotifying，可拒绝 TryWrite、已有 WriteAccess、Interaction 写回及 Undo/Redo；允许同步嵌套选择导航。它不能阻止还持有 Context::ass 的旧代码直接写模型，最终 owner 封闭仍必需。
5. Lua、Load/Close/Save（含保存副本清理）、字体收集输入隔离未在本阶段提前实现。Lua 原有脚本行号协议与后台原始模型参数属于下一阶段。

保留的 const 行视图仅用于同步读取。BaseGrid 的 const 行绘制索引是 Commit 时重建的绘制缓存，选择/范围状态存 IDs；它不提供模型写权限。独立 AssFile、副本、解析器和导出模型的正常可变能力保留。signal.h、Context owner、Undo 历史格式和第三方依赖均未修改。

验证范围：本次是 Windows 自动生产 wx 事件/模型/基础回归；没有声称执行实体键鼠、真实音视频设备、HiDPI/多屏、完整主窗口重入或 macOS/Linux 测试。上下文菜单选行、Lua 与 Load/Close 的机械接口适配经过主程序构建和调用链核查，未单独进行这些端到端人工操作。已有音频、视觉、Drawing、B3、保存重开等回归全部保留。native_preview_regression.cpp 是旧独立测试脚本源码，仅做接口适配，没有将它算入585项。此次没有新增持久化功能或移动端功能。

用户原有差异清单、未跟踪 checkasm.wrap 及未跟踪 c1c1 调查报告不纳入任何提交，未修改它们。C1-C1b 交付后停止，等待复核，不自动执行 C1-C1c。
