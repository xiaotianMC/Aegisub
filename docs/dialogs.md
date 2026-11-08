# 对话框

弹窗统一改成和主窗口一样的自绘控件。已完成的对话框在各自的小节里说明；公共部分如下。

## 公共控件

- **底部按钮** `flat::DialogButtons`（`src/flat_dialog.{h,cpp}`）：代替 `CreateStdDialogButtonSizer`。
  - 按钮是 `FlatButton`，带标准 id（`wxID_OK`、`wxID_CANCEL`、`wxID_APPLY`、`wxID_HELP`、`wxID_CLOSE`、`wxID_YES`、`wxID_NO`）和系统的标签，所以对话框自己的处理和 wxDialog 的默认处理（确定接受并关闭、应用写入、取消关闭）照常工作。
  - 帮助和其他附加按钮（`AddLeft`）在左边；标准按钮在右边，顺序按系统：GTK 和 macOS 是“应用、取消、确定”，确定在最右；Windows 是“确定、取消、应用”。`AddRight` 加在标准按钮之前。
  - 对话框为之而来的按钮（确定、是；只有关闭时是关闭）用强调色填充，文字白色；聚焦时填充内加一圈浅色环。
  - 只有关闭时，关闭是确认 id，和 `CreateStdDialogButtonSizer` 一样。
- **按键**（`flat::UseFlatButtonKeys`，`DialogButtons` 自动装上；自己排按钮的对话框单独调用）：系统按钮的按键行为不变。
  - Esc 和标题栏的关闭按钮：按取消（或对话框 escape id 对应的按钮；escape id 是 `wxID_ANY` 时依次找取消、关闭、确认按钮）。按钮不可用时不按。
  - 回车：焦点在单行输入框（没有 `wxTE_PROCESS_ENTER`）、勾选框、或面板本身时，按确认按钮；多行文本、自己处理回车的输入框、列表、按钮、下拉框里的回车归它们自己。
  - Alt + 按钮标签里带下划线的字母：按该按钮。
- **数字框** `flat::NumberField`：名称写在框内的 `FlatFrame`，代替 `wxSpinCtrl`。可以输入（超出范围不采用，离开或回车时显示实际采用的值）、滚轮一步步改、按住名称左右拖动连续调整；`precision` 是保留的小数位。`SetValue` 不触发 `on_change`。样式编辑器的数字框就是它。
- **列表** `FlatList`（`src/flat_list.{h,cpp}`）：代替 `wxListBox` / `wxCheckListBox`。
  - 圆角的输入框底色上一行行文字；任何一行有副文字时每行两行高，副文字灰色在下；右侧可以有灰色备注。
  - 选择和 `wxListBox` 一样：单击、Ctrl / Shift 加单击、方向键、Home/End、翻页，多选时 Ctrl+A；发 `wxEVT_LISTBOX`、双击或回车发 `wxEVT_LISTBOX_DCLICK`。
  - 可勾选的每行前有勾选框（和样式面板同一种画法），点框或按空格勾选（空格对所有选中行），发 `wxEVT_CHECKLISTBOX`；点框不改变选择。
  - 设置类方法（`SetSelection`、`Check` 等）都不发事件。
  - 有副文字的行，鼠标停在上面时提示里是这一行的全文（文字和副文字），副文字太长被截断时可以看全；离开后恢复列表自己的提示。
- **分段开关** `FlatSwitch`（`src/flat_controls.{h,cpp}`）：代替单选按钮和 `wxRadioBox`。一排等宽的段，选中的一段用强调色填充；方向键逐段移动，点击不抢焦点；单段可以停用（变暗、点不动）、带提示；Alt + 段标签的下划线字母选中它。`Choose` 像用户那样选（调 `on_choose`），`SetSelection` 不调。
- 勾选框 `FlatCheck` 的标签和按钮一样可以带 `&`，Alt + 该字母勾选；标签里的 `&` 画的时候去掉（中文的“延后(&W)”画成“延后(W)”）。
- 其余沿用已有的：`FlatFrame`（框内带名称的输入框；名称为空时不画）、`FlatChoice`、`FlatButton`、`flat::SectionTitle`。

## 规则

- 不用静态框；分组用 `SectionTitle`（粗体标题加细线），组少时不加标题。
- 输入框的名称写在框内；四周 8–10 DIP，行距 4 DIP。
- 对话框底色用 `theme::UseSurface`。
- 一行一组选项时，灰色的名称在左、控件在右（两列的 `wxFlexGridSizer`），代替静态框。

## 已改的对话框

