# C1-B3 长生命周期对话框迁移记录

## 实施前调查

分支 codex/style-name-width，HEAD 与 fork 远端均为 be08d44。C0、C1-A、C1-B、B1、B2 完整存在；fork 无开放 PR。相关上游 #760、#756、#744、#676 等不合入本批。两份用户已有改动保护，不暂存。

| 文件 | 类/函数 | 生命周期 | 修改对象 | 原写入时机 | 迁移方案 |
| --- | --- | --- | --- | --- | --- |
| src/dialog_rectangle.cpp | EditDrawing/update/OK/cancel | modal、连续预览 lambda | Text、图形标签 | 空消息 Commit 实时写当前行，取消再写原文 | 独立行快照、独立渲染预览；确认短授权，取消仅丢弃预览 |
| src/dialog_styling_assistant.h/.cpp | OnActiveLineChanged/Commit/OnListClicked/OnListDoubleClicked | modeless、逐行 | Style | 缓存 active_line 裸指针，点击或快捷键提交一行 | ID/原字段/样式定义；实际原功能是逐行，不扩大为批量选择集赋样式 |
| src/dialog_translation.h/.cpp | Commit/NextBlock/PrevBlock/OnExternalCommit | modal、逐块逐行 | Text | 块缓存替换，直接修改 active_line 并 Commit；preview 命令也正式提交 | 独立原行、块、译文；编辑授权与缓存分离，保留原 preview=提交但不前进的语义 |
| src/dialog_kara_timing_copy.cpp | OnStart/OnAccept/OnClose/FindNextStyleMatch | modal、多行延迟 | 目标 Text 中的 Karaoke 标签 | 保存多个 live 指针；Close 按钮批量直接赋值 | 全行独立快照，源/目标/待提交均用 ID；全部候选校验后单次提交；X/Escape 丢弃 |
| src/dialog_video_properties.cpp | UpdateVideoProperties/update_ycbcr_matrix/update_play_res/update_layout_res | 多次 modal 提示 | Script Info、可能重采样全部 Styles/Events | 提示后直接写当前文件；ResampleResolution 自己 Commit | 保留旧提示与逐步骤 Undo；在独立文件计算，确认后验证并短授权发布数据 |
| src/project.cpp | DoLoadVideo -> UpdateVideoProperties | 当前工程与视频边界 | 上述字幕属性 | 传入当前 AssFile 裸指针 | 仅此对话框边界改传 Context，不迁移加载队列 |
| src/base_grid.cpp、src/command/vis_tool.cpp | EditDrawing 调用 | Enter/双击/命令 | 委托 Drawing | 不一定有外层写租约 | Drawing 内部完整授权，不依赖入口 |
| src/command/tool.cpp | styling/translation/Kanji 命令 | 快捷键/打开/提交 | 委托指定对话框 | 不持有完整编辑会话 | 对话框内部处理，保持快捷键和命令名称 |

扩展检索：Properties、ShiftTimes、TimingProcessor、Attachments、StyleEditor/Manager、Spellchecker 的写回已在 C1-A 接入短 WriteScope；选择对话框仅改变选择。导出使用独立文件；Automation/加载保存、后台任务属于 C1-C。本批不顺带重构这些模块。

通用能力是 Interaction 的 Current/UpdateRows/Commit/End 和工程令牌。B1 的 AudioEditSession、B2 的视觉几何缓存是专用实现，不搬进对话框。新增最小对话框快照助手，只保存值数据，复用原门禁；打开只读，实际编辑登记任务，不持有内部互斥锁等待用户。

实施顺序：最小快照/依赖校验能力与 Drawing，编译测试；Styling，编译测试；Translation，编译测试；Kanji，编译测试；Video Properties 边界及必要数据更新，编译测试；最终全回归、Release、独立提交推送。暂不实现比较窗口、应用引擎、原子发布或 C1-C 全面收口。


## 最终实现与接口

