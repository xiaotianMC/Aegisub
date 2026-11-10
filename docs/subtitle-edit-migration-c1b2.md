# C1-B2 视觉工具迁移记录

## 实施前调查

当前分支 codex/style-name-width，HEAD 506d5c5247c3256bcc0f0a6670eccc24986c0d26，与 fork 相同。C0、C1-A、C1-B、B1 提交均在当前历史。fork 无开放 PR；上游相关 #760、#756、#744、#684、#453 不合入本批。两份受保护用户文件保留，未暂存。

继承关系：VisualToolBase → VisualTool<Feature> → Cross、Drag、RotateXY、RotateZ、Scale、Clip、VectorClip；Rectangle 直接继承 VisualToolBase。VisualDraggableFeature 是 UI 节点，Drag 特化有起止时间和配对点，VectorClip 特化有 spline 索引。VideoDisplay 独占当前工具；工具拥有 features 和独立几何缓存；Spline 借用工具进行坐标换算。

| 文件 | 类/函数 | 修改字段 | 原写入时机 | 生命周期 | 迁移方案 |
| --- | --- | --- | --- | --- | --- |
| src/visual_tool.cpp/.h | SetOverride/SetSelectedOverride | ASS Text | 直接解析并修改当前行 | 移动、双击、工具栏 | 独立快照；编辑前 Interaction；最终统一写回 |
| src/visual_tool.cpp/.h | Commit/OnMouseEvent | 当前 Text、历史 | 每次鼠标移动 Commit 并合并 | 按下至释放 | 移动仅更新渲染副本，释放单次正式提交；捕获丢失取消 |
| src/visual_tool.cpp/.h | active_line、GetActiveDialogueLine、OnCommit/OnSeek/OnActiveLineChanged | 可变当前行缓存 | 外部事件刷新 | 跨回调 | active_line 只指向工具拥有的独立快照；修订/选择/视频变化取消 |
| src/visual_feature.h | VisualDraggableFeature::line | 关联当前行 | 创建 feature 时保存裸指针 | 跨回调 | 稳定行 ID；临时重新定位；UI 配对点指针仍由 features 所有权约束 |
| src/visual_tool_drag.cpp/.h | MakeFeatures、UpdateDrag、OnDoubleClick、OnSubTool | pos/move/org | 拖点、双击、工具栏 | 多目标持续拖动/一次操作 | 选择缓存改 ID；仅改快照；工具栏闭合自己的会话 |
| src/visual_tool_cross.cpp | OnDoubleClick | pos/move/org | 双击 | 一次操作 | 基类事件授权，独立快照与确认 |
| src/visual_tool_rotatexy.cpp | UpdateHold/UpdateDrag | frx/fry/org | 移动 | 连续交互 | 基类授权；保留原几何及标签算法 |
| src/visual_tool_rotatez.cpp | UpdateHold/UpdateDrag | frz/org | 移动 | 连续交互 | 同上 |
| src/visual_tool_scale.cpp | UpdateHold | fscx/fscy | 移动 | 连续交互 | 同上 |
| src/visual_tool_clip.cpp | CommitHold/UpdateHold/UpdateDrag | clip/iclip | 移动 | 连续交互 | 选中快照；保留 inverse、四角联动 |
| src/visual_tool_vector_clip.cpp/.h | Save/Commit/InitializeDrag/InitializeHold/UpdateHold | vector clip/iclip | 拖点、删除、转换、插入、freehand | 有立即完成和连续两类 | 除基类外覆盖内部 Commit 与子工具切换；取消重建 spline |
| src/visual_tool_rectangle.cpp/.h | OnMouseEvent/OnKeyDown/Finish | 新 drawing 行、图形样式 | 完成时直接插入 | 拖动或多击 polygon | 起始登记、全程有效性、完成前分配；现有 Interaction 增加窄插入能力 |
| src/video_display.cpp | SetTool/Unload/OnMouseEvent/视频源监听 | 工具和捕获所有权 | 切工具、视频、窗口关闭 | 窗口生命周期 | 显式取消后退休旧工具；工具栏回调必须解绑；异常结束会话 |
| src/async_video_provider.h/.cpp | 新批量预览入口 | worker 自己的 AssFile 副本 | 高频预览 | 有序 worker 队列 | 只传独立目标行数据；取消还原原始行，不修改当前模型 |

