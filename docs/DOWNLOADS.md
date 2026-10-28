# 首次构建下载清单

核对日期：2026-10-28。适用于当前 Windows x64 源码包。MiB = 1,048,576 字节，GiB = 1,073,741,824 字节；下载量与解压、编译后的磁盘占用不同。

## 自动下载的固定依赖

`python build.py bootstrap --java8-home <JDK8目录>` 会获取以下 14 项。完整 URL、准确字节数和 SHA-256 见 [dependencies.lock.json](../dependencies.lock.json)。

| 内容 | 版本 | 下载体积 |
| --- | --- | ---: |
| Android 7.1 基础镜像，Google APIs ARMv7 | API25 r18 | 908.83 MiB |
| Android NDK，Windows x64 | r15c | 810.37 MiB |
| Android Build Tools | 25.0.3 | 48.96 MiB |
| Android Build Tools | 37 | 58.25 MiB |
| Android Platform | 25 r03 | 81.47 MiB |
| Zig，Windows x64 | 0.13.0 | 75.50 MiB |
| R8 / D8 | 8.3.37 | 15.86 MiB |
| Apktool | 3.0.3 | 14.76 MiB |
| slirp4netns，ARMv7 网络组件 | 1.3.4 | 1.34 MiB |
| Rust 编译器，Windows GNU | 1.99.1 | 98.55 MiB |
| Cargo，Windows GNU | 1.99.1 | 11.95 MiB |
| Rust 标准库，Windows GNU | 1.99.1 | 27.08 MiB |
| Rust 标准库，ARMv7 musl | 1.99.1 | 27.20 MiB |
| Rust MinGW 工具 | 1.99.1 | 5.24 MiB |

合计 **2,291,503,744 字节，约 2.134 GiB**，即十进制约 2.29 GB。

## Python 库

bootstrap 还通过 pip 安装以下固定版本。本次在 Windows x64 / Python 3.9 实际下载并与官方 PyPI SHA-256 对照，未引入其他传递依赖。

| 包 | 版本 | 下载字节 |
| --- | --- | ---: |
| dissect.extfs | 3.14 | 28,258 |
| dissect.cstruct | 4.6 | 54,246 |
| dissect.util | 3.22 | 149,489 |

合计 **231,993 字节，约 226.56 KiB**。其他 Python 版本、架构或 pip 索引可能选择不同分发包；网络协议及索引查询流量未计入。

## 需要提前安装的工具

这些工具不由 bootstrap 自动安装。下表提供明确的安装包体积示例，地址或官方发布元数据已核对；本轮没有重新安装全部工具。已有兼容环境可以跳过。JDK8 和 Java/Javac 17+ 承担不同任务，两者都需要。

| 工具示例 | 下载体积 |
| --- | ---: |
| Python 3.9.13 x64，原验证环境版本 | 27.88 MiB |
| JDK8，Temurin 8u504-b01 | 101.53 MiB |
| Java/Javac 17+，此处用 Temurin 21.0.9+10 ZIP 计量 | 195.47 MiB |
| PowerShell 7.6.5 x64 | 108.48 MiB |
| CMake 3.29.1 x64 | 43.67 MiB |
| Ninja 1.11.1 | 0.27 MiB |
| Git for Windows 2.55.0.windows.5，提供 OpenSSL | 62.32 MiB |

示例合计 **539.61 MiB**。已有可被构建找到的 OpenSSL 时，不必仅为此安装 Git。示例版本用于说明体积，不代表必须选择这些版本或厂商；下载地址见 [DOWNLOAD_AUDIT.json](DOWNLOAD_AUDIT.json) 的 `manualToolExamples`。

如果电脑上这些工具都没有，按上述示例计算，固定依赖、Python 库和工具安装包合计约 **2.661 GiB**，另加约 **46 MiB** 源码 ZIP。

## 测试范围和修正

- 核对了发布源码 ZIP 内的依赖锁文件与工程的一致性。
- 联网检查 14 项固定依赖；slirp4netns 旧地址返回 404，已改为官方正确资产名 `slirp4netns-armv7l`。
- 14 份本机缓存均通过准确大小和 SHA-256 检查；本轮没有重新下载全部大文件。
- 修正后的 slirp4netns 已完整下载并校验；三个 Python 分发包也已完整下载并与官方 PyPI 哈希核对。
- 工具示例通过官方地址或发布资产元数据核对。详细记录见 [DOWNLOAD_AUDIT.json](DOWNLOAD_AUDIT.json)。
- Rust 构建使用 `--offline`，没有额外 crates 下载。SwiftShader、AOSP 依赖源码、Rime 字典和 CA 文件已随源码提供，不需要另下完整 AOSP。
- `prepare-reader-pinyin.py` 在下载目录缺少文件时有三个 Rime 原始文件的下载回退；正常 `prepare/all` 已从源码复制，不会触发。
- 已下载文件通过 SHA-256 后会复用。正常重复构建不会再下载这些固定依赖；pip 还可能查询索引。

建议预留至少 **20 GiB** 工作空间。镜像解压、工具链、链接库和编译缓存会占用更多空间；本地约 10.6 GB 的构建目录不需要整体上传开源。
