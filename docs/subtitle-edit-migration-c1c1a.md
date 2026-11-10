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

## 功能族 4：样式及相关依赖、Info、Extra、附件与重采样

实际文件：
- `src/base_grid.cpp`
- `src/command/tool.cpp`
- `src/dialog_attachments.cpp`
- `src/dialog_properties.cpp`
- `src/dialog_spellchecker.cpp`
- `src/dialog_style_editor.cpp`
- `src/dialog_style_manager.cpp`
- `src/subtitle_binding.cpp`
- `src/subtitle_project_access.cpp`
- `src/subtitle_project_access.h`
- `src/video_controller.cpp`
- `tests/tests/ordinary_edit.cpp`
- `tests/tests/subtitle_project_access.cpp`
- `src/subtitle_value_edit.h`
- `docs/subtitle-edit-migration-c1c1a.md`

UpdateValues 接收独立原始/结果值和明确部分掩码，检查修订及全部原始行/样式/Info/Extra/附件依赖，完整准备后发布。行不增删/重排，已有样式以来源序号保留节点身份；新增/删除/重排样式及 Style/\r 引用在同一批次处理。新样式名称冲突、重复/无效来源、新的悬空 extra 引用在首次 live 写入前拒绝；原有同名样式与未知 extra 按原语义保留。不读写 Properties，不提供 mutable live getter、LegacyMutable 或 callback(AssFile&) 后门。
SubtitleValueEdit 是普通低频值编辑的独立工作数据：样式编辑/管理/导入/排序、Script Properties、附件增删、绑定和普通重采样沿用既有计算算法，再授权写回。普通重采样先快照后提示，不用旧设置覆盖外部修订；保留原单条撤销。拼写单次和自动替换的最后出口也统一经 WriteAccess；查询/renderer 输入进一步改 const。独立 Store/读入/导出/预览的 mutable AssFile 仍可正常操作。
新增两项模型测试验证所有依赖及 extra 引用在写入前拒绝、节点身份/Properties 保持；新增真实样式重命名与标签引用/重复 Apply/Undo，以及 Shift Times、Timing Processor、Resample 的生产窗口确认事件。Shift Times 是非模态窗口，补测最初误用 modal hook，已改成实际打开窗口并向其控件派发事件，关闭后不在宿主留下登记。

最终实际验证：

| 程序 | 结果 | 范围 |
| --- | ---: | --- |
| ordinary-edit-tests.exe | 97/97 | 普通 36、音频 24、视觉 37；含本阶段新增 6 项生产入口测试 |
| tests/subtitle-compare-tests.exe | 38/38 | 生产模型、读取/选择/Interaction，新增 5 项受控接口测试 |
| tests/gtest-main.exe | 436/436 | C0/阶段一及全部基础回归 |
| 合计 | 571/571 | 相对 034f7c6 新增 11 项 |

Windows x64 Meson Release 主程序与全部测试目标构建通过；已有编译警告保留。最终日志为 build/c1c1a-dependent-{build,ordinary,model,base}.log；补充对话框日志 build/c1c1a-dependent-dialogs.log。隔离目录 build/c1c1a-isolated，未运行旧 setup，未修改源夹具。基础文件权限测试在正常 Windows 权限环境执行。现有 ASS 保存重开/未确认内容不落盘回归随全套执行，不新增保存格式，不重复另造移动端验证。

提交名称：refactor(compare): gate ordinary dependent value edits。提交前复查 xiaotianMC、fork push=true、upstream push=false、fork PR=0、远端为 8a6a52f；最终提交/远端完整哈希在交付回复核对。

## C1-C1a 最终边界及复核

指定 a 的普通字幕写入口已迁移到受控行值、行结构和相关值操作。C1-A/B 的业务算法与 Undo 格式、amend 规则保留；已有 B1/B2/B3 写入口继续走 Interaction。没有修改 signal.h，没有新增第三方依赖，没有加入比较 UI/应用引擎/原子发布。

| 剩余路径 | 当前用途/风险 | 后续阶段 |
| --- | --- | --- |
| Context::ass 与 AssFile 公开容器、owning service pointers | 仍可直接到达 mutable 当前模型；a 尚不提供工程所有权封闭的编译期证明 | c |
| Selection mutable getters/payload/setters、BaseGrid::RowsForWrite | 仍泄漏可变能力；已迁移模块的剩余借用仅为 UI 选择定位/绑定扩展，未借它们写字幕字段 | b |
| subtitle_row_list_edit.h 内部 ID→旧 UI 选择桥接；subtitle_binding 的 ExpandBoundSelection；search/spell 的 mutable iterator | 当前不写字幕，后续 ID/const 选择 API 替换；不是新的永久 mutable getter | b |
| SubsController UndoInfo::Apply/OnCommit 内部原始容器操作与 public AddUndoManager | 入口已获 scope；受信历史维护及 PushState 注册边界尚未封闭，不改历史结构 | b/c |
| Lua/后台任务、Load/Close/Save/CleanExtradata、崩溃恢复、字体后台读取 | 仍有原先记录的非授权/跨生命周期路径；此次不顺带迁移 | c |
| Properties：UI State/持久 Metadata | 逻辑拆分、Metadata 独立修订/脏状态、ASS 兼容尚待实施；保存清理副本策略同样留 c | b/c |
| Selection 通知头文件描述 | 保持实际先 selection 后 active、重复选择仍通知；仅纠正文档的工作留 b | b |

普通 Commit 观察者仍按旧 signal 语义传播异常：模型/历史已发布后抛异常不声称自动回滚或全观察者重绑。结构/样式被移除的旧节点在当前操作及通知/选择刷新期间保留；完整异常后观察者恢复与候选发布仍不由 a 解决。不能将 a 的授权和值验证当作全工程原子事务。

高频字段/文本操作只复制目标行；结构及低频依赖编辑使用 O(n) 独立值快照与验证，未做跨平台性能承诺，也未声称通过实体键盘/鼠标、真实音视频、HiDPI、多显示器、字典后端及完整主窗口关闭人工验收。当前为原生桌面项目，未执行 macOS/Linux 构建。

两份保护文件的最终 SHA256 与开始一致：差异清单 5CE9B8077D0C83938BF5A4F9E8944586A0879D66049C44C0E62B840CE109127B；checkasm.wrap CE07862593607282739E118A1F94306E79AAFB0D9100C8C51C6517804017FA39。它们未暂存或提交。上一轮 c1c1 调查报告保持原来的未跟踪状态。
C1-C1a 编码与自动回归完成；最后普通推送及远端哈希核对成功后交付。本轮到此停止，不自动进入 C1-C1b，等待复核确认。完整工程写入隔离仍未完成。
