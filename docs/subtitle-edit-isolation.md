# 字幕版本比较 C0：工程访问与编辑门禁基础设施

本批沿用阶段一提交 `0c8a85459077c5cf348f4b08d07530615f691bb6`。
按已确认的渐进边界交付，不增加比较窗口、菜单、应用流程或自动保存策略。
**完整编辑隔离尚未完成，不能据此开放比较功能。**

## 文件和依赖

| 文件 | 职责 |
| --- | --- |
| `libaegisub/include/libaegisub/subtitle_edit_gate.h`、`libaegisub/common/subtitle_edit_gate.cpp` | 工程身份、单调代数、写租约、比较会话、版本令牌、关闭和重绑定状态 |
| `libaegisub/include/libaegisub/id_allocator.h`、`src/ass_dialogue.cpp` | 有界原子行 ID 分配，保持复制语义 |
| `libaegisub/include/libaegisub/subtitle_rebind.h`、`libaegisub/common/subtitle_rebind.cpp` | 局部逐观察者通知、明确确认、旧状态保留 |
| `src/subtitle_project_access.h/.cpp` | 默认 const 读取、租约约束的字段写入、跨回调失效检查 |
| `src/include/aegisub/context.h`、`src/context.cpp` | 每工程创建独立门禁和访问对象，销毁前关闭 |
| `src/ass_file.h/.cpp` | 仅当前工程附加代数跟踪；快照不复制、交换该关联 |
| `src/selection_controller.h/.cpp` | 只读选择接口、选择预构建、静默安装 |
| `libaegisub/meson.build`、`src/meson.build`、`tests/meson.build` | 注册生产代码和测试 |
| `tests/tests/subtitle_edit_gate.cpp`、`subtitle_rebind.cpp` | 不依赖 wx 的基础模块测试 |
| `tests/tests/subtitle_project_access.cpp`、`subtitle_selection.cpp` | 生产 AssFile、AssDialogue、SelectionController 集成测试 |

门禁、ID 分配和重绑定协议不依赖 wxWidgets；访问对象和选择控制器依赖现有应用模型。
阶段一比较引擎、读取器和算法不变。独立 AssFile、读取器目标、基线和将来的候选状态仍可使用原来的可变接口。

## 门禁契约

`TryBeginWrite()` 登记一个写任务，允许同工程多个登记；只要还有登记存在，就拒绝比较。
调用方必须在启动异步任务之前取得租约，并移动到任务生命周期中，直到最后一次模型操作结束才释放。
重复取得租约不是数据互斥：当前模型仍只允许所属主线程访问。后台任务可登记生命周期、处理独立数据，再将持租约的工作交回主线程。
不得持有门禁互斥量等待任务、UI 或回调；互斥量仅保护短状态转换。

`TryBeginComparison()` 在写任务、其他会话或待完成重绑定存在时拒绝启动。
普通取消、异常展开和移动对象由 RAII 释放。进入重绑定后，丢弃失败批次不会解锁。
不同 Context 的门禁完全独立。未接入门禁的旧任务目前不会被登记，这是开放比较前的阻断项。

工程身份为进程内有界原子递增编号。代数在写租约取得、会话显式推进及旧 Commit 时递增，均检查溢出。
取消不会回退；不使用 Undo commit_id。旧 Commit 钩子只负责失效旧版本令牌，不能撤销已经发生的直接字段写入。
裸字段赋值而不 Commit 仍无法被此钩子发现。

`RevisionToken` 绑定工程和代数。延迟观察者必须用带令牌的 `Read` / 选择读取接口，不能缓存裸指针并在下一回调中继续使用。
未带令牌的 const 读取是当前同步读取，不是不可变快照；返回的 const 引用也不能跨模型修改持有。

## 受控访问完成度

`Context::subtitleAccess->Read()` 默认返回 `AssFile const&`，包含 const 容器和 const 行。
`Edit(lease)` 检查工程归属，返回不暴露可变指针的 WriteAccess。
支持按 ID 写普通字幕字段、按名称写样式字段、更新已存在的 Extradata 值。
禁止借普通字段接口更改行 Id/Row 或样式名称；结构修改和依赖处理留待后续批次。
写句柄仅保存弱授权；租约释放或访问对象关闭后再次调用会失败。单次调用在内部暂时持有登记，避免租约并发释放造成授权提前结束。
当前模型访问限创建访问对象的线程；不可将当前模型传到工作线程。
普通字段写入不是原子批量事务，也不自动创建 Undo 记录或发通知；业务迁移时须另行设计提交边界。

仍保留的缺口：

| 旧接口/路径 | 当前状态 | 后续处理 |
| --- | --- | --- |
| `Context::ass` 和 AssFile 公共容器/Properties | 可直接取得和保存可变指针 | 逐模块迁移后关闭当前工程的旧可变访问 |
| `AssDialogueBase`、`AssStyle` 公共字段 | 旧指针可直接赋值 | 改用租约绑定的字段操作，清理跨回调指针 |
| `GetActiveLine/GetSelectedSet/GetSortedSelection` | 仍返回旧可变指针 | 用新 const 读取，写入按 ID 重新定位 |
| 普通选择操作 | 保留旧行为，未强制门禁 | 在完整工程集成时接入有效会话或写授权 |
| command、编辑框事件、视频/音频工具、绑定和无模式对话框 | 仍走旧模型 | 分批迁移，不能只在 Commit 后拒绝 |
| Lua、异步写任务、排队加载、自动保存、CleanExtradata | 未全部登记/隔离 | 登记从启动到退出的完整生命周期，清理当前模型写路径 |

