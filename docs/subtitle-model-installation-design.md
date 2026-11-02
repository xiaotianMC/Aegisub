# C1-C1c-7 宿主模型安装正式设计

2026-11-02；设计先于生产代码修改。基线 d114561f481f8b42223997c14d41535be08621f9，codex/style-name-width，实时远端相同。账号xiaotianMC，fork push=true/upstream=false，fork开放PR=0，上游开放PR已检查，不合入。用户差异清单、checkasm.wrap、未跟踪c1c1报告保护，不stash/reset/clean/纳入提交。

## 真实调用链与范围

SubsController::Load 当前独立解析后先swap当前AssFile、更新路径/Metadata、清历史，再调用Commit。Commit先NoteLegacyCommit/重排行号、PushState可信历史，最后AnnounceCommit；Signal遇异常中断其余回调。两项诊断已复现授权过晚和部分通知。Selection已按IDs存储；UI/Metadata已分开；保存、字体后台输入已独立。

本阶段只建设安装协议及接入生产Load的发布边界；不迁移完整Lua/Close业务、不私有化Context::ass、不实现Apply Engine。既有公开Owner仍是阶段外旁路，不能据本阶段宣称所有字段写入封闭。

## 状态与合法转换

| 状态 | 活动模型/操作权限 | 退出 |
| --- | --- | --- |
| Preparing | 独立构建；普通写/比较按原门禁，变化会使输入过期；保存、新安装和宿主关闭拒绝；只捕获期望令牌 | Prepared或Aborting |
| Prepared | 候选/历史/Metadata/路径/选择/观察者快照齐全；仍不改live；保存、新安装和宿主关闭拒绝 | Authorizing或Aborting |
| Authorizing | 宿主线程检查、目标工程/文档/代数、Metadata修订、可信能力及无其他写/比较；取得C0独占会话；此时未推进修订 | 观察者准备、Aborting |
| 观察者准备（Rebinding.prepare） | 无模型写、比较或保存；prepare只构建私有数据，不改变现有绑定；取消仍有效 | Publishing或Aborting |
| Publishing | 唯一、无分配的模型/历史/Metadata/路径/选择安装提交点；此前预留全部容量并检查计数器；旧状态已归事务所有 | Rebinding.confirm或RecoveryBlocked |
| Rebinding.confirm | 持C0会话及retired模型；逐观察者Dispatch，明确Acknowledge；普通写/比较/保存、重入安装禁止；不能取消成普通失败 | Committed或RecoveryBlocked |
| Committed | 全部确认后Complete，释放旧模型/会话；恢复正常操作 | 可开始下一安装 |
| Aborting | 发布前丢弃候选/准备值，释放会话；不改模型/历史/路径/修订/绑定 | 终止 |
| Recovering | 显式重试未确认观察者；保留同一候选与旧模型、相同文档身份 | Committed或RecoveryBlocked |
| RecoveryBlocked | 封闭写、比较、保存和新发布；只可查询状态/失败观察者、显式重试或关闭宿主；旧节点继续保留 | Recovering或宿主关闭 |

Publishing开始前就进入受控状态，不能先swap再标记安装开始。取消仅在提交点前生效；发布后取消请求不伪装回滚，必须继续确认或进入RecoveryBlocked。准备失败保证原工程不变；发布后本阶段选择失败封闭，不承诺任意观察者副作用可完整回滚。

## 身份和权限

保留C0工程State身份，区分Project Identity（现有document编号）与Document Epoch（同一宿主每次文档安装推进）。Generation继续单调推进，不回退、不以undo commit_id代替。令牌比较包含工程、文档epoch与generation；旧任务在成功发布后不能授权新文档写入。WriteLease登记记录文档epoch，失效检查覆盖Valid、Token.Valid/Hold及Edit。

安装权限是不可复制、不能由业务构造的Installation Capability，只能由可信宿主组件（本阶段SubsController）在专用安装器内创建。普通WriteLease/ComparisonSession不能伪造它。独占借用复用C0 ComparisonSession机制，用于排除其他写事务与比较，但“有ComparisonSession”本身不等于可替换模型。推进文档身份只能由安装器通过最小内部接口完成。所有计数器先检查耗尽；拒绝和准备失败不推进令牌。

候选是独立值/独立AssFile深复制；不将调用者保留的mutable节点直接移入live，不返回活动AssFile&或公开mutable回调。独立模型仍使用原可变API。

## 准备、授权与提交点

Load捕获工程/文档/修订与Metadata修订，独立解析，构建安装候选；补默认行和样式，验证唯一有效行ID、顺序、Extradata ID唯一及行引用完整、目标选择和Active存在；准备文件路径、保存基线、Undo/Redo初态及观察者名单。所有可能分配的大型状态先完成，避免重复不必要深复制。

授权首先检查宿主线程、令牌、身份、Metadata修订，再尝试C0独占会话，拒绝任意已活动ComparisonSession或未退出写事务，不能共享普通嵌套scope强行安装。检查发布中/RecoveryBlocked及候选单次使用状态。prepare回调不改绑定；准备失败或取消释放会话，原修订也保持不变。

准备就绪后预构造C0 SubtitleRebindBatch、retired状态所有权、目标Selection容器及历史列表。在唯一提交点推进Document Epoch/Generation，swap已准备的模型/历史/Metadata/路径/选择。旧模型始终由安装事务和C0 retired持有。不会调用普通AssFile::Commit冒充安装：可信历史直接使用已准备的原历史格式初态；随后按Commit、Selection、Active既有相对次序逐项重绑定通知。

