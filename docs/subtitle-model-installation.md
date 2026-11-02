# C1-C1c-7 宿主模型安装与恢复交付报告

2026-11-02。C1-C1c-7 的安装基础设施及最小生产 Load 发布边界已完成；本报告不宣布整个 C1-C1c 完成。正式设计先于生产代码，见 `subtitle-model-installation-design.md`。交付后停止，等待复核，不进入 c-8、完整 Lua 迁移或 Context Owner 最终私有化。

## 1. 安全基线与权限

基线 `d114561f481f8b42223997c14d41535be08621f9`；分支 `codex/style-name-width`。开始时本地与 fork 远端一致。重新检查工作区、暂存区、未跟踪文件、远端、开放 PR、C0 和真实 Load/Commit 调用链后实施。

当前认证账号 xiaotianMC，对 `xiaotianMC/Aegisub` push=true，对 `TypesettingTools/Aegisub` push=false；沿用现有 fork，无新建 fork，无上游合入，无强推。fork 开放 PR 为 0；上游开放 PR 已重新列出检查，不混入本阶段。作者信息不作为写入权限证据。

保留用户已有修改和未跟踪状态，不 reset、clean、stash 或暂存保护文件。阶段开始、各功能提交前及最终交付均核对以下 SHA256：

| 保护文件 | SHA256（保持不变） |
| --- | --- |
| 本版与原版差异清单.md | 5CE9B8077D0C83938BF5A4F9E8944586A0879D66049C44C0E62B840CE109127B |
| subprojects/checkasm.wrap | CE07862593607282739E118A1F94306E79AAFB0D9100C8C51C6517804017FA39 |
| docs/subtitle-edit-migration-c1c1.md | 41FD538A097521C3251427CAAABAD46E3D4FBE14AB17C19DB15A26E72B167FE5 |

## 2. 原始失败链与 C0 能力

原 Load：独立解析 → swap 活动模型 → 修改文件路径/Metadata → 清空 Undo/Redo → 普通 Commit。真正的 WriteLease 拒绝发生在历史 OnCommit，已经晚于上述修改。原诊断表现为 Load 虽抛出拒绝，文本、行 ID 和 Undo 描述仍被改变。

原观察者链：Commit 先推进旧代数并维护历史，随后普通 Signal 逆序通知。某个回调抛异常会中断后续通知；Load 局部旧模型在异常展开时析构，未刷新的网格/样式索引等仍可能借用旧节点。原诊断中后续读取观察者收到 0 次通知。

C0 SubtitleRebindBatch 原有能力：固定观察者名单、逐观察者 Ticket/显式 Acknowledge、局部异常隔离、Pending/Failure、retired 数据持有、未完成批次析构后门禁仍封闭，以及 Complete 后解除封闭。它没有生产 Load 提交点、完整候选准备或宿主观察者适配。本阶段复用这些机制，增加私有文档发布能力；没有修改通用 signal.h，也没有把整个 Signal 的正常返回当成 ACK。

原两项负向诊断源和失败日志继续保留在忽略的 build 目录；正式测试覆盖同一生产问题并加强断言，不删除失败证据，不把旧诊断计入正常通过项。

## 3. 最终安装协议与状态机

