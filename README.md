# RetyOs

**简体中文** | [English](README.en.md)

在 Kindle 上运行 Android 7.1.1 的开源实验。

受到墨卓项目的启发，我尝试在 Kindle 原生 Linux 内核上搭建 Android 运行环境，希望为轻量阅读应用提供一个可研究、可修改、由社区共同完善的开源方案。

本项目使用 Android 7.1.1 ARMv7 用户空间，自行适配容器启动、Binder/ashmem 兼容、显示、触控、应用管理与冻结恢复。目前已能启动 Android、安装 APK，并运行微信读书等应用；应用加载、翻页、白屏和兼容性仍有待改进。

个人时间与测试设备有限，维护以业余时间为主。欢迎熟悉 Linux、Android 和 Kindle 底层开发的朋友参与修复、性能优化、文档完善及其他机型适配。当前版本适合开发与实验，尚不适合希望获得稳定日常体验的用户。

## 当前状态

- 已验证：Android 启动、APK 安装、桌面、360 DPI、冻结恢复和返回 Kindle。
- 运行环境：Android 7.1.1、32 位 ARMv7；应用需要支持 Android 7.1 和相应架构。
- 测试设备：Kindle Colorsoft，最近复测固件为 5.20.1；其他机型尚未验证。
- 已知限制：部分应用卡顿、白屏或无法运行

## 先了解构建范围

源码可以生成当前完整的容器安装包。本项目的控制器、桌面、显示、触控和兼容组件会重新编译；ART、Java Framework 等基础组件使用固定版本的官方 Android 7.1.1 预编译运行时，首次构建时下载。

这不是完整 AOSP 产品的源码构建，也不包含微信读书等第三方 APK、用户数据或越狱工具。来源与许可范围见 [PROVENANCE.md](docs/PROVENANCE.md)。

## 1. 在电脑上编译

需要 **Windows x64**，提前安装 Python 3.9+、PowerShell 7、Java/Javac 17+、单独的 JDK8、CMake、Ninja 和 OpenSSL。建议预留 **20 GiB** 空间。

在 PowerShell 7 中进入源码目录，执行：

```powershell
python build.py bootstrap --java8-home "C:\Java\jdk8"
python build.py doctor
python build.py all
```

把 `C:\Java\jdk8` 换成实际 JDK8 目录。首次自动下载约 **2.13 GiB**，后续复用通过校验的依赖；[下载清单](docs/DOWNLOADS.md)另列了需要预装的工具。

成功后得到 `out/kindle-android-install.tar`。失败时查看 `reports/build-steps.json` 和对应日志；分步构建与修改方法见 [BUILD.md](docs/BUILD.md)。

**请保留本机生成的桌面签名密钥，不要上传它。** 升级已有安装时应沿用原签名；不要直接用另一台电脑生成的新签名覆盖原数据。签名路径和升级设置见 [BUILD.md](docs/BUILD.md)。

## 2. 通过 USB 安装到 Kindle

前提：Kindle 已越狱，并已配置可以运行 `.sh` 文件的脚本工具。仅把脚本当作普通书籍打开不能执行。首次安装建议预留至少 **3 GiB** 存储空间。

1. 如已有 Android，先运行 `stop-android.sh`，等待完全返回 Kindle。**冻结保留不等于彻底停止。** 更新前备份 `kindle-android/data.img`，备份时也必须完全停止。
2. 在电脑上解压 `kindle-android-install.tar`。
3. USB 连接 Kindle，将解压出的 **`kindle-android` 和 `documents` 两个文件夹**复制到内部存储根目录，合并同名文件夹。不要再套一层外部目录；包内许可证和构建清单保留在电脑上即可。
4. 安全弹出 Kindle 并拔掉 USB，通过已有脚本工具运行 `documents/install-android.sh`。
5. 安装脚本完成后，运行 `documents/start-android.sh`。首次会准备数据和系统缓存，耗时较长，请等待启动进度完成。

安装脚本会创建独立的 `data.img`；已有数据镜像不会被自动擦除。不要在 Android 正在运行或冻结时替换、移动、删除该镜像。

## 3. 日常使用和安装应用

| 操作 | 方法 |
| --- | --- |
| 启动或恢复 Android | 运行 `start-android.sh` |
| 返回 Kindle，保留 Android 会话 | 点击 Android 桌面右上角 **Kindle** |
| 彻底停止 Android | 回到 Kindle 后运行 `stop-android.sh` |
| 安装 APK | 在 Android 桌面的“文件”中打开 APK |

需要用电脑拷入 APK 时，先彻底停止 Android，再连接 USB，把文件放入内部存储的 `AndroidFiles` 文件夹。安全弹出并拔线后启动 Android，即可在“文件”中找到它。文件夹不存在时可以自行创建。

优先使用轻量、支持 Android 7.1 的 ARMv7 或纯 Java 应用；仅支持 ARM64 的 APK 无法运行。冻结恢复与完整冷启动是两条不同路径，不能把恢复耗时当成冷启动耗时。

## 4. 删除容器

1. 回到 Kindle，运行 `stop-android.sh`，等待原系统恢复，再连接 USB。
2. 如需保留 Android 应用及数据，先把整个 `kindle-android/data.img` 复制到电脑。
3. 删除内部存储中的 **`kindle-android` 文件夹**，以及 `documents` 下的 `install-android.sh`、`start-android.sh`、`stop-android.sh`。

删除 `kindle-android` 会一并删除 Android 应用和登录数据。**不要删除整个 `documents` 文件夹。** `AndroidFiles` 中手动拷入的文件可自行决定保留。

## 风险与许可

本项目属于实验性质，不保证稳定性或跨机型兼容。设备脚本以 root 运行，涉及进程、挂载、显示和存储，可能导致卡死或数据丢失；使用前请备份重要文件并阅读 [SAFETY.md](docs/SAFETY.md)。

项目自有代码采用 [MIT](LICENSE)。AOSP、Linux/MediaTek、SwiftShader、Rime 等上游内容保留原许可，外部 Android 运行时按提供方条款获取，详见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
