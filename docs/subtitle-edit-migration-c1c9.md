# C1-C1c-9 Lua 隔离与事务化执行：调查与迁移记录

状态：**C1-C1c-9a–9e 代码与 Linux 自动验证完成；Windows x64 Release 未执行，人工验收未执行。** 因此本阶段的完成条件 17（Windows 构建）尚未满足，不能宣布 C1-C1c-9 完成。正式设计见 [lua-isolation-design.md](lua-isolation-design.md)。第 1–6 节是 9a 调查记录（实施前状态），第 7 节起是实施结果。

## 1. 基线、环境与保护文件

- 2026-11-03，Linux 工作区 `/home/bily/project/Aegisub`（不是 `O:/Aegisub-Native`），分支 `codex/style-name-width`。
- 开始时 HEAD `ac306076b`（docs(compare): report C1-C1c-8 load/close lifecycle migration），工作区干净，与 `origin/codex/style-name-width` 一致（C1-C1c-8 的三个提交已由用户授权推送）。
- `684b4a3`（Windows 验收补充）**不存在于本仓库**；按要求没有重建或推送。
- 保护文件：
  - `本版与原版差异清单.md`：本克隆中为已跟踪文件且与 HEAD 一致，SHA256 `6a922d69925ba61d4511fc29a712a3291e0034abdc2a39c8539b672bd0cf35d2`（Windows 工作区记录值 `5CE9…127B` 对应其本地修改/换行版本，两处不可直接比较）。开始与结束一致，未修改、未暂存。
  - `subprojects/checkasm.wrap`、`docs/subtitle-edit-migration-c1c1.md`：只存在于 Windows 工作区，本克隆中**不存在**，无法核对，本阶段提交不包含这两个路径。
- 历史文档在本仓库的实际文件名为 `subtitle-edit-migration-c1c1a/b/c.md`；任务中列出的 `c1c1.md`、`c1c.md`、`c1c1c8-windows.md` 在本仓库不存在。已阅读：isolation、c1a、c1b、c1b1–3、c1c1a、c1c1b、c1c1c、model-installation-design、model-installation、c1c1c8。

## 2. Lua 入口与读写路径（按当前源码）

行号对应 HEAD `ac306076b`。

| 入口 | 触发 | 读取 | 写入 | 授权 | Undo | Selection | 生命周期 | 异常/取消 | 线程 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `LuaCommand::operator()`（auto4_lua.cpp:869） | 菜单/快捷键/工具栏执行宏 | `new LuaAssFile(L, c->ass.get(), true, true)`：指针指向**活动** Styles/Events 节点；Info 按需复制 | `ProcessingComplete`（auto4_lua_assfile.cpp:715）对活动容器整表重排 + 逐个 `ass->Commit` | **无**；仅 Commit 内 `OnCommit` 的 `TryWrite`，拒绝发生在容器已改写之后 | 每个非空检查点一次 Commit，再以宏显示名 Commit 剩余修改 | 返回的行号表经 `lines` 换算为 ID；无返回时按原行号映射（:898–969） | `LuaAssFile` 引用计数 2：C++ 侧完成/取消各减一，Lua `__gc` 减一 | 脚本错误/取消 → `UserCancelException` → `Cancel()`；Extradata 已写入活动模型不回退；Commit 中途抛出 → 部分写入 | Lua 在 `DialogProgress` 后台线程运行（dialog_progress.cpp:147），主线程跑模态循环 |
| `LuaCommand::Validate`（:829） | 菜单打开、Toolbar `OnIdle`（toolbar.cpp:151–161）、命令调用前 | 只读绑定活动模型 | `parse_karaoke_data` → `LuaToAssEntry` → 活动 `AddExtradata`（assfile:324） | 无 | 无 | 读取当前选择行号 | 同步，`ProcessingComplete()` 无 Commit | `lua_pcall` 失败记日志返回 false | 主线程；可能与同一 `lua_State` 的后台宏并发（静态审查） |
| `LuaCommand::IsActive`（:974） | 菜单/Toolbar 切换状态 | 同上 | 同上 | 无 | 无 | 同上 | 同上 | 同上 | 同上 |
| `LuaExportFilter::ProcessSubs`（:1044） | 导出对话框 → `AssExporter::Export`（ass_exporter.cpp:83） | 导出局部副本 `AssFile subs(*c->ass)` | 写该副本，不写活动模型 | 不需要 | 不设 Undo（`can_set_undo=false`） | 无 | 同上 | 取消 → `Cancel()` 并重抛 | 后台线程，只触及副本 |
| `LuaExportFilter::GenerateConfigDialog`（:1081） | 导出对话框配置面板 | 只读绑定活动模型 | 同 Validate 的 Extradata 副作用 | 无 | 无 | 无 | 同步 | 失败记日志 | 主线程 |
| `ExportFilter::LoadSettings/GetConfigDialogWindow`（auto4_base.cpp:185–200） | 导出 | Automation Settings | `SetAutomationSetting`（受控 Metadata 接口） | 已有 | Metadata 修订 | 无 | 同步 | — | 主线程 |
| `LocalScriptManager::Reload`（auto4_base.cpp:369） | `FileOpen` 提交后外围 | `context->ass->Properties.automation_scripts`（活动模型直接读） | 无字幕写；保存脚本列表走 `SetMetadata`（:475） | Metadata 接口 | 无 | 无 | Committed 之后 | 记为外围错误 | 主线程 |
| `LuaScript::Create/Destroy`（:470/:608） | 加载、重载、自动加载 | 无字幕数据 | 无 | — | — | — | `lua_State` 按脚本常驻，跨调用复用 | — | 主线程；运行中重载无保护（UI 模态时不可达，静态审查） |
| 运行时辅助（:109–358） | 脚本调用 | `file_name`（SubsController）、`project_properties`（**活动** `c->ass->Properties`）、视频/音频/关键帧、`decode_path` | `set_status_text` 异步到主线程 | — | — | — | registry 保存裸 `Context*` | — | 在工作线程直接读取主线程对象（数据竞争，静态审查） |
| `aegisub.set_undo_point`（assfile:689） | 脚本 | `lines` 指针表 | 只追加检查点 | `can_set_undo` | 见第 4 节 | — | 绑定到最近创建的 LuaAssFile | 过期对象报错 | 工作线程 |

