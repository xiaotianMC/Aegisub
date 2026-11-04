# C1-C1c-14 审计剩余项（B7、C3、C5、C7、C8、C9）

本阶段处理 `docs/subtitle-edit-c1c-final-audit.md` 中剩下的全部项目：B7、C3、C5、C7、C8、C9。完成后，审计 A、B、C 三组的项目都已处理。没有实现 Apply Engine 或比较界面。没有修改 `signal.h`、Undo/Redo 历史格式、保存格式或 Lua API（脚本可见的函数名和返回值形状不变）。没有新增公开可变 getter、`const_cast`、宽泛 friend 或全局锁。

## 1. 修复概览

| 项 | 修复 | 位置 |
|---|---|---|
| B7 | 宏和导出过滤器运行前，在宿主线程上捕获脚本可能查询的全部宿主状态，作为运行快照：文件名、路径令牌、项目属性、时间码、关键帧、视频尺寸与宽高比、音频选区。工作线程上的辅助函数（`file_name`、`project_properties`、`decode_path`、`frame_from_ms`、`ms_from_frame`、`video_size`、`keyframes`、`get_audio_selection`）只读快照。没有快照时，只在宿主线程上读取活动状态（脚本加载、validate、isactive、配置对话框）；在其他线程上返回 nil。导出过滤器通过 `LoadSettings` 记录导出所用的上下文（导出器在宿主线程上紧接着调用 `ProcessSubs`）。`set_status_text` 在宿主线程上才读取窗口指针 | `auto4_lua.cpp` |
| C3 | 普通行写入（`UpdateRows`、`ReplaceRows`，以及不含 `COMMIT_EXTRADATA` 的 `UpdateValues`）中，新增或改变的 extradata 引用必须指向已有条目，否则在第一次写入之前拒绝。行已有的引用可以保留，因为载入的文件里可能本来就有未解析的引用。粘贴时，剪贴板行里的 `{=id}` 只保留能在当前文档中解析的（同一文档内复制粘贴），其余丢弃。**行样式有意不校验**：ASS 允许引用不存在的样式（渲染时退回 Default），载入和粘贴都依赖这一点 | `subtitle_project_access.cpp`、`command/edit.cpp` |
| C5 | 新模型发布之后，观察者注册表不再冻结。冻结原本是为了防止新订阅者读到旧模型、随后又错过替换通知；模型发布后这种情况不会再发生。重试投递仍使用准备好的观察者列表，新订阅者不在其中（它们本来读的就是新模型）。因此恢复受阻期间可以打开样式管理器、样式助手、翻译助手，也可以切换视觉工具 | `subtitle_model_observers.h`（`Prepared::EndSubscriptionFreeze`）、`subs_controller.cpp` |
| C7 | 编辑活动行的命令在没有活动行时不做任何事：`edit/line/reverse_move`（含 Validate）、`subtitle/bind`、`subtitle/unbind`、`edit/revert`、`edit/clear`、`edit/clear/text`、`edit/insert_original`。另外：拼写检查替换；视频“下一个/上一个边界”在换行后的空检查；`VideoController::PlayLine` 不再在音频播放之后再次解引用活动行 | `command/edit.cpp`、`command/video.cpp`、`dialog_spellchecker.cpp`、`video_controller.cpp` |
| C8 | 自动保存基线增加纪元号，每次排队自动保存和手动保存都会推进它。自动保存失败时，只有纪元号未变（期间没有其他保存移动过基线）才回滚基线。手动保存失败时恢复纪元号 | `subs_controller.{h,cpp}` |
| C9 | libass 消息回调改为在互斥量下检查“日志已启用”标志。新增 `libass::StopLogging()`，在日志接收器销毁之前调用（`AegisubApp::OnExit`、测试主函数）。不等待字体缓存工作线程完成，因为首次建立字体索引可能需要几分钟，不能因此阻塞退出 | `subtitles_provider_libass.{h,cpp}`、`main.cpp`、`tests/support/ordinary_main.cpp` |

## 2. 语义变化（有意）

- **B7**：导出过滤器现在可以正常调用 `aegisub.project_properties()` 等函数（以前会因线程检查而报错）。返回值是运行开始时的值，运行期间宿主侧的变化不可见，与宏一致。
- **C3**：从其他文档复制的行，粘贴时会丢弃无法解析的 extradata 引用。能解析的引用照常保留，所以同一文档内复制粘贴的行为不变。
- **C5**：恢复受阻期间可以新增订阅。
- **C8**：手动保存之后，失败的自动保存不再引发一次多余的自动保存。