Load的备份/MRU/FileOpen等外围业务必须明确属于提交后工作，不能用其成功冒充模型确认；本阶段只做最小必要边界接入，完整外围生命周期迁移留c-8。接口必须可区分提交成功后的外围错误与尚未提交失败。

## 逐观察者登记与确认

C0 SubtitleRebindBatch已有名单、Ticket、单观察者异常隔离、确认/失败记录、retired持有、门禁封闭和RAII；复用它，不改signal.h，不另造通用全局锁。

普通Commit/Selection通知保持原相对次序、重复通知、正常异常语义。模型安装使用项目本地的观察者登记快照，逐个交给C0 Dispatch，不能把整个Signal当一个已确认观察者。登记适配继续支持已有连接生命周期，但每个必要观察者获得自己的Ticket。

生产观察者只有在审计并清除旧模型引用、安装新绑定/独立快照后，才在自己的重绑定逻辑显式Acknowledge。缺少确认即失败，即便回调正常返回也不能自动成功。未迁移的legacy回调可继续收到安装通知，但没有Ticket确认则进入RecoveryBlocked，不允许静默绕过。

必须覆盖AssFile Commit、Selection与Active的全部实际订阅：网格、编辑框/文字初态、历史选择、音频/Karaoke、视觉工具、视频/UI、B3对话框和样式缓存等。仅存独立快照者仍需明确确认清理/重建完成。安装中的订阅快照固定；新增订阅拒绝或推迟到提交后，不能缺失应确认观察者。

断开连接与销毁所有者严格区分。每个生产订阅额外携带独立weak所有者凭据；对应强凭据声明为首个成员，晚于所有模型引用/缓存销毁。栈式对话框的强凭据也早于草稿/连接声明。只有显式凭据过期才可作为withdrawn确认，既不调用销毁对象，也不将普通返回、单纯Disconnect或回调函数对象析构冒充释放全部引用。Disconnect但所有者仍存活的槽保留在名单内，必须继续封闭；未提供凭据的事务内消失同样保持Pending，直到明确恢复或关闭宿主。凭据仅证明本对象的引用已释放，不能转交仍存活的外部借用者。

prepare保持原绑定，确认发生在发布后；后续观察者不得因前一个抛异常而遗漏。准备回调期间连UI State写入也拒绝；确认期间允许观察者更新视口UI State，但Metadata、选择setter及字幕写仍被封闭。C0记录逐观察者异常，宿主公开状态与未确认名单。重入模型写、安装、保存或恢复均拒绝；不持有模型互斥锁调用观察者或等待UI。正常主窗口Close事件和直接deferred Destroy均做最小宿主保护：安装进行中请求取消并阻止销毁发布栈；失败封闭后允许关闭宿主，先销毁/注销观察者再释放模型。FrameMain析构中的provider关闭仅清理资源，不再写sealed Metadata。任意业务直接delete Context/FrameMain仍是尚未私有化Owner的阶段外旁路，不承诺其安全。

## 保留与恢复边界

授权前/prepare失败：没有替换、没有历史/Selection/Metadata/路径/保存状态更改、没有代数推进，原绑定仍有效。

发布后确认失败：候选是可见当前模型，但尚未报告安装成功；原模型、原历史及恢复元信息保留，所有引用仍有所有者。禁止普通写、比较、新发布、Save/AutoSave；故障组件不得继续进行编辑。其他必要观察者仍会被派发，故障引用不会因旧模型释放变为悬空。

RecoveryBlocked解除有两条明确路径：宿主线程显式重试，在同一文档/修订与事务下全部必要观察者重新绑定并确认后Complete；或关闭宿主，注销所有观察者，关闭门禁，释放retired数据。重试重新派发整个固定名单，包含曾确认者，因此确认回调必须可重复刷新；未确认者不能被跳过。没有发布完成模型的异常不得通过Retry伪装成功，只可关闭宿主。定时自动保存遇安装/恢复封闭时跳过，显式保存请求返回拒绝。本阶段不提供“忽略失败观察者继续”的按钮，不承诺任意副作用回滚。

## 验证与分批

第一族：正式设计、不可伪造能力/身份扩展、候选验证/单次使用、观察者登记及C0确认/retired协议、状态转换、取消/失败封闭/退出；独立AssFile和生产对象基础测试。第二族：真实SubsController::Load接入、全观察者适配、保存/关闭最小保护、两项负向诊断升级为正式应通过回归及全量真实入口测试。依赖需要时可按完整功能族增加提交。

必须验证解析/默认/重复ID/非法extra/授权/过期/活动比较/其他写/prepare失败的完整状态包不变；发布后正常/抛异常/后续读取/不确认/注销/销毁/重入、旧节点保留、恢复/关闭；旧文档任务拒绝、重复安装、取消、Metadata/Selection/历史、保存重开及原595项全部通过。每批Windows x64 Release、隔离目录全测、diff/保护文件hash检查、独立提交、普通push、实时远端核对。Linux/macOS只报告实际CI；人工关闭、真实媒体、HiDPI独立列出。

本设计初稿先于生产代码；以上补充记录生产验证中收紧的实际边界。实现及测试细节、必要的具体适配调整和未完成事项记入subtitle-model-installation.md。C1-C1c-7达到完成条件后停止，不自动进入c-8、Lua或Owner最终私有化。
