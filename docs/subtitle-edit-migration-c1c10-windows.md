# C1-C1c-10 Windows x64 Release 复核

日期：2026-11-03。交付版本：`4b461b2329080c0e48f9a18b07a61f58c7b60317`。结论：主程序及项目测试目标构建成功，**655/655 完整回归通过**，Owner 编译/API 检查及真实桌面宏、历史、保存、关闭重开、正常退出均通过。修复了 Owner 检查脚本的两处 Windows 兼容问题，没有修改生产代码。

## 1. 版本与同步

接入前 HEAD 为 `678d985`，用户原有工作区内容仅差异清单修改、未跟踪 c1c1 调查文档与 checkasm.wrap。实时核对账号 xiaotianMC，上游 push=false、现有 fork push=true。fork 开放 PR=0；上游公开 API 首次限流，使用现有凭据只读重试成功，未合入 PR 或同步上游。

从现有 fork 指定分支取回并快进接入四个提交，旧历史全部保留：

- `ef9b66b` — `docs(compare): investigate C1-C1c-10 owner access and plan privatization`
- `0cab1df` — `refactor(compare): make the live subtitle model private to its owner`
- `3ebeabd` — `test(compare): guard live model owner access at compile time`
- `4b461b2` — `docs(compare): report C1-C1c-10 owner privatization`

相对原 HEAD 共 21 文件、1266 行新增、533 行删除。未 rebase/reset/stash/clean，未修改保护文件，未推送。本次检查脚本修复及本报告作为一个本地功能提交保存，最终 SHA 在交付消息给出，不在报告自引用。

## 2. 构建与完整测试

原生 Windows x86_64，VS x64/MSVC 工具链，Meson release。复用已验证的 `build/c1c1c8-windows-build.cmd` 环境，构建并链接 aegisub.exe、ordinary-edit-tests.exe、subtitle-compare-tests.exe、gtest-main.exe，以及 luajit-52.exe、aegisub-lua.exe 辅助目标成功，退出码 0。日志：`build/c1c10-windows-build.log`。

所有 gtest 通过 run_isolated.py 使用独立夹具、临时目录与配置，无过滤。XML 中 failures/errors/disabled 均为 0，所有用例实际运行，没有跳过。

| 程序 | 实跑/通过 |
| --- | --- |
| ordinary-edit-tests.exe | **163/163** |
| subtitle-compare-tests.exe | **56/56** |
| gtest-main.exe | **436/436** |
| 合计 | **655/655** |

证据：`build/c1c10-windows-{app,model,base}.log` 与对应 `.xml`。包括新增两个 Owner 运行时用例、全部编译期 concept/static_assert、27 项 Lua 回归及 Linux 未运行的 38 项。既有 Load/Close、RecoveryBlocked、观察者重绑定、MRU/备份/FileOpen 外围故障、独立快照及 Undo/Redo 全部保留并通过。

## 3. Owner 检查脚本的 Windows 问题与修复

首轮实际运行 `tests/owner_access_check.py` 发现：

1. MSVC 输出含不能按 Python 本地编码严格解码的字节，subprocess 读取线程抛 UnicodeDecodeError，随后拼接输出失败。曾尝试 replacement 字符，但本地控制台又无法编码该字符；最终使用 `errors="backslashreplace"` 保留无法解码字节的转义形式。检查仍只以编译器退出码判定，未忽略编译错误。
2. 正例暴露真实参数错误：脚本去掉 `/Yu`、`/Fp`，却保留 `/FIagi_pre.h`；该头名由生产 PCH 提供，作为普通文件无法找到，MSVC 报 C1083。现在保留配套 `/FI`、`/Yu`、`/Fp`，继续剔除生成目标/PCH 的参数，以相同生产 PCH 做 `/Zs` 检查。

只修改此脚本，不改变 Owner 可见性、friend、只读接口、生产代码或负例要求。原失败日志均保留：`build/c1c10-windows-owner-check.log`、`-v2.log`、`-v3.log`。