`src/` 中再无其他把 Lua 结果写入活动模型的路径（已检索 `LuaAssFile`、`ProcessingComplete`、`ProcessSubs`、`auto4_`、`Automation4::`）。

### 直接取得活动 AssFile 可变访问的 Lua 代码

1. auto4_lua.cpp:839、876、984、1091：`c->ass.get()` 传给 `LuaAssFile`（非 const）。
2. auto4_lua_assfile.cpp:324：`ass->AddExtradata`（含只读上下文与工作线程）。
3. auto4_lua_assfile.cpp:715–751：`ProcessingComplete` 清空并重排 `Info/Styles/Events`、`ass->Commit`。
4. auto4_lua.cpp:952–965：发布后直接遍历 `c->ass->Events` 计算选择。
5. auto4_lua.cpp:340–355：工作线程读 `c->ass->Properties`（只读但跨线程）。

## 3. Lua 可修改的数据（以当前 API 为准）

| 数据 | 能力 |
| --- | --- |
| Dialogue | 读写 comment、layer、start/end、style、actor、effect、margin_l/r/t（margin_b 只读镜像 margin_t）、text、extra；插入、删除、替换、整体重排 |
| Script Info | `class="info"`：增删改（key/value），惰性复制 |
| Styles | `class="style"` 全部字段；增删、改名、移动（不更新引用） |
| Extradata | 仅通过 dialogue 的 `extra` 表（字符串键/值）；新增条目去重追加，不能删除或改写已有条目；未在 `extra` 中出现的未知数据随未修改行原样保留 |
| 不可访问 | Attachments、Properties/Metadata、UI State、选择以外的项目状态；只能读 `project_properties` |

API 不做样式引用、重复样式名校验；替换行一律获得新行 ID；绑定组标识存于 Extradata 值（`aegisub.enhanced.subtitle-binding`），通过 `extra` 原样回写即保持组语义。

## 4. Undo 点与历史：从源码得出的旧行为

| 情况 | 旧行为 |
| --- | --- |
| 有修改后 `set_undo_point(d)` | 生成一步，描述 d；状态为该时刻的完整行表 |
| 无修改时 `set_undo_point` / 连续重复 | 不记录 |
| `set_undo_point("")` 且历史非空 | 活动模型改变并通知，但 `OnCommit` 不入栈、不推进 commit_id（该修改并入下一步快照） |
| 最后一个 Undo 点后仍有修改 | 以宏显示名再生成一步；显示名为空时只改容器、不 Commit、不通知 |
| 每一步 | amend=-1 不合并；空 Styles/Events 补默认节点（新 ID）；清 Redo；截断 Undo Levels；`Save on Every Change` 可能保存 |
| 每步记录的选择 | 脚本运行前的选择 ID 与文字选区；脚本返回的选择只写入栈顶 |
| 每步 Extradata | 原实现中工作线程早已写入活动模型，所以每步都是**最终**完整 Extradata |
| 失败/取消 | 不 Commit，不产生历史；但已写入的 Extradata 残留 |