| 状态 | 行为及权限 | 退出 |
| --- | --- | --- |
| Idle / Committed | 正常门禁和编辑/保存行为；可开始新安装 | Preparing |
| Preparing | 捕获宿主令牌和 Metadata 修订；独立解析/复制；普通编辑和比较仍按原门禁，若变化则输入过期；保存/新安装/宿主销毁拒绝 | Prepared / Aborting |
| Prepared | 默认样式和行、ID/Extradata、选择/Active、路径、保存基线、历史和订阅快照已准备；未改 live | Authorizing / Aborting |
| Authorizing | 检查宿主线程、工程/文档/代数、Metadata 修订；尝试独占 C0 会话，拒绝其他 writer/comparison | prepare / Aborting |
| prepare | 回调只能准备私有值，保持原绑定；字幕、Metadata、Selection setter、UI State 和保存写入拒绝；允许取消 | Publishing / Aborting |
| Publishing | 先进入该状态；私有推进文档修订后，用已准备值交换活动模型、历史、路径、Metadata 和选择 | Rebinding / RecoveryBlocked |
| Rebinding | Commit → Selection → Active；每名单成员单独 C0 Dispatch；普通写/比较/保存/新发布封闭，视口 UI 刷新允许 | Committed / RecoveryBlocked |
| Aborting | 发布前丢弃候选/准备值并释放会话，原模型、历史、Metadata、路径、选择、保存状态、令牌和绑定不变 | 下一次 Preparing |
| Recovering | 显式重派发整个固定名单，使用同一事务/文档修订，观察者需可重复刷新 | Committed / RecoveryBlocked |
| RecoveryBlocked | 保留旧模型/恢复信息；封闭字幕写、比较、保存和新安装；定时自动保存跳过 | 显式重试 / 宿主关闭 |

解析、默认补齐、校验、历史初态和路径准备都在独立模型/值上完成。源模型深复制防止调用者保留 mutable 节点别名；Load 的解析副本在发布前释放，不在通知期间额外保留大型输入。只有必要的候选、历史快照和退役模型留存。

提交点不调用普通 AssFile::Commit。文档修订预先检查耗尽，再在 Publishing 内私有发布；随后交换已准备容器/值并安装目标 IDs。原 UndoInfo 格式不变，只增加从独立候选构造初始历史的内部构造函数。安装成功时是新的干净初态；普通编辑、合并、Undo/Redo 及恢复选择语义由原回归继续验证。

取消仅在发布前生效。发布后不伪装成普通取消或完整回滚，必须完成确认或进入 RecoveryBlocked。

## 4. 工程身份、文档身份和安装权限

C0 原 `SubtitleRevision::document` 继续标识工程 State；新增 `document_epoch` 表示同一宿主内文档实例。Generation 继续单调，发布时 epoch 和 generation 各推进一次。普通字幕 Commit 不推进 epoch；Undo/Redo 不回退身份。比较修订令牌包含三者，旧令牌在发布后失效，失败封闭也不把代数倒回旧值。

WriteLease 的注册、Valid、Token.Valid/Hold 加入 epoch 检查。宿主授权同时核对期望字幕令牌和独立 Metadata 修订；其他 WriteLease、Interaction 或 ComparisonSession 尚未结束时拒绝安装。工程、文档、代数、历史/Metadata 及新增登记计数器的增量都先检查耗尽，不回绕。

`SubtitleModelInstallation` 的构造、Install、状态推进和 C0 PublishDocumentRevision 全部为私有可信路径，只有 SubsController 拥有安装器。普通业务不能构造或获得该 Capability，普通 lease/session 也不能推进文档身份。公开状态、待确认名称、取消及恢复入口不返回 mutable AssFile 或活动模型 callback。编译期断言验证不能从 SubsController& 公开构造安装器。

受控旧任务的修订/写权限在换文档后被拒绝。AutoSave 的旧 lifetime 在发布时立即失效，不能因保留旧模型而继续保留旧任务的写回授权。尚未迁移的 Lua 和公开 Context::ass 仍是明确的阶段外旁路，不能据令牌扩展宣称所有 legacy Lua 已自动隔离。

## 5. 观察者登记、确认和旧模型生命周期

普通通知保持原有逆登记顺序、异常传播、Selection 先于 Active，以及选择集不变时重复 Selection 通知的行为；普通 Ticket 为空，没有安装权限。

安装冻结各登记名单，禁止安装期间新增订阅，按每个实际订阅分别 Dispatch。单个回调异常由 C0 记录，后续观察者仍派发；缺少 ACK、抛异常、回调被 Block 或只正常返回都不能成功。生产适配在审计过的重建/失效逻辑完成后显式 ACK，不由登记器因正常返回代劳。