B1 的 AudioEditSession 是音频专用，不复用。通用能力为 SubtitleProjectAccess::Interaction、Current/UpdateRows/Commit/End、C0 门禁及修订令牌。signal.h、Lua、加载保存队列、B3 对话框不在修改范围。

## 实施顺序与依赖

先扩展受控 drawing 插入和独立渲染预览，并编译；再迁移基类快照与 feature ID、全部派生工具及生命周期；最后增加实际 wx 事件测试、完整回归、Release 构建与独立提交。保持无新增依赖、不使用 wxYield，不在每次鼠标移动时解析整个工程。


## 最终实现

### 基类和目标缓存

`VisualToolBase::RefreshRows` 创建工具拥有的 AssDialogueBase 数据副本，使用 AssDialogue(Base) 保留来源 ID，不把节点放入当前 AssFile。`active_line`、有序行视图都只借用这些独立对象。`VisualDraggableFeature::line_id` 和 Drag 的 selection 均为 ID；位置相同时依照字幕文件顺序建立 features，避免排序后命中优先级变为 ID 顺序。

`BeginEdit` 在任何编辑缓存变化之前调用 `TryBeginInteraction`。原选中行、活动行和点击目标构成授权集合；绑定组选择扩展后，在已有租约仍有效时扩展目标授权。所有普通选择操作使用当前 ID 临时重新定位，并验证 Interaction；不保存跨租约的可变当前行指针。旧 SelectionController 接口的工程级收口仍是 C1-C。

每次继续拖动先检查 Interaction::Current 和捕获状态。过期、丢失捕获、丢失释放事件均取消，不将旧结果重用于新修订。空闲缓存面对 C1-A 空操作造成的代数推进，仅在工程身份和全行数据逐字段完全相等时重新绑定；时间比较用原始毫秒。真实数据不同则丢弃手势并刷新，不能把旧几何参数强行套到新内容。

### SetOverride / Commit 的语义

`SetOverride` 只接受本工具持有且属于有效授权目标集合的快照；沿用原 ASS 标签解析/替换算法，仅替换指定标签及原本互斥的别名，原文、其他覆盖标签和后续块保留。它不修改当前工程。

`Commit` 在拖动中只将修改过的独立目标行发送给 `AsyncVideoProvider::PreviewSubtitles`；不修改当前 AssFile、不创建历史记录。鼠标释放、双击或立即完成操作最终调用 `EndEdit(true)`，先用 ValidateOriginals 检查原目标全字段（含 1ms 变化），再由 Interaction::UpdateRows 重新定位、验证全部目标，只替换当前行的 Text，随后单次正常 Commit。未发通知的旧接口改动也不能被旧快照覆盖。一次拖动仍对应原先合并后的一条撤销记录；不同拖动分别撤销，不新增全局 Undo 格式。编辑框和修改标记在正式确认时更新，拖动中的视频字幕通过独立副本实时显示。

渲染队列只接收值数据。预览在 worker 的既有 AssFile 副本中先验证整个批次再修改，按来源 ID 和 Row 校验；复制时仅为渲染器保留来源 ID，未改变 AssDialogue/AssFile 的通用复制语义或全局 ID 分配器。取消将当前工程的只读副本重新送给渲染器，队列版本淘汰后续请求之前的旧渲染。没有将预览写进当前工程或磁盘。

### 各工具

