# 波形图与视频工具栏

## 可视化工具栏的位置

视频左侧那一列可视化工具（标准、拖动、旋转、缩放、裁剪、形状……以及下面的子工具栏）移到了视频**右侧**，也就是视频和右边波形图之间。单独弹出的视频窗口同样在右侧。按钮、顺序和功能都没有变（`src/video_box.cpp`）。

这一列是单独的内容块（见[外观](theme.md)），左右到视频块和音频块的空隙相同，所以在两者正中间（`VideoBox::SetToolbarGap`）。

## 新的波形配色 “Studio”

默认的波形配色改为 **Studio**：

- 黑色背景，波形用薄荷绿。
- **未选中的区域**只有一种较浅的绿色，没有深浅层次。
- **选中的区域**（当前行和其他选中行的时间范围）用鲜艳的绿色，并分两层：外层是峰值，内层是平均值，颜色更浅。
- 中心线用波形本身的颜色，音频结束之后也是。

这种只在选中区域分深浅的画法由配色方案的 `Averages In Selection Only` 决定。Studio 打开了这一项，所以首选项里的“波形样式”（最大值 / 最大值 + 平均值）对它不起作用。原来的 **Green**、**Icy Blue** 仍然可以在“首选项 → 界面 → 颜色 → 音频配色方案”里选，它们的画法和以前完全一样。

已有的配置文件保存了旧的配色列表，所以第一次启动时会自动加入 Studio；如果波形配色还是原来默认的 Green，会改成 Studio。之后再选回 Green 会保留（`MigrateAudioColourSchemes`，`src/audio_colorscheme.cpp`）。

## dB 刻度

- 在 0、−3、−6、−9、−12、−18、−24 … dB 的位置画深绿色横线（类似 Audition），上下对称；左侧标出数值，数值下面垫一块黑底，盖在波形上也看得清。数值居中对齐在自己的线上，靠边时收进显示范围；相邻两条线至少隔一个数值的高度再加 4 像素（按实际字体和显示缩放计算，`AudioDisplay::DbLabelGap`），放不下的不画，所以数值不会重叠。
- 波形高度仍是**线性**的：−6 dB 在 0 dB 一半的高度，所以越往中心刻度越密。相邻两条线至少相隔 16 像素（字体更高时按字体），放不下的刻度自动省略；纵向放大后 0 dB 超出显示范围，就从能显示的那一级开始。
- 开关：“首选项 → 音频 → 显示 → 在波形上显示 dB 刻度”（`Audio/Display/Draw/dB Scale`，默认打开）。频谱模式下不显示。
- 只做了单声道（与以前一样把多声道混成一条），没有多声道显示。

实现：刻度位置 `AudioWaveformRenderer::DbGrid`，刻度线画在波形位图里（`src/audio_renderer_waveform.cpp`），数值在 `AudioDisplay::PaintDbScale` 里叠加（`src/audio_display.cpp`）。

## 秒线

波形里每秒一条的竖线（“首选项 → 音频 → 显示 → 秒边界”）在 Studio 配色下画成和 dB 刻度线一样的深绿色实线，横竖网格一致。以下情况仍用“首选项 → 界面 → 颜色 → 音频显示 → 秒边界”的颜色（默认蓝色虚线）：

- 用 Green、Icy Blue 等其他配色时；
- 频谱模式下；
- 你在首选项里改过秒边界颜色时（改过的颜色总是优先）。

实现：`SecondsMarkerProvider`（`src/audio_marker.cpp`）。

## 水平缩放

波形右边原来的竖直“水平缩放”滑块去掉了，缩放改到波形下方的横向滚动条上，用法和 Premiere 的缩放滚动条一样：

- **拖滑块的左边缘或右边缘**：另一边缘不动，看到的时间范围跟着鼠标变长或变短。滑块两端画有两道竖纹，鼠标移上去变成左右箭头。缩放是连续的，不再限于固定档位；最多放大到原来最高一档（+30），最多缩小到最低一档。
- **拖滑块中间**：滚动，按住的位置保持在鼠标下（原来是让滑块中心跳到鼠标处）；点滑块外面仍是让滑块中心跳过去。
- **Alt + 滚轮**：鼠标在波形上时，无论“滚轮默认缩放”是否打开都按档缩放，以鼠标所在时间为中心。原来的 Ctrl + 滚轮照旧。

缩放结果按最接近的档位存入 `Audio/Zoom/Horizontal`，下次打开从这一档开始。滑块太窄时（不到 18 像素）不画竖纹，两端仍可拖动，中间留一段用来滚动。

实现：`AudioDisplayScrollbar`（拖动区分）、`AudioDisplay::ZoomToTimeRange`、`NearestZoomLevel`（`src/audio_display.cpp`），Alt + 滚轮在 `AudioBox::OnMouseWheel`（`src/audio_box.cpp`）。

## 时间标尺

- 顶部时间标尺：Studio 下是深灰底、灰色刻度和秒数（类似 Audition），波形下方的滚动条也是灰色；布局没变。旧配置里还是第一版 Studio 绿色标尺的，启动时改成灰色，自己改过的颜色不动。

## 测试

- `audio_edit.studio_waveform_shades_only_selected_ranges_and_draws_a_db_scale`：dB 刻度位置（0 dB 在边缘、−6 dB 在一半高度、间距、放大后省略 0 dB、太矮时不画）；Studio 未选中区域只有一种亮度、选中区域有峰值和平均值两种亮度；Green 仍按波形样式设置；秒线在 Studio 下用刻度线颜色，其他配色、频谱模式和自定义颜色时用原设置；旧配置迁移只执行一次。
- `ordinary_edit.visual_tools_sit_to_the_right_of_the_video`：主窗口和弹出窗口里，两列工具栏都在视频右边。
- `audio_edit.visual_tools_are_centred_between_the_video_and_the_waveform`：按主窗口的排列，工具栏左右两边到视频和波形的距离相等。
- `audio_edit.scrollbar_thumb_edges_zoom_and_alt_wheel_zooms`：音频框只剩纵向缩放和音量两个滑块；拖滑块中间只滚动不缩放；拖右边缘时左边缘不动、拖左边缘时右边缘不动；越过另一边时停在最大放大，向外拖停在最大缩小；缩放级别存入选项；Alt + 滚轮在两种滚轮设置下都缩放。
- 离屏截图检查过一段起伏的测试音频：未选中浅绿、选中鲜绿两层、刻度线和数值。
