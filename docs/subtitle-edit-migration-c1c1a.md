# C1-C1a 受控值操作与业务迁移

范围：遵循 C1-C1 报告第 9 节的 a；不关闭 Selection 旧接口，不私有化 Context owner，不实现 Lua/加载保存隔离。
负责人已确认五项决策：调整 a/b/c 顺序；Lua/Load/Close/Save 最小适配归 c；UI State 不影响字幕修订，持久 Metadata 独立修订/脏状态且 ASS 格式兼容；保存只清理独立副本、不改 live/Undo；保持实际选择通知顺序及重复通知，仅纠正文档。后四项在相应批次执行，本批不提前实施 b/c。

开始状态：codex/style-name-width，034f7c695e1f5d6c7f7f01f7db7cf903384c8adb，本地/远端相同。账号 xiaotianMC；fork push=true/upstream push=false；fork 开放 PR=0，已复查上游 PR，不合入。差异清单和 checkasm.wrap 保持原样；上一轮未提交的 c1c1 调查报告保持未跟踪，不混入功能提交。

## 功能族 1：只读查询、订阅与普通行值

新增 AssFile::GetStyle const overload 与 const Commit 订阅；独立 AssFile 的 mutable overload/构造/容器能力保留，未修改 signal.h。已迁移的普通、B1/B2/B3 观察者及查询改走 subtitleAccess->Read()。
WriteAccess 增加整批 UpdateRows、按 ID 的 LinkAdjacent/Commit；Interaction 复用普通值校验。完整验证重复/缺失 ID 与 Row 后才改所有行，节点身份和 Undo amend 规则不变。WriteAccess::Check 拒绝 AnnounceCommit 期写入，包含已保存句柄及直接 gate->Edit 旁路。
编辑框 Text/元数据/多行时间先准备独立值再统一写回；不为每次输入复制全文件。网格注释开关使用 ID 字段操作。未改菜单/热键/保存格式。

实际文件：
- `src/ass_file.cpp`
- `src/ass_file.h`
- `src/audio_karaoke.cpp`
- `src/audio_timing_dialogue.cpp`
- `src/base_grid.cpp`
- `src/command/edit.cpp`
- `src/dialog_kara_timing_copy.cpp`
- `src/dialog_properties.cpp`
- `src/dialog_rectangle.cpp`
- `src/dialog_resample.cpp`
- `src/dialog_style_manager.cpp`
- `src/dialog_styling_assistant.cpp`
- `src/dialog_timing_processor.cpp`
- `src/dialog_translation.cpp`
- `src/dialog_video_properties.cpp`
- `src/frame_main.cpp`
- `src/grid_column.cpp`
- `src/subs_edit_box.cpp`
- `src/subs_edit_box.h`
- `src/subs_edit_ctrl.cpp`
- `src/subtitle_project_access.cpp`
- `src/subtitle_project_access.h`
- `src/video_box.cpp`
- `src/video_controller.cpp`
- `src/visual_tool.cpp`
- `tests/tests/subtitle_project_access.cpp`

验证：Windows x64 Release 主程序及所有测试目标构建通过；真实生产 wx 应用回归 91/91（普通/B1/B2/B3），模型 35/35（新增整批拒绝/节点身份与通知期所有句柄拒绝 2 项），基础 436/436，共 562/562。全部隔离运行，未运行旧 setup。日志 build/c1c1a-values-{build,ordinary,model,base}.log。

剩余：尚待迁移命令和批处理/结构/样式/Info/Extra/附件等 C1-C1a 操作族；Context::ass、AssFile 公开容器和 Selection mutable getter/通知仍存在，Lua、Load/Close/Save 等属 c。此提交不代表 C1-C1a 或完整工程隔离完成。实体设备/完整主窗口人工验收不由隐藏窗口事件测试替代。

提交名称：refactor(compare): route ordinary row values through write access。每批普通提交并推送 fork/codex/style-name-width，实际哈希由远端核对及交付回复记录。

## 功能族 2：文本命令和时间批处理（d0fb72b）

提交名称 refactor(compare): stage ordinary text and timing edits，完整哈希 d0fb72be0d861dc089b9a58901d075e524fa87ee，普通推送并核对远端一致。
实际文件：src/subtitle_row_edit.h、src/command/edit.cpp、src/command/time.cpp、src/dialog_shift_times.cpp、src/dialog_timing_processor.cpp、src/search_replace_engine.cpp、tests/tests/ordinary_edit.cpp。
目标行独立值覆盖标签/颜色/活动行文本、时间对齐/吸附、ShiftTimes、TimingProcessor、替换。颜色回调校验修订。新增真实多行标签 Undo/Redo 及 adjoin/批量替换入口测试。Release 构建通过，应用 93/93、模型 35/35、基础 436/436，总 564/564。隔离日志 build/c1c1a-rows-{build,ordinary,model,base}.log。提交前本记录写入进程启动失败，因此本节随下一完整批次补齐，不另做微小提交。

## 功能族 3：普通行结构操作

实际文件：src/subtitle_project_access.h/.cpp、src/subtitle_row_list_edit.h、src/command/edit.cpp、src/command/grid.cpp、src/command/subtitle.cpp、tests/tests/subtitle_project_access.cpp、tests/tests/ordinary_edit.cpp、本记录。
普通插入/删除/粘贴覆盖/复制/拆分/合并/重组/排序/移动在独立行列表上沿用原算法。ReplaceRows 在全部原始字段、修订、ID 和结果集合验证后准备新节点，再写回；保留未删除的 live 节点身份，退役节点由调用者保持至通知和选择更新结束。没有公开 mutable live pointer 或通用 callback(AssFile&)；不是比较候选应用/原子发布引擎。
选择仅经 helper 内部 ID→旧 API 的同步桥接，未改 Selection 接口/通知行为。新测试验证错误/重复/过期结果整批拒绝与退役存活，以及生产复制/移动/合并/拆分、原节点身份和 Undo/Redo。Release 构建通过，应用 94/94、模型 36/36、基础 436/436，共 566/566。隔离日志 build/c1c1a-structure-{build,ordinary,model,base}.log。
剩余：a 的样式/Info/Extra/附件和重采样；b/c 的公开 Selection、owner、Lua、I/O 及 Properties 旁路未收口。保护文件哈希不变。
提交名称：refactor(compare): gate ordinary row structure changes。