修复后，允许路径的对照正例编译成功，**19 个负例全部被 MSVC 拒绝，API 扫描通过**。独立运行日志：`build/c1c10-windows-owner-check-v4.log`。随后通过仓库正式 Meson 注册再次执行 `live model owner access` 与 `luajit-52`，**2/2 通过**；日志 `build/c1c10-windows-registered-checks.log`，完整捕获见 `build/meson-logs/testlog.txt`。这些检查独立列出，不重复计入 655 项 gtest。

## 4. 已知退出竞态专项

交付文档记录 Linux 上 `lua_macro.isolation_comparison_session_rejects_the_whole_result_without_partial_write` 可能在断言通过后因 libass 字体缓存后台日志与退出竞态崩溃。本次除完整回归之外，针对该用例单独启动五个隔离进程，**5/5 断言通过、退出码均为 0**，结果记录于 `build/c1c10-windows-exit-race-results.json`。

这仅表示本次 Windows 未复现，不证明既有竞态不存在或已修复。本轮没有修改 libass、日志或后台队列生命周期。

## 5. 真实桌面冒烟

使用 Computer Use 操作 `build/c1c10-windows-smoke/c1c10-entry-smoke.exe`；独有进程名避免与用户安装的旧版混淆。它与 build/aegisub.exe 字节一致，SHA256 为 `87BAD5C3C02AF01A4D43C52F824898ADFEA5708065D520E01C0BF7442060362D`。

配置、自带 Automation 副本和 ASS 文件均在同一隔离目录。临时 hotkey.json 为原自带 Strip tags 宏设置 Ctrl-Alt-9；未修改宏代码或绕过生产 LuaCommand。

实际执行并核对：

- 正常命令行启动完整 FrameMain，标题显示 4b461b2，正确打开 smoke.ass，Save 禁用且无错误模态框。
- 原文本 `{\b1}C1-C1c-10 Owner smoke`；运行宏并 Ctrl-S 保存后读取磁盘，标签确已移除（after-strip.ass）。
- Ctrl-Z、Ctrl-S 后读取磁盘，原标签恢复（after-undo.ass）；Ctrl-Y、Ctrl-S 后标签再次移除（after-redo.ass）。首次宏结果与 Redo 文件 SHA256 同为 `812136AC68BE5B3C77F4272C2140A59B755A81B723ABCDF49B1BC2A7A64DBA44`。
- 保存完成后标题无星号、Save 禁用；通过界面 Alt-F4 退出，**NormalExit=true、ExitCode=0**（first-exit.json）。
- 再次以同一二进制打开保存结果，窗口正常且干净，MRU 含正确路径，磁盘内容保持宏最终结果；通过界面退出，**NormalExit=true、ExitCode=0**（reopen-exit.json）。未强杀这两个验收实例，隔离 crashdumps 目录为空。

证据均在 `build/c1c10-windows-smoke/`，另有 after-redo-accessibility.txt、reopen-accessibility.txt、mru.json 和应用日志。自动回归还覆盖独立 ASS 保存重开、Metadata 与 AutoSave；本轮未新增持久化产品功能或移动端实现。

未执行真实媒体设备/HiDPI 全功能人工验收、任意第三方宏、故意崩溃后的恢复保存；不把正常保存重开说成故障崩溃恢复。未实现 Apply Engine、比较窗口或进入其他后续开发阶段。

## 6. 保护与 Git 收尾

保护文件接入前、测试后均核对一致：

| 文件 | SHA256 |
| --- | --- |
| 本版与原版差异清单.md | `5CE9B8077D0C83938BF5A4F9E8944586A0879D66049C44C0E62B840CE109127B` |
| subprojects/checkasm.wrap | `CE07862593607282739E118A1F94306E79AAFB0D9100C8C51C6517804017FA39` |
| docs/subtitle-edit-migration-c1c1.md | `41FD538A097521C3251427CAAABAD46E3D4FBE14AB17C19DB15A26E72B167FE5` |

git diff --check 通过。只将 Owner 检查脚本修复与本报告纳入本地提交，三个用户文件不暂存、不提交。生产二进制在 4b461b2 上构建，后续本地提交仅改检查脚本和文档，不改变已验收的生产程序。未推送，等待用户确认。