| 工具 | 最终状态 |
| --- | --- |
| Cross | 双击先授权，pos/move/org 仅修改选中快照；一次完成提交 |
| Drag | 所有可见行 feature 保存 ID，保留 file order；pos/move/org、多选和工具栏转换走同一授权路径 |
| RotateXY | frx/fry 和 org 编辑由模板事件授权，沿用原旋转/吸附算法 |
| RotateZ | frz 和 org 同上 |
| Scale | 原源文件无需修改；SetSelectedOverride 和模板事件已经收口，原 fscx/fscy、修饰键算法保留 |
| Clip | 四角和矩形范围是独立 UI 几何；clip/iclip 保存到选中快照 |
| VectorClip | Save/内部 Commit 走快照；删除、转换、插点为立即完成；线、Bezier、自由绘制为持续会话；外部子工具切换取消，内部 freehand 完成切模式不取消最终结果 |
| Rectangle | 矩形、椭圆、三角、多边形保持原路径算法与独立绘制；完成时受控插入新节点和必要的“图形”样式，保留源行身份、顺序、图层和时间 |

Rectangle 的新接口 `Interaction::InsertBefore` 只处理本批绘图插入：验证原目标、重复 ID、样式及空 Extradata；先分配独立节点和样式，再执行不分配内存的 intrusive 插入。拒绝把同一节点放进两个文件，也不借此引入未知附加数据导入规则。样式和新行在同一正常 Commit 中撤销/重做；原有同名图形样式保持不变。

### 生命周期和异常

- VideoDisplay 独占工具；切工具、视频源、卸载、进入预览平移模式先取消旧交互。析构仅解绑、释放捕获、丢弃预览和登记，不提交字幕。
- 外部 Commit、选择变化、帧变化、显示坐标变化都会取消并重建。工具自身点选触发的字幕同步跳帧暂缓 feature 重建，避免仍在按下回调时销毁刚点击的节点；完成后按新帧更新可见 features。
- 鼠标事件和 Rectangle 键盘完成路径有异常清理。EndEdit 的作用域退出保证登记释放；无授权或过期不能进入 SetOverride/正式写回。
- Drag、VectorClip 工具栏绑定明确解绑；使用 wxWeakRef 处理工具栏比画布先销毁的情况。features 配对指针只指向工具自己的 UI 节点；Spline 索引在取消后重建。
- 普通 Commit 的观察者异常保持原语义：若数据及历史已经发布，不伪称回滚；可用原 Undo 恢复。此次没有实现全局事务、修改 signal.h 或加入比较批量发布。

## 实际文件清单

新增：`docs/subtitle-edit-migration-c1b2.md`。

修改：`src/visual_tool.h`、`src/visual_tool.cpp`、`src/visual_feature.h`、`src/visual_tool_drag.h`、`src/visual_tool_drag.cpp`、`src/visual_tool_cross.cpp`、`src/visual_tool_rotatexy.cpp`、`src/visual_tool_rotatez.cpp`、`src/visual_tool_clip.cpp`、`src/visual_tool_vector_clip.h`、`src/visual_tool_vector_clip.cpp`、`src/visual_tool_rectangle.cpp`、`src/video_display.cpp`、`src/async_video_provider.h`、`src/async_video_provider.cpp`、`src/subtitle_project_access.h`、`src/subtitle_project_access.cpp`、`tests/tests/ordinary_edit.cpp`。共 19 个文件。Scale 通过基类覆盖，无需为了列入迁移清单而修改其算法文件。

## 测试与构建结果

Windows x86_64、Meson release，Visual Studio 主程序及全部测试程序构建通过；保留项目已有的编译警告。每个阶段先编译，预览/插入前置模块先通过原有 40 项应用测试，再运行视觉事件测试。

| 测试程序 | 结果 |
| --- | --- |
| ordinary-edit-tests.exe | 73/73：普通 16、音频 24、新视觉 33 |
| tests/subtitle-compare-tests.exe | 33/33：读取器、生产模型、C0/C1-B 交互与选择 |
| tests/gtest-main.exe | 435/435：全部基础与阶段一比较回归 |
| 合计 | 541/541，本批新增 33 项 |

