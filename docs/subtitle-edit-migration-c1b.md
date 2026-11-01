# C1-B：调查和实施记录

基线 `875f593`，分支 `codex/style-name-width`，远端一致。C0、C1-A 保留。
fork 无开放 PR；相关上游开发包括 #744、#760、#762、#668、#718。本批不合入上游代码。
用户差异清单与 checkasm.wrap 保持原样，不提交。

## 写入路径及生命周期调查

| 文件 | 函数 | 生命周期 | 修改对象/现有方式 | 迁移要求 |
| --- | --- | --- | --- | --- |
| audio_timing_dialogue.cpp | TimeableLine::Apply、AudioTimingControllerDialogue::DoCommit | marker 修改后自动/手动提交 | 缓存 AssDialogue*，直接写 Start/End；再联动邻行 | ID 缓存；写前校验整个目标集合；pending marker 之前登记，Commit/Revert/切行释放 |
| audio_timing_dialogue.cpp | OnLeftClick、OnRightClick、OnMarkerDrag、SetMarkers、Next、AddLeadIn/Out、ModifyStart/Length | 点击、拖动、延迟提交 | marker/modified_lines 在 Commit 前已改变 | 授权拒绝必须发生在 marker 更新之前，不能只保护 DoCommit |
| audio_timing_karaoke.cpp | DoCommit、ApplyLead | 音节调整与确认 | active_line 指针写 Text/Start/End | ID、原版本、活动任务登记；保留歌词标签和合并 Commit |
| audio_timing_karaoke.cpp | OnLeftClick/OnRightClick/OnMarkerDrag、ModifyStart/Length、AddLeadIn/Out、Next/Prev | 多回调编辑/播放切换 | 音节、marker、label、pending_changes | 输入事件前登记，确认/还原结束；播放状态单独保持 |
| audio_karaoke.h/.cpp | OnMouse、SetTagType、AcceptSplit、CancelSplit、LoadFromLine、SetEnabled | split/join 缓存、确认或取消 | active_line 指针、共享 Karaoke 对象 | 缓存改 ID，切分开始前登记；两个控制器共享缓存的取消/交接必须明确 |
| command/audio.cpp、command/time.cpp | 计时委托命令、确认/还原/切换模式 | 同步命令到持续交互 | 调用控制器，未统一保护内部独立事件 | 命令边界及控制器双重审查，不能认为 C1-A time 边界足够 |
| visual_tool.h/.cpp | OnMouseEvent、OnMouseCaptureLost、OnActiveLineChanged、OnCommit、Commit、SetOverride、SetSelectedOverride | 按下—拖动—释放/失去捕获 | active_line、features 和 selection 缓存可变指针 | 一次交互登记；每步校验目标/版本；切工具/切行/异常结束登记 |
| visual_feature.h、visual_tool_drag.cpp | feature.line、DoRefresh、UpdateDrag、OnDoubleClick | feature 跨帧保存 | feature 指向行，位置与 move 标签 | feature 保存稳定 ID；全部读写重新定位 |
| visual_tool_cross.cpp | OnDoubleClick | 一次操作 | SetSelectedOverride/Commit | 操作前授权 |
| visual_tool_rotatexy.cpp、visual_tool_rotatez.cpp | InitializeHold、UpdateHold、UpdateDrag | 连续旋转 | 覆盖标签与基类提交 | 每步写回使用同一任务授权 |
| visual_tool_scale.cpp | InitializeHold、UpdateHold | 连续缩放 | fscx/fscy 标签 | 版本及目标检查，保留精度 |
| visual_tool_clip.cpp | InitializeHold、UpdateHold、UpdateDrag | 矩形 clip 拖动 | clip 标签 | 交互结束/取消处理 |
| visual_tool_vector_clip.cpp | Save、Commit、RemoveSelection、OnMouseEvent | 绘制、拖点、删除点 | vector clip 标签；派生类自己持有交互状态 | 除基类外单独接入事件和 Save |
| visual_tool_rectangle.cpp | OnMouseEvent、Finish | 自定义绘图，不完全走模板基类事件 | 新建样式与 Events 行 | 插入之前授权；生成行 ID 与状态有效性；独立审查 |
| dialog_rectangle.cpp | EditDrawing 的 update/OK/cancel lambda | modal 实时预览 | 捕获 line 指针，实时 Text 写入及取消恢复 | 需要完整 modal 生命周期租约与原文；不能改成仅确认预览 |
| dialog_video_properties.cpp | UpdateVideoProperties、update_ycbcr_matrix/play_res/layout_res | 视频切换及提示框 | Script Info/分辨率及重采样 | 区分当前工程和独立文件，提示前后验证工程身份 |
| dialog_styling_assistant.h/.cpp | OnActiveLineChanged、Commit、OnStyleClicked | 无模式、延迟提交 | active_line 指针、Style | ID/版本；每次提交前重新定位 |
| dialog_translation.h/.cpp | OnActiveLineChanged、OnExternalCommit、Commit | 无模式分段翻译 | active_line 指针、标签块缓存、Text | 缓存对应版本；切行和外部更新失效 |
| dialog_kara_timing_copy.cpp | OnAccept、OnClose、ResetForNewLine | 多行结果积累后写回 | LinesToChange 和源/目标行指针 | 保存 ID/原版本及独立结果；写回前验证所有行，防止部分写入 |
| audio_display.cpp、audio_controller.cpp | AudioMarkerInteractionObject::OnMouseEvent、AudioDisplay::SetDraggedObject/OnTimingController、AudioController::SetTimingController | 捕获丢失/拖动结束/切计时控制器 | 拖动对象保存原 timing_controller 与 marker 裸指针；控制器先析构后发切换通知 | B1 必须先明确失效通知顺序；不能在拖动对象析构时直接调用可能已析构的计时控制器 |