结论：现有 `UndoInfo` 格式可以表达这些步骤，只需一个由已准备值构造的内部构造函数；多步历史可在提交点一次 splice，不需要压缩成单步，也不需要逐次活动 Commit。**未发现需要暂停的阻断。** 上表须在 9b 开头用真实 Lua 运行时刻画测试确认后才作为最终期望。

## 5. 行 ID 与 Extradata

- `Snapshot()` 保留行 ID；Lua 未改的行在工作模型中保持 ID，发布时按位置映射复用原 live 节点，与旧实现“未改行节点不变”一致。
- 替换/插入行由进程级单调分配器取得新 ID；删除不复用；排序不改变节点身份。不能也不会按行号重新识别旧行。
- 令牌未变时活动 Extradata 是工作模型 Extradata 的前缀；新增条目 ID 从 `next_extradata_id` 递增，不可能与活动条目冲突。仍做前缀、唯一、引用完整性校验；值不解析，不把数字值当作引用。

## 6. 发现的问题与风险

| # | 问题 | 证据 | 处理 |
| --- | --- | --- | --- |
| 1 | 宏执行全程无写授权，Commit 拒绝发生在容器改写后 → 比较会话期间运行宏会部分写入 | assfile:723–743；subs_controller.cpp:533 | 9c 原子发布 |
| 2 | 多步 Commit 中任一步或观察者异常 → 前几步已发布 | assfile:734–737 | 9c/9d 一次发布 |
| 3 | 工作线程写活动 Extradata；失败/取消残留；与 AutoSave 快照存在数据竞争 | assfile:324；subs_controller.cpp:478 | 9b 工作模型 |
| 4 | 只读 Validate/IsActive/配置通过 `parse_karaoke_data` 写活动 Extradata | assfile:646 | 9b 暂存目标 |
| 5 | 工作线程读取 `c->ass->Properties`、SubsController 文件名 | auto4_lua.cpp:109、333 | 9b 启动时捕获 |
| 6 | Toolbar 空闲刷新可能在宏运行期间于主线程使用同一 `lua_State` | toolbar.cpp:151；dialog_progress.cpp:147 | 9e 运行中守卫（静态审查，未复现） |
| 7 | 运行中 Reload/Destroy 关闭 `lua_State` 无保护 | auto4_lua.cpp:608 | 9e（UI 模态时不可达，静态审查） |
| 8 | 未经 `exception_wrapper` 的闭包（delete/insert/append 等）让 C++ 异常穿过 Lua C 边界 | assfile:373–380 | 9e |
| 9 | 宏开始/结束对 Interaction、比较会话无冲突检查；旧结果无文档身份校验 | auto4_lua.cpp:869 | 9c |

外部副作用（文件、剪贴板、状态栏、对话框、日志）不属于字幕事务，执行即生效。

## 7. 实施结果（9b–9e）

### 7.1 架构