## 3. 测试

| 测试 | 覆盖 | 旧代码（C1-C1c-13） |
|---|---|---|
| `lua_macro.isolation_export_filter_host_helpers_read_a_host_snapshot` | 导出过滤器中 `project_properties`、`file_name`、`decode_path`、`keyframes`、`video_size`、`frame_from_ms` 返回快照值；活动文档不变 | **失败**（Lua 错误：线程检查） |
| `subtitle_project_access.row_writes_reject_new_unresolved_extradata_references` | 保留已有的未解析引用可以；新的未解析引用在写入前被拒绝（同一批中的其他行也不写入）；`ReplaceRows` 插入带未解析引用的行被拒绝；能解析的引用被接受 | **失败** |
| `ordinary_edit.pasted_lines_drop_extradata_references_that_do_not_resolve` | 粘贴的行只保留当前文档中存在的引用 | **失败** |
| `document_lifecycle.subscriptions_are_allowed_while_recovery_is_blocked` | 恢复受阻期间新增提交/选择监听者、构造样式助手都不抛出，读到的是新模型；重试投递不包含它们；恢复后正常接收提交 | **失败** |
| `ordinary_edit.active_line_commands_do_nothing_without_an_active_line` | 没有活动行时，上述命令和字体、颜色、粗体命令都不改变模型和历史 | **崩溃**（退出码 245） |
| `document_lifecycle.failed_autosave_does_not_undo_a_later_manual_save_baseline` | 自动保存失败前已有手动保存，之后的自动保存请求不再写文件 | **失败** |
| C9（无专门测试） | 以前间歇崩溃的 `lua_macro.isolation_validate_parse_karaoke_does_not_write_live_extradata` 连续运行 10 次 | 去掉测试主函数中的 `StopLogging()` 后，10 次中 1 次退出崩溃（245）；加上后 10 次全部正常 |

“旧代码”一列的做法：把 `src/` 和测试主函数换回 C1-C1c-13 提交的版本，保留新增测试，构建后逐项运行；随后恢复修改。

### 验证结果（Linux，GCC 15.2，wxGTK 3.2）

- `gtest-main`：434/434。
- `subtitle-compare-tests`：61/61（新增 1 项）。
- `ordinary-edit-tests`：逐项隔离运行，共 184 项，146 通过、38 失败。失败集合与 C1-C1c-11、13 **完全相同**（37 项 CaptureMouse，1 项 GTK 文件选择框）。新增的 6 项全部通过。
- `luajit-52`、`live model owner access`：通过。
- ASan：新增的 5 项 ordinary 测试无 AddressSanitizer 错误，泄漏报告的分配点中没有模型或自动化对象；`subtitle-compare-tests` 61/61，零错误零泄漏。

## 4. 审计状态

| 组 | 状态 |
|---|---|
| A1–A4 | C1-C1c-11 已关闭 |
| B1–B5 | C1-C1c-11 已关闭 |
| B6 | C1-C1c-13 已关闭 |
| B7 | 本阶段关闭 |
| C1 | C1-C1c-12 已关闭 |
| C2、C4、C6 | C1-C1c-13 已关闭 |
| C3 | 本阶段关闭（extradata 引用）。样式按设计不校验 |
| C5、C7、C8、C9 | 本阶段关闭。C7 只覆盖审计列出的命令类调用点；其余 `ReadActiveLine()` 调用点都已逐一检查，均已有空检查 |

## 5. 未处理项与风险

- Windows/MSVC 仍未构建（C1-C1c-11 至 14 均未验证）。本阶段新增的 `std::mutex` 静态对象和 `wxIsMainThread` 在 MSVC 上都有，但需要实际确认。
- C9 的修复只保证 libass 回调不再进入已销毁的日志接收器。字体缓存线程本身仍可能在进程退出时还在 fontconfig 中运行，这与上游行为相同。
- 遗留的比较界面设计输入：颜色选择器、样式助手、音频编辑会话打开期间都持有 `Interaction`，会阻止开始比较（失败关闭）。

## 6. 保护文件

`本版与原版差异清单.md` 开始和结束时的 SHA256 均为 `6a922d69925ba61d4511fc29a712a3291e0034abdc2a39c8539b672bd0cf35d2`。
