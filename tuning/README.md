# Startup tuning

Small, low-risk changes to `device/kindle-android/start.sh`, plus two read-only
tools to find out where a cold boot actually spends its time.

Nothing here changes the container, the Android image, or any app-visible
behaviour. Both changes are startup-path only.

## What changed

### 1. Sub-second service-stop polling

`start.sh` stops `lab126_gui` and X before the container takes over the display,
then polls `initctl status` in a `while` loop with a 1 s tick and a 30 s budget.

Those services normally reach `stop/waiting` in well under a second, so the 1 s
tick costs a full second before the first check and rounds every stop up to the
next whole second — twice, once per service.

The tick is now 200 ms (150 ticks ≈ the same 30 s budget). `usleep` is absent on
some firmwares, in which case the original 1 s tick and 30 s budget are kept, so
behaviour degrades to exactly what it was.

Nothing else about the stop sequence changes: the same `stop/waiting` match, the
same timeout, the same `restore-framework.sh` path on failure.

### 2. sha256 result cache for the bootstrap tarballs

Every cold boot re-hashes `system-cache-bootstrap.tar` (and `fixed-bootstrap.tar`
on the fixed-runtime path) purely to compare the digest against the value
recorded in `*.ready` at build time. These tarballs live on the FAT32 user
partition and are hashed single-threaded on ARMv7.

A digest is only needed when the file may have changed, so the `size:mtime`
signature is stored next to the digest and the re-hash is skipped while both are
unchanged. Any rewrite of the tarball updates mtime, which invalidates the entry;
so does a corrupt cache line. On a miss the code falls back to plain hashing, so
the worst case is the old behaviour plus two small reads.

Cache files: `$BASE/system-cache-bootstrap.sha-cache`, `$BASE/fixed-bootstrap.sha-cache`.

Trade-off, stated plainly: a tarball corrupted *in place* with the same size and
the same mtime would now pass the check. mtime cannot be preserved by an
overwrite, so this needs a deliberate `touch -d` to reproduce, and the payload
digest for the extracted content is still verified separately.

## What was considered and rejected

**Trimming `BOOTCLASSPATH`.** Removing rarely used boot jars (`core-junit`,
`apache-xml`, `org.apache.http.legacy`) looks free, because `runtime/run.sh`
contains a long list of `rm -f "$stage"/rootfs/system/framework/*.jar`. That list
is a leftover: `prepare-system-cache.sh` packs `system-cache-bootstrap.tar` with
`--exclude=rootfs/system`, and `/system` is bind-mounted from `system-cache.img`
(ext4, read-only). So those `rm` calls operate on an empty staging directory and
delete nothing.

Which means the real `/system` still ships `boot.art` and `boot-*.oat`. ART
fingerprints the image against the boot class path; changing the class path
invalidates the image and ART boots without it, interpreting everything. On this
CPU that is strictly slower. **Do not trim `BOOTCLASSPATH`.**

The same reasoning removes the motivation for a `dex2oat` precompile step: the
boot image already exists, so cold boot is not paying for boot-class compilation.

## Using the tools

Both are read-only. `BASE` defaults to `/mnt/us/kindle-android`.

```sh
sh tuning/measure.sh     # per-stage segment table for the last boot
sh tuning/diagnose.sh    # where /system comes from, ART state, PM scan cost
```

`measure.sh` reads `entry.log` / `session.log` and splits the boot on the
`ANDROID_BOOT_STAGE` monotonic timestamps that `start.sh` already emits, so no
extra instrumentation is needed to get a segment table.

Take one measurement **before** applying the patch and one **after the second**
cold boot (the first boot after the patch only writes the digest cache; the
second one hits it). Compare segment by segment — an overall number hides which
change did what.

## Rollback

Revert the commit, or restore the two loops and the two hashes by hand. There is
no persistent state to clean up beyond the two `.sha-cache` files, which are
harmless if left behind.