| 部分 | 实现 |
| --- | --- |
| 工作模型 | `SubsController::BeginLuaTransaction`：宿主线程、非安装/恢复封闭；用一次 ComparisonSession 探测拒绝任何写登记/Interaction/比较；捕获 `RevisionToken`；`Snapshot()` 深复制（行 ID、Properties、Extradata、`next_extradata_id`）；记录选择/活动行 ID。运行期间不持租约、会话或互斥锁 |
| Lua 绑定 | `LuaAssFile(source const&, target*)`：宏读写工作模型；Validate/IsActive/过滤器配置以 const 方式同步读活动模型，`target=nullptr`；导出过滤器读写自身独立副本。`lines` 改为 const 指针，活动节点不可能被写 |
| 节点所有权 | 原行属工作模型；脚本新建的样式/对白行在 `allocated_lines`，Script Info 副本与新条目在 `info_entries`；完成、取消、过滤器应用均无删除队列，无重复释放 |
| 运行快照 | 宏运行时 `project_properties`、`file_name` 读取启动时捕获值（registry `run_snapshot`），工作线程不再读活动 AssFile/SubsController |
| 结果 | `LuaAssFile::TakeMacroResult`：Undo 点、最后修改、最终行表、所有新建节点（`LuaSubtitleResult`），对象随即过期 |
| 准备 | `PrepareLuaPublication`：再次检查令牌 → 取得 ComparisonSession 独占借用 → 在借用内复核令牌；工作原行按位置映射回活动节点（复核行 ID）；校验节点归属、重复节点、行 ID 唯一、Extradata 前缀/唯一/新行引用存在；补默认样式/行；按 OnCommit 规则准备全部历史条目；计算退役节点。全部在独立数据上完成 |
| 发布 | `PublishLua`：`AdvanceRevision`（可能抛出的最后一步）→ noexcept 交换 Info/Extradata、重建 intrusive 链表、重排行号、splice 历史、清 Redo、Undo Levels 截断、commit_id → 一次 `AnnounceCommit` → 安装选择 → 释放借用 → 按条件最多一次 `Save on Every Change` |
| 观察者异常 | 提交点后模型与历史已一致；按普通 Commit 语义向上传播；退役节点与历史移入 `lua_retired_after_failure`，随 SubsController 析构释放，未刷新的观察者不会触及已释放内存 |
| 运行守卫 | `LuaScript::running`：运行中 Validate/IsActive 返回 false，不碰 Lua 状态；Reload 拒绝；`LuaCommand` 保存脚本指针，判断不读 registry |
| C ABI | `delete/deleterange/insert/append/script_resolution` 与 ipairs 迭代器改经 `exception_wrapper` |

Lua 发布不调用 `AssFile::Commit`，不推进 document_epoch，不触发 FileOpen/MRU，不清空 Undo（Redo 按原规则清空）。

### 7.2 与旧实现的差异（均已在测试中逐项断言）

| 差异 | 性质 |
| --- | --- |
| 多步运行只发一次 Commit 通知（类型为各步按位或） | 已批准的有意差异 |
| `Save on Every Change` 按最终状态最多保存一次 | 已批准的有意差异 |
| 宏显示名为空：仍不生成历史，但发通知并重排行号 | 已批准的有意差异（防止悬空节点） |
| 首次修改 Script Info 前的 Undo 点：旧实现撤销后 Script Info 全部丢失（实测 4 条 → 0 条），现保留原 Script Info | 缺陷修复 |
| 失败/取消后 Extradata 不再残留；只读回调不再写活动 Extradata | 缺陷修复 |
| 比较会话或其他写任务期间：旧实现第一步写入后抛异常（部分写入），现启动即拒绝、零修改 | 缺陷修复 |
| 字幕对象方法错误现带原始消息，原为 “C++ exception” | 缺陷修复 |
| 运行期间文档被修改、替换或开始比较：结果被拒绝并记录错误 | 新保护 |
| 中间步骤间不再穿插观察者引起的选择调整 | 通知合并的副作用 |

ID、Undo 步数、描述、每步完整数据、空/重复 Undo 点、选择返回、Redo 顺序、脏状态与旧实现一致，由刻画测试锁定（8 项刻画测试先在未修改代码上通过，日志 `build/c1c9-premigration-lua.log`）。

### 7.3 修改文件

- 新增：`src/subtitle_lua_transaction.h`、`tests/tests/lua_isolation.cpp`
- 修改：`src/auto4_lua.h`、`src/auto4_lua.cpp`、`src/auto4_lua_assfile.cpp`、`src/subs_controller.h`、`src/subs_controller.cpp`、`src/auto4_base.cpp`（本地脚本列表改经 `Read()`）、`src/ass_exporter.cpp`（导出副本改经 `Read()`）、`meson.build`（测试源与 `AEGISUB_SOURCE_ROOT`）
- 文档：本记录、`docs/lua-isolation-design.md`

未修改：`signal.h`、Undo 历史格式（仅新增从准备值构造 `UndoInfo` 的内部构造函数）、ASS 格式、Lua API、`Context::ass` 声明、第三方依赖。

## 8. 测试

新增 `lua_macro` 27 项，全部使用真实 `LuaScript`/`LuaCommand`、`DialogProgress` 工作线程与模态循环、SubsController、SelectionController 和门禁：

