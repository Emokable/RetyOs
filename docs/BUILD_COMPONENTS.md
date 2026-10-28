# Current build component usage

Every build step has a declared consumer in `config/build-components.json`. Package verification checks that all steps are covered, representative outputs exist, and each output is either an installer input or an explicitly consumed intermediate. This checks component-level usage; it is not a per-function liveness analysis.

The runtime retains boot self-checks, display recovery, shared storage, HTTPS certificates and application compatibility libraries. They are required support components. Upstream headers, code generators and license notices remain available for rebuilds; they must not be deleted merely because a filename is not directly listed in a command.

| Build step | Purpose | Consumer |
| --- | --- | --- |
| `prepare.py` | Binder source preparation | `build-graphics-guard.py` |
| `adapt-abi8.py` | Binder ABI 8 adaptation | `build-android-native.py` |
| `build-android-native.py` | Namespace helpers and Binder client | Installer runtime / host helper |
| `build-bionic-probe.py` | Startup linker self-check | Installer runtime / host helper |
| `build-ashmem.py` | Startup shared-memory self-check | Installer runtime / host helper |
| `build-container-init.py` | Container init | Installer runtime / host helper |
| `build-libbinder.py` | Binder runtime | Installer runtime / host helper |
| `build-container-installd.py` | Application installation service | Installer runtime / host helper |
| `build-headless-battery.py` | Battery service | Installer runtime / host helper |
| `build-graphics-guard.py` | Runtime guard, service manager and ashmem compatibility | Installer runtime / host helper |
| `build-headless-gles.ps1` | Software GLES and intermediate PixelFlinger link library | Installer runtime / host helper |
| `build-mock-hwcomposer.ps1` | Framebuffer compositor HAL | Installer runtime / host helper |
| `build-headless-surfaceflinger.ps1` | SurfaceFlinger | Installer runtime / host helper |
| `build-r12-pixelflinger.py` | Runtime optimized PixelFlinger | Installer runtime / host helper |
| `build-null-audio.py` | Null audio HAL | Installer runtime / host helper |
| `build-swiftshader-arm.ps1` | Software GLES compatibility for installed apps | Installer runtime / host helper |
| `prepare-webview-api25.py` | Framework resource input | `prepare-minimal-framework-config.py` |
| `build-apk-probe.py` | Desktop, files, applications and keyboard | Installer runtime / host helper |
| `prepare-minimal-framework-config.py` | Container framework resources | Installer runtime / host helper |
| `prepare-ca-certificates.py` | HTTPS trust store | Installer runtime / host helper |
| `build-netd-exit-trace.py` | Container netd compatibility | Installer runtime / host helper |
| `build-system-controls.py` | System controls and UI bridge | Installer runtime / host helper |
| `build-zygote-namespace-enter.py` | Zygote namespace entry | Installer runtime / host helper |
| `build-apk-probe-runner.py` | Install and launch coordinator | Installer runtime / host helper |
| `build-stage5-hwtcon-gralloc.ps1` | HWTCON display pipeline | Installer runtime / host helper |
| `build-stage5-exclusive-input.ps1` | Exclusive touch input | Installer runtime / host helper |
| `build-stage5-hwtcon-probe.ps1` | Framebuffer recovery helper | Installer runtime / host helper |
| `build-fuse-mount-broker.py` | Shared files mount broker | Installer runtime / host helper |
| `build-sdcard.py` | Android shared storage | Installer runtime / host helper |
| `build-getsockname-trace.py` | Container socket compatibility | Installer runtime / host helper |
| `build-userxattr-compat.py` | Data filesystem compatibility | Installer runtime / host helper |
| `build-session-maintenance.py` | Application maintenance | Installer runtime / host helper |
| `build-alarm-fallback-fix.py` | Alarm compatibility | Installer runtime / host helper |
| `build-package-service-dump.py` | Package manager bridge | Installer runtime / host helper |
| `build-session-freezer.py` | Warm-session freeze and thaw | Installer runtime / host helper |
| `build-session-power-guard.py` | Session power management | Installer runtime / host helper |
| `build-session-preflight.py` | Native Binder preflight | Installer runtime / host helper |
| `build-native-frame-probe.py` | Native-home display verification | Installer runtime / host helper |
| `build-session-controller.py` | Session controller and native frame restoration | Installer runtime / host helper |
| `build-isolated-network-tool-shim.py` | Isolated network tool compatibility | Installer runtime / host helper |
| `build-runtime-java.py` | SystemServer bootstrap and boot helpers | Installer runtime / host helper |

## Removed in this cleanup

Removed 44 project-owned obsolete diagnostics, benchmarks and download/source snapshots, including the optional density probe and experimental ashmem pool probes. Removed the unused CursorWindow experiment and mock-gralloc build steps. Removed unused baseline ashmem/guard/servicemanager variants and the installd diagnostic preload from the remaining scripts. The actual HWTCON gralloc, system servicemanager, framework ashmem compatibility and recovery helpers remain.

The component pipeline now has 41 steps. Building and packaging does not connect to or modify the Kindle.
