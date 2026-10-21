# 原生生成器结构与强一致性维护方案

本文说明 `NativeMapGenerator` 的代码组织方式和维护门禁。首要目标不是减少代码，而是在改善可定位性的同时，保证原生地图最终产物不发生变化。

## 1. 一致性标准

原生生成器依赖单一 RNG 调用顺序、x87 数值行为、浮点落地时机、容器遍历顺序和堆 tie-break。有限参数样本只能提供差分证据，不能证明全部 32 位 seed 和全部参数组合等价。

因此，结构整理采用两层保证：

1. 结构保证：重组前后展开得到的 `native_generator.cpp` 源码完全相同。
2. 运行门禁：使用原版 Oracle 样本比较完整 `.map` 字节，防止构建配置、资源或工具链发生偏移。

第一层约束所有输入的算法代码没有变化；第二层验证真实编译和运行产物。

## 2. 为什么保留单一编译单元

`rmg` 使用 `set_optimize("fastest")`，Windows 和 Linux x86_64 还分别编译原生 x87 helper。将数值函数移动到不同 translation unit 可能改变：

- 编译器内联判断和函数边界。
- 浮点值保存在寄存器还是内存的时机。
- 匿名命名空间 helper 的可见性。
- 静态对象初始化和全局诊断状态顺序。
- 优化器观察到的调用图。

因此不采用 `native_terrain.cpp`、`native_resources.cpp` 等多 translation unit 方案。`src/native_generator.cpp` 仍然是唯一编译入口，具体实现按原始顺序包含 `.inc` 分片。

## 3. 当前代码布局

结构整理前：

- `native_generator.cpp` 约 24,900 行。
- `NativeMapGenerator` 有 102 个成员方法。
- `generate()` 约 870 行。
- `native_generator.h` 约 830 行。
- 生成、诊断和地图导出接驳集中在一个物理文件。

结构整理后，`native_generator.cpp` 只保留头文件、`namespace rmg` 和 14 个顺序 include：

| 分片 | 职责 |
| --- | --- |
| `helpers.inc` | 常量、原生数值 helper、内部类型和诊断辅助函数 |
| `stage_report.inc` | 阶段采集和阶段报告序列化 |
| `workspace.inc` | mapgen scratch 生命周期和区域工作区投影 |
| `pipeline.inc` | `generate()` 与 21 个阶段编排 |
| `terrain_flood.inc` | 通用分区和 flood |
| `terrain_special.inc` | `MapType=3/4` cliff、plateau、ore chain |
| `terrain_ordinary.inc` | `MapType=0/1/2` 普通地形 |
| `terrain_post.inc` | terrain post-pass、岸边绿地和池塘 |
| `regions.inc` | region、bridge、cliff 和 level/zone 处理 |
| `starts_height.inc` | 出生点、exclusion 和高度噪声 |
| `objects_resources.inc` | objectives、科技建筑和 Tiberium |
| `height_surface.inc` | 坡面、surface、LAT 和 grass |
| `decorations.inc` | 非严格原生装饰实现 |
| `trees.inc` | tree patch 和树木布置 |

这些 `.inc` 文件不是独立编译单元，不允许自行添加 include 或 namespace，也不允许嵌套包含其他实现分片。

## 4. 展开源码门禁

`scripts/verify-native-generator-layout.ps1` 会：

1. 读取 `native_generator.cpp`。
2. 按 include 顺序展开 `native_generator_detail/*.inc`。
3. 统一换行符为 LF。
4. 对完整展开源码计算 SHA-256。
5. 与冻结基线比较。

当前基线为：

```text
97D8E4A173503D95305B3C32A65258D6D839189268606108810568F5ACEC6A23
```

这是 native-only 公开发布版的冻结基线，不等同于包含其他策略分支的内部源码哈希。首次建仓时已经删除这些分支；后续仍应使用原版 Oracle 复核默认 native 路径的最终 `.map` 字节。

运行方式：

```powershell
pwsh -NoProfile -File .\src\RandomMapGen\scripts\verify-native-generator-layout.ps1
```

该门禁由 CI 调用。只要函数体、声明顺序、常量、注释或空白发生变化，展开哈希都会失败。

算法修复确实需要修改展开源码时，不能直接更新哈希。必须先完成 Oracle 验证、解释首次差异和修复证据，再在同一个受审查变更中更新基线。

## 5. Oracle 产物门禁

结构改动优先使用最终产物快速门禁：

