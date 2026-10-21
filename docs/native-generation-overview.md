# 原生随机地图生成流程

本文是 `RandomMapGen` 原生模式的维护入口，描述当前已经通过逆向和原版 Oracle 验证的稳定行为。它回答“输入如何变成最终 `.map`”以及“哪些实现细节不能随意调整”。

IDA 地址、函数逐项证据、函数覆盖清单和调用树不作为公开仓库内容分发。本仓库只保留维护生成逻辑所需的高层流程和一致性约束。

## 1. 模式与输入

### 1.1 `native` 与 `game-native`

- `native`：直接接收完整的 `MapSeedOptions`，执行原生生成流程。
- `game-native`：接收游戏随机地图对话框中的可见选项，先复刻原版选项随机化，再调用同一套 `native` 生成流程。

`game-native` 的可见输入为：

| 参数 | 范围 | 含义 |
| --- | --- | --- |
| `terrainType` | `1..4` | 游戏界面中的地形类型 |
| `timeOfDay` | `0..3` | 时间 |
| `climate` | `0..1` | 气候 |
| `mapSize` | `0..3` | 地图尺寸 |
| `resources` | `0..3` | 资源量 |
| `players` | `2..8` | 玩家数 |
| `randomizationSeed` | 32 位整数 | 选项随机化种子 |

`deriveNativeGameOptions()` 按原版顺序设置隐藏选项：

1. `WaterAmount`
2. `Ruggedness`
3. `UrbanPresence`
4. `Accessibility`
5. `RegionSize`
6. `Tiberium`
7. `TiberiumLayout`
8. `Vegetation`
9. 最终 16 位地图生成 `Seed`

其中 `Tiberium = max(1, Resources * 20)`，不消耗随机数；固定上下限的 range 同样不消耗随机数。实际 RNG 抽取顺序为 `WaterAmount`、`Ruggedness`、`UrbanPresence`、`Accessibility`、`RegionSize`、`TiberiumLayout`、`Vegetation`，最后抽取 `Seed`。

`randomizationSeed` 只驱动上述选项随机化。最后一次选项 RNG 抽取得到的 `MapSeedOptions.seed` 才驱动地图生成；二者不能合并为同一个种子。

### 1.2 地图类型

| `MapType` | 原版含义 | 主地形路径 |
| --- | --- | --- |
| `0` | Archipelago，原版界面隐藏 | 普通路径 |
| `1` | Continent | 普通路径 |
| `2` | Team Continents | 普通路径 |
| `3` | Inland | 特殊路径 |
| `4` | Mountainous | 特殊路径 |

`Level` 不进入核心生成逻辑。原版以基础高度 `4` 初始化地图，光照由 `Time` 和 `RMGLevelLightSettings` 决定。

## 2. 顶层流程

生成器只使用一个 `NativeRandomizer`。从隐藏选项派生结束开始，后续调用顺序本身就是输出契约。

```text
界面选项（game-native）
  -> 派生 MapSeedOptions
  -> 初始化地图、RNG 和原生工作区
  -> MapType 0/1/2：普通水底图地形生成
     MapType 3/4：特殊空白陆地图地形生成
  -> 区域识别与处理
  -> 出生点、出生保护区
  -> 科技建筑和资源
  -> 高度、坡面、地表、植被
  -> 最终区域重建与地图导出
```

诊断模式可以在固定边界采集状态，但不得消耗 RNG、改变容器顺序或影响最终地图。

## 3. 两类地形主路径

### 3.1 普通路径：`MapType=0/1/2`

普通路径先把有效区域初始化为水，再通过 noisy flood 生成陆地。

- `Type 0`：将地图划为矩形区域，在各区域内生成多个带噪声的 flood patch，形成群岛。
- `Type 1`：从中心附近开始，以连续的小 patch 向外扩张，形成大陆。
- `Type 2`：横向或纵向切成两半，在椭圆和距离约束下分别 flood，形成团队大陆。