新增 `src/subtitle_dialog_edit.h`。`SubtitleDialogEdit` 只保存独立值快照、稳定行 ID、全行 ID 顺序、原始字段、样式定义、Script Info、Extradata 键值和工程生命周期令牌。`ResetActive/ResetAll/Reset` 建立只读基线；`Begin` 校验后复用 C1-B `Interaction` 登记待完成任务；`Commit` 重新验证完整原数据并经 `UpdateRows/Commit` 写回；`End/Invalidate` 可靠释放。没有第二套门禁，没有跨回调的当前可变行指针。临时 AssDialogue 指针均指向对话框自己拥有的对象。

`RevisionToken::Alive` 区分“旧修订”和“工程已关闭”；先检查弱父窗口和令牌存活，再访问 Context。普通只读 UI 也可能申请无操作租约并增加修订，因此空闲快照允许在全部原字段、依赖和顺序完全相等时重新取得 Interaction；活动 Interaction 仍必须通过 Current，外部 Commit 会显式失效，不能仅凭相同文字恢复过期任务。

Video Properties 的公开入口改为 Context，唯一生产调用点 `Project::DoLoadVideo` 只调整接口边界。提示前复制视频元数据；每个原有步骤在独立 AssFile 进行计算（仅该既有重采样对话框需要，不是比较应用候选引擎），保留原行 ID。`Interaction::UpdateLayout` 仅更新同 ID、同顺序的行值、同名称样式值和 Script Info，不创建、删除或重排节点；所有检查和准备分配先于首个写入。`AssStyle::SwapData` 只交换数据、不交换 intrusive hook，随后 Info swap。当前节点身份、选择、附件、Properties 和 Extradata 均保留。独立文件的普通复制可产生合法 ID 空洞，恢复 ID 不回退分配器。

## 生命周期、确认及取消

| 对话框 | Open / Editing / Preview | Committing / Finished | Cancelled / Closed |
| --- | --- | --- | --- |
| Drawing | 打开只读；首次控件变化登记；独立 renderer 预览 | 完整验证后一次 Text Commit | X/Cancel/异常丢弃，恢复 renderer 当前字幕；模型从未写入预览 |
| Styling | 打开只读；用户输入登记，自动补全规则不变 | 名称按原大小写不敏感规则匹配；每行一次 Commit，下一行重新快照 | 未提交输入丢弃；隐藏、销毁释放登记；外部修改要求重新选行 |
| Translation | 原行、解析块和译文分离；输入登记 | “预览”仍是提交但不前进；确认逐块/逐行提交；插入标签不重解释为新导航块 | 未确认译文丢弃；隐藏/销毁释放；切换行重新快照，旧译文不跟随到新行 |
| Kanji | 打开只读，Start 建立源/目标全快照并登记；匹配算法不变 | Accept Line 仅入独立队列；Close 按钮验证整批并一次 Commit | X/Escape 丢弃整批；析构不提交；重新 Start 先清空旧队列 |
| Video Properties | 各提示只操作独立计算数据，空闲不持租约 | 确认后短授权；保留三个步骤及重采样内部 Commit 的原 Undo 顺序 | 取消提示不写该步骤；外部变更/授权失败停止，保留此前已确认步骤 |

Kanji 原来 Escape 会调用应用 Close，X 会丢弃。依本次“取消不修改、行为明确”的要求，Escape 与 X 一致丢弃，显式 Close 按钮仍应用已接受行。Styling 实际没有“对选择集批量赋样式”功能，本批保留逐行行为；连续两行的整体性由各行自己的历史步骤保证，不增加新产品能力。

## 完整性及异常边界

多行结果先完整构造，再检查目标齐全、重复 ID、工程、行原始字段（含 1ms）、顺序、样式及附加数据，最后由一个 Interaction 更新全部值。任一目标不存在、重排、同名样式定义变化、附加数据键值变化均拒绝，不写前几行。未选行不写，保留绑定信息/未知 Extradata，不解释其未知语义。读取、标签计算和候选分配失败不修改模型；事件和提交边界捕获异常使任务失效后重新抛出。

普通 Commit 的外部通知仍遵循现有异常语义，没有修改 signal.h：若观察者在 Undo 快照已入栈、整批值已写完后抛出，保留完整已提交结果和对应撤销，释放任务并传播异常，不假装回滚成功。本批没有实现原子发布或观察者重绑定事务。Drawing 取消的 renderer 恢复在清理阶段为尽力恢复，分配失败也不妨碍释放任务，当前模型不受影响。

