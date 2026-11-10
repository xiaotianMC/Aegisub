# C1-C1c 活动工程 Owner 与生命周期收口

状态：已按任务第十节暂停并提交阻断调查；尚未完成 C1-C1c，不具备进入 Apply Engine 的条件。

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

## 第四批：字体收集后台输入与窗口退出（C1-C1c-5 后台读取边界）

第三批实提交c67175fe1e49aeecf5ee61846986b1e17408b724，普通推送/远端一致。其新增撤销测试首轮错误地把两个STARTACTION视作同组；按既有两组语义修正断言后完整复验，未改生产撤销粒度。

修改 src/dialog_fonts_collector.cpp、tests/tests/ordinary_edit.cpp 及本报告。原 DialogFontsCollector 长期保存活动 AssFile* 和 Context 路径引用，将 live 模型直接传给公共后台队列，窗口退出仅靠禁用关闭按钮。本批在每次Start主线程取得 shared_ptr<const AssFile> 独立快照并传入worker，路径在宿主线程复制；窗口拥有自己的串行队列，析构等待队列退出，worker不再在事件目标销毁后继续投递。等待期间不持有模型/门禁互斥锁，不新增模型租约，独立字体读取在比较会话中可启动。

Windows x64 Release 成功；应用111/111、模型48/48、基础436/436，共595/595。新增实际模型窗口和Start按钮后台路径测试：比较期间启动、不改变字幕修订；快照启动后编辑当前字幕、取消窗口和父窗口销毁均正常，当前文本保持后续编辑。其余594项保留。日志 build/c1c1c-fonts-{build,app,model,base}.log，差异检查通过。

提交名称：fix(compare): detach font collection worker input。

Owner/Lua/Load/Close/完整安装及重绑定仍待后续，不据本批宣布C1-C1c完成。

## 阻断调查与暂停决定（2026-11-02）

执行用户任务第十节的明确停止条件：“如果某个子阶段发现原架构无法安全迁移，暂停并提交调查报告，不要用临时绕过方案强行完成。”此处暂停是上述任务要求；不是需要重复确认此前五项决定，也不是因为构建未通过或缺少推送权限。

### 两项实际失败的生命周期诊断

在 build 下单独构建诊断副本，复用生产应用对象、真实 SubsController/AssFile/Selection/编辑框入口及同一测试宿主，只添加两项诊断；没有改动正式测试源文件、测试数量或判定，也没有修改 signal.h。

1. 比较会话期间调用生产 SubsController::Load。Load 确实抛出授权拒绝，但拒绝前已经 swap 候选、更新文件路径/Metadata、清空 Undo。诊断中当前 Text 从 must survive denied load 变成 replacement candidate；行ID从1变3；Undo描述从 modify text 变空。
2. 新建一个抛异常的 Commit 观察者后调用生产 Load。抛异常后当前模型仍是 replacement candidate，并未恢复原文本；后续读取观察者的刷新计数为0。生产加载的局部旧模型会在异常展开后析构，因此仍未刷新、持有旧行/样式索引的观察者有悬空引用风险。诊断没有故意解引用悬空地址，也没有把可能的崩溃说成已实际发生。

结果：两项诊断均失败，准确复现尚未满足的安全要求。**它们不是通过项，不计入595/595正常回归。** C1-C1c整体不满足“所有测试通过”的完成条件。

诊断文件：build/c1c1c-lifecycle-probe.cpp、c1c1c-lifecycle-probe.exe、c1c1c-probe-build.cmd、c1c1c-lifecycle-probe.rsp；日志 build/c1c1c-probe-build.log 和 build/c1c1c-lifecycle-probe.log。全部在忽略的build目录和run_isolated.py生成的临时目录执行，无用户数据被改动。运行命令：

```
python tests/run_isolated.py build/c1c1c-lifecycle-probe.exe --output-root build/c1c1c-lifecycle-probe-data --gtest_filter=ordinary_edit.lifecycle_probe_*
```

诊断源码（保留以便复核/重建，而不是删除负向失败）：

