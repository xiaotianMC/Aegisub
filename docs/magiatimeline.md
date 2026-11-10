# 用 MagiaTimeline 给硬字幕打轴

[MagiaTimeline](https://github.com/HurryPeng/MagiaTimeline) 是一个独立的开源程序（GPLv3），能从视频里烧录的硬字幕中找出每句字幕出现和消失的时间。本软件不包含它的任何代码，只是把它当作外部程序启动，把视频交给它，再把它的结果读回来。所以本软件的许可证不受影响；用户需要自己安装 MagiaTimeline。

MagiaTimeline 的作者希望用它打轴的视频注明“自动打轴：MagiaTimeline”。

## 用法

1. 按它的说明安装：
   - 源码版：装好 Python 3.12，运行它的 `install.bat` / `install.sh`，会在它的文件夹里建一个 `venv` 虚拟环境；
   - 或者解压发布页上的 Windows 打包版。
2. 打开视频，**时间 → 用 MagiaTimeline 给硬字幕打轴…**。第一次会问 MagiaTimeline 装在哪个文件夹，之后记住（**首选项 → 视频 → MagiaTimeline → 安装位置**，可以填文件夹、`MagiaTimeline.py` 或 `MagiaTimeline.exe`）。
3. 运行时弹出进度窗口，显示它最新输出的一行。可以取消：会结束它以及它启动的所有子进程，字幕不变。
4. 运行完，它找到的每一段在字幕末尾各添加一行：时间就是那一段，正文为空，样式和当前行相同。新加的行都处于选中状态。一步撤销。

MagiaTimeline 只给出时间，不识别文字：它输出的每一行正文是 `Subtitle_Dialog_3` 这样的编号，不会保留。

## 设置

- **策略**（首选项 → 视频 → MagiaTimeline）：留空就用它自己 `config.yml` 里写的 `strategy`；也可以填 `bcs`、`dtd`（1.1 版的默认）等它支持的策略名。
- 其余所有参数，包括字幕区域 `dialogRect`、各策略的阈值，都直接读它文件夹里的 `config.yml`，要调整就改那个文件。本软件只替换其中的 `source`、`destination`，以及填了策略时的 `strategy`。

## 怎么调用它

- **找启动方式**：文件夹里有 `MagiaTimeline.py`，并且有它安装脚本建的 `venv`（Windows 下是 `venv\Scripts\python.exe`，其他系统是 `venv/bin/python3`），就用这个 Python 运行 `-u MagiaTimeline.py`。否则有 `MagiaTimeline.exe` 就运行它。再否则用系统里的 `python` / `python3` 运行脚本。它自带的 `MagiaTimeline.bat` / `.sh` 不转发参数，所以不经过它们。
- **配置**：读它文件夹里的 `config.yml`，把 `source` 换成当前视频，把 `destination` 换成临时文件夹（`?temp`）里的 `aegisub-magiatimeline`，写成 `?temp/aegisub-magiatimeline.yml`，用 `--config` 传给它。路径用 YAML 单引号字符串，路径里有单引号也不会出错。
- **工作目录**是它的文件夹：它配置里的 `template.asst`、`ConfigSchema.json` 等都是相对路径。
- **结束时的“按回车继续”**：1.0 版每次运行结束都会等回车；1.1 版可以用 `--no-pause` 关掉，但 1.0 不认这个参数。所以不传这个参数，而是一启动就往它的标准输入写一个换行再关闭，两个版本都能正常退出。
- **输出编码**：设置 `PYTHONIOENCODING=utf-8`、`PYTHONUTF8=1`、`PYTHONUNBUFFERED=1`，让它的输出是 UTF-8 且即时可读，不受 Windows 代码页影响。
- **判断失败**：它会捕获自己的异常，打印 `Exception caught` 后照常退出，退出码说明不了问题。所以只要输出里出现 `Exception caught` 或 Python 的 `Traceback`，或者没有写出结果文件，就算失败，并把它最后 15 行输出显示出来。

## 实现

- `src/magia_timeline.{h,cpp}`：找启动方式（`FindLaunch`）、改写配置（`WriteConfig`）、读结果（`ReadTimeline`）、启动进程并在进度窗口里等待（`Run`）。
- `src/command/tool.cpp`：`tool/magiatimeline` 命令，在“时间”菜单里。
- 选项 `Tool/MagiaTimeline/Path`、`Tool/MagiaTimeline/Strategy`。

## 验证

`ordinary-edit-tests` 里：

- `magia_timeline.config_is_given_the_video_and_where_to_write_and_keeps_the_rest`：按 1.1 版 `config.yml` 的结构改写，旧的列表（连同注释掉的项）被替换，其余不变；路径里的单引号；指定策略和不指定策略。
- `magia_timeline.found_by_its_folder_its_script_or_its_program`：有 `venv`、没有 `venv`、只有打包的 exe、什么都没有、直接给脚本路径。
- `magia_timeline.its_errors_are_read_from_what_it_prints`。
- `ordinary_edit.magiatimeline_adds_an_empty_line_for_each_subtitle_it_finds_in_one_step`：用一个假的 MagiaTimeline（`venv/bin/python3` 是一个 shell 脚本：检查工作目录、读 `--config`、打印输出、等回车、写出 `.ass`）真正启动一个进程。检查它收到的视频和策略；结果按时间排好、正文为空、样式与当前行相同、被选中、一步撤销。它报错时不添加任何行，并显示它说的话。
- `ordinary_edit.magiatimeline_cancelled_stops_it_and_what_it_started_and_adds_nothing`：取消时它和它启动的子进程都被结束，字幕不变。去掉“连子进程一起结束”时这个测试会失败。

还没有做的：

- 没有用真正的 MagiaTimeline 跑过，只用了上面那个按它的源码模拟的假程序。
- Windows 打包版 exe 的文件名、它是不是接受 `--config` 的命令行版，都还没有确认（发布包里的 exe 名字没有查到；1.1 版另有一个 `MagiaTimeline-GUI.py` 图形界面）。
