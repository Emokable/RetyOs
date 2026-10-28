# Third-party notices

This repository contains files derived from or distributed with
other projects. Existing copyright notices and SPDX identifiers in those files
remain in force. The root MIT license does not replace them.

## Android Open Source Project (AOSP)

Some files in `sources/local-audio/` and other source files retain AOSP copyright
and Apache License 2.0 notices. See the headers in each file and the official
Apache License 2.0 text at <https://www.apache.org/licenses/LICENSE-2.0>.

## Rime Pinyin Simplified data

`sources/stage4/minimal/assets/pinyin.tsv` is third-party dictionary data. The
associated author list and Apache License 2.0 text are retained in `third_party/`
and alongside the asset. Do not treat this data as MIT-licensed project code.

## slirp4netns

The Kindle runtime's slirp4netns executable is intentionally not included.
The original device package contains its upstream license and checksum; obtain
the matching ARMv7 release from its upstream project and verify its checksum
before packaging it.

## Included upstream source

AOSP files used in the native builds are included in `sources/aosp7/` with
their file notices. Apache 2.0 text is retained in `LICENSES/Apache-2.0.txt`.
`sources/device-kernel-colorsoft/` contains GPL-2.0-only MediaTek/Linux
interface and display/touch source; see its notices and `LICENSES/GPL-2.0.txt`.
The MIT license does not cover these files or change their obligations.

`sources/swiftshader-2018/` contains upstream commit
`8e6ed2e35bce87eccd69569840a81c49669b6cb6` plus Android adaptations. Its
Apache license and LLVM/Subzero notices remain in that tree.

`config/cacert.pem` contains public Mozilla/curl trust anchors under MPL 2.0.
The build records their SHA-256 and emits the corresponding notice. This is
public CA data, not a private key or personal certificate.

The Android runtime image and binary tools are externally locked inputs, not
committed source assets. Obtain them from their providers under the applicable
terms. Firmware, third-party reader APKs and user data are excluded.