```cpp
#include "O:/Aegisub-Native/tests/tests/ordinary_edit.cpp"

TEST_F(ordinary_edit, lifecycle_probe_denied_load_must_preserve_live_model_and_history) {
    Input("must survive denied load");auto id=c->ass->Events.front().Id;
    auto undo=c->subsController->GetUndoDescription();
    AssFile candidate;candidate.LoadDefault();candidate.Events.front().Text="replacement candidate";
    auto path=agi::fs::path((std::filesystem::current_path()/"probe-candidate.ass").wstring());
    SubtitleFormat::GetWriter(path)->WriteFile(&candidate,path,0,"utf-8");
    auto session=c->subtitleEditGate.TryBeginComparison();ASSERT_TRUE(session);
    EXPECT_ANY_THROW(c->subsController->Load(path,"utf-8"));
    EXPECT_EQ("must survive denied load",c->ass->Events.front().Text.get());
    EXPECT_EQ(id,c->ass->Events.front().Id);
    EXPECT_EQ(undo,c->subsController->GetUndoDescription());
}
TEST_F(ordinary_edit, lifecycle_probe_throwing_observer_must_not_leave_partially_published_load) {
    Input("must survive observer failure");int refreshed=0;
    agi::signal::Connection reader=c->ass->AddCommitListener([&](int,AssDialogue const*){++refreshed;});
    agi::signal::Connection throwing=c->ass->AddCommitListener([](int,AssDialogue const*){throw std::runtime_error("probe observer");});
    AssFile candidate;candidate.LoadDefault();candidate.Events.front().Text="replacement candidate";
    auto path=agi::fs::path((std::filesystem::current_path()/"probe-observer.ass").wstring());
    SubtitleFormat::GetWriter(path)->WriteFile(&candidate,path,0,"utf-8");
    EXPECT_THROW(c->subsController->Load(path,"utf-8"),std::runtime_error);
    EXPECT_EQ("must survive observer failure",c->ass->Events.front().Text.get());
    EXPECT_EQ(1,refreshed);
}

```

### 为什么不能直接把 Owner 改 private 后加授权包装

- 当前实际链为 Project::DoLoadSubtitles -> SubsController::Load -> 独立解析 -> live swap -> 清历史 -> AssFile::Commit -> 私有历史OnCommit检查 -> 只读通知。只在OnCommit检查WriteLease显然过晚。
- 只把检查提前、或给Load/Close统一加普通scope，能修复“被拒绝时已经写入”的第一项，却无法修复第二项的中途发布、历史与选择回滚、旧节点释放和观察者失败。
- C0 SubtitleRebindBatch 现有实现会逐观察者记录明确确认，并在失败时持有retired数据、保持门禁封闭；但是当前生产 Commit/Load 没有接入这个协议。通用Signal遇异常中断，不能把整个Signal当成单个成功观察者、不能以“回调正常返回”冒充明确确认、也不能忽略抛异常后释放旧数据。
- C0当前WriteLease登记绑定State及owner_active，没有每文档身份字段；Load/Close只做旧Commit失效代数，不能仅靠原租约或取消标记证明旧结果不会写入新文档。需要安装时的独立工程/文档身份校验与失效边界。
- Lua目前的ProcessingComplete逐个应用pending undo point，直接依赖当前模型的节点和Extradata；Validate/IsActive/config及set_context的若干后台读取还借用Context。机械地改一个独立输入或为每一步加scope，不能证明整个结果批次在失败、取消、换文档时无部分发布，也不能解决同一个观察者失败问题。
- 因此本次没有创建LegacyMutable、通用mutable AssFile回调、私有owner的公开替身或全局布尔锁来跨过这个边界。

### 继续实施所需的最小架构前置

建议先形成一个明确的“宿主模型安装及回滚”完整功能族，由Load/Close、Lua结果和可信历史恢复共用内部安装能力，再最终private Owner：