| 观察者族 | 安装时处理 |
| --- | --- |
| SelectionController / SubsController | 新 ID 选择已经安装；确认模型替换及历史选择/活动行状态 |
| BaseGrid / SubsEditBox / InitialLineState | 重建行索引、编辑内容和初态文本，不借用退役节点 |
| AudioKaraoke / Dialogue timing | 刷新独立缓存、marker 和活动行/选择状态 |
| VisualToolBase / VisualToolDrag / VideoController / VideoBox | 刷新独立行、特征、选择、渲染器字幕快照和时间显示 |
| 样式管理器、翻译、样式助手、Kanji、移轴、绘图/视频属性对话框 | 清理或重建索引，失效旧草稿；翻译安装刷新不自动跳行/弹出结尾导航 |
| FrameMain | 标题刷新及明确确认 |

Disconnect 本身不证明旧引用已经释放。生产订阅另带 weak 所有者凭据；强凭据声明为首个成员，晚于全部借用/缓存成员销毁。栈对话框也保证凭据比草稿/连接晚析构。只有显式凭据过期才可撤销该观察者要求；连接断开但所有者存活时继续 Pending。没有凭据的事务内注销同样不能自动确认。回调自我销毁时登记器临时持有回调对象，等回调返回且所有者凭据过期后才能撤销。

Pending 保留旧模型、旧历史、旧选择/活动行、旧 Metadata 保存基线/修订、旧路径和旧保存计数等恢复数据；C0 batch 和 gate 同时钉住退役模型。只有全部必要观察者 ACK 或凭据证明释放引用后，Complete 才解除门禁，随后释放 Pending 的旧状态。失败不能提前释放。

## 6. 失败、恢复与外围业务边界

| 结果 | 明确含义 |
| --- | --- |
| 普通失败 / 发布前 Aborting | 原工程完全不变；包括活动 comparison/writer、非法候选、prepare 失败/取消、过期输入和错误线程 |
| RecoveryBlocked | 部分或全部候选已发布，但确认未完成；不是安装成功，也不承诺完整回滚 |
| Committed | 全部必要确认完成，门禁恢复，旧模型可释放 |
| 完整回滚 | 本阶段未实现任意发布后观察者副作用的完整回滚，不以此描述 RecoveryBlocked |

恢复诊断入口：`InstallationStatus()`、`PendingInstallationObservers()`；C0 内部还保留每观察者 Failure。`RetryModelInstallation()` 只允许宿主线程、确实已发布且处于 RecoveryBlocked 的事务；重试仍有未确认项则返回 false，全部确认才返回 true 并完成外围 FileOpen。禁止忽略失败观察者或新建另一发布来清掉故障。

若 Publishing 内发生异常而模型发布尚未完成，保持封闭，Retry 不会伪装成功，只能关闭宿主。普通模型交换使用预先准备的无分配交换路径，观察者错误通过受测的确认阶段进入封闭。

正常 FrameMain Close 事件和直接 deferred Destroy 在安装同步栈内均拒绝销毁并请求取消；RecoveryBlocked 后允许关闭宿主。FrameMain 先销毁/注销子观察者；provider 关闭仅清理资源，不再尝试修改封闭 Metadata；Context 内部按既有成员顺序释放观察者、安装器和模型。任意回调直接 delete Context/FrameMain 或篡改公开 Owner 仍是本阶段未封闭的 legacy 权限，不能声称安全。

Load 的备份、MRU、FileOpen/关联媒体等属于提交后外围工作。它们抛异常时 InstallationStatus 仍是 Committed，不能说成“候选未发布”或回滚成功。本阶段不迁移全部这些业务；后续调用者必须按状态区分提交后的外围错误。恢复目前提供内部显式 API，没有新增面向用户的恢复按钮。

## 7. 实际修改文件与功能批次

第一族（9 文件）：

- docs/subtitle-model-installation-design.md、docs/subtitle-model-installation.md。
- libaegisub/common/subtitle_edit_gate.cpp、libaegisub/include/libaegisub/subtitle_edit_gate.h。
- libaegisub/common/subtitle_rebind.cpp、libaegisub/include/libaegisub/subtitle_rebind.h。
- src/subtitle_model_candidate.h、src/subtitle_model_observers.h。
- tests/tests/subtitle_project_access.cpp。

