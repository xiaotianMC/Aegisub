# 自动打轴

顶层菜单“自动打轴(&M)”放在“时间”和“视频”之间，按工作流排：

| 菜单项 | 命令 | 说明 |
| --- | --- | --- |
| MagiaTimeline 硬字幕打轴... | `tool/magiatimeline` | 从“时间”菜单移过来 |
| 按画面切换生成时间轴 | `keyframe/timeline` | 见[画面切换检测](scene-detection.md) |
| 检测画面切换... | `keyframe/detect` | “视频”菜单里也有 |
| 吸附到附近的画面切换 | `time/snap/scene_nearby` | “时间”菜单和字幕表格右键菜单里也有 |
| 生成双语轴 | `autotiming/bilingual` | 见下 |
| 清除自动打轴标记 | `autotiming/clear_marks` | 见下 |

语音识别、对齐选中行、检查时间轴等以后做好再加入。整体方案见“日文语音识别自动打轴方案”文档。

## 双语轴

每句原文一行（样式 `JP`），同一时间配一行译文（样式 `CN`），原文保留在文件里，译者和校对对照着看，也能直接出双语字幕。两个样式名暂定，只在 `src/bilingual.h` 一处定义。

- **生成双语轴**：选中的行（注释行和 `CN` 行跳过）改用 `JP` 样式，各在下面插一行同一时间的空 `CN` 行，图层、边距、说话人跟原行一样，Effect 栏留空（标记只在原文行上）。文件里没有 `JP` / `CN` 样式时，先按当前行的样式各复制一份——长什么样留给用户设。已经配好对的行不再加。新样式和新行是一步撤销；没什么可做时不留撤销记录。
- **翻译助手**：当前行是 `JP` 样式、并且有同一时间的 `CN` 行时，译文写进 `CN` 行，原文不动；下一行、上一行跳过 `CN` 行；回到译过的行时，译文框里是已有的译文。原文有多段（中间夹着标签）时，`CN` 行和原文的标签一致就保留它已译的其他段，不一致就用原文填上其他段。不是双语的文件照旧用译文覆盖原文。

“配对”的规则：同一开始、同一结束时间、`CN` 样式、不是注释的第一行。

## Effect 栏的标记

自动打轴在原文行的 Effect 栏里留给译者的记号，用空格分开：

| 记号 | 意思 |
| --- | --- |
| `#组号` | 被拆开的连续行，同组的要连起来翻 |
| `?词` | 没把握的词，可以有多个 |

例如 `#12 ?マミ ?ソウルジェム`。ASS 按逗号分栏，所以词里的逗号写成全角“，”，空格去掉。栏里原有的其他内容保留在前面。以 `Banner;`、`Scroll up;`、`Scroll down;` 开头的值是渲染器的效果，从不当作标记读。

**清除自动打轴标记**：去掉所有行的 `#组号` 和 `?词`，其他内容不动，一步撤销；没有标记时不留撤销记录。翻译校对完、跑会用 Effect 栏的 Lua 脚本之前用。

兼容性（对照代码核对过）：渲染器只认上面三种开头，记号不冲突；卡拉 OK 模板器只处理 Effect 为空或含 `karaoke` 的行，带记号的行会被跳过（歌曲行不打记号）；横幅/滚动效果写入时替换整栏，记号会丢；视频预览在 Effect 非空时改时间会多重画一次；导出 SRT 等格式不带 Effect 栏。字幕表格的 Effect 列、“选择行”和“查找替换”的 Effect 字段都能直接用来找带 `?` 的行。

## 代码

- `src/bilingual.{h,cpp}`：样式名、配对、标记的读写和清除。
- `src/command/autotiming.cpp`：生成双语轴、清除标记。新样式先用 `SubtitleValueEdit` 提交，行用 `SubtitleRowListEdit` 以 amend 合进同一步（`SubtitleRowListEdit::Commit` 加了 `amend_id` 参数）。
- `src/dialog_translation.cpp`：`LoadActive` 同时取原文行和配对行，`SkipLine` 跳过译文行，`TargetBlocks` 取配对行里与原文标签一致的各段。

## 测试

- `bilingual_marks.read_and_written_in_the_effect_field_beside_what_else_it_holds`
- `ordinary_edit.make_bilingual_pairs_each_line_with_an_empty_translation_line_in_one_step`：配对、样式、一步撤销（连同样式），翻译助手写进译文行、跳过译文行、回来时带出已有译文。
- `ordinary_edit.clear_auto_timing_marks_leaves_the_rest_of_the_effect_field`
- `ordinary_edit.timeline_from_scene_changes_gives_the_shots_with_no_line_one_and_keeps_the_rest`
- `ordinary_edit.one_scene_snap_distance_set_before_goes_out_and_a_little_way_in`
