# 原生样式名称显示修复（Aegisub 3.5.1 / Windows x64）

这是基于官方 `v3.5.1` 的独立测试版。运行便携目录中的 `aegisub.exe`，直接使用原有样式下拉框，无需加载样式选择插件。

- 展开列表按当前字体下的最长样式名称加宽，最多占用所在屏幕的工作区宽度。
- 收起时悬停于样式框，提示显示当前行的完整样式名称。
- 切换字幕行、修改样式及撤销时更新提示。
- 区分仅大小写不同的样式名称，例如 `Main` 和 `main`。
- 3.5.1 已使用 UTF-8 转换自定义 Automation 按钮名称；保留该上游修复。

此改动没有新增保存设置。样式应用仍沿用 Aegisub 原有的批量应用、撤销和 `.ass` 保存机制。便携版配置存放于便携目录，原有安装及其配置保持独立。此版本针对 Windows 桌面；没有移动端版本。

## 手动复查

1. 用测试版打开 `tests/data/style_names.ass`。
2. 展开原生样式框，确认列表比收起时宽，长英文名称和中文名称可以辨认。
3. 选择第二行字幕并悬停样式框，确认完整中文名称正确；依次选择第三、四行，确认 `Main` 和 `main` 没有混淆。
4. 选中多行、更换样式，执行撤销，再另存为一个新文件并重新打开，确认所选行的样式引用、文本和时间保留。
5. 调整窗口宽度，并在其他屏幕/缩放设置上打开列表，确认弹出宽度重新计算。极长名称仍受屏幕宽度限制，可选择后通过提示查看。
6. 如需独立检查中文按钮，通过自动化管理器加载 `tests/data/style_name_unicode_check.lua`，运行 `StyleNameFix Unicode check`，确认五个中文按钮无乱码。

## 构建

使用官方 README 所述的 Visual Studio、Python、Meson、CMake、Ninja 工具链，按官方 Windows CI 的静态依赖参数构建。原生变更位于 `src/subs_edit_box.cpp` 和 `src/subs_edit_box.h`。运行现有 Aegisub 测试：`meson test -C build --suite Aegisub --print-errorlogs`。
