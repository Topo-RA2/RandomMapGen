# 来源与边界说明

`RandomMapGen` 是对 RA2/YR 原生随机地图生成行为的独立重实现。仓库包含算法代码、格式编解码、测试入口和文档，不包含：

- `gamemd.exe`、`gamemd-spawn.exe` 或其他游戏二进制；
- 游戏 MIX、TMP、SHP、VXL、PAL、地图和音频资源；
- IDA 数据库、详细反汇编导出或原版调试产物；
- 其他地图策略生成器的实现。

用户应自行提供合法取得并安装的 RA2/YR 资源。工具只读取指定目录中的文件。

本项目不隶属于 Electronic Arts 或 Westwood Studios，也不代表其授权。相关游戏名称、商标和素材归各自权利人所有。
