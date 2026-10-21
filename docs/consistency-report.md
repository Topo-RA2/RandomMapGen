# 地图产物一致性报告

日期：2026-10-21

平台：Windows x64，MSVC release

参考版本：既有 `RandomMapGen 1.0.0` release 二进制

候选版本：本仓库 `RandomMapGen 1.1.0-native`

输出格式：`yrini-product`，内置 `yr` TileCatalog，不使用外部游戏资源

## 结论

13 组固定输入的产品 `.map` 文件长度和 SHA-256 全部与参考版本一致，首个差异字节均为 `-1`（不存在差异）。

该结果证明当前样本组合下产物一致，不构成对所有 32 位种子、全部参数组合和所有 mod 资源的穷举证明。

## native 模式

| 案例 | 长度 | SHA-256 |
| --- | ---: | --- |
| type0-doc | 130322 | `549CBF2EE34F67CA7030BE8CE3CBF7B31991E9CF011CAD0EF2CE6E5002EFCDB5` |
| type1-default | 124578 | `5C93804CD8291042310BFBAB59BA556EAC5078074C5AAEF8EE9121785B61D816` |
| type2-teams | 130325 | `B754D0FA73EF721FEB2FF16CB3AEE04851456D610E1B99D38AAF98F89CA0FF03` |
| type3-inland | 133699 | `4C9FE6B4D4B1C221C7655C723DCACC488E6D26681291A23AF7992B64AE58C4F9` |
| type4-mountain | 136980 | `4C4D3B0429704378AEF4228A73A70FA43C68584EF042BB08679D23F420C9E8FC` |
| snow-rich | 124706 | `B16FC287BCA7D74244318415ABDB2D78CEDE72B5D6E38BCBF5F9903268BDD21C` |
| zero-water-resource | 84663 | `0A43B49C32C7E552B7E7C910CF8F1F0F5AEA1235F30F7928F618957FA9F01878` |
| max-seed | 124947 | `B9B9BAC6C50A1385267C5C30219203F766FFEFD6AE02E7F0B43E00739BDF4ECD` |

覆盖：

- `MapType=0..4`
- 2、4 人地图
- 雪地 Theater
- 零水量、零资源、零植被
- 富矿和 TiberiumLayout
- `Seed=4294967295`

## game-native 模式

| 案例 | 长度 | SHA-256 |
| --- | ---: | --- |
| game-doc | 119021 | `E97E6A046ADE69B3E0F9D29476343D1226E486D2778B7072417612F567F7FED9` |
| game-small | 104820 | `54DE2E387868B3362699486AA993794CCE802225BBC9FE4442149EB32D0C5B85` |
| game-large | 199090 | `823EFA1ED97C1DA509F9C91FB91442BB37807D83EA04AFD22612C82BFF800729` |
| game-eight | 257096 | `471036B71B8909DC5E8779150640A8B295D9E970C90F409E0EB0D0F04CC4B1A5` |
| game-max-random | 112542 | `894C031E69B66311EB515BA616F4722285C89E72BF9608FEF1979BD86607638F` |

五组派生参数、最终 16 位地图种子和 RNG 抽取次数均一致。`native-options-report-out` JSON 存在一个主动差异：

```text
参考: "mode": "native"
候选: "mode": "game-native"
```

除该标签外，`derived` 内容一致。

## 预览 yrini

8 组 native 预览文件均仅有一行文本差异：

```text
参考: MapEvaluatorVersion=ra2-pvp-v2
候选: MapEvaluatorVersion=ra2-native-v1
```

这是 native-only 评估器版本标识的主动调整。除该行外，预览内容一致；每个文件长度增加 3 字节。

## 边界

- 尚未使用真实 RA2/YR MIX、Theater INI 和 TMP 资源复跑。
- 尚未验证 Linux x86_64 的最终字节一致性。
- 尚未对全部参数组合执行穷举。
- 后续算法改动仍应同时运行本报告样本和真实游戏资源的原版 Oracle 样本。