核心 flood 使用原生菱形坐标和最小堆。其行为依赖堆的 tie-break、邻居访问顺序、400 格下限和 fringe 的提交时机，不能替换为常规队列 flood fill。

地形生成后执行普通 `GenerateTerrain` post-pass。`MapType=0/1/2` 不人为添加陆地走廊；即使某个阶段名为 `regions_connected`，普通路径也只保留原版的阶段边界。

### 3.2 特殊路径：`MapType=3/4`

特殊路径不调用普通 `GenerateTerrain`。进入路径时地图是高度 `4` 的空白陆地，然后由 `PlaceCliffsAndOre` 系列逻辑建立水域、悬崖、台地和矿区拓扑。

- `WaterAmount == 0` 时不调用 `PlaceCliffsAndOre`。
- `WaterAmount != 0` 时进入特殊地形布置。
- `WaterAmount > 20` 时才尝试生成 cliff line。
- 后续包含 plateau、ore chain、zone outward/directional、区域拆分、桥梁/悬崖以及 level/zone 收尾。

这些步骤使用独立的 mapgen scratch。特殊工作区会持续保存在 `g_MapgenData` 的本地模型中，直到后续可见投影边界，不能在单个辅助函数返回后提前清空。

## 4. 固定阶段顺序

原生生成流程目前按以下 21 个证据边界采集。表中的阶段名同时用于阶段对比工具。

| 序号 | 阶段 | 主要行为 |
| ---: | --- | --- |
| 1 | `init_water` | 初始化基础地图；普通路径为全水，特殊路径为 level 4 空白陆地 |
| 2 | `terrain_generated` | 完成普通 flood 或特殊 cliff/plateau/ore-chain 主地形 |
| 3 | `post_process_terrain` | 普通地形 post-pass；特殊路径保留对应边界 |
| 4 | `decorate_ponds` | 处理池塘边缘和相邻地形 |
| 5 | `regions_built` | 初次识别连通区域 |
| 6 | `regions_expanded` | 扩张区域元数据 |
| 7 | `regions_connected` | 执行原版对应连接阶段；普通路径不额外造走廊 |
| 8 | `regions_rebuilt` | 重建区域 |
| 9 | `regions_processed` | 特殊路径执行完整 region reprocess |
| 10 | `green_near_shore` | 处理岸边绿地 |
| 11 | `starts` | 按区域配额选择出生点 |
| 12 | `spawn_exclusion` | 标记出生保护区 |
| 13 | `tech` | 按地图类型放置中立科技建筑 |
| 14 | `tiberium` | 放置区域资源和出生资源 |
| 15 | `height_slope` | 生成高度并清理坡面 |
| 16 | `surface` | 按 Theater 选择并布置地表 |
| 17 | `trees` | 生成树木 patch |
| 18 | `grass` | 放置草地覆盖物 |
| 19 | `decorations` | 仅作为证据边界；严格原生路径不调用 `PlaceDecorations` |
| 20 | `final_rebuild` | 最终区域和投影数据收尾 |
| 21 | `final` | 生成可序列化的最终地图状态 |

严格原生 `GenerateMap_598960` 不调用 `PlaceDecorations`。因此 `UrbanPresence` 不会在原生模式中产生城市区域、平民建筑、单位或额外树木目标，相关装饰指标保持为零。

## 5. 区域与出生点

### 5.1 区域处理

- 小区域过滤阈值为 `max(width * height * 0.03, 400)`。
- 玩家 waypoint 配额按区域面积累计分配。
- 普通路径与特殊路径的 region/zone 生命周期不同，不能用同一种“统一重建”替代。
- source grid、原生 Iso cell 和保存后的 map/local rect 是三套不同坐标，阶段之间必须显式转换。

### 5.2 出生点

`SelectSpawnCells` 使用 farthest-point sampling，使同一区域内后续出生点尽量远离已选点。出生点确定后，每个出生点最多标记 400 个 exclusion cell，供后续科技建筑和资源放置避让。

## 6. 科技建筑与资源

### 6.1 科技建筑

科技建筑目标和放置入口由 `MapType` 路由：