第二族（44 文件）：

- docs/subtitle-model-installation-design.md；libaegisub/include/libaegisub/path.h。
- src/subtitle_model_installation.h；src/subs_controller.cpp/.h；src/subtitle_model_candidate.h；src/subtitle_model_observers.h。
- src/ass_file.cpp/.h；src/subtitle_project_access.cpp/.h；src/selection_controller.cpp/.h。
- src/audio_karaoke.cpp/.h；src/audio_timing_dialogue.cpp。
- src/base_grid.cpp/.h；src/subs_edit_box.cpp/.h；src/initial_line_state.cpp/.h。
- src/dialog_kara_timing_copy.cpp；src/dialog_rectangle.cpp；src/dialog_shift_times.cpp；src/dialog_style_manager.cpp。
- src/dialog_styling_assistant.cpp/.h；src/dialog_translation.cpp/.h；src/dialog_video_properties.cpp。
- src/frame_main.cpp/.h；src/project.cpp/.h。
- src/video_box.cpp/.h；src/video_controller.cpp/.h；src/visual_tool.cpp/.h；src/visual_tool_drag.cpp。
- tests/tests/ordinary_edit.cpp；tests/tests/subtitle_project_access.cpp。

交付报告收尾只更新本报告；不再改变已验证的生产代码。未修改 signal.h、Lua 实现、Context::ass 声明、第三方依赖和 ASS/Undo 历史格式。独立 AssFile 保持原有可变能力。

## 8. 正式测试和构建结果

第一族独立 Windows x64 Release 构建成功，应用 111/111、模型 55/55、基础 436/436，合计 **602/602**。新增 7 项独立候选/C0 协议回归。

第二族最终 Windows x64 Release 构建成功（Meson buildtype=release，VS x64 工具链，aegisub.exe 和测试目标全部链接）。最终全量：

| 测试程序 | 通过数 |
| --- | --- |
| ordinary-edit-tests.exe | 124/124 |
| subtitle-compare-tests.exe | 55/55 |
| gtest-main.exe | 436/436 |
| 合计 | **615/615** |

原正常基线 **595/595** 全保留，新增 **20 项**（模型 7 + 生产应用 13）；两个原始阻断已纳入这 13 项正式应用回归，不额外重复计数。

原诊断一升级为 `model_installation.production_load_denied_by_comparison_preserves_complete_host_state`：真实 ComparisonSession 期间调用 SubsController::Load，比较完整内容/ID/原始毫秒、全部 Properties、修订/Metadata 计数、Selection/Active、Undo/Redo 描述及可用性、路径、脏/保存状态、旧绑定；拒绝后 Redo 仍能恢复原记录。**通过**。

原诊断二升级为 `model_installation.production_observer_failure_is_recovery_blocked_and_retries_explicitly`：多个真实观察者、正常/抛异常/后续读取，断言逆序继续派发、旧节点存活、Pending 精确、RecoveryBlocked 和写/比较/保存/自动保存/新安装/Close 拒绝；失败重试仍封闭，修复后显式恢复并编辑/Undo/Redo。**通过**。

其他新增覆盖：候选深复制/默认/绑定/ID/Extradata 校验及取消；显式 ACK/正常返回不确认；prepare 失败/取消保持整个原状态；后台 Load 拒绝；观察者重入及真实 wx Close 事件调用生产关闭保护；回调自毁和 prepare 销毁凭据；Disconnect 不释放所有者引用；重复 Load、文档 epoch、旧任务写回拒绝、重复选择通知；安装后兼容 ASS 保存重开；真实网格、音频 provider、样式/翻译对话框重绑定并继续 Undo/Redo；真实 VideoDisplay/视觉拖动、渲染读取和安装后编辑。

所有测试使用 run_isolated.py 和不同临时输出目录，不运行旧 setup，不修改源码夹具或用户配置。保留开发期间失败日志；曾发现测试回调误用普通空 Ticket、视觉夹具坐标系不明确，分别改为限定安装观察范围及明确 640x480 字幕坐标，保留真实交互与回归断言后全量复验。注销测试进一步加严为“只有所有者释放才允许撤销”，没有放宽确认要求。

