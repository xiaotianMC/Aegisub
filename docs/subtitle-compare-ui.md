# C2-B 比较界面

本阶段为 C2-A Apply Engine 加上用户界面：**文件 → 与字幕文件比较…**（命令 `subtitle/compare`）。用户选择一个基线文件，在窗口中查看差异、勾选要应用的项目（修改过的行可以只选部分字段），然后一次应用。应用走 C2-A 的 `ApplyComparison`，是一次可撤销的修改。没有修改 `signal.h`、Undo/Redo 历史格式、保存格式或 Lua API。

## 1. 流程

1. **试探**：命令先试探比较会话（立即释放）。有任何写者（颜色选择器、样式助手、音频编辑等）时，在打开文件框之前提示“正在编辑，关闭编辑工具后重试”。
2. **选择文件**：与“打开字幕”相同的文件框和格式列表（`Path/Last/Subtitles`）。
3. **读取**：`CharSetDetect::GetEncoding`（无法确定时让用户选编码），`SubtitleFormat::GetReader`，`subtitle_compare::ReadBaseline`。TXT 导入、MicroDVD 帧率等已有对话框在宿主线程上照常弹出。格式不支持、读取失败、用户取消都只提示或返回，不改任何东西。
4. **比较**：`BeginComparison` 取得比较会话（此后其他编辑全部被拒绝）。没有差异时提示并结束。
5. **窗口**（模态，“Compare Subtitles”）：
   - 顶部：基线文件名与修改/新增/删除数量。
   - 过滤：全部、修改、新增、删除；“全部勾选/全部取消”作用于当前过滤。
   - 列表：应用（复选框）、类型、当前行号、基线行号、时间（毫秒精度）、当前文本、基线文本、变化字段、备注（匹配说明：多个相同候选、顺序变化、时间与文本指向不同行、无可靠匹配）。
   - 字段区：选中的修改行列出每个变化字段“当前值 → 基线值”，可逐个勾选。新增和删除整行应用。
   - 报告区：每次勾选后试运行（`CheckApply`），显示将应用的数量、要复制的样式、两边都没有的样式、要复制的附加数据键；不能应用时列出原因（绑定组必须一起应用、样式定义冲突、样式重名、附加数据无法读取），“应用”按钮禁用。
6. **应用**成功后关闭窗口；**取消**或关闭窗口则结束比较，什么都不改。

## 2. 结构

| 部分 | 位置 |
|---|---|
| 视图模型（无 wx 依赖）：显示行、字段值、勾选状态、生成 `Choice` | `src/subtitle_compare_view.{h,cpp}` |
| 窗口与读取流程 `ShowCompareDialog(c, path)` | `src/dialog_compare.cpp`、`src/dialogs.h` |
| 命令与菜单 | `src/command/subtitle.cpp`、`src/libresrc/default_menu_platform.json` |

### 勾选规则

- 默认全部不勾选。
- 勾选修改行：取该行全部变化字段；如果之前只选了部分字段，保留那个部分选择。
- 在未勾选的行上勾一个字段：只取这个字段，并勾选该行（未勾选的行在字段区显示为全部未选，所以从零开始）。
- 取消最后一个字段：该行变为未勾选。

## 3. 测试

视图模型（`tests/tests/subtitle_compare_view.cpp`，`subtitle-compare-tests`，3 项）：行内容与字段显示顺序、1 毫秒差异可见、勾选与字段规则、按类型全选、生成的选择直接交给规划器。

窗口（`ordinary-edit-tests`，5 项，真实对话框、真实 ASS 写入与读取）：

| 测试 | 覆盖 |
|---|---|
| `compare_window_applies_checked_changes_as_one_undo_step` | 窗口打开期间写入被拒绝；初始“应用”禁用；勾选后报告正确；应用后一次撤销步骤、行 ID 不变、会话释放、Undo 还原 |
| `compare_window_cancel_changes_nothing_and_releases_the_session` | 勾选后取消：文档、历史不变，会话释放 |
| `compare_window_takes_only_the_chosen_fields` | 字段区只勾 Actor：只改 Actor，文本不变 |
| `compare_window_shows_refusals_and_keeps_apply_disabled` | 只勾绑定组的一行：报告说明原因，“应用”禁用，文档不变 |
| `compare_command_refuses_while_editing_and_reports_unreadable_files` | 有写者时命令在文件框之前拒绝；不支持的文件、无差异的文件只提示，不打开窗口 |

测试中发现并修复了两个问题：时间显示经过 `int` 转换会被舍入到厘秒（1 毫秒差异看不出来），改用 `GetMilliseconds()`；在未勾选的行上勾一个字段会连带取全部字段，改为只取该字段。

## 4. 未处理与风险

- 比较在宿主线程同步运行（带忙碌光标），没有进度条和取消。C2-A 的性能数据下 10000 行约 0.1 秒，暂不需要；更大的文件以后可放进进度对话框。
- 列表用 `wxDataViewListCtrl`，一次性加入所有变化行。行数极多时首次显示会慢一些。
- 编辑工具（颜色选择器、样式助手、音频编辑）打开时无法开始比较，命令会提示原因，但不会替用户关闭这些工具。
- 菜单项只加在 `default_menu_platform.json` 的文件菜单中，没有默认快捷键，也没有工具栏按钮。
- 新增的界面字符串尚未加入翻译文件。
- Windows/MSVC 未构建；GTK 下的真实交互（鼠标点击复选框）未做手动验证，测试通过发送 wx 事件驱动。
