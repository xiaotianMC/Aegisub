# 用 MagiaTimeline 给硬字幕打轴

[MagiaTimeline](https://github.com/HurryPeng/MagiaTimeline) 是一个独立的开源程序（GPLv3），能从视频里烧录的硬字幕中找出每句字幕出现和消失的时间。本软件不包含它的任何代码，只是把它当作外部程序启动，把视频交给它，再把它的结果读回来。所以本软件的许可证不受影响；用户需要自己安装 MagiaTimeline。

MagiaTimeline 的作者希望用它打轴的视频注明“自动打轴：MagiaTimeline”。

## 用法

1. 按它的说明安装：
   - 源码版：装好 Python 3.12，运行它的 `install.bat` / `install.sh`，会在它的文件夹里建一个 `venv` 虚拟环境；
   - 或者解压发布页上的 Windows 打包版。
2. 打开视频，**时间 → 用 MagiaTimeline 给硬字幕打轴…**。第一次会问 MagiaTimeline 装在哪个文件夹，之后记住（**首选项 → 视频 → MagiaTimeline → 安装位置**，可以填文件夹、`MagiaTimeline.py` 或 `MagiaTimeline.exe`）。
3. 先弹出设置对话框（见下），点“运行”开始；取消则什么都不做。
4. 运行时弹出进度窗口，显示它最新输出的一行。可以取消：会结束它以及它启动的所有子进程，字幕不变。
5. 运行完，它找到的每一段在字幕末尾各添加一行：时间就是那一段，样式和当前行相同。勾了“识别文字”时正文是它识别出的文字，否则为空。新加的行都处于选中状态。一步撤销。

不识别文字时，它输出的每一行正文是 `Subtitle_Dialog_3` 这样的编号，不会保留。

## 设置对话框

`src/dialog_magia_timeline.{h,cpp}`。对话框里的选项都来自它自己的 `config.yml`：先读出里面有哪些键（`magia_timeline::Inspect`），版本没有的选项就不显示，免得写进它不认的键。

- **策略**：下拉框列出 `config.yml` 里所有带预设的顶层键（`bcs`、`dtd`、`mr`……），每项后面附一句用途（通用（推荐）、描边文字、魔法纪录剧情关卡……）。1.0 没有 `dtd`，就不会列出。
- **预设**：所选策略下的预设。换策略时，原来的预设这个策略没有，就用它 `config.yml` 里的 `preset`，再没有就用第一个。
- **帧**：分段开关“跳过不变的帧 / 逐帧”（`engine: speculative / framewise`）。只有 `bcs`、`dtd`、`otl`、`prk` 能跳过不变的帧；选了其他策略时这一段停用，自动改成逐帧。逐帧时可以设“每 N 帧”（`framewise/sampleInterval`）。
- **最大尺寸**（`maxResWidth` × `maxResHeight`，1.1 起才有）、**使用显卡**（`enableOpenCL`）。
- **识别文字（OCR）**：把 `ocr` 加进 `extraJobs`。只有 `bcs`、`dtd`、`otl` 能做，且只能在跳过不变的帧时做（它自己的限制），不满足时勾选框停用，旁边灰字说明原因。
  - 有 `ocr/doPaddle` 和 `ocr/doTesseract` 时，用分段开关选 PaddleOCR 或 Tesseract（一个设 true、另一个设 false）。1.0 默认两个都开，显示为 PaddleOCR。
  - 语言框对应所选引擎的语言键：Tesseract 是 `ocr/tesseractLang`，PaddleOCR 是 `ocr/paddleLang`（只有 1.0 有）；没有对应的键时停用。
  - 新版默认把识别结果写进输出的 ASS（`standaloneOutput: false`），勾了识别文字时本软件强制这样设。1.0 把文字写到单独的 `<输出名>-ocr.txt`（`ocr/suffix`），每行 `名称,文字`，本软件按名称把文字填到对应的行上（`magia_timeline::AddReadText`）。
- **底部按钮**：左边“打开 config.yml”（用系统默认程序打开，其余设置都在里面）、“恢复它的设置”（回到 `config.yml` 里的值）；右边取消、运行。

**保存**：对话框里的选择只保存和 `config.yml` 不同的部分（`magia_timeline::Changes`），存在选项 `Tool/MagiaTimeline/Settings` 里，每行 `路径=值`，如 `strategy=bcs`、`bcs/default/dialogRect=[0.1, 0.9, 0.8, 1.0]`。下次打开时从 `config.yml` 的值出发，再叠加保存的选择（`magia_timeline::Effective`）；`config.yml` 里已经没有的键、没有的策略或预设会被忽略。用户的 `config.yml` 本身从不修改。

**路径**：配置里的值用路径表示：顶层键直接写（`strategy`），两格缩进的键写成 `上级/键`（`framewise/sampleInterval`），四格缩进的写成 `策略/预设/键`（`bcs/default/dialogRect`）。一行一项的列表读成 `[a, b]`。

## 怎么调用它

- **找启动方式**：文件夹里有 `MagiaTimeline.py`，并且有它安装脚本建的 `venv`（Windows 下是 `venv\Scripts\python.exe`，其他系统是 `venv/bin/python3`），就用这个 Python 运行 `-u MagiaTimeline.py`。否则有 `MagiaTimeline.exe` 就运行它。再否则用系统里的 `python` / `python3` 运行脚本。它自带的 `MagiaTimeline.bat` / `.sh` 不转发参数，所以不经过它们。
- **配置**：读它文件夹里的 `config.yml`，把 `source` 换成当前视频，把 `destination` 换成临时文件夹（`?temp`）里的 `aegisub-magiatimeline`，再把对话框里和它不同的值替换到对应的行上（`magia_timeline::WriteConfig`）：只替换已有的行，替换时连同它下面的列表项一起换掉，其余行（包括注释）原样保留；`config.yml` 里没有的键不添加，免得旧版本不认。写成 `?temp/aegisub-magiatimeline.yml`，用 `--config` 传给它。
- **路径只写 ASCII**：路径写成 YAML 双引号字符串，中文等非 ASCII 字符写成 `\u89C6` 这样的转义，引号和反斜杠也转义。原因是 1.0 版读配置时不指定编码，用的是系统代码页（中文 Windows 是 GBK）；打包版 exe 又不认 `PYTHONUTF8`，直接写 UTF-8 的中文路径会报 `'gbk' codec can't decode byte ...`。纯 ASCII 在任何代码页下读出来都一样。它 `config.yml` 里其余内容原样保留，所以那部分本来是什么编码就还是什么编码。
- **工作目录**是它的文件夹：它配置里的 `template.asst`、`ConfigSchema.json` 等都是相对路径。
- **结束时的“按回车继续”**：1.0 版每次运行结束都会等回车；1.1 版可以用 `--no-pause` 关掉，但 1.0 不认这个参数。所以不传这个参数，而是一启动就往它的标准输入写一个换行再关闭，两个版本都能正常退出。
- **输出编码**：设置 `PYTHONIOENCODING=utf-8`、`PYTHONUTF8=1`、`PYTHONUNBUFFERED=1`，让它的输出是 UTF-8 且即时可读，不受 Windows 代码页影响。打包版不认这些变量，输出仍是系统代码页，所以读回来的内容如果不是合法的 UTF-8，就按系统代码页转换后再显示。
- **判断失败**：它会捕获自己的异常，打印 `Exception caught` 后照常退出，退出码说明不了问题。所以只要输出里出现 `Exception caught` 或 Python 的 `Traceback`，或者没有写出结果文件，就算失败，并把它最后 15 行输出显示出来。

## 实现

- `src/magia_timeline.{h,cpp}`：找启动方式（`FindLaunch`）、读配置（`Inspect`）、改写配置（`WriteConfig`）、保存的选择（`ParseSaved` / `FormatSaved` / `Effective` / `Changes`）、读结果（`ReadTimeline`、`AddReadText`）、启动进程并在进度窗口里等待（`Run`）。
- `src/dialog_magia_timeline.{h,cpp}`：设置对话框。
- `src/command/tool.cpp`：`tool/magiatimeline` 命令，在“时间”菜单里。
- 选项 `Tool/MagiaTimeline/Path`（首选项里可改）、`Tool/MagiaTimeline/Settings`（对话框里的选择）。

## 验证

`ordinary-edit-tests` 里：

- `magia_timeline.its_config_is_read_for_what_it_offers`：按 1.1 和 1.0 的结构读出策略、预设和各个值（行尾注释、上面的注释、一行一项的列表、没有值的键）；区域的读写；保存的选择叠加到配置上时，旧版本没有的键、策略、预设被忽略；只保存和配置不同的值。
- `magia_timeline.config_is_written_with_what_was_chosen_where_it_is_and_nothing_added`：同名的键只改所选策略下的那个；列表连同旧的项一起替换，后面的行保留；没选的行（带注释）原样；1.0 没有的键不添加。
- `magia_timeline.text_read_by_1_0_is_put_on_the_stretches_of_its_names`：1.0 的 `-ocr.txt` 按名称填到对应的段上。
- `magia_timeline.config_is_given_the_video_and_where_to_write_and_keeps_the_rest`：按 1.1 版 `config.yml` 的结构改写，旧的列表（连同注释掉的项）被替换，其余不变；中文路径、引号、反斜杠和 BMP 以外的字符都写成转义，整个文件只有 ASCII；指定策略和不指定策略。
- `magia_timeline.found_by_its_folder_its_script_or_its_program`：有 `venv`、没有 `venv`、只有打包的 exe、什么都没有、直接给脚本路径。
- `magia_timeline.its_errors_are_read_from_what_it_prints`。
- `ordinary_edit.magiatimeline_adds_an_empty_line_for_each_subtitle_it_finds_in_one_step`：用一个假的 MagiaTimeline（`venv/bin/python3` 是一个 shell 脚本：检查工作目录、读 `--config`、打印输出、等回车、写出 `.ass`）真正启动一个进程。检查它收到的视频和策略；结果按时间排好、正文为空、样式与当前行相同、被选中、一步撤销。它报错时不添加任何行，并显示它说的话。
- `ordinary_edit.magiatimeline_cancelled_stops_it_and_what_it_started_and_adds_nothing`：取消时它和它启动的子进程都被结束，字幕不变。去掉“连子进程一起结束”时这个测试会失败。
- `ordinary_edit.magiatimeline_is_run_as_chosen_in_its_dialog_and_the_choice_kept`：对话框列出配置里的策略并选中它的默认；选专用策略时只能逐帧、不能识别文字；换策略后预设跟着换；选 `bcs`、跳过不变的帧、勾识别文字后运行，假程序收到的配置里是这些值，并且 `standaloneOutput: false`；识别出的文字填进行里，编号不保留；只保存和配置不同的选择。下次打开时显示上次的选择，“恢复它的设置”回到配置里的值；取消时不运行、不改字幕、不改保存的选择。换成 1.0 的配置：没有 `dtd` 和最大尺寸，保存的 `dtd` 退回它自己的 `bcs`；两个 OCR 引擎可选，改成 Tesseract 和语言后写进配置；文字从 `-ocr.txt` 按名称读回。

还没有做的：

- 没有用真正的 MagiaTimeline 跑过，只用了上面那个按它的源码模拟的假程序。
- Windows 打包版 exe 的文件名、它是不是接受 `--config` 的命令行版，都还没有确认（发布包里的 exe 名字没有查到；1.1 版另有一个 `MagiaTimeline-GUI.py` 图形界面）。
