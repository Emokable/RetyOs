# RetyOs

[简体中文](README.md) | **English**

An open experiment running Android 7.1.1 on Kindle.

Inspired by the 墨卓 project, I started building an Android environment on Kindle's native Linux kernel. The aim is an open project that the community can study, modify and improve for lightweight reading applications.

This project uses an Android 7.1.1 ARMv7 userspace with custom container startup, Binder/ashmem compatibility, display, touch, application management and freeze/resume integration. Android boots, APKs can be installed, and applications such as WeRead have been run. Application loading, page turns, white screens and compatibility still need work.

Maintenance is limited by personal time and available test devices. Contributions to debugging, performance, documentation and additional Kindle models are welcome. The current version is intended for development and experimentation; it is not yet suitable for users seeking a dependable daily system.

## Current status

- Verified: Android startup, APK installation, the desktop, 360 DPI, freeze/resume and returning to Kindle.
- Runtime: Android 7.1.1, 32-bit ARMv7. Applications must support Android 7.1 and a compatible architecture.
- Test device: Kindle Colorsoft; the most recent device checks used firmware 5.20.1. Other models are unverified.
- Limitations: some applications are slow, display white screens or fail to run. Cold startup remains slow. Physical USB hot-plug and long-term stability testing are incomplete.

## What the source builds

The source produces the current complete container installation bundle. Project components, including the controller, desktop, display, touch and compatibility layers, are rebuilt. Core components such as ART and the Java Framework come from a pinned official Android 7.1.1 prebuilt runtime downloaded during setup.

This is not a full AOSP product build. Third-party APKs such as WeRead, user data and jailbreak tools are not included. See [PROVENANCE.md](docs/PROVENANCE.md) for source and dependency boundaries. Supporting documentation is currently primarily in Chinese.

## 1. Build on the computer

Use **Windows x64** with Python 3.9+, PowerShell 7, Java/Javac 17+, a separate JDK8 installation, CMake, Ninja and OpenSSL. Reserve at least **20 GiB** of workspace.

Open PowerShell 7 in the source directory and run:

```powershell
python build.py bootstrap --java8-home "C:\Java\jdk8"
python build.py doctor
python build.py all
```

Replace `C:\Java\jdk8` with your JDK8 directory. Initial automatic downloads total approximately **2.13 GiB**; verified dependencies are reused. The [download inventory](docs/DOWNLOADS.md) also lists tools that must be installed beforehand.

The output is `out/kindle-android-install.tar`. On failure, inspect `reports/build-steps.json` and the corresponding log. See [BUILD.md](docs/BUILD.md) for individual build steps and modification instructions.

**Keep the locally generated desktop signing key private and retain it for upgrades.** An existing installation should use its original signing key. Do not overwrite existing data with a desktop signed using a newly generated key on another computer. Key paths and upgrade settings are documented in [BUILD.md](docs/BUILD.md).

## 2. Install using USB

The Kindle must already be jailbroken and configured with a tool that can execute `.sh` scripts. Opening a script as a normal book will not execute it. Reserve at least **3 GiB** of device storage for a fresh installation.

1. If Android is already installed, run `stop-android.sh` and wait until the native Kindle interface has fully returned. **A retained frozen session is not fully stopped.** Back up `kindle-android/data.img` before upgrading; backups also require a complete stop.
2. Extract `kindle-android-install.tar` on the computer.
3. Connect the Kindle over USB. Copy the extracted **`kindle-android` and `documents` folders** to the root of internal storage, merging existing folders. Do not add an extra enclosing directory. Keep the bundle's licenses and build manifests on the computer.
4. Safely eject the Kindle and unplug USB. Use your existing script tool to run `documents/install-android.sh`.
5. Once installation finishes, run `documents/start-android.sh`. Initial data and system-cache preparation takes longer; wait for the startup progress to complete.

The installer creates a separate `data.img` and does not automatically erase an existing data image. Never replace, move or delete that image while Android is running or frozen.

## 3. Everyday use and APK installation

| Action | Method |
| --- | --- |
| Start or resume Android | Run `start-android.sh` |
| Return to Kindle while retaining Android | Tap **Kindle** at the top right of the Android desktop |
| Fully stop Android | Return to Kindle, then run `stop-android.sh` |
| Install an APK | Open the APK through the desktop's Files entry, labelled “文件” |

Before transferring APKs from a computer, fully stop Android. Connect USB and copy files into the `AndroidFiles` folder in internal storage, creating it if necessary. Safely eject and unplug the Kindle, then start Android and open Files.

Choose lightweight ARMv7 or Java-only applications compatible with Android 7.1. ARM64-only APKs cannot run. Warm resume and full cold startup are separate paths; resume timings must not be described as cold-boot timings.

## 4. Remove the container

1. Return to Kindle, run `stop-android.sh`, wait for the native interface to recover, then connect USB.
2. To retain Android applications and data, first copy the entire `kindle-android/data.img` to the computer.
3. Delete the **`kindle-android` folder** and the three scripts `install-android.sh`, `start-android.sh` and `stop-android.sh` inside `documents`.

Deleting `kindle-android` also deletes Android applications and login data. **Do not delete the entire `documents` folder.** Files manually copied into `AndroidFiles` can be kept or removed separately.

## Risks and licensing

This is experimental software with no guarantee of stability or compatibility across Kindle models. Device scripts run as root and manage processes, mounts, display and storage; failures may cause hangs or data loss. Back up important files and read [SAFETY.md](docs/SAFETY.md) before use.

Original project code is licensed under [MIT](LICENSE). AOSP, Linux/MediaTek, SwiftShader, Rime and other upstream material retain their respective licenses. The external Android runtime is obtained under its provider's terms. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
