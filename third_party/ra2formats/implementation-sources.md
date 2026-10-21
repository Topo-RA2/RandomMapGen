# 实现来源

本目录只包含 RandomMapGen 原生资源读取所需的最小格式子集。实现依据公开文件格式定义，不包含游戏代码或游戏资源。

实现依据：

- TS/RA2 TMP tile header、菱形像素与 ExtraData 的公开格式定义；
- classic C&C、TS/RA2 MIX header、Westwood 文件名 CRC、RSA key block、Blowfish header 加密，以及 checksum 对 body 计算 SHA-1 的约定；
- Westwood INI、Base64、Format5 container、Format80 与 LZO1X 编码约定；
- RA2/YR PAL 的公开约定：256 个 RGB 项，每分量 6-bit；

MIX 加密 header 使用 OpenSSL 3 的 BIGNUM 模幂和 Blowfish ECB 原语；Westwood 固定公钥、key block 分块方式及 header 布局由本仓库实现和校验。

若后续引入其他第三方实现，必须在此文件记录名称、版本、许可证和用途。