- `MapType=0`：优先处理 zero-quota region 分支。
- `MapType=2`：按 region 轮次放置。
- `MapType=1/3/4`：在全图选择随机目标。

### 6.2 Tiberium / Ore

资源放置包含两个阶段：region seed 和 waypoint seed。基础预算来自 `RMGMinimumTiberium`、`RMGMaximumTiberium` 及当前选项。

资源区域遍历顺序是原生行为的一部分：

- 普通路径中，zone 顺序由该区域最早的原生 Iso seed 决定。
- `MapType=3/4` 的富矿工作区直接保留 pathfinding `baseZone`。
- 全局 waypoint 数组仍保持原始 region 顺序，不能随放矿用 region 顺序一起重排。

`Resources` 只选择矿类型和特殊矿簇分支，不决定地形类型。`Theater` 选择地表布置路径，也不由 `MapType` 或 `Resources` 代替。

## 7. 高度、地表与植被

高度和坡面阶段同时依赖生成结果、原生坐标以及 theater tile 规则。地表阶段由 `Theater` 选择 rich/simple 路径，随后生成树木和草地。

树木目标由 `Vegetation`、`MaxTrees` 和 MapSeed 宽度共同决定。生成期间使用的 scratch 字段必须与最终地图字段分离：

- `mapgenSurfaceBlocked`
- `mapgenTempTag`
- `mapgenZoneId`

不能为了减少字段而将它们复用为最终 tile、overlay 或 zone 状态。

## 8. 最终地图输出

`yrini-product` 输出必须复刻原版的 section 顺序和编码边界，包括：

- `IsoMapPack5`
- `OverlayPack`
- `OverlayDataPack`
- `[Structures]`
- `[Waypoints]`
- `[Header]`
- `[Map]` 中的 `Size` / `LocalSize`
- `[Digest]`
- CRLF 换行和完整 `.map` 字节布局

`ScenarioClass::UniqueID` 的绝对入口状态取决于原版进程启动历史，无法仅由地图参数推出。验证时通过 `--native-scenario-unique-id` 注入入口状态；进入生成器后的对象 ID 生命周期由本地实现继续复刻。

## 9. 必须保持的原生不变量

以下项目即使看起来只是实现细节，也可能改变最终地图：

1. 单一 `NativeRandomizer` 的调用次数和顺序。
2. x87 PC53、RC_CHOP、float store 时机和 LUT sqrt。
3. 浮点表达式的分组、临时变量落地位置和函数边界。
4. 容器遍历顺序、堆 tie-break 和邻居访问顺序。
5. 菱形坐标与 source/Iso/saved-map 坐标之间的转换。
6. mapgen scratch 的生命周期和清理边界。
7. 诊断、阶段采集和函数轨迹不得消耗 RNG 或修改生成状态。

因此，原生路径的重构应以“移动完整函数体、保持表达式和调用顺序”为基本原则。不能用更通用、更现代或理论上等价的算法直接替换已验证实现。

## 10. 验证策略与证据边界

推荐采用两层差分验证：

1. 快速门禁：用相同输入分别调用原版 Oracle 和本地生成器，直接比较最终 `.map` 字节。
2. 失败诊断：只有最终产物不一致时，再采集 21 个阶段和 RFT1 函数轨迹，定位首次分叉。

整理前的完整实现有 13 组参数样本与原版 Oracle 的最终 `.map` 逐字节一致，覆盖 `MapType=0..4`、3 至 8 人、零水量、零资源、雪地、富矿、高水量和极端崎岖度。公开 CI 使用内置 TileCatalog 验证构件可重复生成；真实游戏资源下的最终字节一致性仍需本地 Oracle 样本复核。

这些结果证明当前覆盖样本一致，不等于对所有 32 位种子和全部参数组合做了数学穷举。新增原生逻辑改动时，应继续扩充有代表性的 Oracle 参数矩阵，并始终保留最终字节比较作为验收门禁。

## 11. 相关文档

- [代码结构审查与优化计划](native-generator-structure.md)