## 自动测试与构建

每个对话框修改后实际编译并执行其测试。Windows x64 / Visual Studio / Meson release 主程序与全部测试目标构建通过，已有编译警告保留。最终结果以 `build/c1b3-verified-build.log`、`build/c1b3-verified-ordinary.log`、`build/c1b3-verified-base.log`、`build/c1b3-verified-model.log` 为准。

| 程序 | 实际通过 | 范围 |
| --- | --- | --- |
| ordinary-edit-tests.exe | 91/91 | 原普通 16、音频 24、视觉 33，加本批 18 |
| tests/gtest-main.exe | 436/436 | 阶段一及基础，含新增工程存活令牌测试 |
| tests/subtitle-compare-tests.exe | 33/33 | 生产读取、模型、选择及交互 |
| 合计 | 560/560 | 本批新增 19 项 |

新增覆盖真实 Drawing 控件预览/确认/取消、样式输入与确认、翻译控件连续多行及多次预览、实际 Kanji 按钮流程（非连续两目标、确认/窗口关闭/外部修改）、真实 YUV4MPEG 视频属性提示与重采样、Undo/Redo、保存重开、中文空格/标签/行 ID/1ms/注释/层/Actor/Effect/未知附加数据保持、窗口隐藏、父窗口销毁、异常通知和过期依赖。独立助手/Interaction 测试另外验证无授权、重复 ID、删行、重排行、同名样式冲突、全批拒绝、未选行保持和节点身份；这些接口探针不冒充真实 UI 操作。

所有测试通过 `tests/run_isolated.py` 使用独立临时夹具、配置、输出和视频文件，未运行旧 setup、未修改源码夹具。测试中等待 renderer 使用正常 wx 事件循环和辅助线程，没有 wxYield。保存沿用现有 ASS 接口，没有新增持久化格式或移动端实现（当前是原生桌面工程）。

## 人工验收及剩余范围

待人工：实体键盘/快捷键及焦点手感、复杂 Karaoke 自动匹配、多显示器/HiDPI、真实 HDR/不同色彩矩阵视频的所有提示分支、真实图形预览观感、完整主窗口关闭与加载队列重入。这些不以隐藏窗口测试冒充通过。未进行 Linux/macOS 构建。B1/B2 原有人工清单仍保留。

B3 指定五个写入出口已经迁移；旧 Context::ass、AssFile 公开容器/字段、Selection 可变 API 仍存在全工程绕过路径。Lua、后台任务、加载/关闭队列、手动及自动保存/备份/CleanExtradata、Properties 等旧公共写入路径收口属于 C1-C。本批只约束 Video Properties 的实际字幕数据，不将播放位置、缩放等纯显示状态纳入字幕 Interaction。比较窗口、菜单、批量应用和原子发布均未实现。

## 文件及 Git 交付

生产文件：新增 `src/subtitle_dialog_edit.h`；修改 `libaegisub/common/subtitle_edit_gate.cpp`、`libaegisub/include/libaegisub/subtitle_edit_gate.h`、`src/ass_style.h/.cpp`、`src/subtitle_project_access.h/.cpp`、`src/dialog_rectangle.cpp`、`src/dialog_styling_assistant.h/.cpp`、`src/dialog_translation.h/.cpp`、`src/dialog_kara_timing_copy.cpp`、`src/dialog_video_properties.cpp`、`src/dialogs.h`、`src/project.cpp`。

测试文件：`tests/tests/ordinary_edit.cpp`、`tests/tests/subtitle_edit_gate.cpp`。文档：本文件与 `docs/subtitle-edit-migration-c1b.md` 的当前状态补充。阶段一算法无改动。

提交名称 `refactor(compare): gate long-lived editor dialogs`，目标 `fork/codex/style-name-width`，普通推送并核对远端哈希，实际哈希在最终回复记录。提交前复查 xiaotianMC，fork push=true、上游 push=false、fork 开放 PR=0、远端为 be08d44。两份用户文件 SHA256 保持 5CE9B8077D0C83938BF5A4F9E8944586A0879D66049C44C0E62B840CE109127B 和 CE07862593607282739E118A1F94306E79AAFB0D9100C8C51C6517804017FA39，未纳入提交。
