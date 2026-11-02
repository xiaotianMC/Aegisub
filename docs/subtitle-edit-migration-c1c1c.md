# C1-C1c 活动工程 Owner 与生命周期收口

状态：实施中，尚未完成 C1-C1c，不具备据此开放 Apply Engine 的条件。

## 基线、安全检查与依赖顺序

2026-11-02，以实际 HEAD 3610a4530dbd94ccdec064a6ce6b16dde55c6a91（C1-C1b）为基线，分支 codex/style-name-width。重新检查所有未提交、暂存和未跟踪文件；用户原有修改仅差异清单、checkasm.wrap、未跟踪 c1c1 调查报告。三者保护，不提交、覆盖、stash、reset 或 clean。实时远端与 HEAD 一致。账号 xiaotianMC，fork push=true、upstream push=false；fork 开放 PR=0；上游全部开放 PR 已核查，不合入。上游 PR 首次查询超时，重试成功。

保护文件 SHA256：

- 本版与原版差异清单.md：5CE9B8077D0C83938BF5A4F9E8944586A0879D66049C44C0E62B840CE109127B
- subprojects/checkasm.wrap：CE07862593607282739E118A1F94306E79AAFB0D9100C8C51C6517804017FA39
- docs/subtitle-edit-migration-c1c1.md：41FD538A097521C3251427CAAABAD46E3D4FBE14AB17C19DB15A26E72B167FE5

必读历史文档中的失效边界按当前源码复核；前期资料表格保留各自交付时状态，当前 Selection 已完成 b 收口。用户任务中的 defb72b 与实际历史 d0fb72b 有拼写差异，不据此修改历史。

调整顺序：先独立保存快照、Properties/后台输入、可信历史与模型安装、Lua/Load/Close，最后私有化 Owner。直接先 private Context::ass 会使剩余调用者不可构建，或被迫引入禁止的 mutable 兼容接口；本阶段不引入这种接口。每个完整功能族独立构建、测试、提交、普通推送并核对远端。

## 第一批：保存快照与自动保存结果生命周期（C1-C1c-5 的先行依赖）

实际调用链：SubsController::Save 原先先修改当前文件名/路径/脏标记，再对 context->ass 执行 CleanExtradata 并直接写出；AutoSave 复制 AssFile 但原 AssFile 深复制不复制 Properties，worker 完成后主线程 lambda 借用裸 FrameMain*。Load/Close 换文档、析构排队完成结果没有文档归属凭据。

本批修改：

- SubtitleProjectAccess::Snapshot 在宿主线程/存活检查后取得完整独立副本，复制 Properties 并保留稳定行 ID，不复制 live gate 关联；返回可变对象是独立副本，不是活动模型。
- 手动保存先计算目标路径相对媒体的 Properties 副本，清理副本和写出；输出成功后再更新当前路径和保存状态。输出失败时内存模型、路径、历史、脏状态不变；FileSave 通知异常亦恢复原路径/Properties/保存标记。
- AutoSave 启动前取得完整快照并清理，仅向后台传独立副本。完成时回到主线程，先检查文档/控制器存活凭据，再使用控制器当前窗口；Load、Close 换凭据，析构在等待 worker 前撤销凭据，旧结果不会解引用已销毁控制器。失败结果恢复该版本的自动保存标记以允许后续重试。
- 原有定时器和公开 RequestAutoSave 共用生产入口，测试直接执行相同后台流程，未添加返回当前 mutable 模型的接口。
- CleanExtradata 在 Extradata 为空时仍清除保存副本的悬空行引用，当前内存工程不被清理，不新增 Undo 或字幕修订。独立 AssFile 原有 mutable 能力继续保留。

实际修改文件（另含本报告）：

- `src/ass_file.cpp`
- `src/project.cpp`
- `src/project.h`
- `src/subs_controller.cpp`
- `src/subs_controller.h`
- `src/subtitle_project_access.cpp`
- `src/subtitle_project_access.h`
- `tests/tests/ordinary_edit.cpp`
- `tests/tests/subtitle_project_access.cpp`

验证：Windows x64 Release 主程序及全部测试目标构建成功；应用107/107、模型46/46、基础436/436，合计589/589。新增四项：完整独立快照/ID/Properties/关闭后拒绝/空附加数据引用清理；真实 Save 的 UTF-8/注释/样式/绑定/时间精度及内存无清理；真实保存失败的路径/历史/脏状态保持；实际 AutoSave 队列保存输入后编辑和控制器销毁，文件重开含初始内容/Properties/绑定且旧完成回调安全。其余585项回归全部保留。

日志：build/c1c1c-save-build.log、c1c1c-save-app.log、c1c1c-save-model.log、c1c1c-save-base.log。测试均用 run_isolated.py，不运行旧 setup，不写用户配置/夹具。测试首轮发现断言误以为 ASS 能无损保存1ms、现有读取器能保留行尾空格；已按现有 ASS 厘秒舍入与读取行为修正兼容性断言，并分别验证原始保存字节保留尾空格、当前内存1001/2345ms不变、重开1000/2350ms。没有修改格式或解析规则。

已有局限/待后续批次：Context::ass 仍公开 mutable；历史/Load/Close 模型事务与 Lua 尚未封闭；Properties 尚无独立 Metadata 修订/脏状态；自动保存目前仍按旧字幕 commit_id 判定变化，Metadata-only触发纳入下一批；字体收集尚持有旧输入，需要独立化。此批不声称已完成 c-5 全部边界或整个 C1-C1c。

