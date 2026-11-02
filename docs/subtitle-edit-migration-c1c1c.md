# C1-C1c 活动工程 Owner 与生命周期收口

状态：实施中，尚未完成 C1-C1c，不具备据此开放 Apply Engine 的条件。

## 基线、安全检查与依赖顺序

2026-11-02，以实际 HEAD 3610a4530dbd94ccdec064a6ce6b16dde55c6a91（C1-C1b）为基线，分支 codex/style-name-width。重新检查所有未提交、暂存和未跟踪文件；用户原有修改仅差异清单、checkasm.wrap、未跟踪 c1c1 调查报告。三者保护，不提交、覆盖、stash、reset 或 clean。实时远端与 HEAD 一致。账号 xiaotianMC，fork push=true、upstream push=false；fork 开放 PR=0；上游全部开放 PR 已核查，不合入。上游 PR 首次查询超时，重试成功。

保护文件 SHA256：

- 本版与原版差异清单.md：5CE9B8077D0C83938BF5A4F9E8944586A0879D66049C44C0E62B840CE109127B
- subprojects/checkasm.wrap：CE07862593607282739E118A1F94306E79AAFB0D9100C8C51C6517804017FA39
- docs/subtitle-edit-migration-c1c1.md：41FD538A097521C3251427CAAABAD46E3D4FBE14AB17C19DB15A26E72B167FE5

必读历史文档中的失效边界按当前源码复核；前期资料表格保留各自交付时状态，当前 Selection 已完成 b 收口。用户任务中的 defb72b 与实际历史 d0fb72b 有拼写差异，不据此修改历史。

调整顺序：先独立保存快照、Properties/后台输入、可信历史与模型安装、Lua/Load/Close，最后私有化 Owner。直接先 private Context::ass 会使剩余调用者不可构建，或被迫引入禁止的 mutable 兼容接口；本阶段不引入这种接口。每个完整功能族独立构建、测试、提交、普通推送并核对远端。

## 第一批：保存快照与自动保存结果生命周期（C1-C1c-5 的先行依赖）

实际调用链：SubsController::Save 原先先修改当前文件名/路径/脏标记，再对 context->ass 执行 CleanExtradata 并直接写出；AutoSave 复制 AssFile 但原 AssFile 深复制不复制 Properties，worker 完成后主线程 lambda 借用裸 FrameMain*。Load/Close 换文档、析构排队完成结果没有文档归属凭据。

本批修改：

- SubtitleProjectAccess::Snapshot 在宿主线程/存活检查后取得完整独立副本，复制 Properties 并保留稳定行 ID，不复制 live gate 关联；返回可变对象是独立副本，不是活动模型。
- 手动保存先计算目标路径相对媒体的 Properties 副本，清理副本和写出；输出成功后再更新当前路径和保存状态。输出失败时内存模型、路径、历史、脏状态不变；FileSave 通知异常亦恢复原路径/Properties/保存标记。
- AutoSave 启动前取得完整快照并清理，仅向后台传独立副本。完成时回到主线程，先检查文档/控制器存活凭据，再使用控制器当前窗口；Load、Close 换凭据，析构在等待 worker 前撤销凭据，旧结果不会解引用已销毁控制器。失败结果恢复该版本的自动保存标记以允许后续重试。
- 原有定时器和公开 RequestAutoSave 共用生产入口，测试直接执行相同后台流程，未添加返回当前 mutable 模型的接口。
- CleanExtradata 在 Extradata 为空时仍清除保存副本的悬空行引用，当前内存工程不被清理，不新增 Undo 或字幕修订。独立 AssFile 原有 mutable 能力继续保留。

实际修改文件（另含本报告）：

- `src/ass_file.cpp`
- `src/project.cpp`
- `src/project.h`
- `src/subs_controller.cpp`
- `src/subs_controller.h`
- `src/subtitle_project_access.cpp`
- `src/subtitle_project_access.h`
- `tests/tests/ordinary_edit.cpp`
- `tests/tests/subtitle_project_access.cpp`

验证：Windows x64 Release 主程序及全部测试目标构建成功；应用107/107、模型46/46、基础436/436，合计589/589。新增四项：完整独立快照/ID/Properties/关闭后拒绝/空附加数据引用清理；真实 Save 的 UTF-8/注释/样式/绑定/时间精度及内存无清理；真实保存失败的路径/历史/脏状态保持；实际 AutoSave 队列保存输入后编辑和控制器销毁，文件重开含初始内容/Properties/绑定且旧完成回调安全。其余585项回归全部保留。

日志：build/c1c1c-save-build.log、c1c1c-save-app.log、c1c1c-save-model.log、c1c1c-save-base.log。测试均用 run_isolated.py，不运行旧 setup，不写用户配置/夹具。测试首轮发现断言误以为 ASS 能无损保存1ms、现有读取器能保留行尾空格；已按现有 ASS 厘秒舍入与读取行为修正兼容性断言，并分别验证原始保存字节保留尾空格、当前内存1001/2345ms不变、重开1000/2350ms。没有修改格式或解析规则。

已有局限/待后续批次：Context::ass 仍公开 mutable；历史/Load/Close 模型事务与 Lua 尚未封闭；Properties 尚无独立 Metadata 修订/脏状态；自动保存目前仍按旧字幕 commit_id 判定变化，Metadata-only触发纳入下一批；字体收集尚持有旧输入，需要独立化。此批不声称已完成 c-5 全部边界或整个 C1-C1c。

未执行 Linux/macOS CI或人工实体键鼠/真实设备/HiDPI验收；不存在新增移动端实现。人工与自动验证分别记录。最终 Owner、历史、Lua、Load/Close、Properties、编译期负例、端到端关闭重开及 CI 结果随后续批次补齐。

提交名称：fix(compare): isolate save snapshots and autosave results。普通推送与远端哈希由实际提交后的核对结果记录，不使用强制推送。