- **平移时间**（`dialog_shift_times.cpp`）：时间或帧数用分段开关选，下面只显示对应的一个框（时间框或数字框）；方向、影响范围、时间字段各一个开关。没有视频帧率时“帧”停用，没有选中行时“选中行”“选中行及之后”停用。右侧历史是 `FlatList`，单击一条载入它的平移，“清除”清空；确定后列表立刻加上这一次。
- **查找 / 替换**（`dialog_search_replace.{h,cpp}`）：查找和替换各是一个 `FlatFrame`，右端的箭头弹出最近用过的内容；四个勾选框两列；字段和范围是分段开关。右侧一列按钮，“查找下一个”是强调色（在查找框里回车也是它），最下是“关闭”（Esc）。
- **跳转**（`dialog_jumpto.cpp`）：帧和时间两个框并排，名称在框内，改一个另一个跟着变；底部确定 / 取消。
- **选择行**（`dialog_selection.cpp`）：匹配 / 不匹配、模式、字段、动作都是分段开关（动作的段写短，完整说法在提示里），对话行和注释行两个勾选框至少留一个；底部帮助、应用、取消、确定。
- **覆盖粘贴**（`dialog_paste_over.cpp`）：十一个字段是三列勾选框，按行里的位置排（注释、层、开始、结束 / 样式、说话人、特效、文本 / 三个边距）；下面一排快捷按钮：全部、无、时间、文本。
- **翻译助手**（`dialog_translation.{h,cpp}`）：原文、译文、按键各用小节标题分开，当前行号灰色写在原文标题右侧；按键四列（名称灰色）；“播放音频”“播放视频”放在底部取消按钮之前。
- **时间后处理**（`dialog_timing_processor.cpp`）：左边是可勾选的样式列表和全部 / 无；右边按小节排：只处理选中行、提前 / 延后、相邻行连续、对齐关键帧。每个毫秒数是数字框，后面灰色的 ms，勾上对应的框才可用；相邻行的偏向原来是滑块，现在是 0–100 的数字框（越大越偏向调整前一行的结束时间）。没有关键帧和时间码时关键帧的勾选框停用并说明原因。一个样式都没勾或一项处理都没开时确定不可用。
- **重采样分辨率**（`dialog_resample.cpp`）：源和目标各一行“宽 × 高”数字框加“从字幕 / 从视频”按钮，下一行是 YCbCr 矩阵下拉框（第一项“—”表示不转换颜色）。宽高比处理是分段开关，宽高比不变时停用；边距是四个数字框围着“对称”勾选框，对称时右和下跟着左和上。
- **拼写检查**（`dialog_spellchecker.cpp`）：拼错的词和替换为是两个带名称的框，建议是 `FlatList`（单击填入替换框，双击直接替换），下面是词典语言下拉框；右侧两个勾选框和一列按钮，“替换”是强调色；底部帮助和关闭。
- **字体收集**（`dialog_fonts_collector.cpp`）：动作是下拉框；字幕文件没有所在文件夹时不列出“复制到字幕文件所在文件夹”（下拉框不能单项停用）。目标是带名称的框加“浏览”，下面一行灰色说明；日志在小节标题下；底部“开始！”是强调色，取消改名“关闭”。
- **导出**（`dialog_export.cpp`、`ass_exporter.cpp`、`export_framerate.cpp`）：滤镜是可勾选的 `FlatList`，每个滤镜的说明作为副文字写在名称下（不再有单独的说明框），上移 / 下移连同勾选一起移动；文本编码是下拉框。勾选的滤镜的设置在右侧，标题是小节标题；帧率转换的设置是输入帧率框加“从视频”、输出用“可变 / 固定”开关加帧率框（可变时帧率框停用）、反向转换勾选框。

## 测试

- `ordinary_edit.dialog_buttons_are_drawn_and_answer_the_keys_the_system_buttons_do`：标准 id 和标签、确定按钮是强调色；Esc 按取消，回车在单行输入框里按确定、在多行文本里不按，Ctrl+回车不算；Alt+字母；不可用的按钮不按；标题栏关闭按钮按取消；没有自己的处理时取消关闭对话框；只有关闭时关闭是确认 id。
- `ordinary_edit.number_field_is_typed_stepped_and_dragged_in_range`：输入、保留小数位、超范围不采用、滚轮、拖动从按下时的值起算并夹在范围内、`SetValue` 不算用户修改、格式化。
- `ordinary_edit.flat_list_is_chosen_and_ticked_as_the_system_lists_are`：单击、Shift、Ctrl、勾选框不改变选择、双击、事件顺序、设置不发事件；单选列表忽略 Ctrl / Shift。
- `ordinary_edit.shift_times_is_drawn_switches_and_a_history_list`：没有帧率和选中行时对应的段停用、上次的选择、Alt+字母选段、按帧平移后历史加一条、单击历史载入。
- `ordinary_edit.search_and_replace_is_drawn_fields_checks_and_switches`：两个输入框、四个勾选框、两个开关；Alt+字母勾选；全部替换并保存选择；Esc 关闭。
- `ordinary_edit.select_lines_is_drawn_switches_and_checks`：开关和勾选框载入上次的选择；对话行和注释行至少留一个；Alt+字母选段；确定后选中匹配的行、关闭并保存。
- `ordinary_edit.paste_over_ticks_fields_in_the_columns_of_a_line`：十一个勾选框载入设置、“时间”只勾开始和结束、确定后保存。
- `ordinary_edit.jump_to_names_its_fields_and_jumps`：两个带名称的框、输入帧数时间跟着变、确定后跳到该帧。
- `ordinary_edit.translation_assistant_is_drawn_with_titles_and_foot_buttons`：预览勾选框、底部四个按钮、没有音视频时播放按钮停用、Esc 按取消。
- `ordinary_edit.timing_processor_is_drawn_ticks_and_millisecond_fields`：样式默认全勾、没有关键帧时停用、数字框随勾选框可用、全部 / 无决定确定是否可用、确定后保存。
- `ordinary_edit.resample_is_drawn_number_fields_a_switch_and_even_margins`：宽高比不变时开关停用、改目标宽度后可用、手动时边距可用且对称、取消不改动。
- `ordinary_edit.spell_checker_is_drawn_fields_a_list_and_buttons`：拼错的词、单击建议填入替换框、Esc 关闭（没有词典时跳过）。
- `ordinary_edit.fonts_collector_chooses_an_action_from_a_list`：没有字幕文件夹时不列出对应动作、选择动作保存并启用目标框。
- `ordinary_edit.export_lists_filters_with_their_descriptions`：每个滤镜有说明、勾选后显示它的设置、鼠标停在行上提示全文、移动时勾选跟着走。