1. 在独立输入中完整准备候选值、默认行/样式、ID和Extradata引用、Lua全部历史点、目标选择，以及安装失败时所需的原历史/Metadata/路径/模型状态，准备失败不碰live。
2. 仅宿主线程通过绑定目标工程、文档身份、修订的私有安装能力进入发布。旧任务令牌明确失效；普通写授权不能伪造恢复或文档替换权限；不向业务返回live AssFile&。
3. 对必须重绑定的具体观察者接入C0明确确认/失败保留协议。退役节点、历史和控制器状态由安装任务拥有，不能依赖Load局部变量或裸窗口存活；异常和关闭重入不能使该所有权提前消失。
4. 规定并实现失败恢复：候选/首个历史点发布前拒绝保证原工程不变；发布中通知失败要保留足够状态，回滚或进入明确的失败恢复状态，不能继续开放普通写/比较。旧数据在全部必要观察者完成重绑定前不得释放。
5. 对两项现有负向诊断改为正式应通过回归，再新增Lua取消/错误/多Undo点、旧工程结果、工程关闭、观察者失败重入/销毁及Save/AutoSave恢复验证。不得仅让当前正常595项继续通过就宣布该族完成。

这属于后续编码必须先完成的内部发布能力，不是提前实现比较窗口或Apply Engine。本轮停止于该安全边界，等待复核后继续，不自动扩大为临时兼容绕过。

## 当前交付总表与最终结论

| 项目 | 本次实际状态 |
| --- | --- |
| 保存/CleanExtradata/一致性快照 | 已完成先行隔离；不修改内存字幕，不新增Undo；失败保持、保存重开验证通过 |
| AutoSave/字体收集 | 已完成独立后台输入和已覆盖的退出/旧结果拒绝；完整工程切换/恢复验收尚未完成 |
| UI State/Metadata | 已拆分值类型和活动更新入口；独立Metadata修订/脏状态/保存自动保存触发及ASS兼容重开通过；旧Owner旁路仍待封闭 |
| Undo/Redo | 私有HistoryRestore、只读载荷及私有live历史注册完成；既有历史格式/恢复/合并/通知回归通过；最终owner借用与安装失败整体恢复仍待完成 |
| Owner最终访问模型 | **未实现**；Context::ass仍公开mutable，不能宣称普通业务已无旁路 |
| Lua | **未迁移**；仍有live引用、后台Context和逐点发布路径，上述调查已列明 |
| Load/Close | **未完成**；两项实测负向失败，必须先补内部安装/回滚/重绑定能力 |
| 编译期保护 | Snapshot独立模型可变、UI无Metadata字段、历史行const/恢复凭据非公开等已通过；Owner不可变等最终负例尚未满足 |
| 运行时/功能回归 | 已完成四族的595/595正常回归通过；另2项生命周期诊断失败，不合并掩盖 |
| 持久化 | Save、独立ASS重开、Metadata-only Save→Close→Load及AutoSave文件重开通过；绑定、样式、注释、UTF-8、标准ASS时间精度已测；完整自动保存恢复流程未完成 |
| Windows | x64 Release实际构建成功 |
| Linux/macOS CI | GitHub Actions按当前fork分支查询workflow_runs为空；未执行，不声称通过 |
| 人工 | 实体键鼠、真实媒体/设备、HiDPI、多屏、完整主窗口重入和Lua生态脚本人工验收未执行 |
| Apply Engine基础条件 | **尚未达到**；C1-C1c未完成 |

已独立提交、普通推送并逐批核对一致：

- 9c8b507103b83dba816febb275542eb0477c1c90 — fix(compare): isolate save snapshots and autosave results
- 844bac5b3cfc570412360f0b0a66542f78780d33 — refactor(compare): separate project metadata from ui state
- c67175fe1e49aeecf5ee61846986b1e17408b724 — refactor(compare): restrict live history restoration authority
- ff46c1c5da5c0de605dcb3b3eea3d35a31fd39cc — fix(compare): detach font collection worker input

暂停调查报告独立提交名称：docs(compare): record lifecycle publication blockers。该报告提交哈希以提交后实际远端核对为准，避免自引用哈希。

最终检查三个用户保护文件SHA256与本轮开始完全一致；原有三个未提交/未跟踪项目均保留，不纳入提交。没有改signal.h、增加第三方依赖、改变Undo历史格式、引入永久mutable兼容入口、实现比较窗口、Apply Engine或移动端。
