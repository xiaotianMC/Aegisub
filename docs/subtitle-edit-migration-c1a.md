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

## 最终覆盖和计数口径

按源码中的授权分支计 **99 处**，其中同一函数的独立写入分支分别计数；不以字段赋值次数、调用次数或测试断言次数计数。
命令 operator 共 **71 个**（第二批 70 个，第三批另加 tool/resampleres）。盘点范围内普通写入入口 **0 个待迁移**；下列授权分支外的私有 helper 由表中入口的同一作用域覆盖，不重复计数。
完整入口语义、对象和测试要求见前面的实施前表；以下表格标明实际已接入的位置。

| 实际修改文件 | 已接入分支数 | 已接入函数/分支与位置 |
| --- | ---: | --- |
| `src/base_grid.cpp` | 5 | 已接入：SelectRow、OnMouseEvent 注释/选择、OnContextMenu、OnKeyDown；授权行 250, 485, 494, 615, 773 |
| `src/dialog_attachments.cpp` | 2 | 已接入：AttachFile、OnDelete；授权行 164, 247 |
| `src/dialog_properties.cpp` | 1 | 已接入：OnOK（SetInfoIfDifferent 由其覆盖）；授权行 224 |
| `src/dialog_shift_times.cpp` | 1 | 已接入：Process；授权行 367 |
| `src/dialog_spellchecker.cpp` | 2 | 已接入：FindNext（CheckLine 显式传递 lease）、Replace；授权行 248, 342 |
| `src/dialog_style_editor.cpp` | 1 | 已接入：Apply（包含 StyleRenamer）；现有样式使用工程版本令牌拒绝过期编辑；授权行 426 |
| `src/dialog_style_manager.cpp` | 5 | 已接入：OnCopyToCurrent、PasteToCurrent、OnCurrentDelete、OnCurrentImport、MoveStyles(false)；授权行 536, 583, 669, 683, 869 |
| `src/dialog_timing_processor.cpp` | 1 | 已接入：OnApply（Process 和 SortDialogues 由其覆盖）；授权行 310 |
| `src/search_replace_engine.cpp` | 2 | 已接入：FindReplace(true)、ReplaceAll；GetMatcher 改为纯只读，Replace 由外层覆盖；授权行 190, 263 |
| `src/subs_controller.cpp` | 3 | 已接入：OnCommit、Undo、Redo（UndoInfo::Apply 由后两者覆盖）；授权行 313, 375, 390 |
| `src/subs_edit_box.cpp` | 2 | 已接入：SetSelectedRows、CommitTimes（文本、全部元数据及相邻时间联动）；授权行 486, 510 |
| `src/subtitle_binding.cpp` | 2 | 已接入：BindSelectedSubtitles、UnbindSelectedSubtitles；授权行 63, 100 |
| `src/command/edit.cpp` | 34 | 已接入：33 个 operator 和 PasteSubtitleLines；覆盖 update_lines、parsed_line、paste/delete/duplicate/combine/split helpers；授权行 411, 425, 439, 453, 467, 481, 495, 509, 523, 668, 687, 767, 781, 795, 838, 851, 864, 919, 948, 1024, 1104, 1174, 1192, 1205, 1239, 1267, 1310, 1337, 1352, 1363, 1378, 1394, 1414, 1429 |
| `src/command/grid.cpp` | 16 | 已接入：16 个 operator：next/create、12 个 sort、move/up/down、swap；授权行 68, 108, 130, 144, 158, 172, 186, 200, 214, 228, 242, 256, 270, 384, 403, 423 |
| `src/command/subtitle.cpp` | 4 | 已接入：4 个 insert operator；授权行 140, 176, 189, 222 |
| `src/command/time.cpp` | 17 | 已接入：17 个 operator；其中音频计时委托只保护普通调用边界；授权行 112, 125, 139, 210, 224, 270, 286, 300, 313, 326, 339, 352, 365, 378, 393, 406, 420 |
| `src/command/tool.cpp` | 1 | 已接入：tool_resampleres 的 operator，覆盖当前工程 ResampleResolution 调用边界；授权行 99 |

附加修改文件：`src/ass_file.h/.cpp`（局部通知阶段），`src/subtitle_project_access.h/.cpp`（共享 C0 租约），`src/base_grid.h`、`src/subs_edit_box.h`、`src/dialog_style_editor.h`（const 缓存/行 ID/过期令牌），`src/subs_controller.h`（历史重入状态），`src/command/edit.h`（粘贴数据入口），`meson.build`（Windows 复用应用对象的测试程序），两份交付文档，以及 `tests/support/ordinary_main.cpp`、`tests/tests/ordinary_edit.cpp`、`tests/tests/subtitle_project_access.cpp`。

### 必须保留的兼容性说明