本批不提供新的字幕增删、替换、样式导入或 Extradata 导入接口。解析器和导入导出不受错误限制。

## ID 和选择

进程级 `IdAllocator::Next()` 用 CAS 递增，范围为 1 到 INT_MAX；先检查上限，再执行加法，不会溢出回绕。
耗尽抛出 `std::overflow_error`。测试使用独立有界实例，不重置生产分配器。
取消构建或解析失败可以产生空洞；Undo/Redo 不回退它。
普通 AssDialogue 复制和 AssFile 深复制仍分配新 ID；UndoInfo 保存 AssDialogueBase，恢复构造仍保留已有 ID。

`PrepareSilent(session, ids, active_id)` 预先分配并验证行 ID，拒绝模型中的重复 ID 和不存在的目标。
`InstallSilent(session, prepared)` 在任何状态改变前检查线程、工程、版本和会话；之后只交换预建 std::set 并写活动指针/active_row。
set swap 的不抛异常特性由 static_assert 验证。安装不分配、不通知、不扩展绑定组。
准备失败、过期、异工程或重复使用均不改变当前选择。当前接口面向已安装的模型；将来候选发布需按最终模型准备选择，不能复用旧模型的 PreparedSelection。

## 局部通知和旧节点生命周期

通用 `signal.h` 不变，原 Signal 调用异常行为不变。
`SubtitleRebindBatch` 是待发布流程使用的独立协议，**本批未接入任何现有发布路径或观察者**。
创建前调用方提供拥有旧节点的 shared_ptr 和完整的必要观察者名单；须保证名单完整，不能用空名单掩盖未迁移观察者。
每个观察者通过 Ticket 获取目标代数，在丢弃全部旧节点引用、安装新状态之后明确 Acknowledge。
回调正常返回不等于确认。Dispatch 隔离单个回调异常并记录 exception_ptr，允许继续调用其他观察者和重试失败者。
确认后再抛异常仍视为失败。回调执行中不能 Complete，同一观察者不能递归 Dispatch。

只有所有登记的观察者确认且无正在执行的通知，Complete 才释放旧状态并结束会话。
失败时保留批次对象以重试，不能恢复普通编辑；批次被意外丢弃时，门禁继续保留旧状态并封闭至工程关闭。
关闭使全部版本令牌失效；尚存的批次仍持有旧节点，覆盖关闭发生在通知内的情况。
批次的构造、Dispatch、Complete 和销毁由所属工程线程串行调度；生命周期内不得并发销毁批次对象。
协议不自动推断现有 Signal 回调是否丢弃了旧指针，也不提供实际原子发布。

必须逐一迁移/审核的观察者至少包括：

| 位置 | 重新绑定职责 |
| --- | --- |
| `src/base_grid.cpp`：BaseGrid::OnSubtitlesCommit、OnActiveLineChanged | 行映射、活动行与显示缓存 |
| `src/subs_edit_box.cpp`：SubsEditBox::OnCommit、OnActiveLineChanged、OnSelectedSetChanged | 活动行和编辑字段 |
| `src/audio_timing_dialogue.cpp`：AudioTimingControllerDialogue::OnFileChanged/Revert/OnSelectedSetChanged | 时间标记和行引用 |
| `src/audio_karaoke.cpp`：AudioKaraoke::OnFileChanged/OnActiveLineChanged | 行与音节缓存；同时审核 audio_timing_karaoke.cpp |
| `src/visual_tool.cpp`：VisualToolBase::OnCommit/OnActiveLineChanged | 可视工具的行、特征点与派生类缓存 |
| `src/initial_line_state.cpp`：InitialLineState::OnActiveLineChanged | 编辑初始行快照 |
| `src/dialog_translation.cpp`：DialogTranslation::OnExternalCommit/OnActiveLineChanged | 无模式翻译对话框行引用 |
| `src/dialog_style_manager.cpp`：DialogStyleManager::LoadCurrentStyles/OnActiveLineChanged | 样式指针与列表 |

此清单是当前已定位的必要迁移点，不是所有业务观察者已经安全的证明。开放实际发布前还须扫描所有 AddCommitListener、选择监听和跨回调缓存。

## 验证

使用 `tests/run_isolated.py` 复制测试数据到 build 下的独立目录；不运行 setup.bat/setup.sh，不清理或覆盖用户数据。
基础测试覆盖重复租约、拒绝比较/写入、RAII 异常、后台登记、独立工程、过期令牌、并发 ID 和耗尽、局部通知失败隔离、明确确认、延迟释放、关闭和通知重入。
生产模型测试覆盖 const 类型、租约越界、异工程授权、跨线程拒绝、快照/复制语义、真实 AssDialogue 并发 ID、静默选择、失败保持原状态和绑定行为回归。
必须保持阶段一比较/读取测试与一万行性能场景通过，并构建 Windows x64 主程序。

下一批仍需完成业务入口迁移、观察者登记、批量 Undo/Redo 和发布事务，之后才可接入比较界面。

本批最终验证：Windows x64 Release 主程序及两个测试程序构建通过。
隔离目录中基础回归 435/435、生产模型集成 21/21 通过，其中本批新增 27 项；保留阶段一全部 429 项测试。
阶段一一万行五种场景本次单轮实测约 16.6–70.2 ms（单点修改、逆序、完全不同、重复行、全部文本修改），仅作本机回归记录。
构建/测试日志位于 `build/c0-verified-build.log`、`build/c0-verified-main-tests.log` 和 `build/c0-verified-model-tests.log`，不纳入 Git。