最终日志：build/c1c1c7-load-build.log；build/c1c1c7-final-v2-app.log；build/c1c1c7-final-v2-model.log；build/c1c1c7-final-v2-base.log。定向 13/13 日志 build/c1c1c7-focused-v3.log。第一族日志 build/c1c1c7-prepare-*.log。

Linux/macOS：GitHub API 对当前分支返回 actions total_count=0，**未运行、未验证**。仓库自动 push CI 仅匹配 master/标签，本分支无开放 PR；未把本地 Windows 成功当作其他平台 CI 成功。

## 9. 提交、普通推送及远端一致性

| 功能族 | 提交名称 | 已推送提交 / 当批远端核对 |
| --- | --- | --- |
| 候选、观察者准备和文档身份 | refactor(compare): prepare model installation observers and candidates | ef64f043770ebe1d97b9c9be0d4f4ef903e46562，远端当批相同 |
| 生产发布与恢复 | fix(compare): install host models with explicit observer recovery | a61484f1caccbb21c4cadaf1e5d88046e0c5f536，远端当批相同 |

每个功能族均独立构建、全量测试、diff 检查、保护哈希核对、提交及普通推送。第二族远端证据：build/c1c1c7-production-remote.json。完整文件清单另见 build/c1c1c7-production-files.json。

本报告收尾提交名称 `refactor(compare): document host installation recovery delivery`；收尾只改变本报告，无须重复运行未改变的二进制。最终报告提交 SHA 和远端相等结果记录在交付消息及 build/c1c1c7-delivery-remote.json，避免在报告内自引用自身提交哈希。

## 10. 剩余边界与人工验收

未实现、留待后续复核授权：完整 Lua 多步结果/取消/错误/多 Undo 点发布、所有后台 Context 借用、全部 Load/Close 外围调用者迁移、Owner 最终私有化。公开 Context::ass 及 legacy Close/Undo 模型恢复仍是需继续收口的可信内部路径；本阶段只给它们安装期间拒绝保护，没有声称都已迁移为安装器。

普通 Commit 保持原 Signal 异常传播语义；本阶段修复的是受控模型安装通知，不能声称普通编辑的任意观察者异常已获得同样事务恢复。

自动化实际验证了真实生产对象、wx 事件和 dummy 音视频 provider。完整 FrameMain 桌面人工点选关闭、真实媒体格式/设备、故障组件 UI 的人工恢复操作、多屏/HiDPI，以及 Linux/macOS 仍需人工/CI验收；不把这些列为已执行。FrameMain 关闭和 Destroy 的接入已构建并审查，wx Close 回归使用真实事件和同一生产 DeferHostClose 保护函数；它不等于完整 FrameMain 手工验收。

## 11. 最终问题答复

- 现在能否安全安装新工程：可以，在已接入的宿主 Load 安装边界内，发布前完成独立准备和授权，发布后逐观察者确认；成功必须为 Committed。
- 授权拒绝是否保证原工程完全不变：是，正式生产回归核对完整状态包与绑定；不把阶段外直接 Owner 写入算作安装授权。
- 观察者失败能否完整恢复：没有承诺完整回滚；进入受测的 RecoveryBlocked，保留旧模型并封闭危险操作；修复观察者后显式重试完成新工程安装。
- 什么时候允许释放旧模型：全部必要观察者明确确认，或显式所有者凭据证明引用已释放并撤销；或者宿主安全关闭并销毁/注销观察者之后。
- RecoveryBlocked 如何解除：同一事务的宿主线程 RetryModelInstallation 全部确认成功，或关闭宿主；不能忽略待确认项。发布未完成的异常只允许关闭。
- 是否具备后续安全迁移 Load、Close、Lua 的基础：已具备私有安装能力、文档身份、逐观察者确认、退役保留及失败封闭基础；这些业务的完整迁移仍需另行授权和测试。
