# RandomMapGen

`RandomMapGen` 是《红色警戒 2：尤里的复仇》原生随机地图生成流程的独立 C++ 实现，用于研究、复现和离线批量生成 RA2/YR 随机地图。

本仓库只包含 `native` 和 `game-native` 两种模式：

- `native`：直接接收底层 `MapSeedOptions`，包括 32 位地图种子、尺寸、玩家数、`MapType`、资源、高度、植被、区域等参数。
- `game-native`：接收原版随机地图对话框的可见参数，先按原版 RNG 顺序派生隐藏参数和 16 位地图种子，再执行同一套原生生成器。

仓库不包含其他地图策略生成器、游戏原版二进制、IDA 数据库或游戏美术资源。

## 特性

- 保持原生生成器的单一 RNG 顺序、菱形坐标、堆排序、浮点落地和 21 个阶段边界。
- 支持 `MapType=0..4`、2 至 8 人、全部 Theater 和 RA2/YR 原生参数范围。
- 输出调试文本、预览 `yrini` 或接近原版保存格式的 `yrini-product`。
- 支持用户自备的 RA2/YR 游戏资源目录，用于读取 Theater INI、MIX 和 TMP tile 元数据。
- 无游戏资源时可用内置 `yr` TileCatalog 生成预览地图。
- Windows x64 与 Linux x86_64 独立 xmake 构建。
- 提供 `rmg_native` 静态库和 `rmg` 命令行程序。

## 构建

要求：

- xmake >= 3.0.6
- 支持 C++20 的编译器
- Windows：Visual Studio 2022 x64 和 NASM
- Linux：x86_64 的 GCC/Clang 和 GNU assembler

Windows：

```powershell
xmake f -m release -p windows -a x64 -y
xmake build -y rmg
xmake run rmg --version
```

Linux：

```bash
xmake f -m release -p linux -a x86_64 -y
xmake build -y rmg
xmake run rmg --version
```

xmake 会按锁定版本获取 `nlohmann_json` 和 OpenSSL 3。

## Release

推送匹配 `v*` 的 tag 会触发正式发布。工作流将构建并测试 Windows x64 和 Linux x86_64，然后发布：

```text
RandomMapGen-<tag>-windows-x64.zip
RandomMapGen-<tag>-windows-x64.zip.sha256
RandomMapGen-<tag>-linux-x86_64.tar.gz
RandomMapGen-<tag>-linux-x86_64.tar.gz.sha256
```

发布包包含二进制、README、AGPL 许可证、NOTICE、第三方声明、示例和文档。

## 快速开始

直接指定底层地图种子：

```powershell
xmake run rmg --mode native --seed 123 --width 80 --height 80 --players 2 `
  --format yrini --out out/native-123.yrini
```

模拟游戏对话框：

```powershell
xmake run rmg --mode game-native --randomization-seed 305419896 `
  --game-terrain-type 2 --time-of-day 3 --climate 1 `
  --map-size 2 --resources 2 --players 4 `
  --format yrini --out out/game-native.yrini
```

同时生成预览地图和产品地图：

```powershell
xmake run rmg --mode native --seed 123 --width 80 --height 80 --players 2 `
  --format yrini --out out/native-preview.yrini `
  --product-out out/native-product.map
```

使用本机游戏资源：

```powershell
xmake run rmg --mode native --seed 123 --width 80 --height 80 --players 2 `
  --game-resource-dir "C:\path\to\Yuris Revenge" `
  --format yrini-product --out out/native-product.map
```

游戏资源目录不随本仓库分发。工具只读取用户提供目录中的文件，不修改它们。

## CLI

运行 `rmg --help` 查看完整参数。原生参数包括：

```text
--seed --width --height --players --theater --map-type
--resources --time --water-amount --tiberium
--tiberium-layout --vegetation --ruggedness
--urban-presence --accessibility --region-size --terrain-type
--sed --rmg-config
```

`game-native` 参数包括：

```text
--randomization-seed --game-terrain-type --time-of-day
--climate --map-size --resources --players
```

`--sed` 使用原生 `[RandomMap]` 字段。直接传入的 `--width` 和 `--height`
表示可玩尺寸；通过 SED 加载的尺寸保留原版尺寸插值语义。

## 库接口

公共入口位于 `rmg/native_api.h`：

```cpp
#include "rmg/native_api.h"

rmg::MapSeedOptions options;
options.seed = 123;
options.numPlayers = 2;
options.width = 80;
options.height = 80;

const rmg::NativeGenerationResult result =
    rmg::generateNativeMap(options);
```

需要真实 Theater TMP 元数据时，先加载 `NativeTheaterCatalog`，再将指针传给
`generateNativeMap()`。

## 确定性

相同代码、编译器和优化配置下，相同输入应产生相同输出。以下内容属于输入契约：

- 代码版本和构建平台
- 地图种子与全部生成参数
- RMG 配置
- 游戏资源目录内容

原生核心通过展开源码哈希和固定输入回归防止无意变化。完整原版 `.map`
逐字节一致性需要额外的原版 Oracle 样本验证；仓库不附带原版游戏文件。

当前产品地图对照结果见
[`docs/consistency-report.md`](docs/consistency-report.md)。

## 验证

```powershell
pwsh -NoProfile -File ./scripts/verify-native-only-tree.ps1
xmake build -y rmg rmg_tests
xmake run rmg_tests
pwsh -NoProfile -File ./tests/test_native_cli.ps1 -RmgPath ./build/windows/x64/release/rmg.exe
```

## 许可证

本项目使用 `AGPL-3.0-or-later`。第三方依赖和声明见
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md)。

本项目与 Electronic Arts、Westwood Studios 或《命令与征服》品牌无隶属或授权关系。