| 类别 | 测试 |
| --- | --- |
| 旧行为刻画（8） | 多 Undo 点逐步 Undo/Redo；空描述 Undo 点并入下一步；只读宏无历史；替换行新 ID/未改行保留 ID/插入/删除；返回选择与活动行；Script Info/样式/Extradata 经历史往返；运行错误与 `aegisub.cancel()`；保存在全局的旧 subs 对象报错 |
| 旧行为补充（3） | 删除全部对白后默认行与逐步撤销；改写绑定行保持绑定 Extradata 条目及未知条目；空显示名宏 |
| 事务（11） | 失败不残留 Extradata；比较会话期间零修改；只读 Validate 不写 Extradata；首次改 Script Info 前的 Undo 点；运行中编辑→过期；运行中开始比较→拒绝且之后可正常运行；运行中 Close→新文档不受污染；完成后用户取消→丢弃；运行中 Validate 不重入；发布后观察者异常；多步运行单次通知 |
| 兼容与持久化（4） | 7 个 `automation/autoload` 脚本全部加载、strip-tags 实际运行并撤销；导出过滤器只改其独立副本；宏结果保存后重开；方法错误消息 |
| 性能（1） | 见第 9 节 |

### 结果（Linux，Ubuntu 26.04，GCC，wxGTK 3.2，Meson release）

| 程序 | 结果 |
| --- | --- |
| ordinary-edit-tests | 124 通过（97 原有 + 27 新增）；另 38 项为 C1-C1c-8 已记录的 wxGTK 限制（37 项 `CaptureMouse` 断言中止 + 1 项 GTK 文件对话框），与改动前相同，未计入通过 |
| subtitle-compare-tests | 55/55 |
| gtest-main | 433/433 |
| 合计 | 612 项通过 |

每个 ordinary 测试单独进程运行以免中止项影响其他项（日志 `build/c1c9-ordinary-each.log`）；`lua_macro` 全套另连续 3 次通过。取消测试最初用 idle 事件驱动对话框导致挂起，改为定时器轮询后连续 5 次稳定；这是测试驱动问题，产品代码未改。

**Windows x64 Release：未执行。** 必须在 Windows 工作区拉取本阶段提交后构建并运行（预期 ordinary 162 项全部可运行）。

## 9. 性能（Linux release，单次运行，`build/c1c9-perf.log`）

宿主阶段用与宏相同的生产 API 计时（每行一个新节点、两个 Undo 点、200 条 Extradata）；“完整宏”是真实 Lua 宏（两遍改写全部行、两个 Undo 点）的端到端时间，包含脚本执行和进度对话框。

| 行数 | 快照 | 结果转换 | 校验+历史准备 | 发布 | 完整宏 | RSS 前→后 |
| ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 1000 | 0.1 ms | 0.5 ms | 0.8 ms | 0.4 ms | 39 ms | 84.4 → 92.0 MB |
| 5000 | 0.7 ms | 3.1 ms | 5.9 ms | 2.3 ms | 123 ms | 92.9 → 100.0 MB |
| 10000 | 1.3 ms | 9.7 ms | 13.5 ms | 5.1 ms | 261 ms | 100.8 → 112.0 MB |

额外成本是每次宏运行一份工作模型深复制和一次映射；Undo 历史内存与旧实现相同（每步一份值快照）。Validate/IsActive 不复制模型。校验与发布在宿主线程，10000 行时合计约 19 ms。不检查进度的长循环仍只能等待脚本结束（合作式取消），未强制中断线程。

## 10. 剩余旁路与限制

- `Context::ass` 仍公开可变（C1-C1c-10 范围）。
- Lua 运行时辅助函数仍在工作线程读取视频/音频控制器、Project、`c->path`（非字幕文档数据，上游既有）；registry 保存裸 `Context*`。
- 运行中只对同一脚本的 Reload 做了拒绝；`LocalScriptManager::Reload`（FileOpen）或析构在宏运行中销毁脚本仍无保护，UI 模态期间不可达，仅静态审查。
- 外部副作用（文件、剪贴板、状态栏、对话框、日志）不回滚。
- 通知后的观察者异常不升级为 RecoveryBlocked，与普通编辑一致。

## 11. 保护文件、Git 与人工验收

- `本版与原版差异清单.md` 开始与结束 SHA256 均为 `6a922d69925ba61d4511fc29a712a3291e0034abdc2a39c8539b672bd0cf35d2`；另两个保护文件不在本克隆中。
- 提交仅保存在本地，**未推送**；提交名称与哈希见交付消息。
- 人工验收（未执行）：Windows 构建与全量测试；桌面运行常用第三方宏（含多 Undo 点、对话框、进度、取消）；工具栏放置带 validate 的宏并在运行中观察；运行中关闭主窗口；真实大文件性能。
