# C1-C1c-8 Windows x64 Release 复核报告

日期：2026-11-03。结论：**本轮 Windows 验收通过，626/626，无失败、禁用或跳过；主程序入口、链接、真实启动及正常退出通过。** 没有推送，没有进入 C1-C1c-9、Lua 隔离或 Owner 私有化。

## 1. 交付版本与工作区

本地最初停在 `29ac415b5072747ca58ccbc70dee0e1a43901812`。用户确认云端交付到位后，只取回 fork 指定分支并执行快进，保留原历史，不做 rebase/reset/stash/clean。GitHub 开放 PR 已在接入前检查：fork 为 0；上游开放 PR 已查询，不合入。

接入并保留的 C1-C1c-8 三个提交：

| SHA | 提交名称 |
| --- | --- |
| `3891846` | `build(tests): build application-model tests on non-Windows hosts` |
| `b9622f5` | `fix(compare): route subtitle close through host installation and separate post-commit work` |
| `ac30607` | `docs(compare): report C1-C1c-8 load/close lifecycle migration` |

三者已由云端出现在 fork 的 `codex/style-name-width`，接入时实时远端为 `ac306076b09cc655928409c89b073cab08ec6a41`。本轮没有执行任何 push，也没有同步或改动上游。相对 `29ac415`，这三个提交共 10 文件、544 行新增、66 行删除。

原有工作区内容保持原样：已修改的 `本版与原版差异清单.md`、未跟踪的 `subprojects/checkasm.wrap` 和 `docs/subtitle-edit-migration-c1c1.md`。开始时暂存区为空，没有其他非忽略改动。

本轮只补充一段现有应用回归中的 MRU 写入失败检查及本报告：不改生产代码，不改旧提交，不新增测试数量，不修改信号语义、Lua、Owner、第三方依赖或用户保护文件。此测试与报告作为单独本地提交保存，未获推送确认前不推送。最终提交 SHA 及远端核对在交付消息给出，避免报告自引用。

GitHub CLI 当前未登录，不能验证当前账号或 `permissions.push`；没有借用旧报告或提交署名推断权限。公开 API 和 Git 远端读取成功，未据此决定直接推送或创建 fork。

## 2. Windows 完整构建

- 原生 Windows x86_64，VS x64 工具链，MSVC 19.51.36260、LINK 14.51.36260，Meson 1.12.1、Ninja。
- 配置实际核对：`buildtype=release`、`tests=true`、`b_ndebug=if-release`、静态依赖。
- 成功构建并链接：`aegisub.exe`、`ordinary-edit-tests.exe`、`tests/subtitle-compare-tests.exe`、`tests/gtest-main.exe`，以及辅助目标 `tests/luajit-52.exe`、`automation/tests/aegisub-lua.exe`。
- 配置重生成触发了依赖重编译，最终成功构建日志末尾为 `[1441/1441] Linking target ordinary-edit-tests.exe`，进程退出码 0。

首次配置缺少 sed 路径；一次路径设置覆盖 VS 工具路径；随后 MSYS sed 遇到受限执行环境的命名对象权限错误；允许该工具正常运行后又发现 NASM 未在 PATH。最终只在忽略目录中的构建启动脚本补齐现有 MSYS/NASM 路径、加载 VS x64 环境，并使用允许 MSYS 正常运行的执行环境解决。没有通过改产品代码或第三方依赖绕过错误。失败日志全部保留，不能将它们说成产品测试失败。

复现脚本：`build/c1c1c8-windows-build.cmd`。最终日志：`build/c1c1c8-windows-build-v5.log`；前四轮日志为 `c1c1c8-windows-build.log`、`-v2.log`、`-v3.log`、`-v4.log`。

## 3. 626 项实跑结果

全部使用 `tests/run_isolated.py` 复制临时夹具、独立 TEMP/TMP 和输出目录；未运行旧 setup，未过滤测试，未使用 Linux 排除列表，未改写源码夹具或日常用户配置。三份 XML 的 failures/errors/disabled 均为 0；全部 testcase 为实际运行，无 skipped。

| 程序 | 实跑/通过 | 日志与 XML 前缀 |
| --- | --- | --- |
| ordinary-edit-tests.exe | **135/135** | `build/c1c1c8-windows-app` |
| subtitle-compare-tests.exe | **55/55** | `build/c1c1c8-windows-model` |
| gtest-main.exe | **436/436** | `build/c1c1c8-windows-base` |
| 合计 | **626/626** | 各前缀分别有 `.log` 与 `.xml` |

应用套件明细：ordinary_edit 44、audio_edit 24、visual_edit 38、selection_ui 6、model_installation 12、document_lifecycle 11，全部通过。Linux 未运行的 29 项视觉、5 项音频拖动、3 项选择 UI 和 1 项样式导入测试均包含在本次无过滤 Windows 实跑中，**38 项现已实际通过**。

额外执行现有 `luajit-52.exe`，退出码 0，不计入 626。没有开始 Lua 隔离或新增该阶段功能。Windows 未注册 POSIX-only 的 busted 自动化套件，不将其宣称为本轮通过项。

## 4. 重点行为

