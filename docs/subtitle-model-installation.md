# C1-C1c-7 宿主模型安装与恢复交付记录

状态：实施中，尚未接入生产Load，不宣称两个原始阻断已修复。先完成正式设计（subtitle-model-installation-design.md），再修改生产组件；本阶段结束前不进入c-8、Lua完整迁移或Owner私有化。

## 安全基线

2026-11-02，基线d114561f481f8b42223997c14d41535be08621f9，分支codex/style-name-width，远端一致。xiaotianMC对fork push=true/upstream=false，fork PR=0，上游开放PR已审阅，不合入。保留原有三个用户修改/未跟踪文件。

保护SHA256：

- 本版与原版差异清单.md：5CE9B8077D0C83938BF5A4F9E8944586A0879D66049C44C0E62B840CE109127B
- subprojects/checkasm.wrap：CE07862593607282739E118A1F94306E79AAFB0D9100C8C51C6517804017FA39
- docs/subtitle-edit-migration-c1c1.md：41FD538A097521C3251427CAAABAD46E3D4FBE14AB17C19DB15A26E72B167FE5

## 原始链及C0能力

Load独立解析后先swap活动模型、修改路径/Metadata和清Undo，再Commit。Commit先失效代数/PushState，再AnnounceCommit，普通Signal抛异常会停止后续回调。两项先前失败诊断将转成正式生产回归，不能以本批组件测试替代。

C0 SubtitleRebindBatch已提供每观察者Ticket、显式Acknowledge、局部异常隔离、Pending/Failure、retired持有、未完成批次析构后门禁持续封闭。正常返回不是确认；完整批次Complete才释放retired。本阶段复用这些能力，不改signal.h。

## 第一子批：候选、观察者准备及内部文档身份

该族先完成安装协议的可独立验证组件，完整宿主状态机和生产Load接入随后实施。

实际文件：

- docs/subtitle-model-installation-design.md：先于生产代码写入的正式状态机、权限、准备/提交点、确认/失败/恢复设计。
- src/subtitle_model_candidate.h：独立深复制、保留ID和Properties、默认行/样式、ID/Extradata引用完整性校验、目标Active/绑定选择、取消与单次消费准备；仅const读取，不移动源mutable节点到live。
- src/subtitle_model_observers.h：项目本地登记快照、准备函数、冻结订阅、原普通逆序派发及异常语义、逐观察者C0 Dispatch。每个活跃回调必须显式确认，legacy正常返回仍Pending；已完成注销按withdrawn确认。无全局锁，无模型可变回调。
- libaegisub/include/libaegisub/subtitle_edit_gate.h、common/subtitle_edit_gate.cpp：现有document字段仍标识工程State，新增document_epoch区分宿主内新文档；Generation继续单调；WriteLease/Token校验epoch。预留/推进文档修订为内部会话能力，不向普通业务开放安装。
- libaegisub/include/libaegisub/subtitle_rebind.h、common/subtitle_rebind.cpp：可预留新文档修订，计数器先检查；仅friend安装器可在Publishing推进并更新批次令牌。保留所有原C0行为。Ticket默认值代表普通通知，不具有安装权限。
- tests/tests/subtitle_project_access.cpp：新增7项候选和C0登记组件回归及不可公开推进文档/候选只读类型约束。

验证：Windows x64 Release主程序和测试目标成功；应用111/111、模型55/55、基础436/436，共602/602，保留原595项。新增覆盖候选独立性/默认/取消、重复ID/缺失extra拒绝、绑定和稳定ID、普通顺序及异常、逐个显式确认/异常后仍派发/旧节点保留/重试、正常返回不确认/注销安全退出、准备失败不推进修订和可重新订阅。首次两个模型测试错误调用依赖Options的LoadDefault而模型宿主没有用户配置，已改独立行夹具，保留原校验断言并完整复验。

日志：build/c1c1c7-prepare-build.log、c1c1c7-prepare-app.log、c1c1c7-prepare-model.log、c1c1c7-prepare-base.log。均使用run_isolated.py，不运行旧setup，不修改用户配置和源码夹具。diff检查通过；三个保护文件哈希不变。

提交名称：refactor(compare): prepare model installation observers and candidates。提交/普通推送后核对实际远端哈希。

## 尚未完成

宿主Installation Capability及完整状态机、生产Load提交点、必要生产观察者显式确认、两个诊断的正式通过、RecoveryBlocked的Save/重入/关闭/重试保护、端到端安装保存重开仍待后续批次。暂不报告安全安装已完成。Linux/macOS CI和人工操作仅按实际执行结果在最终补齐。