- 继续使用 C0 的门禁；普通嵌套操作共享同一个底层 WriteLease。比较期间拒绝发生在直接字段/容器写入和历史移动之前。
- 普通租约覆盖同步 modal 交互时只保留任务授权，没有持有门禁互斥锁。样式管理器本身不长期持租约，每个写事件分别授权。
- 网格、编辑框、样式管理器/编辑器及拼写窗口不再缓存当前模型的可变行/样式指针；短暂可变行表只在有效租约内使用，网格每次写事件构建 O(n) 行表。旧 SelectionController 可变 API 仍是后续缺口。
- Commit 的 PushState 内部历史维护可以共享租约；AnnounceCommit 阶段拒绝新的普通写操作。Undo/Redo 在历史恢复或提交通知中拒绝重入。Signal 的原有异常传播不变，没有修改 signal.h。
- 查找原来通过 const_cast 规范化当前字段，这是只读绕过。本批改为在副本上做 NFC 规范化，并使用已有 ICU Edits 将匹配范围映射回原文 UTF-8 字节。单次替换保留未匹配的原始字节；正则批量替换保留原有规范化替换结果。没有新增依赖。
- 样式复制到剪贴板原来会 UpdateData 当前样式，现仅序列化独立副本。普通复制仍不需要写租约。已迁移代码不使用 const_cast。
- 样式编辑器在工程版本变化后拒绝应用旧快照，并提示重新打开；防止 Undo/Redo/删除后的地址复用造成误写。独立样式库仍使用原接口，未被错误地限制为当前工程。
- 不新增 Undo 记录格式，不改变普通 Commit 合并规则、热键资源、菜单定义、保存策略或附加数据解释规则。
- 新门禁拒绝不产生部分编辑；原算法遇到分配/解析/外部观察者异常时的强原子性尚未提供。已发生的普通写入不会因租约释放自动回滚。该限制不能以本批测试通过掩盖，完整候选/原子发布仍属于后续阶段。

### 明确剩余的写入入口

本次进一步定位 **30 个后续函数入口**，普通模块不能替它们授予长期授权。此数字是下面清单的函数数，不是“全局只剩 30 条赋值路径”的保证；任意持有旧公共字段或可变 API 的调用者仍可能绕过门禁。
这些属于明确排除的 C1-B/C 范围，在本批无法迁移的原因分别是脚本事务、实时工具生命周期、加载/保存事务和旧访问面，不能开放比较会话入口。

| 后续批次 | 文件 | 尚未迁移函数（按独立函数计） | 数量 |
| --- | --- | --- | ---: |
| C1-B/C | src/auto4_lua_assfile.cpp | LuaAssFile::ObjectIndexWrite、ObjectDelete、ObjectDeleteRange、ObjectAppend、ObjectInsert、ProcessingComplete | 6 |
| C1-B | src/audio_karaoke.cpp | AudioKaraoke::AcceptSplit | 1 |
| C1-B | src/audio_timing_dialogue.cpp | AudioTimingControllerDialogue::DoCommit | 1 |
| C1-B | src/audio_timing_karaoke.cpp | AudioTimingControllerKaraoke::DoCommit、ApplyLead | 2 |
| C1-B | src/visual_tool.cpp | VisualToolBase::SetOverride、Commit（派生视觉工具也必须登记生命周期） | 2 |
| C1-B | src/visual_tool_rectangle.cpp | VisualToolRectangle::Finish | 1 |
| C1-B | src/visual_tool_vector_clip.cpp | VisualToolVectorClip::Save | 1 |
| C1-B | src/dialog_rectangle.cpp | EditDrawing（实时 update/cancel 回调） | 1 |
| C1-B/C | src/dialog_video_properties.cpp | UpdateVideoProperties（含 matrix/play/layout helper） | 1 |
| C1-B | src/dialog_styling_assistant.cpp | DialogStyling::Commit | 1 |
| C1-B | src/dialog_translation.cpp | DialogTranslation::Commit | 1 |
| C1-B | src/dialog_kara_timing_copy.cpp | DialogKanjiTimer::OnClose（接受结果的批量写回） | 1 |
| C1-C | src/project.cpp | Project::UpdateRelativePaths、CloseVideo（及加载队列的整体生命周期） | 2 |
| C1-C | src/subs_controller.cpp | Load、Close、Save、AutoSave；自动保存策略本批未改变 | 4 |
| C1-C | src/selection_controller.cpp | SetActiveLine、SetSelectedSet、SetSelectionAndActive 的旧公共接口 | 3 |
| C1-C/收口 | src/ass_file.cpp | Commit 的旧直接调用、CleanExtradata；其他公共容器/字段写入面也须收口 | 2 |

独立读取器、导出副本和独立样式库不是当前工程写任务，保留其可变接口。普通网格/时间命令委托音视频功能时，本批仅保护调用边界，音视频内部的其他触发路径仍未受保护。后台任务登记、排队加载、必要观察者确认、旧节点退休协议接入与原子发布没有提前实现。

### 实际测试和局限

最终 Windows x64 Release 主程序及三个测试程序构建通过。基础 435/435、生产模型 24/24、真实应用对象集成 16/16 通过，共 475 项；相对 C0 新增 19 项。
集成测试逐一直接调用 71 个命令 operator，覆盖菜单/快捷键/工具栏共用的处理函数。wx 事件测试覆盖真实编辑框、网格、样式编辑器、样式管理器、脚本属性、附件；执行粘贴、增删、排序、样式导入/同名覆盖/移动、绑定、单次及批量替换、修改标记、Undo/Redo、通知及历史重入拒绝。
程序调用事件代替人工鼠标验收；没有逐个字典测试拼写后端，也没有完成音视频/脚本/加载保存的全链路验收。后续批次必须补测其生命周期，不能宣称全工程隔离完成。
测试设置、MRU、热键配置、字体预览宿主和数据均使用隔离目录；不运行旧 setup。粘贴测试不触碰系统剪贴板。日志为 build/c1a-final-build.log、c1a-final-main-tests.log、c1a-final-model-tests.log、c1a-final-ordinary-tests.log。
