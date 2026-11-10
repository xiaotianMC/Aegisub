# C2-A Apply Engine（比较结果的选择性应用）

本阶段实现字幕版本比较的第二阶段核心：把用户在比较结果中选中的项目，作为**一次可撤销的修改**应用到当前文档。只有引擎和测试，**没有比较窗口或菜单**（比较界面是下一阶段）。没有修改 `signal.h`、Undo/Redo 历史格式、保存格式或 Lua API，没有新增公开可变 getter、`const_cast`、宽泛 friend 或全局锁。

## 1. 已确认的策略

`docs/subtitle-version-compare.md`“阶段二前置条件”列出的待定项，由项目负责人确认如下：

| 问题 | 策略 |
|---|---|
| 部分绑定组 | **拒绝**。应用之后，每个受影响的绑定组必须保持当前原样、恰好等于基线中的组，或被整组移除；否则拒绝并给出组令牌 |
| 未知附加数据 | **按 ID 重映射导入**。键值原样复制，ID 用 `PlanExtraMapping` 重新分配（相同键值复用已有 ID）；报告列出导入的键，供界面提示 |
| 无锚点插入位置 | **使用比较给出的边界** `insertion_boundary`。同一间隙内按基线顺序，与选择顺序无关 |
| 样式 | **导入缺失、拒绝冲突**。Importable 的样式定义在同一次提交中复制；Conflict、Ambiguous 拒绝；Missing（两边都没有）保留名称，ASS 允许 |
| 自动保存冲突 | 比较会话期间所有写者（包括保存、自动保存的基线移动、元数据）都被门禁排除（C1-C1c-12）。应用是普通提交，之后自动保存照常 |

## 2. 结构

| 部分 | 位置 | 职责 |
|---|---|---|
| 规划器 | `src/subtitle_apply.{h,cpp}` | 纯函数 `subtitle_apply::Prepare`：读两个独立模型和比较项目，返回最终样式/行列表（现有节点或新节点）、要追加的附加数据、提交类型与报告。不修改输入，拒绝以报告返回而不抛出 |
| 比较会话 | `src/subtitle_comparison.h` | `SubtitleComparison`：持有门禁的比较会话、起始修订令牌、当前文档快照、基线、字段能力和比较项目。只能由 `SubsController` 创建 |
| 发布 | `SubsController::BeginComparison` / `CheckApply` / `ApplyComparison` | 开始比较；试运行；应用。发布复用 C1-C1c-9 已审计的 Lua 发布提交点（`PreparePublication` + `PublishLua`），不新增第二条写入路径 |

### 调用顺序

1. `BeginComparison(baseline, fields)`：宿主线程；安装中、恢复受阻、通知中或已有写者/比较时抛 `SubtitleApplyRejected`，不持有任何东西。成功后取得比较会话：普通编辑、Undo/Redo、Lua、打开/关闭、保存、元数据修改都无法开始。
2. `CheckApply(comparison, choices)`：试运行，只返回报告。
3. `ApplyComparison(comparison, choices)`：
   - 有拒绝：什么都不改，比较仍然有效，用户可以重新选择。
   - 没有可应用的修改：结束比较，不改文档。
   - 否则：在同一会话下取新快照作为工作模型（核对行 ID 与比较时一致），规划，然后一次发布。撤销描述为 “apply comparison”，选择为应用的行（只有删除时保留幸存的原选择）。比较结束，门禁释放。
4. `SubtitleComparison::End()`：放弃，不应用。

### 选择 `Choice{item, fields}`

- Modified：只取 `fields ∩ item.changed ∩ 比较字段` 中的字段，其余保持当前值；行 ID 不变。
- Added：整行加入；基线不支持的字段（例如 SRT 的 Style、Actor）取默认值；新行 ID。
- Deleted：删除当前行。
- Unchanged，或 Modified 选不到任何变化字段：拒绝 `NothingToApply`。越界、重复：`UnknownItem`、`DuplicateChoice`。

### 依赖规则细节

- 样式只为“从基线取来的”内容检查：取了 Style 字段的行检查行样式，取了 Text 字段的行检查 `\r` 引用的样式。只改文本时，行原有的样式不受影响，即使两边定义不同也不拒绝。查找规则与 `AnalyzeDependencies` 相同（不区分大小写，按字段比较，新导出 `subtitle_compare::SameStyle` 共用）。
- 附加数据：从基线取 Extra 字段的行，用基线的条目替换；其他行保留当前引用。双方任何一行有无法解析的引用时整体拒绝 `InvalidExtradata`。

## 3. 测试

规划器（`tests/tests/subtitle_apply.cpp`，在 `subtitle-compare-tests` 中，10 项）：字段子集与行 ID、插入边界与基线顺序、无效选择、不支持字段、部分绑定组（新增与删除两个方向）、样式导入/冲突/缺失、只改文本不受样式影响、附加数据重映射、无法解析的附加数据。

集成（`ordinary-edit-tests`，3 项，真实 `SubsController`）：

| 测试 | 覆盖 |
|---|---|
| `comparison_freezes_writers_and_apply_is_one_undoable_step` | 比较期间行写入、项目写入、第二个比较都被拒绝，编辑框输入不写入；试运行不改文档；应用只发一次提交通知；修改行保留 ID；选择与活动行；撤销描述；会话释放；Undo/Redo 精确往返 |
| `refused_apply_changes_nothing_and_keeps_the_comparison` | 部分绑定组被拒绝时文档、历史、修订号、附加数据都不变，比较仍有效；整组选择后成功应用并带上绑定 |
| `ending_a_comparison_releases_it_and_stale_or_foreign_use_is_rejected` | 有写者时不能开始；End 释放门禁，之后应用抛出且不改文档；只选 Unchanged 被拒绝；空选择结束比较不改文档 |

这是新功能，旧代码无法编译这些测试，因此没有“旧代码失败”一列。

## 4. 未处理与风险

- 比较窗口、菜单与基线文件选择（含编码、FPS、轨道对话框）属于下一阶段。`BeginComparison` 同步运行比较，接受取消回调；大文件时界面应在进度对话框中调用。
- 发布提交点复用 Lua 路径，类名仍带 Lua 前缀（`LuaSubtitleTransaction`、`LuaSubtitlePublication`），只在 `SubsController` 内部使用。以后可改名为通用名称，本阶段不改动已交付代码的结构。
- 比较会话期间界面所有写操作都会被拒绝（失败关闭）；颜色选择器、样式助手、音频编辑会话打开时无法开始比较。比较界面需要决定如何向用户呈现这一点。
- Windows/MSVC 未构建。
