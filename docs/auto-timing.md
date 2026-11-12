# 自动打轴

顶层菜单“自动打轴(&M)”放在“时间”和“视频”之间，按工作流排：

| 菜单项 | 命令 | 说明 |
| --- | --- | --- |
| 导入语音识别结果... | `autotiming/import_speech` | 见下 |
| MagiaTimeline 硬字幕打轴... | `tool/magiatimeline` | 从“时间”菜单移过来 |
| 按画面切换生成时间轴 | `keyframe/timeline` | 见[画面切换检测](scene-detection.md) |
| 检测画面切换... | `keyframe/detect` | “视频”菜单里也有 |
| 吸附到附近的画面切换 | `time/snap/scene_nearby` | “时间”菜单和字幕表格右键菜单里也有 |
| 生成双语轴 | `autotiming/bilingual` | 见下 |
| 清除自动打轴标记 | `autotiming/clear_marks` | 见下 |

对齐选中行、检查时间轴等以后做好再加入。整体方案见“日文语音识别自动打轴方案”文档。

## 导入语音识别结果

读入 WhisperX 输出的 JSON（`--output_format json`），faster-whisper 同样形状的 JSON 也认（置信度字段叫 `probability`）。用到的是每段的时间、文字、说话人，和每个词的时间、置信度（`score`）、说话人；对齐器没给时间的词（数字、符号）按前后的词补上。`\uXXXX` 转义的文字照常读（项目的 JSON 读取器原来不支持，这次补上了）。

WhisperX 的建议用法（带说话人）：

```
whisperx ep01.wav --language ja --model large-v3 --diarize --hf_token <令牌> --output_format json
```

**断句**（一行一句，宁长勿碎）：

1. 相邻两段之间：上一段没有句末标点（。？！…），停顿不超过 300 毫秒，说话人相同，合起来不超过最长时长，就接成一句。
2. 段内：句末标点、超过 800 毫秒的停顿、换说话人，都开始新的一句。
3. 超过最长时长（默认 7 秒）的句子拆开，每块至少 1.5 秒。拆点打分：从句结束（“、”最好，“けど/から/ので/のに”次之，“て/で”再次）、词间停顿越长越好、越靠中间越好；不拆在“ない/です/ます/だ/よ/ね/か/の”和标点之前，日文按 ICU 分词不拆在词中间。
4. 跨过画面切换、两边都至少 300 毫秒的行，切换点前后 200 毫秒内有至少 100 毫秒的停顿就在那里拆开。
5. 拆开的几行在 Effect 栏写同一个 `#组号`。

**时间**（顺序很重要）：

1. 加前后余量（时间后处理器的设置），余量范围内有切换点时停在切换点；不越过前一行和后一行（本来就重叠的除外）。
2. 不足最短时长（默认 1 秒）的向后延长，不越过下一行和切换点。
3. 相邻两行间隔不超过时间后处理器的“相邻间隔”时连起来；间隔里有切换点时，两行都对到切换点。
4. 一头离切换点不到 300 毫秒（在行内）时，这一头对到切换点。
5. 已有画面切换时，最后用“吸附到附近的画面切换”的四个方向距离对齐到帧。

**标记**：置信度低于阈值（默认 0.5）的词写成 `?词`，连续的几个合成一个；说话人（如 `SPEAKER_00`）写进 Actor 栏；整行是括号标注（“（笑い）”）或只有 ♪、〜 的行改成注释行，不混进对白。

**放到哪里**：字幕还没写东西时替换掉原来的空行，否则按时间插在原有行之间，原有行不动。首选项勾了“每行配一行空的译文行”时，直接按双语轴生成（`JP` 行加空 `CN` 行，样式缺的会补上）。新样式和所有行是一步撤销，新行全部选中。

设置在首选项“音频”页的“语音识别”一节：是否生成双语轴、超过多长拆句、每行至少显示多久、置信度阈值。其他阈值目前是代码里的默认值（`speech::Rules`），等拿真实样片调过再决定要不要开放。

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
- `src/speech_lines.{h,cpp}`：读入识别 JSON（`ReadJson`）、断句和修整时间（`MakeLines`），不依赖界面，可单独测试；`speech::Import` 在 `command/autotiming.cpp` 里，把结果放进字幕。
- `libaegisub/common/cajun/reader.cpp`：支持 `\uXXXX` 转义和代理对。
- `src/command/autotiming.cpp`：生成双语轴、清除标记。新样式先用 `SubtitleValueEdit` 提交，行用 `SubtitleRowListEdit` 以 amend 合进同一步（`SubtitleRowListEdit::Commit` 加了 `amend_id` 参数）。
- `src/dialog_translation.cpp`：`LoadActive` 同时取原文行和配对行，`SkipLine` 跳过译文行，`TargetBlocks` 取配对行里与原文标签一致的各段。

## 测试

- `speech_lines.*`：读入（转义、缺时间的词、置信度、说话人、faster-whisper 格式、错误文件）；跨段接句、句末和换人断开、长停顿断开；长句在从句处拆开并同组；不确定的词、注释行；余量、最短时长、相邻行相接、切换点处相接、切换点吸走行尾；在切换点的停顿处拆开。
- `ordinary_edit.speech_recognition_imported_a_sentence_a_line_in_one_step`：双语轴导入、说话人和标记、余量、连同样式一步撤销；已有字幕时按时间插入。
- `lagi_cajun.ReadUnicodeEscapes`
- `bilingual_marks.read_and_written_in_the_effect_field_beside_what_else_it_holds`
- `ordinary_edit.make_bilingual_pairs_each_line_with_an_empty_translation_line_in_one_step`：配对、样式、一步撤销（连同样式），翻译助手写进译文行、跳过译文行、回来时带出已有译文。
- `ordinary_edit.clear_auto_timing_marks_leaves_the_rest_of_the_effect_field`
- `ordinary_edit.timeline_from_scene_changes_gives_the_shots_with_no_line_one_and_keeps_the_rest`
- `ordinary_edit.one_scene_snap_distance_set_before_goes_out_and_a_little_way_in`
