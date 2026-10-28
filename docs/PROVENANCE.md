# 来源与构建边界

| 组件 | 固定来源 | 本仓库位置 |
| --- | --- | --- |
| AOSP | Android `android-7.1.2_r39`；早期 servicemanager commit `5f1a7cfd320022c90d6e06ab78d602459177736c` | `sources/aosp7/` 与 `sources/` 的 Binder 文件，保留修改后源码及原许可 |
| SwiftShader | `8e6ed2e35bce87eccd69569840a81c49669b6cb6`，含 Android 适配 | `sources/swiftshader-2018/`，包括 LLVM/Subzero 源码 |
| Kindle HWTCON/触控 | Kindle Colorsoft GPL 包 `Kindle_src_5.19.3.0.1_4721720001.tar.gz` 的 Linux 5.15 相关代码 | `sources/device-kernel-colorsoft/`，保留 MediaTek/GPL 声明，不构建或刷写内核 |
| 项目代码 | 本项目当前源码 | `sources/session-controller/`、`sources/ashmem/`、`sources/container-init/`、`sources/stage3/4/5/` |
| Rime 数据 | `rime/rime-pinyin-simp` commit `0c6861ef7420ee780270ca6d993d18d4101049d0` | `third_party/rime-pinyin-simp/`，Apache 2.0 |
| Android 基础运行时 | 官方 Google APIs API25 ARMv7 r18 | 从锁定 ZIP 提取，不纳入 Git |
| 编译工具和 slirp4netns | `dependencies.lock.json` 中的 URL/长度/SHA-256 | bootstrap 获取到忽略目录；slirp4netns 当前使用固定上游 ARMv7 二进制 |
| CA 数据 | 固定 SHA-256 的 Mozilla/curl 公共 CA 源数据 | `config/cacert.pem`，MPL 2.0 |

`config/runtime-layout.json` 对每个包内文件标明来源：`api25` 是官方镜像路径和文件哈希，`build` 是本轮编译输出，`source` 是可编辑配置或运行脚本。运行脚本最初由旧 payload 提取为文本；此后构建直接读取仓库里的 `runtime/`，不读取旧包或设备备份。

本工程重建本项目移植的容器系统包。未修改的 ART、Java framework 和原有系统包来自官方基础镜像。若要重编这些基础组件，需要另建完整 AOSP 产品构建，并验证 ABI/OAT 一致性。用户应用、登录状态、书籍和数据镜像不属于构建输入。