```powershell
.\src\RandomMapGen\scripts\verify.ps1 `
  -NativeOriginFinalProductOnly `
  -NativeOriginArtifactDir $artifacts `
  -NativeOriginGameResourceDir C:\path\to\game `
  -NativeOriginPrebuiltRmg C:\path\to\rmg.exe `
  -NativeOriginCellParityOutPrefix build\native-structure-matrix\current
```

`-NativeOriginPrebuiltRmg` 或 `RMG_NATIVE_ORIGIN_PREBUILT_RMG` 让矩阵复用已经编译的可执行文件。没有提供时，脚本使用 `xmake build -y -j 8 rmg` 构建一次，然后复用同一产物。

每个样本检查：

- 输入 manifest 与 `spawnmap.ini` 一致。
- `ScenarioClass::UniqueID` 入口状态正确。
- 最终 `.map` 长度和 SHA-256 相同。
- 哈希不同后报告首个字节差异。

2026-08-21 的结构整理门禁使用 13 个 Oracle 样本，覆盖 `MapType=0..4`、3 至 8 人、零水量、零资源、雪地、富矿、高水量和高崎岖度，全部最终 `.map` 逐字节一致。

最终产物不一致时才进入完整诊断：

1. 比较 21 个阶段及 RNG 状态。
2. 定位第一个不同的阶段、cell 和字段。
3. 阶段边界仍不足时启用 RFT1 函数轨迹。

不要在成功矩阵中默认采集完整阶段和 RFT1，避免把验证时间消耗在无差异数据上。

## 6. 每次结构改动的顺序

每次只处理一个连续职责，按以下顺序验收：

1. 展开源码 SHA-256 完全一致。
2. `git diff --check` 通过。
3. Windows x64 release 编译 `rmg`。
4. WSL/Linux x86_64 release 编译 `rmg`。
5. 固定种子重复生成结果一致。
6. 13 个 Oracle 最终 `.map` 逐字节一致。
7. 运行 `game-native` 选项 golden 和同种子重复生成回归。

任何一步失败都停止后续整理。不得在失败的结构改动上继续叠加修复。

## 7. 允许的整理

满足展开源码哈希不变时，可以：

- 将连续的完整函数块移动到现有或新的 `.inc` 分片。
- 调整 `native_generator.cpp` 中分片位置，但展开顺序必须保持不变。
- 更新职责文档和函数索引。
- 改进验证脚本，但验证代码不能进入生成 RNG 路径。

移动分片时必须保留：

- 函数和匿名 helper 的相对顺序。
- 原有注释、空白和浮点表达式。
- 静态变量和全局变量定义顺序。
- namespace 边界。

## 8. 禁止混入结构整理的改动

以下改动会改变展开源码或编译器观察到的程序结构，不属于强一致性结构整理：

- 拆成多个 `.cpp` translation unit。
- 聚合 `NativeMapgenWorkspace` 或改变成员布局。
- 将 `NativeGenerationReport` 改为嵌套报告结构。
- 引入 PImpl。
- 把 `captureStage` 的字符串接口改成枚举接口。
- 把全局 theater/trace 状态改成实例成员。
- 统一预解析所有环境变量。
- 抽取公共 flood/heap 模板。
- 替换 `unordered_map`、`unordered_set` 或改变 reserve/遍历顺序。
- 改名、格式化、合并浮点表达式或调整临时变量。
- 调整 x87 helper、LUT 或函数内联边界。

这些需求若确实有价值，必须作为单独的行为变更处理。有限 Oracle 样本可以降低风险，但不能将它们表述为对全部参数空间的严格证明。

## 9. 环境变量分类

当前 `native_generator` 中约有 53 处环境变量读取。后续文档化时必须区分：

- 纯观测：stage dump、CSV dump、RFT1 trace 等，不应改变 RNG 或地图。
- 诊断索引：选择记录某个 cell/event，原则上不改变生成。
- 行为覆盖：Type2 compatibility profile、region skip/fix/level 等，会主动改变算法路径。

行为覆盖不能放入名为 `NativeDiagnosticOptions` 的统一对象，也不能默认启用。第一轮结构整理保持所有读取位置和时机不变。

## 10. 后续边界

原生兼容代码作为冻结的 conformance core 维护。新功能优先放在其外部：

```text
输入解析 / game-native 选项派生
  -> 冻结的 NativeMapGenerator conformance core
  -> map_writer
```

公开仓库只包含原生生成链路。新的地图策略、平衡算法或玩法生成器应作为独立项目维护，不得重新接入 `NativeMapGenerator` 的 RNG 和原生阶段顺序。