以上是待迁移路径，不是已完成覆盖表。C1-C 的 Lua、加载/关闭、Save/AutoSave、CleanExtradata 和旧选择/容器 API 不在本批迁移。

## 最小基础设施要求

C0 WriteLease 可移动且由 Registration RAII 登记；互斥锁只在状态转换时持有。
C1-A WriteScope 可以跨回调持有，但仅验证授权与所属线程；它自身没有“缓存目标仍然新鲜”的保证。
因此不能直接把 WriteScope 存入工具后就宣布长期交互安全。需要在其上组合工程修订令牌、稳定目标 ID 集合和显式结束接口。

先单独验证此组合：活动期间比较被拒绝；跨回调保留授权；每次写前检查工程/版本/目标存在；外部改动后拒绝继续；异常释放；工程关闭后失效；不同工程独立；反复打开关闭无任务泄漏。
租约可以跨线程移动只表示生命周期登记；当前模型访问仍限所属线程，禁止工作线程写当前 AssFile。

会话结束只解除登记，不隐含回滚、创建 Undo 或恢复预览。具体工具必须保留自己的原状态并实现原有取消行为，之后才能释放任务登记。
实时编辑自己的 Commit 后才更新版本；不能把外部改动无条件认作自己的新基线。

## 顺序与验收

基础设施独立构建和测试后，依次实施 B1 音频、B2 全部视觉工具、B3 五类对话框。
每批分别构建、执行真实入口及生命周期测试、审查差异后提交推送。只有三个模块均完成，才能报告 C1-B 完成。

## 当前交付状态

本次先交付最小基础扩展，**B1、B2、B3 均尚未完成迁移，不能报告 C1-B 完成**。

- 修改 `src/subtitle_project_access.h/.cpp`：TryBeginInteraction、Interaction::Current/UpdateRows/Commit/End。复用 C0/C1-A 租约，不另建锁。目标 ID 集合和行数据分离，无可变当前行指针跨回调流出。
- 每次写回前检查原工程修订、目标集合全部存在、无重复 ID、更新目标受授权及 Row 未改变；校验失败发生在行赋值之前，并释放登记。
- 只有自己的 Commit 恰好推进一个工程代数，才承认新的基线；出现额外嵌套修订则拒绝继续。Commit 异常保留原 Signal 传播并释放任务登记。
- End 只结束任务，不自动回滚已写数据，不创建撤销记录；旧多步骤编辑的异常原子性仍未实现。后续工具不能把它当作取消恢复协议。
- 新增九项生产模型测试，覆盖跨回调、持续提交、外部修订、删除、重复/异工程目标、跨线程拒绝、关闭、重复创建销毁、异常和额外嵌套修订。它们不等于真实音频/视觉交互验收。

业务写入覆盖率目前仍为：音频 0、视觉 0、上述五类对话框 0。原有可变指针和写入路径尚存，详见上表；没有把基础设施测试算成业务迁移完成。

B1 下一步必须先解决 AudioMarkerInteractionObject 的结束/捕获丢失/控制器切换生命周期，随后改造 TimeableLine 和共享 Karaoke 缓存。在该边界明确前，直接给 DoCommit 加租约仍会漏掉提前的 marker/lead 修改，不能作为完整交付。

实际验证：Windows x64 构建通过；基础 435/435、原有真实普通入口 16/16、模型集成 33/33（其中本次新增 9 项）通过，共 484 项。全部使用 tests/run_isolated.py 的隔离目录；日志在 build/c1b-session-*-tests.log 和 build/c1b-session-build.log。没有执行真实音频拖动、视觉拖动或上述五类对话框的 C1-B 业务验收。