未执行 Linux/macOS CI或人工实体键鼠/真实设备/HiDPI验收；不存在新增移动端实现。人工与自动验证分别记录。最终 Owner、历史、Lua、Load/Close、Properties、编译期负例、端到端关闭重开及 CI 结果随后续批次补齐。

提交名称：fix(compare): isolate save snapshots and autosave results。普通推送与远端哈希由实际提交后的核对结果记录，不使用强制推送。

## 第二批：UI State / Metadata 分离（C1-C1c-6）

第一批实提交并普通推送：9c8b507103b83dba816febb275542eb0477c1c90，本地与远端一致。

ProjectProperties 逻辑拆成 ProjectMetadata 和 ProjectUIState，独立 AssFile 保留扁平字段兼容现有解析/写出。活动工程 UI 更新仅接收 UI 值类型，Metadata 使用限定字段 enum 和专用自动化设置接口，不接受通用 Properties 成员指针。访问均校验宿主线程和存活；UI 不推进字幕或 Metadata 修订，Metadata 只推进自身修订。Metadata 按已保存值判脏，恢复原值可清除脏状态；字幕历史/Undo 格式保持不变。

迁移媒体相对路径、脚本列表、导出偏好、样式库名及自动化设置；视频/网格的显示状态不再直接写混合 Properties。Selection 内部 active_row UI 写回保持原语义。Metadata 通知无可变 payload，通知期间沿用现有只读阶段，主窗口标题订阅独立通知。IsModified 合并字幕历史与 Metadata 脏状态。Save 成功记录实际保存的 Metadata；Load/Close 重设保存基线；自动保存同时检查字幕 commit_id 和 Metadata 修订，Metadata-only 改动不再被漏过。

发现并修复原有自动化设置持久化缺口：ASS writer 原先不写 map，parser 原先多保留键名前一个空格。本批使用原有 Automation Settings 前缀键格式写出并准确取键名，不引入新 ASS 格式。

Windows x64 Release 成功；应用109/109、模型47/47、基础436/436，共592/592。新增真实 Save→Close→Load（非仅独立读取）检验 Metadata/显示状态保存重开、失败保留 Metadata 脏状态；Metadata-only 自动保存实际文件重开；UI/Metadata 独立修订/通知/无读取插入/线程及关闭拒绝、UI 类型不含 Metadata 的编译约束。现有589项保留。日志 build/c1c1c-metadata-{build,app,model,base}.log。git diff --check 通过。

实际修改文件（另含报告）：

- `src/ass_file.h`
- `src/ass_parser.cpp`
- `src/auto4_base.cpp`
- `src/base_grid.cpp`
- `src/dialog_export.cpp`
- `src/dialog_style_manager.cpp`
- `src/frame_main.cpp`
- `src/project.cpp`
- `src/subs_controller.cpp`
- `src/subs_controller.h`
- `src/subtitle_format_ass.cpp`
- `src/subtitle_project_access.cpp`
- `src/subtitle_project_access.h`
- `src/video_controller.cpp`
- `src/video_display.cpp`
- `tests/tests/ordinary_edit.cpp`
- `tests/tests/subtitle_project_access.cpp`

提交名称：refactor(compare): separate project metadata from ui state。

剩余：公开 Context owner、可信历史恢复、Lua 工作数据与结果发布、Load/Close 工程身份和观察者重绑定、字体收集后台输入尚未最终封闭。AssFile parser 使用成员指针只作用于独立目标，不作为活动工程修改入口；不能把本批 Metadata 分离当作整个 Owner 已私有化。

## 第三批：可信历史恢复能力（C1-C1c-2 先行）

第二批实提交844bac5b3cfc570412360f0b0a66542f78780d33，普通推送、本地/远端一致。

实际调用链：AssFile::Commit -> PushState -> SubsController::OnCommit；Undo/Redo -> UndoInfo::Apply -> 模型重建/Commit/选择及文字选区恢复。原 AddUndoManager 对活动工程开放，AssFileCommit.single_line 为 mutable live 行。

修改 src/ass_file.h、src/subs_controller.h/.cpp、tests/tests/subtitle_project_access.cpp、tests/tests/ordinary_edit.cpp 及本报告。独立 AssFile 继续允许自身 AddUndoManager；活动工程的普通注册一律拒绝，由 friend SubsController 在内部连接 PushState。提交载荷的行变为 const，普通业务不能从历史载荷写 live 行。Undo/Redo 在已有工程写授权和 history_action 之后创建私有 HistoryRestore，Apply 额外验证控制器/工程归属、授权存活、精确修订、非通知阶段。凭据类型不能被业务命名，也没有返回可变 AssFile 的回调或替代 API。

保留现有 UndoInfo 的字段、模型重建顺序、旧节点保留、合并规则、描述和选择/文字选区恢复；OnCommit 仍是受信内部维护，按原顺序补默认行/样式、更新历史、联动保存，不变成普通观察者。该类仍借用旧 Context::ass，最终 Owner 私有化时收口内部借用，不能据本批宣布全工程已隔离。

Windows x64 Release 成功；应用110/110、模型48/48、基础436/436，共594/594。新测试实际验证普通写授权不能注册活动历史回调、真实编辑Undo合并与Redo，独立模型历史仍可用；compile约束确认历史恢复凭据非公开、single_line仅const。已有592项及通知回调重入拒绝测试保留。日志 build/c1c1c-history-{build,app,model,base}.log。git diff --check通过。

提交名称：refactor(compare): restrict live history restoration authority。

剩余不变：Owner、Load/Close和Lua模型发布/身份/重绑定、字体收集；公共mutable owner存在期间不声称普通业务所有绕过均被封闭。
