# C1-C1c-9 Lua 隔离与事务化执行：调查与迁移记录

状态：**C1-C1c-9a（调查与设计）已完成；9b–9e 尚未开始，生产代码未修改。** 不能据本记录宣称 Lua 已隔离。正式设计见 [lua-isolation-design.md](lua-isolation-design.md)。

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

## 7. 分批计划

| 批次 | 内容 | 提交 |
| --- | --- | --- |
| 9a（本批） | 调查、设计、测试计划 | docs(compare): investigate lua isolation and design transactional execution |
| 9b | 先写旧行为刻画测试；工作模型/只读绑定/暂存 Extradata/启动时捕获；LuaAssFile 不再接触活动模型 | refactor(compare): isolate lua subtitle execution |
| 9c | `LuaPublication`：令牌与原子独占授权、结构校验、noexcept 提交点、单次通知、选择/活动行、取消与异常 | refactor(compare): publish lua subtitle transactions |
| 9d | 多 Undo 点历史准备、OnCommit 规则复现、ID/Extradata 映射与校验 | fix(compare): preserve lua undo and extradata semantics |
| 9e | 过滤器/配置入口、运行时守卫、异常包装、仓库 Automation 脚本回归、性能、旁路审计、最终报告 | test(compare): verify lua isolation and compatibility |

## 8. 测试计划

- 刻画（9b 首项，旧代码上先通过）：真实 `LuaScript` 从隔离目录加载临时脚本，经 `cmd::get(...)->operator()` 运行；断言每步描述、Undo/Redo 后完整数据、行 ID、Extradata、选择/活动行、脏状态。需先确认测试宿主下 `DialogProgress` 模态与 automation include 路径可用（Linux GTK 下拖动类测试受限，见 c1c1c8 报告）。
- 兼容：第 10.1 节列出的全部当前 API 能力；`automation/autoload` 7 个脚本加载及可在无音视频时运行的宏（strip-tags、cleantags、select-overlaps、karaoke-auto-leadin、kara-templater 等）。
- 事务：编译错误、运行错误、`aegisub.cancel()`、用户取消、C++ 回调异常、转换失败、ID/Extradata 冲突、授权拒绝、令牌过期、RecoveryBlocked、观察者异常、过期 userdata；每项比对完整状态包（文本、ID、顺序、Styles、Info、Extradata、Selection、Active、Undo/Redo、Metadata、路径、脏状态、修订令牌）。
- 生命周期：运行中 Load/Close/New/重载/普通编辑/比较会话 → 拒绝发布；保存后重开；Undo/Redo。
- 性能：1000/5000/10000 行、大量 Extradata/Styles、只读宏、多 Undo 点；分阶段耗时与峰值/历史内存。
- 平台：Linux 实际运行；Windows x64 Release 须在 Windows 工作区执行，未执行不得标记通过。

## 9. 本批验证

只新增两份文档，没有修改生产代码或测试，未重新构建或运行测试（二进制与 C1-C1c-8 交付时相同）。`git diff --check` 通过；保护文件哈希开始/结束一致。提交仅保留在本地，**未推送**，等待负责人确认设计第 8 节后进入 9b。
