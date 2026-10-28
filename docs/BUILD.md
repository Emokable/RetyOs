# 构建和修改

## 前置环境

Windows x86_64，Python 3.9+，PowerShell 7，Java/Javac 17+ 在 PATH，另有 JDK8 安装目录，CMake、Ninja、OpenSSL。Git for Windows 提供的 OpenSSL 也可。

```powershell
python build.py bootstrap --java8-home C:\Java\jdk8
python build.py doctor
python build.py all
```

bootstrap 校验 `dependencies.lock.json` 中所有下载项，安装固定的 Rust/NDK/SDK 等工具和 `requirements.txt` 中的 Python 库。可添加 `--cache D:\build-cache`，只复用校验过的外部下载；不会读取缓存的旧编译产物、设备镜像、payload 或签名私钥。

## 分步运行

```powershell
python build.py prepare
python build.py build
python build.py package
```

prepare 从官方镜像建立独立编译链接环境；build 按依赖顺序重编项目组件；package 检查源码与本轮构建一致后打包。失败查看 `reports/build-steps.json` 和对应日志。中断时可 `python build.py build --resume`；修改源码后使用普通 build 重新编译。

## 产物

- `out/kindle-android-install.tar`：容器部署包。
- `out/kindle-android/payload.tar.gz`：按明确布局生成的用户空间。
- `out/kindle-android/app-graphics-overlay.tar.gz`：本轮编译组件覆盖包。
- `out/runtime-manifest.json`：每个文件的来源、大小和 SHA-256。
- `out/build-manifest.json`：源码、外部输入及输出哈希。
- `reports/build-steps.json`：编译步骤、退出码和耗时。

源码仓库排除编译物、下载物、镜像、日志和私钥。归档顺序及时间字段标准化；工具版本、签名密钥和路径仍可影响二进制，不宣称跨机器逐字节一致。

## 修改入口

| 功能 | 路径 |
| --- | --- |
| 会话/冻结 | `sources/session-controller/src/`、`device/kindle-android/` |
| 桌面/文件/应用/输入法 | `sources/stage4/minimal/` |
| 刷新 | `sources/stage3/mock_gralloc.cpp`、`sources/stage5/hwtcon_frame_core.*` |
| 输入分发 | `sources/aosp7/complete/frameworks/native/services/inputflinger/` |
| SurfaceFlinger | `sources/aosp7/complete/frameworks/native/services/surfaceflinger/` |
| 软件 GL | `sources/swiftshader-2018/` |
| 启动/系统属性 | `runtime/run.sh`、`runtime/rootfs/default.prop`、`device/kindle-android/start.sh` |
| 包内布局 | `config/runtime-layout.json` |

本机编译不会连接 Kindle。新桌面使用本机生成的签名，覆盖已安装的不同签名 APK 前必须处理签名兼容。构建通过不等于设备运行验收；新包需另作受控实机验证。

## 首次安装

完整退出设备上的旧 Android 会话后，将安装 tar 解到 Kindle 用户存储根目录，对应 `kindle-android/`、`documents/`，以及许可证和构建清单。运行 Documents 中的 `install-android.sh` 校验组件并准备独立 data.img，然后运行 `start-android.sh`。不依赖 KOReader，也不包含其插件。

新构建使用新生成的桌面签名，不能把不同签名 APK 盲目写入原数据镜像；升级前保存数据并明确处理签名迁移。首次安装入口不擦除现有 data.img。需要完全退出时使用 `stop-android.sh`；日常保留/恢复由桌面和会话控制器处理。

升级现有安装时，可通过进程环境变量 `KINDLE_ANDROID_APK_KEY`（PKCS#8 DER 私钥）与 `KINDLE_ANDROID_APK_CERT`（DER 证书）明确指定原桌面的签名。两者必须同时设置；密钥不会复制进仓库或安装包，也不记录到构建报告。不要提交这些环境值或私钥文件。

`python build.py verify` 检查安装归档、运行时文件哈希、ARM32 ELF 和动态库依赖。`all` 在打包后自动执行这一步。它不代表实机启动或触控体验验证。

## 构建范围检查

当前主流程为 41 步。`config/build-components.json` 声明每步用途、代表产物和使用方；`verify` 检查是否覆盖实际步骤，产物是否存在，是否进入安装包或被后续步骤使用。完整说明见 [BUILD_COMPONENTS.md](BUILD_COMPONENTS.md)。这项检查用于防止重新混入没有用途的构建步骤，不代表逐函数运行覆盖率。