新增测试包含生产工具和实际 wx 鼠标/按钮/键盘事件：位置拖动、Cross 双击、尺寸、XY/Z 旋转、矩形 clip、vector clip 各编辑模式、图形与多边形、连续拖动、多目标绑定、捕获/释放丢失、工具/子工具/视频/帧/字幕行/工程内容切换、删目标、过期修订、窗口/工具栏销毁、异常观察者、未授权接口、无效预览批次、ASS 标签/样式/附加数据保持、Undo/Redo、空操作租约、比较期间拒绝。接口探针与渲染批次校验是单元测试，不冒充真实鼠标测试。

预览测试实际比较 libass 渲染像素：拖动中画面改变但当前 Text 不变，取消后像素恢复。测试宿主使用正常 wx 事件循环处理字体初始化，等帧发生在辅助线程，不在主线程同步等待需要主线程响应的 worker；没有使用 wxYield。

持久化使用现有 ASS 保存与读取接口实测：确认后的绘图行和图形样式保存重开一致；未确认的预览不出现在文件中。没有修改保存、清理或自动保存策略。

一万行字幕测试中，100 次连续鼠标移动的最终单轮 UI 派发时间为 **0.881 ms**，不含后台渲染、初始化及最终提交，不是帧率保证。高频回调只解析/复制编辑目标，不重复解析全工程；worker 批量预览为 O(n+k)，初始/失效快照为 O(n log n)，确认沿用 Interaction 验证。测试保留 10 秒宽松上限用于防明显阻塞，不作微秒性能承诺。

日志：`build/c1b2-verified-build.log`、`build/c1b2-verified-ordinary.log`、`build/c1b2-verified-base.log`、`build/c1b2-verified-model.log`。全部使用 `tests/run_isolated.py` 的隔离目录，没有运行旧 setup 脚本、修改源码夹具或用户文件。

## 剩余边界及人工验收

本批视觉字幕 Text 和绘图行/样式写入均已接入授权；普通视觉调用者没有保留可变当前字幕行指针。仍保留的指针是工具拥有的独立行快照、UI feature 配对、由画布所有权约束的工具父指针、弱工具栏，以及选择桥接时在有效会话内重新定位的临时当前行指针。

全工程绕过门禁的旧公开 AssFile/Selection API、Lua、工程加载/关闭队列、保存与清理，以及 B3 对话框仍存在。VideoController/VideoDisplay 原有无关工程属性（例如播放位置、缩放记录）不由本批收口，不能据此宣布完整工程隔离。B1 回归通过但其原有人工验收事项也未因此消失。

人工验收仍需实体鼠标与修饰键手感、显示着色/操作点等 OpenGL 叠加层、真实视频解码、HiDPI/缩放、多显示器和完整主窗口关闭；本轮窗口隐藏的事件测试不等于这些项目通过。未执行 macOS/Linux 构建，只使用现有跨平台 wx/标准库能力，不新增第三方依赖。

B3 下一步处理 Rectangle 编辑对话框、视频属性、样式助手、翻译助手和 KanjiTimer 的长生命周期预览/延迟写回。C1-C 继续处理 Lua、后台写任务、加载关闭、保存清理和旧公共接口。比较 UI 和原子应用引擎尚未开始；C1-B 整体未完成，本批交付不能当作完整隔离验收。

## Git

提交名称：`refactor(compare): gate visual tool edits`。目标 `fork/codex/style-name-width`。提交前重新确认账号 xiaotianMC、fork push=true、upstream push=false、fork 开放 PR=0、远端仍为 506d5c5。普通提交/推送，最终哈希在交付回复核对。

`本版与原版差异清单.md` 和 `subprojects/checkasm.wrap` 在本轮前后 SHA256 分别保持 5CE9B8077D0C83938BF5A4F9E8944586A0879D66049C44C0E62B840CE109127B、CE07862593607282739E118A1F94306E79AAFB0D9100C8C51C6517804017FA39，不纳入提交。
