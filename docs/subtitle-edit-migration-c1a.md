# C1-A 普通编辑迁移：盘点、批次和验收

基线：`74480ae`，分支 `codex/style-name-width`。实施前 fork 分支一致、无开放 PR；上游开放 PR 包含 #760（编辑器）、#744（沙箱字幕）、#762（网格快捷键）、#738（文本插入）、#234（样式覆盖确认），本批不合入这些开发。
`本版与原版差异清单.md` 和未跟踪 `subprojects/checkasm.wrap` 为用户现有改动，不提交。

## 实施前盘点

下表按可直接触发的入口和其内部写入函数列出；同一调用链不按字段赋值次数重复计数。
现有 C0 字段操作已接入，以下普通入口实施前均尚未接入，标为“重构”的项目缓存了跨回调可变指针。
网格本身不提供内嵌文本编辑器，文本/时间/元数据编辑实际位于 SubsEditBox；增删排序实际在 Command，不在 SubsController。

| 文件 | 函数/入口 | 修改对象 | 现有修改方式 | 迁移方案 | 测试要求 |
| --- | --- | --- | --- | --- | --- |
| src/base_grid.cpp | BaseGrid::OnMouseEvent 的编号列点击 | Comment | 直接赋值后 Commit | A：写前租约，仅此分支 | 比较拒绝；其他点击保持选择行为 |
| src/subs_edit_box.cpp | SetSelectedRows、CommitText、MakeMarginCtrl 事件 | Text/Layer/Style/Actor/Margin/Effect/Comment | 可变选择行指针、字段 setter | A：公共批量 setter 获租约，保持合并 Commit | 所有字段、多选、拒绝时控件恢复 |
| src/subs_edit_box.cpp | CommitTimes | Start/End/相邻边界 | 直接赋值、LinkAdjacentLine | A：完整生命周期；缓存改为行 ID（重构） | 1ms、负时长修正、相邻联动 |
| src/subs_edit_box.h/.cpp | line、active_style、initial_times | 跨回调指针 | 成员保存可变行/样式 | A：只读成员、时间缓存按 ID（重构） | 回调后不保存写指针 |
| src/command/edit.cpp | color/{primary,secondary,outline,shadow}、style/{bold,italic,underline,strikeout}、font | ASS 标签 | update_lines、picker 回调直接赋值 | B：operator 及共享写入 helper，租约覆盖同步颜色回调 | 多处标签、取消、嵌套编辑 |
| src/command/edit.cpp | line/{cut,delete,duplicate,split/after,split/before} | Events/字段/选择 | 删除节点、插入复制行 | B：单次操作租约 | ID、顺序、全部删除、Undo |
| src/command/edit.cpp | line/join/{as_karaoke,concatenate,keep_first}、line/recombine | Events/Text/时间 | 合并、删除、更新选择 | B：同一租约贯穿 helper 和 Commit | 多行结果、顺序、选择 |
| src/command/edit.cpp | line/{paste,paste/over}、try_paste_lines、paste_lines | Events/全部可选字段 | 解析剪贴板、splice/覆盖 | B：在读取目标指针前获授权 | 粘贴顺序、覆盖、取消 |
| src/command/edit.cpp | line/split/{by_karaoke,estimate,preserve,video} | Events/Text/时间 | 插入、拆分、删原行 | B：完整操作生命周期 | 行数、顺序、字段 |
| src/command/edit.cpp | line/reverse_move、revert、clear、clear/text、insert_original | Text | 直接赋值/UpdateText | B：写前租约 | 标签、原文、Undo 粒度 |
| src/command/edit.cpp | undo、redo | 历史、整个当前模型 | 调用 SubsController | B：委托到已保护控制器 | 描述、修改标记、ID 恢复 |
| src/command/grid.cpp | line/next/create | Events/时间 | 音频 Commit 后可能插入行 | B：只保护普通入口边界，音频内部留 C1-B | 空尾行插入、拒绝时无音频写调用 |
| src/command/grid.cpp | sort/{actor,effect,end,layer,start,style} 及各 /selected | Events 顺序 | AssFile::Sort + Commit | B：每个 operator 租约 | 全部/选择排序、选择和 ID |
| src/command/grid.cpp | move/{up,down}、swap | Events 顺序 | intrusive splice/swap | B：写前租约 | 行顺序、撤销 |
| src/command/subtitle.cpp | insert/{before,after} 及 /videotime、insert_subtitle_at_video | Events/时间/Style | new 行插入、选中新行 | B：完整操作租约 | 首尾插入、合法 ID |
| src/command/time.cpp | continuous/{start,end}、frame/current、snap/{end_video,start_video,scene} | Start/End | helper 直接赋值 | B：operator 和 helper | 毫秒精度、多选 |
| src/command/time.cpp | lead/*、length/*、start/*、next、prev | 音频计时控制器 | 委托音频工具 | B：只保护命令边界；内部暂不迁移 | 普通命令被拒绝时不进入音频 |
| src/subs_controller.cpp | Undo、Redo、UndoInfo::Apply | Info/Styles/Events/附件/Extradata/选择/历史 | 原地重建、list splice | B：历史移动前获取租约，保留原有历史结构 | Undo/Redo 完整恢复、拒绝零修改 |
| src/subs_controller.cpp | OnCommit | 历史、缺省行/样式 | 补空模型、清 Redo、保存快照 | B：嵌套共享租约；普通通知行为保留 | 合并粒度、修改标记 |
| src/subtitle_binding.cpp | BindSelectedSubtitles、UnbindSelectedSubtitles、SetBinding | Extradata/行引用 | AddExtradata/清理旧组/赋值 | B：公共函数保护，内部 helper 同一生命周期 | 组成员、解除、Undo/Redo |
| src/search_replace_engine.cpp | FindReplace(true)、ReplaceAll、Replace | Text/Style/Actor/Effect | 字段赋值，批量循环 | B：仅替换获取租约，查找保持只读 | 单次替换、多行替换、regex |
| src/dialog_properties.cpp | OnOK、SetInfoIfDifferent | Script Info | SetScriptInfo | C：事件入口保护 | 拒绝时不关闭或修改 |
| src/dialog_style_editor.cpp | Apply、StyleRenamer::Replace/Walk | Styles、Style 引用和 \r 标签 | 覆写样式、插入、遍历替换 | C：写前租约；当前样式按名称定位（重构） | 新建、改名、引用、取消 |
| src/dialog_style_manager.cpp | OnCopyToCurrent、PasteToCurrent、OnCurrentDelete、OnCurrentImport、MoveStyles(false) | Styles | push/delete/排序/覆盖 | C：事件级租约，当前缓存改 const（重构） | 复制、导入、覆盖、排序 |
| src/dialog_style_manager.cpp | ShowCurrentEditor、OnCurrent{New,Edit,Copy} | 委托编辑器 | 持有当前样式指针打开 modal | C：只读指针，实际 Apply 重新定位 | Apply 多次、取消、缓存 |
| src/dialog_shift_times.cpp | Process | Start/End | 逐行赋值 | C：事件生命周期租约 | 时间精度、多行 |
| src/dialog_timing_processor.cpp | Process、SortDialogues | Start/End | 排序临时指针、边界修正 | C：处理生命周期租约 | lead/相邻/关键帧行为 |
| src/dialog_spellchecker.cpp | FindNext/CheckLine、Replace | Text | 自动/单次拼写替换 | C：写入路径保护，缓存改 const（重构） | 自动替换、单词替换 |
| src/dialog_attachments.cpp | AttachFile、OnDelete | Attachments | 插入/erase | C：写入分支租约 | 附件不丢失、取消 |
| src/resolution_resampler.cpp；src/command/tool.cpp | ResampleResolution；tool_resampleres | Info/Styles/Events | 遍历字段修改 | C：普通调用边界，独立导出副本保留原 API | 分辨率字段、拒绝写入 |

## 明确不在本批迁移的写入路径

| 文件 | 函数/对象 | 现状和原因 | 后续批次 |
| --- | --- | --- | --- |
| src/auto4_lua_assfile.cpp | LuaAssFile 的行/样式/附加数据写入与 ProcessingComplete | 暂时无法在本批封闭：脚本事务和回调生命周期另行处理 | C1-B/C |
| src/audio_karaoke.cpp、audio_timing_dialogue.cpp、audio_timing_karaoke.cpp、command/audio.cpp | 音频 Commit/计时调整 | 时间轴的独立写入口，普通 time 命令只接边界 | C1-B |
| src/visual_tool*.cpp、video_controller.cpp、dialog_rectangle.cpp、dialog_video_properties.cpp | 可视编辑、视频属性、实时形状对话框 | 无模式/实时预览的长生命周期指针和事件 | C1-B |
| src/dialog_styling_assistant.cpp、dialog_translation.cpp、dialog_kara_timing_copy.cpp | 无模式工具 Commit | 实时预览/音频写工具，禁止顺带迁移 | C1-B |
| src/project.cpp、subs_controller.cpp | 工程加载、Load/Close、UpdateProperties、Save/AutoSave/CleanExtradata | 加载队列、保存和异步备份另有事务边界；本批不改策略 | C1-C |
| src/async_video_provider.cpp、subs_preview.cpp | 后台/预览独立 AssFile | 独立数据不应按当前工程限制；当前指针交接仍需审查 | C1-C |
| src/ass_parser.cpp、subtitle_format*.cpp、ass_style_storage.cpp | 读取器/导入导出副本/独立样式库 | 独立模型允许修改，当前模型调用者负责授权 | 保持独立接口 |
| src/ass_file.h、ass_dialogue.h、selection_controller.h | 旧公共容器和可变指针 API | 渐进边界仍存在；本批必须封闭已迁移入口，但不能声称全部底层字段访问受控 | C1-B/C 后收口 |

## 批次和依赖

1. A：在 C0 SubtitleProjectAccess 上增加主线程普通编辑作用域，嵌套共享同一 WriteLease；迁移网格、编辑框。独立构建并测试后提交。
2. B：依赖 A，迁移实际命令 operator（涵盖直接调用、菜单、快捷键、工具栏），以及控制器/查找/绑定公共入口。独立构建并执行历史、命令和模型测试后提交。
3. C：依赖 A/B，迁移普通对话框和样式管理；存储库/读取器的独立模型 API 保持兼容。再次完整回归后提交。

不更改快捷键、菜单定义、时间取值接口、普通 Commit 合并或历史存储格式。各批串行执行，避免共享入口的未完成迁移被误报为完成。

## 生命周期和失败边界

外层在任何当前模型直接写入之前获取作用域，内层同步操作共享其底层 C0 租约；没有内部互斥锁跨业务处理或 modal 等待。
租约覆盖多行循环、Commit、同步通知和选择更新。RAII 负责取消/异常释放；独立工程互不影响。
普通通知保留原 Signal 异常行为，不引入批量发布异常隔离。PushState 内部快照维护可共享租约；AnnounceCommit 观察者只能刷新，新的普通写作用域被拒绝，异常后恢复通知状态。Undo/Redo 不得在正在提交的通知内重入修改历史。
门禁拒绝发生在第一次模型写入和历史移动之前，因此拒绝不会留下部分修改。原业务取消语义、Undo 粒度保留。
此批不承诺把原有多步骤编辑升级为强异常安全原子事务：解析、分配和观察者异常原本可能留下部分修改，应在报告中区分门禁拒绝与原有异常路径，不能声称已实现候选发布。

## 验证方式

测试必须使用隔离目录，不调用旧 setup。使用真实生产模型及实际入口，补充入口清单静态审查以防漏接，不能只测试包装器。
基础回归、C0/阶段一测试持续通过；每批构建 Windows x64。图形界面若只做程序触发测试，明确报告未做人工鼠标验收。

## 渐进交付记录

- A：`d039e6a`，网格直接事件、编辑框两个公共写入口和嵌套租约/通知边界完成。真实 wx 入口 3 项、基础 435 项、模型 24 项通过。
- B：70 个命令 operator（edit 33、grid 16、subtitle 4、time 17）均写前授权；覆盖直接调用路径，不依赖分发器。Undo/Redo/OnCommit、两个绑定入口、FindReplace 的替换分支和 ReplaceAll、显式文本粘贴入口另行保护。
  Windows 构建、真实普通编辑集成 9 项、模型 24 项通过。70 个命令均在活动比较会话中逐个直接调用并验证零变化；执行真实粘贴、增删、排序、绑定、单次/批量替换、Undo/Redo 和历史重入测试。
  粘贴的数据入口复用原算法，与系统剪贴板读取分离；测试不更改用户剪贴板。