- Load/Close：11 项 document_lifecycle 与 12 项 model_installation 全部通过。涵盖比较/写授权期间拒绝、完整旧状态保持、新空白文档、重复打开关闭、路径/Metadata/选择/历史、保存重开、一次性通知及关联业务错误分类。
- 文档身份：Close 后 document epoch/generation 推进；旧修订令牌、旧任务及排队 AutoSave 结果不能写回新文档。
- RecoveryBlocked：旧节点继续存活，写/保存/新安装封闭；失败重试不运行外围业务，明确确认后成功恢复，外围业务只执行一次，再次 Retry 被拒绝。
- 观察者：真实网格、编辑框、音频/视觉缓存及样式/翻译对话框重绑定；重试不产生重复修订、Undo 或 FileOpen/MRU 副作用。
- 外围错误：原有备份目录失败、FileOpen 监听者抛异常及 Project 警告分类回归通过。本轮在 `load_follow_up_failures_are_reported_after_commit_and_do_not_suppress_later_steps` 中补充真实 MRU 保存路径被普通文件阻断的注入：仅记录 Recent files 错误，状态仍 Committed、未封闭，后续备份文件实际生成、FileOpen 继续执行且路径正确。没有仅用 mock 异常代替持久化错误。
- Undo/Redo：普通编辑、Load/Close 后编辑及恢复后编辑的实际历史恢复全部通过。

本轮没有发现需要修复的 C1-C1c-8 生产代码失败。新增的仅是上述 19 行测试覆盖。

## 5. main_entry.cpp 专项及真实桌面冒烟

核对仓库 wx 宏定义：原 `wxIMPLEMENT_APP` 包含 theme、应用工厂及平台入口；拆分保留 theme 与 `wxIMPLEMENT_APP_NO_MAIN` 于 main.cpp，把 `wxIMPLEMENT_WXWIN_MAIN` 放入 main_entry.cpp。Windows 宏实际定义 WinMain。

实物核对：

- dumpbin 显示主程序和应用测试均为 x64；主程序 subsystem=Windows GUI，测试 subsystem=Windows CUI，两者均有非零入口地址。
- `src_main_entry.cpp.obj` 定义 WinMain，`src_main.cpp.obj` 定义 wxCreateApp 和 wxTheAppInitializer；主程序及控制台测试实际链接、执行成功，无重复入口或未解析入口错误。
- 证据：`build/c1c1c8-windows-pe-headers.log`、`build/c1c1c8-windows-entry-symbols.log`。

真实桌面操作使用 Computer Use，临时目录 `build/c1c1c8-windows-smoke` 放置可移植 config、自建 ASS 与主程序逐字节副本。副本使用独有进程名 `c1c1c8-entry-smoke.exe`，避免桌面工具把同名程序匹配到已安装旧版。副本与原 exe 的 SHA256 相同：`F7BB33E4C3F354B1FB39F28F599D1B320372AF10464CAC136429FD114F13F55D`。

执行并观察：

1. 无参数启动，出现 Aegisub Native 启动页；选择 Subtitle editor 后进入完整空白 FrameMain，标题含 `ac30607`，未出现 RecoveryBlocked 或外围错误对话框；通过界面正常关闭，进程正常退出（首个监测未取得数值退出码）。
2. 命令行传入自建 smoke.ass，完整 FrameMain 显示正确文件名及 1s–3s 时间字段；保存按钮禁用表示干净状态，未出现错误模态框；隔离 mru.json 含正确路径，autoback/smoke.ORIGINAL.ass 已实际生成。
3. 通过界面正常关闭该命令行启动实例；监测记录 NormalExit=true、ExitCode=0。隔离 crashdumps 目录为空。

证据：该目录的 `commandline-accessibility.txt`、`dashboard-exit.json`、`commandline-exit.json`、`mru.json`、`autoback/` 和 `log/`。这是自动操作真实桌面的启动冒烟，不冒充人工全功能/真实媒体设备/HiDPI 验收。

工具异常记录：桌面启动器最初误启动已安装旧版；关闭旧版的操作被自动审批拒绝，原因是可能影响用户会话。本轮没有重试该操作。旧版及最初隐藏实例不计入成功证据；随后仅针对路径和独有进程名已核实的自建实例完成上述验收。

## 6. 保护文件和交付边界

开始、接入后及测试后 SHA256 均与原记录一致，提交前再次核对：

| 保护文件 | SHA256 |
| --- | --- |
| 本版与原版差异清单.md | `5CE9B8077D0C83938BF5A4F9E8944586A0879D66049C44C0E62B840CE109127B` |
| subprojects/checkasm.wrap | `CE07862593607282739E118A1F94306E79AAFB0D9100C8C51C6517804017FA39` |
| docs/subtitle-edit-migration-c1c1.md | `41FD538A097521C3251427CAAABAD46E3D4FBE14AB17C19DB15A26E72B167FE5` |

持久化回归已实跑覆盖原生桌面的保存、关闭、重开、Metadata/MRU/备份和 AutoSave；本轮没有新增持久化产品功能或移动端实现，不虚构移动端验收。

`git diff --check` 通过。仅将测试覆盖与本报告纳入本轮本地提交，不暂存三个用户保护文件。旧三提交保持不变。推送必须等待用户明确确认；Linux 结果沿用已收到的独立交付，macOS 不在本轮通过范围。
