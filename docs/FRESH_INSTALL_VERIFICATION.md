# Fresh-install verification — 2026-10-28

Status: fresh installation and controlled runtime checks passed on Colorsoft firmware 5.20.1. Physical USB transfer and optical display latency remain unverified.

The old container and its data/cache were removed after verified off-device backups. The first clean installation exposed Windows CRLF checksum sidecars. The packager now writes LF bytes and the verifier rejects CRLF or incorrect sidecars. Local component compilation and package validation pass.

After SSH access was restored, a read-only inventory confirmed the deployment was absent. A new installation created a fresh 1.5 GiB data image and booted Android. An application was subsequently installed by the user; later fixes preserved that new data.

Validated candidate SHA-256: `8c997acf21d1cd07747713a2cb88ccd94fd787d9d5b4c9a24d90f81c6b5e13c2`.

## Regressions fixed and checked

- SurfaceFlinger's process-local property shim discarded real properties. Forwarding unhandled reads to the property area restored the actual initial display density from 160 to 360. Initial, base and logical density were all 360, including after clearing the forced-density override. A framebuffer capture showed the enlarged desktop.
- Fresh packaging omitted `native-framework.mode=preserve`. It is now included and verified. Two retain/resume cycles kept the same native X process. Home activation, frame validation and refresh completion were recorded rather than relying on service-running status.
- First installation had no generated system cache. Initial setup now builds a rebuildable read-only ext4 system image; the following cold boot hit its cache with a 190 ms check and skipped payload hashing. The measured subsequent full cold entry to controller active was 55.87 seconds; this does not imply a seconds-long cold boot.
- Generated runtime cleanup now follows the merged system inventory. Full exit reported `SESSION_CLEANUP_STATUS=removed`, `REMAINING_MOUNTS=none`, and `TEMP_DIRECTORY_BYTES=0`. Two older inactive stages were removed only after checking process references and mount/loop ownership.

## Warm transition measurements

Two consecutive cycles, measured on-device with a shared monotonic clock:

| Measurement | Cycle 1 | Cycle 2 |
| --- | ---: | ---: |
| Retain request to native home frame/refresh completion | 3293 ms | 1602 ms |
| Retain request to completion including frozen MTP responder startup | 4699 ms | 2023 ms |
| Resume request to Android software-ready | 1314 ms | 1309 ms |

These are software timestamps, not camera measurements of physical touch-to-visible response. The frozen MTP responder started, but no physical USB cable transfer was performed. The idle responder required the existing USB-disconnected, PID-checked KILL fallback after its bounded TERM grace; connected-host behavior was not exercised. Application responsiveness and long-duration stability are separate acceptance tasks.

Backups and raw device logs remain outside the public source tree. No user APK, login data or data image is included in the installer.

## Source-cleanup candidate redeployment

The 41-step build candidate `4244a5e019ac592d54dc3802af02d4c604bcacb8be54265b4ed6e256a4882093` was deployed after verified off-device runtime and data backups. The stopped data image hash was unchanged by installation. The optional KOReader plugin was removed. Initial and base display density were both 360, the desktop framebuffer capture was checked, and the Kindle root partition remained read-only.

Two controlled retain/resume cycles recorded native-home refresh completion at 1854 / 1518 ms and Android resume software-ready at 1619 / 1668 ms. Native X PID remained unchanged. A subsequent complete exit removed the stage directory with no remaining mounts or temporary bytes. These are software timestamps; physical USB transfer and physical touch-to-visible latency remain unverified. Application rendering performance was not benchmarked by this packaging cleanup.

The following fully stopped cold start hit the cache (220 ms check), with entry-to-controller-active at 76.86 seconds. Warm resume remains a separate path; this cleanup does not achieve a seconds-long full cold boot.

## RetyOs UI update

Installer `6e42728b0175f5ac205c38d2fdf9f4fc847f67309908042c1174fd29e68d5f8d` was deployed with application data preserved. Desktop title and application label are RetyOs; the redundant Kindle/Android version line was replaced with application count. Startup uses a text-only RetyOs page, a thin progress bar, one initial GC16 clear and bounded GL16 partial refreshes. Real framebuffer captures of the boot page and desktop were inspected. Font-layout computation reported no truncation. The progress log began at 5 and reached the first-frame handoff before exiting at 100.
