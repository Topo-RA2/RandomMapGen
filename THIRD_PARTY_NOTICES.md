# Third-Party Notices

本文件汇总构建和运行所依赖的第三方组件。最终分发时应同时保留各依赖包附带的许可证文本。

## nlohmann/json

- 用途：地图评估和 `game-native` 选项报告 JSON 序列化
- 版本：v3.12.0
- 许可证：MIT
- 项目：https://github.com/nlohmann/json

## OpenSSL

- 用途：解密兼容的 Westwood MIX header
- 版本：3.5.5
- 许可证：Apache License 2.0
- 项目：https://www.openssl.org/

## RA2 format compatibility code

`third_party/ra2formats/` 和 `third_party/pack_codec/` 只保留原生资源加载与地图导出所需的
INI、MIX、TMP、Format5、Format80、LZO1X、Base64 和 SHA-1 实现。实现依据公开文件格式约定，
不包含游戏代码或游戏资源。实现来源说明见
`third_party/ra2formats/implementation-sources.md`。
