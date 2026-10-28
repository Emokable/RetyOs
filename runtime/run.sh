set -eu
ulimit -c 0
touch_probe_active=0
touch_was_locked=0
touch_guard_pid=
available=$(df -Pk /mnt/us | tail -n 1 | awk '{print $4}')
[ "$available" -ge 262144 ] || { echo 'insufficient user-storage space' >&2;exit 40; }
stage=$1
case "$stage" in /mnt/us/kindle-a7-art.??????) ;;*) exit 90;;esac
[ "$(readlink -f "$stage")" = "$stage" ]
awk '$2=="/" {if ($4 !~ /(^|,)ro(,|$)/) exit 1; seen=1} END {if (!seen) exit 2}' /proc/mounts
run_hwtcon_recovery(){
 unshare -m -f --propagation private sh "$stage/rootfs/tests/hwtcon-recovery.sh" "$stage" "$1"
}
cleanup(){
 if [ "$touch_probe_active" = 1 ];then
  if [ "$touch_was_locked" = 1 ];then
   echo lock > /proc/touch || { echo TOUCH_STATE_RESTORE_FAILED=locked >&2; return 94; }
   [ -z "$touch_guard_pid" ] || kill "$touch_guard_pid" 2>/dev/null || :
   rm -f "$stage/touch-lock-guard"
   echo TOUCH_STATE_RESTORED=locked >&2
  else
   echo TOUCH_STATE_RESTORED=unlocked >&2
  fi
  touch_probe_active=0
 fi
 if [ -f "$stage/hwtcon-backup-ready" ];then
  run_hwtcon_recovery --restore-full >&2 || { echo "HWTCON_RESTORE_FAILED=$stage; preserving backup and stage" >&2; return 92; }
  rm -f "$stage/hwtcon-backup-ready"
 fi
 if [ -f "$stage/data-loop-device" ];then
  loopdev=$(cat "$stage/data-loop-device")
  case "$loopdev" in /dev/loop[0-9]*) losetup -d "$loopdev" || { echo "ANDROID_DATA_LOOP_DETACH_FAILED=$loopdev" >&2; return 95; };; *) echo ANDROID_DATA_LOOP_DEVICE_INVALID=1 >&2; return 95;; esac
  rm -f "$stage/data-loop-device"
  echo ANDROID_DATA_LOOP_DETACHED=1 >&2
 fi
 rmdir "$stage/hwtcon-exec" 2>/dev/null || [ ! -d "$stage/hwtcon-exec" ]
 if [ -L "$stage/rootfs/sdcard" ];then rm -f "$stage/rootfs/sdcard";else rmdir "$stage/rootfs/sdcard" 2>/dev/null || [ ! -e "$stage/rootfs/sdcard" ] || { echo "ART_CLEANUP_REFUSED_NONEMPTY_SDCARD=$stage/rootfs/sdcard" >&2; return 91; };fi
 rm -f "$stage"/rootfs/art.marker
 rm -f "$stage"/rootfs/property_contexts
 rm -f "$stage"/rootfs/dev/binder
 rm -f "$stage"/rootfs/dev/null
 rm -f "$stage"/rootfs/dev/random
 rm -f "$stage"/rootfs/dev/urandom
 rm -f "$stage"/rootfs/dev/vndbinder
 rm -f "$stage"/rootfs/dev/zero
 rm -f "$stage"/rootfs/system/build.prop
 rm -f "$stage"/rootfs/system/bin/app_process32
 rm -f "$stage"/rootfs/system/bin/art-guard
 rm -f "$stage"/rootfs/system/bin/ashmem-test
 rm -f "$stage"/rootfs/system/bin/bionic-probe
 rm -f "$stage"/rootfs/system/bin/dalvikvm32
 rm -f "$stage"/rootfs/system/bin/init-client-helper
 rm -f "$stage"/rootfs/system/bin/init-container
 rm -f "$stage"/rootfs/system/bin/installd
 rm -f "$stage"/rootfs/system/bin/libbinder-client
 rm -f "$stage"/rootfs/system/bin/linker
 rm -f "$stage"/rootfs/system/bin/servicemanager-bionic
 rm -f "$stage"/rootfs/system/bin/sm-client-bionic
 rm -f "$stage"/rootfs/system/bin/toybox
 rm -f "$stage"/rootfs/system/etc/event-log-tags
 rm -f "$stage"/rootfs/system/etc/fonts.xml
 rm -f "$stage"/rootfs/system/etc/preloaded-classes
 rm -f "$stage"/rootfs/system/etc/public.libraries.txt
 rm -f "$stage"/rootfs/system/etc/permissions/android.hardware.camera.xml
 rm -f "$stage"/rootfs/system/etc/permissions/android.hardware.fingerprint.xml
 rm -f "$stage"/rootfs/system/etc/permissions/android.hardware.touchscreen.multitouch.jazzhand.xml
 rm -f "$stage"/rootfs/system/etc/permissions/android.hardware.wifi.xml
 rm -f "$stage"/rootfs/system/etc/permissions/android.software.live_wallpaper.xml
 rm -f "$stage"/rootfs/system/etc/permissions/android.software.webview.xml
 rm -f "$stage"/rootfs/system/etc/permissions/com.android.location.provider.xml
 rm -f "$stage"/rootfs/system/etc/permissions/com.android.media.remotedisplay.xml
 rm -f "$stage"/rootfs/system/etc/permissions/com.android.mediadrm.signer.xml
 rm -f "$stage"/rootfs/system/etc/permissions/com.google.android.maps.xml
 rm -f "$stage"/rootfs/system/etc/permissions/com.google.android.media.effects.xml
 rm -f "$stage"/rootfs/system/etc/permissions/handheld_core_hardware.xml
 rm -f "$stage"/rootfs/system/etc/permissions/platform.xml
 rm -f "$stage"/rootfs/system/etc/sysconfig/google.xml
 rm -f "$stage"/rootfs/system/etc/sysconfig/google_build.xml
 rm -f "$stage"/rootfs/system/fonts/AndroidClock.ttf
 rm -f "$stage"/rootfs/system/fonts/CarroisGothicSC-Regular.ttf
 rm -f "$stage"/rootfs/system/fonts/ComingSoon.ttf
 rm -f "$stage"/rootfs/system/fonts/CutiveMono.ttf
 rm -f "$stage"/rootfs/system/fonts/DancingScript-Bold.ttf
 rm -f "$stage"/rootfs/system/fonts/DancingScript-Regular.ttf
 rm -f "$stage"/rootfs/system/fonts/DroidSans-Bold.ttf
 rm -f "$stage"/rootfs/system/fonts/DroidSans.ttf
 rm -f "$stage"/rootfs/system/fonts/DroidSansMono.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoColorEmoji.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoNaskhArabic-Bold.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoNaskhArabic-Regular.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoNaskhArabicUI-Bold.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoNaskhArabicUI-Regular.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansArmenian-Bold.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansArmenian-Regular.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansBalinese-Regular.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansBamum-Regular.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansBatak-Regular.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansBengali-Bold.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansBengali-Regular.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansBengaliUI-Bold.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansBengaliUI-Regular.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansBuginese-Regular.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansBuhid-Regular.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansCanadianAboriginal-Regular.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansCham-Bold.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansCham-Regular.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansCherokee-Regular.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansCJK-Regular.ttc
 rm -f "$stage"/rootfs/system/fonts/NotoSansCoptic-Regular.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansDevanagari-Bold.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansDevanagari-Regular.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansDevanagariUI-Bold.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansDevanagariUI-Regular.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansEthiopic-Bold.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansEthiopic-Regular.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansGeorgian-Bold.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansGeorgian-Regular.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansGlagolitic-Regular.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansGujarati-Bold.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansGujarati-Regular.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansGujaratiUI-Bold.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansGujaratiUI-Regular.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansGurmukhi-Bold.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansGurmukhi-Regular.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansGurmukhiUI-Bold.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansGurmukhiUI-Regular.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansHanunoo-Regular.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansHebrew-Bold.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansHebrew-Regular.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansJavanese-Regular.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansKannada-Bold.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansKannada-Regular.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansKannadaUI-Bold.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansKannadaUI-Regular.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansKayahLi-Regular.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansKhmer-Bold.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansKhmer-Regular.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansKhmerUI-Bold.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansKhmerUI-Regular.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansLao-Bold.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansLao-Regular.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansLaoUI-Bold.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansLaoUI-Regular.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansLepcha-Regular.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansLimbu-Regular.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansLisu-Regular.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansMalayalam-Bold.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansMalayalam-Regular.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansMalayalamUI-Bold.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansMalayalamUI-Regular.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansMandaic-Regular.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansMeeteiMayek-Regular.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansMongolian-Regular.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansMyanmar-Bold.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansMyanmar-Regular.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansMyanmarUI-Bold.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansMyanmarUI-Regular.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansNewTaiLue-Regular.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansNKo-Regular.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansOlChiki-Regular.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansOriya-Bold.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansOriya-Regular.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansOriyaUI-Bold.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansOriyaUI-Regular.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansRejang-Regular.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansSaurashtra-Regular.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansSinhala-Bold.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansSinhala-Regular.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansSundanese-Regular.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansSylotiNagri-Regular.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansSymbols-Regular-Subsetted.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansSymbols-Regular-Subsetted2.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansSyriacEstrangela-Regular.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansTagbanwa-Regular.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansTaiLe-Regular.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansTaiTham-Regular.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansTaiViet-Regular.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansTamil-Bold.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansTamil-Regular.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansTamilUI-Bold.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansTamilUI-Regular.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansTelugu-Bold.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansTelugu-Regular.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansTeluguUI-Bold.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansTeluguUI-Regular.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansThaana-Bold.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansThaana-Regular.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansThai-Bold.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansThai-Regular.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansThaiUI-Bold.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansThaiUI-Regular.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansTibetan-Bold.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansTibetan-Regular.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansTifinagh-Regular.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansVai-Regular.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSansYi-Regular.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSerif-Bold.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSerif-BoldItalic.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSerif-Italic.ttf
 rm -f "$stage"/rootfs/system/fonts/NotoSerif-Regular.ttf
 rm -f "$stage"/rootfs/system/fonts/Roboto-Black.ttf
 rm -f "$stage"/rootfs/system/fonts/Roboto-BlackItalic.ttf
 rm -f "$stage"/rootfs/system/fonts/Roboto-Bold.ttf
 rm -f "$stage"/rootfs/system/fonts/Roboto-BoldItalic.ttf
 rm -f "$stage"/rootfs/system/fonts/Roboto-Italic.ttf
 rm -f "$stage"/rootfs/system/fonts/Roboto-Light.ttf
 rm -f "$stage"/rootfs/system/fonts/Roboto-LightItalic.ttf
 rm -f "$stage"/rootfs/system/fonts/Roboto-Medium.ttf
 rm -f "$stage"/rootfs/system/fonts/Roboto-MediumItalic.ttf
 rm -f "$stage"/rootfs/system/fonts/Roboto-Regular.ttf
 rm -f "$stage"/rootfs/system/fonts/Roboto-Thin.ttf
 rm -f "$stage"/rootfs/system/fonts/Roboto-ThinItalic.ttf
 rm -f "$stage"/rootfs/system/fonts/RobotoCondensed-Bold.ttf
 rm -f "$stage"/rootfs/system/fonts/RobotoCondensed-BoldItalic.ttf
 rm -f "$stage"/rootfs/system/fonts/RobotoCondensed-Italic.ttf
 rm -f "$stage"/rootfs/system/fonts/RobotoCondensed-Light.ttf
 rm -f "$stage"/rootfs/system/fonts/RobotoCondensed-LightItalic.ttf
 rm -f "$stage"/rootfs/system/fonts/RobotoCondensed-Regular.ttf
 rm -f "$stage"/rootfs/system/framework/am.jar
 rm -f "$stage"/rootfs/system/framework/android.test.runner.jar
 rm -f "$stage"/rootfs/system/framework/apache-xml.jar
 rm -f "$stage"/rootfs/system/framework/appwidget.jar
 rm -f "$stage"/rootfs/system/framework/bmgr.jar
 rm -f "$stage"/rootfs/system/framework/bouncycastle.jar
 rm -f "$stage"/rootfs/system/framework/bu.jar
 rm -f "$stage"/rootfs/system/framework/com.android.future.usb.accessory.jar
 rm -f "$stage"/rootfs/system/framework/com.android.location.provider.jar
 rm -f "$stage"/rootfs/system/framework/com.android.media.remotedisplay.jar
 rm -f "$stage"/rootfs/system/framework/com.android.mediadrm.signer.jar
 rm -f "$stage"/rootfs/system/framework/com.google.android.maps.jar
 rm -f "$stage"/rootfs/system/framework/com.google.android.media.effects.jar
 rm -f "$stage"/rootfs/system/framework/conscrypt.jar
 rm -f "$stage"/rootfs/system/framework/content.jar
 rm -f "$stage"/rootfs/system/framework/core-junit.jar
 rm -f "$stage"/rootfs/system/framework/core-libart.jar
 rm -f "$stage"/rootfs/system/framework/core-oj.jar
 rm -f "$stage"/rootfs/system/framework/dpm.jar
 rm -f "$stage"/rootfs/system/framework/ethernet-service.jar
 rm -f "$stage"/rootfs/system/framework/ext.jar
 rm -f "$stage"/rootfs/system/framework/framework-res.apk
 rm -f "$stage"/rootfs/system/framework/framework.jar
 rm -f "$stage"/rootfs/system/framework/hid.jar
 rm -f "$stage"/rootfs/system/framework/ime.jar
 rm -f "$stage"/rootfs/system/framework/ims-common.jar
 rm -f "$stage"/rootfs/system/framework/input.jar
 rm -f "$stage"/rootfs/system/framework/javax.obex.jar
 rm -f "$stage"/rootfs/system/framework/media_cmd.jar
 rm -f "$stage"/rootfs/system/framework/monkey.jar
 rm -f "$stage"/rootfs/system/framework/okhttp.jar
 rm -f "$stage"/rootfs/system/framework/org.apache.http.legacy.boot.jar
 rm -f "$stage"/rootfs/system/framework/pm.jar
 rm -f "$stage"/rootfs/system/framework/requestsync.jar
 rm -f "$stage"/rootfs/system/framework/services.jar
 rm -f "$stage"/rootfs/system/framework/settings.jar
 rm -f "$stage"/rootfs/system/framework/sm.jar
 rm -f "$stage"/rootfs/system/framework/svc.jar
 rm -f "$stage"/rootfs/system/framework/telecom.jar
 rm -f "$stage"/rootfs/system/framework/telephony-common.jar
 rm -f "$stage"/rootfs/system/framework/uiautomator.jar
 rm -f "$stage"/rootfs/system/framework/voip-common.jar
 rm -f "$stage"/rootfs/system/framework/wifi-service.jar
 rm -f "$stage"/rootfs/system/framework/wm.jar
 rm -f "$stage"/rootfs/system/framework/arm/boot-apache-xml.art
 rm -f "$stage"/rootfs/system/framework/arm/boot-apache-xml.oat
 rm -f "$stage"/rootfs/system/framework/arm/boot-bouncycastle.art
 rm -f "$stage"/rootfs/system/framework/arm/boot-bouncycastle.oat
 rm -f "$stage"/rootfs/system/framework/arm/boot-conscrypt.art
 rm -f "$stage"/rootfs/system/framework/arm/boot-conscrypt.oat
 rm -f "$stage"/rootfs/system/framework/arm/boot-core-junit.art
 rm -f "$stage"/rootfs/system/framework/arm/boot-core-junit.oat
 rm -f "$stage"/rootfs/system/framework/arm/boot-core-libart.art
 rm -f "$stage"/rootfs/system/framework/arm/boot-core-libart.oat
 rm -f "$stage"/rootfs/system/framework/arm/boot-ext.art
 rm -f "$stage"/rootfs/system/framework/arm/boot-ext.oat
 rm -f "$stage"/rootfs/system/framework/arm/boot-framework.art
 rm -f "$stage"/rootfs/system/framework/arm/boot-framework.oat
 rm -f "$stage"/rootfs/system/framework/arm/boot-ims-common.art
 rm -f "$stage"/rootfs/system/framework/arm/boot-ims-common.oat
 rm -f "$stage"/rootfs/system/framework/arm/boot-okhttp.art
 rm -f "$stage"/rootfs/system/framework/arm/boot-okhttp.oat
 rm -f "$stage"/rootfs/system/framework/arm/boot-org.apache.http.legacy.boot.art
 rm -f "$stage"/rootfs/system/framework/arm/boot-org.apache.http.legacy.boot.oat
 rm -f "$stage"/rootfs/system/framework/arm/boot-telephony-common.art
 rm -f "$stage"/rootfs/system/framework/arm/boot-telephony-common.oat
 rm -f "$stage"/rootfs/system/framework/arm/boot-voip-common.art
 rm -f "$stage"/rootfs/system/framework/arm/boot-voip-common.oat
 rm -f "$stage"/rootfs/system/framework/arm/boot.art
 rm -f "$stage"/rootfs/system/framework/arm/boot.oat
 rm -f "$stage"/rootfs/system/framework/oat/arm/am.odex
 rm -f "$stage"/rootfs/system/framework/oat/arm/android.test.runner.odex
 rm -f "$stage"/rootfs/system/framework/oat/arm/appwidget.odex
 rm -f "$stage"/rootfs/system/framework/oat/arm/bmgr.odex
 rm -f "$stage"/rootfs/system/framework/oat/arm/bu.odex
 rm -f "$stage"/rootfs/system/framework/oat/arm/com.android.future.usb.accessory.odex
 rm -f "$stage"/rootfs/system/framework/oat/arm/com.android.location.provider.odex
 rm -f "$stage"/rootfs/system/framework/oat/arm/com.android.media.remotedisplay.odex
 rm -f "$stage"/rootfs/system/framework/oat/arm/com.android.mediadrm.signer.odex
 rm -f "$stage"/rootfs/system/framework/oat/arm/com.google.android.maps.odex
 rm -f "$stage"/rootfs/system/framework/oat/arm/com.google.android.media.effects.odex
 rm -f "$stage"/rootfs/system/framework/oat/arm/content.odex
 rm -f "$stage"/rootfs/system/framework/oat/arm/dpm.odex
 rm -f "$stage"/rootfs/system/framework/oat/arm/ethernet-service.odex
 rm -f "$stage"/rootfs/system/framework/oat/arm/hid.odex
 rm -f "$stage"/rootfs/system/framework/oat/arm/ime.odex
 rm -f "$stage"/rootfs/system/framework/oat/arm/input.odex
 rm -f "$stage"/rootfs/system/framework/oat/arm/javax.obex.odex
 rm -f "$stage"/rootfs/system/framework/oat/arm/media_cmd.odex
 rm -f "$stage"/rootfs/system/framework/oat/arm/monkey.odex
 rm -f "$stage"/rootfs/system/framework/oat/arm/pm.odex
 rm -f "$stage"/rootfs/system/framework/oat/arm/requestsync.odex
 rm -f "$stage"/rootfs/system/framework/oat/arm/services.odex
 rm -f "$stage"/rootfs/system/framework/oat/arm/settings.odex
 rm -f "$stage"/rootfs/system/framework/oat/arm/sm.odex
 rm -f "$stage"/rootfs/system/framework/oat/arm/svc.odex
 rm -f "$stage"/rootfs/system/framework/oat/arm/telecom.odex
 rm -f "$stage"/rootfs/system/framework/oat/arm/uiautomator.odex
 rm -f "$stage"/rootfs/system/framework/oat/arm/wifi-service.odex
 rm -f "$stage"/rootfs/system/framework/oat/arm/wm.odex
 rm -f "$stage"/rootfs/system/lib/libandroid.so
 rm -f "$stage"/rootfs/system/lib/libandroidfw.so
 rm -f "$stage"/rootfs/system/lib/libandroid_runtime.so
 rm -f "$stage"/rootfs/system/lib/libandroid_servers.so
 rm -f "$stage"/rootfs/system/lib/libart-compiler.so
 rm -f "$stage"/rootfs/system/lib/libart.so
 rm -f "$stage"/rootfs/system/lib/libaudioutils.so
 rm -f "$stage"/rootfs/system/lib/libbacktrace.so
 rm -f "$stage"/rootfs/system/lib/libbase.so
 rm -f "$stage"/rootfs/system/lib/libbcinfo.so
 rm -f "$stage"/rootfs/system/lib/libbinary_parse.so
 rm -f "$stage"/rootfs/system/lib/libbinder.so
 rm -f "$stage"/rootfs/system/lib/libc++.so
 rm -f "$stage"/rootfs/system/lib/libc.so
 rm -f "$stage"/rootfs/system/lib/libcamera2ndk.so
 rm -f "$stage"/rootfs/system/lib/libcamera_client.so
 rm -f "$stage"/rootfs/system/lib/libcamera_metadata.so
 rm -f "$stage"/rootfs/system/lib/libcompiler_rt.so
 rm -f "$stage"/rootfs/system/lib/libcrypto.so
 rm -f "$stage"/rootfs/system/lib/libcutils.so
 rm -f "$stage"/rootfs/system/lib/libdl.so
 rm -f "$stage"/rootfs/system/lib/libdng_sdk.so
 rm -f "$stage"/rootfs/system/lib/libdrmframework.so
 rm -f "$stage"/rootfs/system/lib/libEGL.so
 rm -f "$stage"/rootfs/system/lib/libETC1.so
 rm -f "$stage"/rootfs/system/lib/libexif.so
 rm -f "$stage"/rootfs/system/lib/libexpat.so
 rm -f "$stage"/rootfs/system/lib/libft2.so
 rm -f "$stage"/rootfs/system/lib/libGLESv1_CM.so
 rm -f "$stage"/rootfs/system/lib/libGLESv2.so
 rm -f "$stage"/rootfs/system/lib/libGLESv3.so
 rm -f "$stage"/rootfs/system/lib/libgui.so
 rm -f "$stage"/rootfs/system/lib/libhardware.so
 rm -f "$stage"/rootfs/system/lib/libhardware_legacy.so
 rm -f "$stage"/rootfs/system/lib/libharfbuzz_ng.so
 rm -f "$stage"/rootfs/system/lib/libhwui.so
 rm -f "$stage"/rootfs/system/lib/libicui18n.so
 rm -f "$stage"/rootfs/system/lib/libicuuc.so
 rm -f "$stage"/rootfs/system/lib/libimage_type_recognition.so
 rm -f "$stage"/rootfs/system/lib/libimg_utils.so
 rm -f "$stage"/rootfs/system/lib/libinput.so
 rm -f "$stage"/rootfs/system/lib/libinputflinger.so
 rm -f "$stage"/rootfs/system/lib/libinputservice.so
 rm -f "$stage"/rootfs/system/lib/libjavacore.so
 rm -f "$stage"/rootfs/system/lib/libjavacrypto.so
 rm -f "$stage"/rootfs/system/lib/libjnigraphics.so
 rm -f "$stage"/rootfs/system/lib/libjpeg.so
 rm -f "$stage"/rootfs/system/lib/libkeymaster1.so
 rm -f "$stage"/rootfs/system/lib/libkeymaster_messages.so
 rm -f "$stage"/rootfs/system/lib/libkeystore-engine.so
 rm -f "$stage"/rootfs/system/lib/libkeystore_binder.so
 rm -f "$stage"/rootfs/system/lib/libkindleashmem.so
 rm -f "$stage"/rootfs/system/lib/libLLVM.so
 rm -f "$stage"/rootfs/system/lib/liblog.so
 rm -f "$stage"/rootfs/system/lib/liblogwrap.so
 rm -f "$stage"/rootfs/system/lib/liblz4.so
 rm -f "$stage"/rootfs/system/lib/liblzma.so
 rm -f "$stage"/rootfs/system/lib/libm.so
 rm -f "$stage"/rootfs/system/lib/libmedia.so
 rm -f "$stage"/rootfs/system/lib/libmediadrm.so
 rm -f "$stage"/rootfs/system/lib/libmediandk.so
 rm -f "$stage"/rootfs/system/lib/libmediautils.so
 rm -f "$stage"/rootfs/system/lib/libmedia_jni.so
 rm -f "$stage"/rootfs/system/lib/libmemtrack.so
 rm -f "$stage"/rootfs/system/lib/libmemunreachable.so
 rm -f "$stage"/rootfs/system/lib/libminikin.so
 rm -f "$stage"/rootfs/system/lib/libmtp.so
 rm -f "$stage"/rootfs/system/lib/libnativebridge.so
 rm -f "$stage"/rootfs/system/lib/libnativehelper.so
 rm -f "$stage"/rootfs/system/lib/libnativeloader.so
 rm -f "$stage"/rootfs/system/lib/libnbaio.so
 rm -f "$stage"/rootfs/system/lib/libnetd_client.so
 rm -f "$stage"/rootfs/system/lib/libnetutils.so
 rm -f "$stage"/rootfs/system/lib/libopenjdk.so
 rm -f "$stage"/rootfs/system/lib/libopenjdkjvm.so
 rm -f "$stage"/rootfs/system/lib/libOpenMAXAL.so
 rm -f "$stage"/rootfs/system/lib/libOpenSLES.so
 rm -f "$stage"/rootfs/system/lib/libopus.so
 rm -f "$stage"/rootfs/system/lib/libpackagelistparser.so
 rm -f "$stage"/rootfs/system/lib/libpcre.so
 rm -f "$stage"/rootfs/system/lib/libpdfium.so
 rm -f "$stage"/rootfs/system/lib/libpiex.so
 rm -f "$stage"/rootfs/system/lib/libpng.so
 rm -f "$stage"/rootfs/system/lib/libpowermanager.so
 rm -f "$stage"/rootfs/system/lib/libprocessgroup.so
 rm -f "$stage"/rootfs/system/lib/libprotobuf-cpp-lite.so
 rm -f "$stage"/rootfs/system/lib/libradio.so
 rm -f "$stage"/rootfs/system/lib/libradio_metadata.so
 rm -f "$stage"/rootfs/system/lib/libRS.so
 rm -f "$stage"/rootfs/system/lib/libRScpp.so
 rm -f "$stage"/rootfs/system/lib/libRS_internal.so
 rm -f "$stage"/rootfs/system/lib/libselinux.so
 rm -f "$stage"/rootfs/system/lib/libsensorservice.so
 rm -f "$stage"/rootfs/system/lib/libsigchain.so
 rm -f "$stage"/rootfs/system/lib/libskia.so
 rm -f "$stage"/rootfs/system/lib/libsoftkeymasterdevice.so
 rm -f "$stage"/rootfs/system/lib/libsonivox.so
 rm -f "$stage"/rootfs/system/lib/libsoundpool.so
 rm -f "$stage"/rootfs/system/lib/libsoundtrigger.so
 rm -f "$stage"/rootfs/system/lib/libspeexresampler.so
 rm -f "$stage"/rootfs/system/lib/libsqlite.so
 rm -f "$stage"/rootfs/system/lib/libssl.so
 rm -f "$stage"/rootfs/system/lib/libstagefright.so
 rm -f "$stage"/rootfs/system/lib/libstagefright_amrnb_common.so
 rm -f "$stage"/rootfs/system/lib/libstagefright_avc_common.so
 rm -f "$stage"/rootfs/system/lib/libstagefright_enc_common.so
 rm -f "$stage"/rootfs/system/lib/libstagefright_foundation.so
 rm -f "$stage"/rootfs/system/lib/libstagefright_http_support.so
 rm -f "$stage"/rootfs/system/lib/libstagefright_omx.so
 rm -f "$stage"/rootfs/system/lib/libstagefright_yuv.so
 rm -f "$stage"/rootfs/system/lib/libstdc++.so
 rm -f "$stage"/rootfs/system/lib/libsuspend.so
 rm -f "$stage"/rootfs/system/lib/libsync.so
 rm -f "$stage"/rootfs/system/lib/libtiff_directory.so
 rm -f "$stage"/rootfs/system/lib/libui.so
 rm -f "$stage"/rootfs/system/lib/libunwind.so
 rm -f "$stage"/rootfs/system/lib/libusbhost.so
 rm -f "$stage"/rootfs/system/lib/libutils.so
 rm -f "$stage"/rootfs/system/lib/libvixl.so
 rm -f "$stage"/rootfs/system/lib/libvorbisidec.so
 rm -f "$stage"/rootfs/system/lib/libvulkan.so
 rm -f "$stage"/rootfs/system/lib/libwebviewchromium_plat_support.so
 rm -f "$stage"/rootfs/system/lib/libwilhelm.so
 rm -f "$stage"/rootfs/system/lib/libwpa_client.so
 rm -f "$stage"/rootfs/system/lib/libz.so
 rm -f "$stage"/rootfs/system/usr/icu/icudt56l.dat
 rm -f "$stage"/rootfs/system/usr/idc/pt_mt.idc
 rm -f "$stage"/rootfs/system/usr/share/zoneinfo/tzdata
 rm -f "$stage"/rootfs/tests/Hello.dex
 rm -f "$stage"/rootfs/system/bin/zygote-namespace-enter
 rm -f "$stage"/rootfs/system/lib/libgetsockname-trace.so
 rm -f "$stage"/rootfs/system/bin/surfaceflinger-headless
 rm -f "$stage"/rootfs/system/bin/headless-battery
 rm -f "$stage"/rootfs/system/bin/sf-service-check
 rm -f "$stage"/rootfs/system/lib/libsurfaceflinger-headless.so
 rm -f "$stage"/rootfs/system/lib/libheadlesslog.so
 rm -f "$stage"/rootfs/system/lib/libpixelflinger-headless.so
 rm -f "$stage"/rootfs/system/lib/egl/libGLES_android.so
 rm -f "$stage"/rootfs/system/lib/hw/gralloc.default.so
 rm -f "$stage"/rootfs/system/lib/hw/hwcomposer.default.so
 rm -f "$stage"/rootfs/default.prop
 rm -f "$stage"/rootfs/system/app/GoogleExtShared/GoogleExtShared.apk
 rm -f "$stage"/rootfs/system/app/GoogleExtShared/oat/arm/GoogleExtShared.odex
 rm -f "$stage"/rootfs/system/etc/security/mac_permissions.xml
 rm -f "$stage"/rootfs/system/priv-app/GoogleExtServices/GoogleExtServices.apk
 rm -f "$stage"/rootfs/system/priv-app/GoogleExtServices/oat/arm/GoogleExtServices.odex
 rm -f "$stage"/rootfs/system/priv-app/GooglePackageInstaller/GooglePackageInstaller.apk
 rm -f "$stage"/rootfs/system/priv-app/GooglePackageInstaller/oat/arm/GooglePackageInstaller.odex
 rm -f "$stage"/rootfs/system/priv-app/SettingsProvider/SettingsProvider.apk
 rm -f "$stage"/rootfs/system/priv-app/SettingsProvider/oat/arm/SettingsProvider.odex
 rm -f "$stage"/rootfs/system/priv-app/StatementService/StatementService.apk
 rm -f "$stage"/rootfs/system/priv-app/StatementService/oat/arm/StatementService.odex
 rm -f "$stage"/rootfs/system/bin/audioserver
 rm -f "$stage"/rootfs/system/etc/audio_effects.conf
 rm -f "$stage"/rootfs/system/etc/audio_policy.conf
 rm -f "$stage"/rootfs/system/lib/libaudioflinger.so
 rm -f "$stage"/rootfs/system/lib/libaudiopolicyenginedefault.so
 rm -f "$stage"/rootfs/system/lib/libaudiopolicymanager.so
 rm -f "$stage"/rootfs/system/lib/libaudiopolicymanagerdefault.so
 rm -f "$stage"/rootfs/system/lib/libaudiopolicyservice.so
 rm -f "$stage"/rootfs/system/lib/libaudioresampler.so
 rm -f "$stage"/rootfs/system/lib/libaudiospdif.so
 rm -f "$stage"/rootfs/system/lib/libeffects.so
 rm -f "$stage"/rootfs/system/lib/libmedialogservice.so
 rm -f "$stage"/rootfs/system/lib/libradioservice.so
 rm -f "$stage"/rootfs/system/lib/libserviceutility.so
 rm -f "$stage"/rootfs/system/lib/libsonic.so
 rm -f "$stage"/rootfs/system/lib/libsoundtriggerservice.so
 rm -f "$stage"/rootfs/system/lib/hw/audio.primary.default.so
 rm -f "$stage"/rootfs/system/bin/netd
 rm -f "$stage"/rootfs/system/bin/ndc
 rm -f "$stage"/rootfs/system/bin/vold
 rm -f "$stage"/rootfs/system/bin/ip
 rm -f "$stage"/rootfs/system/bin/iptables
 rm -f "$stage"/rootfs/system/bin/ip6tables
 rm -f "$stage"/rootfs/system/bin/iptables-restore
 rm -f "$stage"/rootfs/system/bin/ip6tables-restore
 rm -f "$stage"/rootfs/system/lib/libdiskconfig.so
 rm -f "$stage"/rootfs/system/lib/libext2_uuid.so
 rm -f "$stage"/rootfs/system/lib/libext4_utils.so
 rm -f "$stage"/rootfs/system/lib/libf2fs_sparseblock.so
 rm -f "$stage"/rootfs/system/lib/libmdnssd.so
 rm -f "$stage"/rootfs/system/lib/libnetdaidl.so
 rm -f "$stage"/rootfs/system/lib/libnl.so
 rm -f "$stage"/rootfs/system/lib/libsoftkeymaster.so
 rm -f "$stage"/rootfs/system/lib/libsparse.so
 rm -f "$stage"/rootfs/system/lib/libsysutils.so
 rm -f "$stage"/rootfs/system/lib/libwifi-service.so
 rm -f "$stage"/rootfs/system/lib/libnetd-exit-trace.so
 rm -f "$stage"/rootfs/fstab.kindlelab
 rm -f "$stage"/rootfs/system/etc/kindlelab-apk-probe.apk
 rm -f "$stage"/rootfs/system/bin/sdcard
 rm -f "$stage"/rootfs/system/bin/kindlelab-fuse-mount-broker
 rm -f "$stage"/rootfs/system/app/WeRead/WeRead.apk
 rm -f "$stage"/rootfs/system/framework/kindlelab-package-dump.jar
 rm -f "$stage"/rootfs/system/app/WebViewGoogle/WebViewGoogle.apk
 rm -f "$stage"/rootfs/system/app/WebViewGoogle/oat/arm/WebViewGoogle.odex
 rm -f "$stage"/rootfs/system/lib/libwebviewchromium_loader.so
 rm -f "$stage"/rootfs/system/priv-app/DocumentsUI/DocumentsUI.apk
 rm -f "$stage"/rootfs/system/priv-app/DocumentsUI/oat/arm/DocumentsUI.odex
 rm -f "$stage"/rootfs/system/priv-app/DownloadProvider/DownloadProvider.apk
 rm -f "$stage"/rootfs/system/priv-app/DownloadProvider/oat/arm/DownloadProvider.odex
 rm -f "$stage"/rootfs/system/priv-app/ExternalStorageProvider/ExternalStorageProvider.apk
 rm -f "$stage"/rootfs/system/priv-app/ExternalStorageProvider/oat/arm/ExternalStorageProvider.odex
 rm -f "$stage"/rootfs/system/priv-app/MediaProvider/MediaProvider.apk
 rm -f "$stage"/rootfs/system/priv-app/MediaProvider/oat/arm/MediaProvider.odex
 rm -f "$stage"/rootfs/system/app/DownloadProviderUi/DownloadProviderUi.apk
 rm -f "$stage"/rootfs/system/app/DownloadProviderUi/oat/arm/DownloadProviderUi.odex
 rm -f "$stage"/rootfs/system/bin/apk-probe-runner
 rm -f "$stage"/rootfs/system/bin/keystore
 rm -f "$stage"/rootfs/system/lib/hw/keystore.default.so
 rm -f "$stage"/rootfs/system/priv-app/DefaultContainerService/DefaultContainerService.apk
 rm -f "$stage"/rootfs/system/priv-app/DefaultContainerService/oat/arm/DefaultContainerService.odex
 rm -f "$stage"/rootfs/system/bin/dex2oat
 rm -f "$stage"/rootfs/system/bin/patchoat
 rm -f "$stage"/rootfs/system/lib/libkindle-userxattr-compat.so
 rm -f "$stage"/rootfs/tests/textservices-null-stub.dex
 rm -f "$stage"/rootfs/tests/hwtcon-frame-probe
 rm -f "$stage"/rootfs/tests/hwtcon-recovery.sh
 rm -f "$stage"/result.txt
 rm -f "$stage"/exit-code.txt
 rm -f "$stage"/run.sh
 rm -f "$stage"/frame-capture/frame.rgba
 rm -f "$stage"/rootfs/dev/ptmx
 rm -f "$stage"/rootfs/tests/SystemServerBootstrap.dex
 rm -f "$stage"/rootfs/dev/__properties__/properties_serial
 rm -f "$stage"/rootfs/dev/__properties__/u:object_r:default_prop:s0
 rm -f "$stage"/rootfs/dev/socket/logdw
 rm -f "$stage"/rootfs/dev/socket/property_service
 rm -f "$stage"/rootfs/dev/socket/installd
 rm -f "$stage"/rootfs/dev/socket/zygote
 rm -f "$stage"/rootfs/data/.layout_version
 rm -f "$stage"/container-run.sh
 rm -f "$stage"/sdcard.log
 rm -f "$stage"/sdcard.pid
 rm -f "$stage"/rootfs/dev/fuse
 rm -f "$stage"/rootfs/mnt/sdcard
 rm -f "$stage"/rootfs/data/user/0
 rm -f "$stage"/rootfs/storage/self/primary
 rm -f "$stage"/rootfs/storage/emulated/0
 rmdir "$stage"/rootfs/system/priv-app/StatementService/oat/arm 2>/dev/null || [ ! -d "$stage"/rootfs/system/priv-app/StatementService/oat/arm ]
 rmdir "$stage"/rootfs/system/priv-app/SettingsProvider/oat/arm 2>/dev/null || [ ! -d "$stage"/rootfs/system/priv-app/SettingsProvider/oat/arm ]
 rmdir "$stage"/rootfs/system/priv-app/MediaProvider/oat/arm 2>/dev/null || [ ! -d "$stage"/rootfs/system/priv-app/MediaProvider/oat/arm ]
 rmdir "$stage"/rootfs/system/priv-app/GooglePackageInstaller/oat/arm 2>/dev/null || [ ! -d "$stage"/rootfs/system/priv-app/GooglePackageInstaller/oat/arm ]
 rmdir "$stage"/rootfs/system/priv-app/GoogleExtServices/oat/arm 2>/dev/null || [ ! -d "$stage"/rootfs/system/priv-app/GoogleExtServices/oat/arm ]
 rmdir "$stage"/rootfs/system/priv-app/ExternalStorageProvider/oat/arm 2>/dev/null || [ ! -d "$stage"/rootfs/system/priv-app/ExternalStorageProvider/oat/arm ]
 rmdir "$stage"/rootfs/system/priv-app/DownloadProvider/oat/arm 2>/dev/null || [ ! -d "$stage"/rootfs/system/priv-app/DownloadProvider/oat/arm ]
 rmdir "$stage"/rootfs/system/priv-app/DocumentsUI/oat/arm 2>/dev/null || [ ! -d "$stage"/rootfs/system/priv-app/DocumentsUI/oat/arm ]
 rmdir "$stage"/rootfs/system/priv-app/DefaultContainerService/oat/arm 2>/dev/null || [ ! -d "$stage"/rootfs/system/priv-app/DefaultContainerService/oat/arm ]
 rmdir "$stage"/rootfs/system/app/WebViewGoogle/oat/arm 2>/dev/null || [ ! -d "$stage"/rootfs/system/app/WebViewGoogle/oat/arm ]
 rmdir "$stage"/rootfs/system/app/GoogleExtShared/oat/arm 2>/dev/null || [ ! -d "$stage"/rootfs/system/app/GoogleExtShared/oat/arm ]
 rmdir "$stage"/rootfs/system/app/DownloadProviderUi/oat/arm 2>/dev/null || [ ! -d "$stage"/rootfs/system/app/DownloadProviderUi/oat/arm ]
 rmdir "$stage"/rootfs/system/usr/share/zoneinfo 2>/dev/null || [ ! -d "$stage"/rootfs/system/usr/share/zoneinfo ]
 rmdir "$stage"/rootfs/system/priv-app/StatementService/oat 2>/dev/null || [ ! -d "$stage"/rootfs/system/priv-app/StatementService/oat ]
 rmdir "$stage"/rootfs/system/priv-app/SettingsProvider/oat 2>/dev/null || [ ! -d "$stage"/rootfs/system/priv-app/SettingsProvider/oat ]
 rmdir "$stage"/rootfs/system/priv-app/MediaProvider/oat 2>/dev/null || [ ! -d "$stage"/rootfs/system/priv-app/MediaProvider/oat ]
 rmdir "$stage"/rootfs/system/priv-app/GooglePackageInstaller/oat 2>/dev/null || [ ! -d "$stage"/rootfs/system/priv-app/GooglePackageInstaller/oat ]
 rmdir "$stage"/rootfs/system/priv-app/GoogleExtServices/oat 2>/dev/null || [ ! -d "$stage"/rootfs/system/priv-app/GoogleExtServices/oat ]
 rmdir "$stage"/rootfs/system/priv-app/ExternalStorageProvider/oat 2>/dev/null || [ ! -d "$stage"/rootfs/system/priv-app/ExternalStorageProvider/oat ]
 rmdir "$stage"/rootfs/system/priv-app/DownloadProvider/oat 2>/dev/null || [ ! -d "$stage"/rootfs/system/priv-app/DownloadProvider/oat ]
 rmdir "$stage"/rootfs/system/priv-app/DocumentsUI/oat 2>/dev/null || [ ! -d "$stage"/rootfs/system/priv-app/DocumentsUI/oat ]
 rmdir "$stage"/rootfs/system/priv-app/DefaultContainerService/oat 2>/dev/null || [ ! -d "$stage"/rootfs/system/priv-app/DefaultContainerService/oat ]
 rmdir "$stage"/rootfs/system/framework/oat/arm 2>/dev/null || [ ! -d "$stage"/rootfs/system/framework/oat/arm ]
 rmdir "$stage"/rootfs/system/app/WebViewGoogle/oat 2>/dev/null || [ ! -d "$stage"/rootfs/system/app/WebViewGoogle/oat ]
 rmdir "$stage"/rootfs/system/app/GoogleExtShared/oat 2>/dev/null || [ ! -d "$stage"/rootfs/system/app/GoogleExtShared/oat ]
 rmdir "$stage"/rootfs/system/app/DownloadProviderUi/oat 2>/dev/null || [ ! -d "$stage"/rootfs/system/app/DownloadProviderUi/oat ]
 rmdir "$stage"/rootfs/data/system/users/0 2>/dev/null || [ ! -d "$stage"/rootfs/data/system/users/0 ]
 rmdir "$stage"/rootfs/data/misc/zoneinfo/current 2>/dev/null || [ ! -d "$stage"/rootfs/data/misc/zoneinfo/current ]
 rmdir "$stage"/rootfs/data/misc/user/0 2>/dev/null || [ ! -d "$stage"/rootfs/data/misc/user/0 ]
 rmdir "$stage"/rootfs/system/vendor/app 2>/dev/null || [ ! -d "$stage"/rootfs/system/vendor/app ]
 rmdir "$stage"/rootfs/system/usr/share 2>/dev/null || [ ! -d "$stage"/rootfs/system/usr/share ]
 rmdir "$stage"/rootfs/system/usr/idc 2>/dev/null || [ ! -d "$stage"/rootfs/system/usr/idc ]
 rmdir "$stage"/rootfs/system/usr/icu 2>/dev/null || [ ! -d "$stage"/rootfs/system/usr/icu ]
 rmdir "$stage"/rootfs/system/priv-app/StatementService 2>/dev/null || [ ! -d "$stage"/rootfs/system/priv-app/StatementService ]
 rmdir "$stage"/rootfs/system/priv-app/SettingsProvider 2>/dev/null || [ ! -d "$stage"/rootfs/system/priv-app/SettingsProvider ]
 rmdir "$stage"/rootfs/system/priv-app/MediaProvider 2>/dev/null || [ ! -d "$stage"/rootfs/system/priv-app/MediaProvider ]
 rmdir "$stage"/rootfs/system/priv-app/GooglePackageInstaller 2>/dev/null || [ ! -d "$stage"/rootfs/system/priv-app/GooglePackageInstaller ]
 rmdir "$stage"/rootfs/system/priv-app/GoogleExtServices 2>/dev/null || [ ! -d "$stage"/rootfs/system/priv-app/GoogleExtServices ]
 rmdir "$stage"/rootfs/system/priv-app/ExternalStorageProvider 2>/dev/null || [ ! -d "$stage"/rootfs/system/priv-app/ExternalStorageProvider ]
 rmdir "$stage"/rootfs/system/priv-app/DownloadProvider 2>/dev/null || [ ! -d "$stage"/rootfs/system/priv-app/DownloadProvider ]
 rmdir "$stage"/rootfs/system/priv-app/DocumentsUI 2>/dev/null || [ ! -d "$stage"/rootfs/system/priv-app/DocumentsUI ]
 rmdir "$stage"/rootfs/system/priv-app/DefaultContainerService 2>/dev/null || [ ! -d "$stage"/rootfs/system/priv-app/DefaultContainerService ]
 rmdir "$stage"/rootfs/system/lib/hw 2>/dev/null || [ ! -d "$stage"/rootfs/system/lib/hw ]
 rmdir "$stage"/rootfs/system/lib/egl 2>/dev/null || [ ! -d "$stage"/rootfs/system/lib/egl ]
 rmdir "$stage"/rootfs/system/framework/oat 2>/dev/null || [ ! -d "$stage"/rootfs/system/framework/oat ]
 rmdir "$stage"/rootfs/system/framework/arm 2>/dev/null || [ ! -d "$stage"/rootfs/system/framework/arm ]
 rmdir "$stage"/rootfs/system/etc/sysconfig 2>/dev/null || [ ! -d "$stage"/rootfs/system/etc/sysconfig ]
 rmdir "$stage"/rootfs/system/etc/security 2>/dev/null || [ ! -d "$stage"/rootfs/system/etc/security ]
 rmdir "$stage"/rootfs/system/etc/permissions 2>/dev/null || [ ! -d "$stage"/rootfs/system/etc/permissions ]
 rmdir "$stage"/rootfs/system/app/WebViewGoogle 2>/dev/null || [ ! -d "$stage"/rootfs/system/app/WebViewGoogle ]
 rmdir "$stage"/rootfs/system/app/WeRead 2>/dev/null || [ ! -d "$stage"/rootfs/system/app/WeRead ]
 rmdir "$stage"/rootfs/system/app/GoogleExtShared 2>/dev/null || [ ! -d "$stage"/rootfs/system/app/GoogleExtShared ]
 rmdir "$stage"/rootfs/system/app/DownloadProviderUi 2>/dev/null || [ ! -d "$stage"/rootfs/system/app/DownloadProviderUi ]
 rmdir "$stage"/rootfs/dev/block/vold 2>/dev/null || [ ! -d "$stage"/rootfs/dev/block/vold ]
 rmdir "$stage"/rootfs/data/system_de/0 2>/dev/null || [ ! -d "$stage"/rootfs/data/system_de/0 ]
 rmdir "$stage"/rootfs/data/system_ce/0 2>/dev/null || [ ! -d "$stage"/rootfs/data/system_ce/0 ]
 rmdir "$stage"/rootfs/data/system/users 2>/dev/null || [ ! -d "$stage"/rootfs/data/system/users ]
 rmdir "$stage"/rootfs/data/misc/zoneinfo 2>/dev/null || [ ! -d "$stage"/rootfs/data/misc/zoneinfo ]
 rmdir "$stage"/rootfs/data/misc/wifi 2>/dev/null || [ ! -d "$stage"/rootfs/data/misc/wifi ]
 rmdir "$stage"/rootfs/data/misc/user 2>/dev/null || [ ! -d "$stage"/rootfs/data/misc/user ]
 rmdir "$stage"/rootfs/data/misc/net 2>/dev/null || [ ! -d "$stage"/rootfs/data/misc/net ]
 rmdir "$stage"/rootfs/data/media/0 2>/dev/null || [ ! -d "$stage"/rootfs/data/media/0 ]
 rmdir "$stage"/rootfs/data/local/tmp 2>/dev/null || [ ! -d "$stage"/rootfs/data/local/tmp ]
 rmdir "$stage"/rootfs/data/dalvik-cache/arm 2>/dev/null || [ ! -d "$stage"/rootfs/data/dalvik-cache/arm ]
 rmdir "$stage"/rootfs/system/vendor 2>/dev/null || [ ! -d "$stage"/rootfs/system/vendor ]
 rmdir "$stage"/rootfs/system/usr 2>/dev/null || [ ! -d "$stage"/rootfs/system/usr ]
 rmdir "$stage"/rootfs/system/priv-app 2>/dev/null || [ ! -d "$stage"/rootfs/system/priv-app ]
 rmdir "$stage"/rootfs/system/lib 2>/dev/null || [ ! -d "$stage"/rootfs/system/lib ]
 rmdir "$stage"/rootfs/system/framework 2>/dev/null || [ ! -d "$stage"/rootfs/system/framework ]
 rmdir "$stage"/rootfs/system/fonts 2>/dev/null || [ ! -d "$stage"/rootfs/system/fonts ]
 rmdir "$stage"/rootfs/system/etc 2>/dev/null || [ ! -d "$stage"/rootfs/system/etc ]
 rmdir "$stage"/rootfs/system/bin 2>/dev/null || [ ! -d "$stage"/rootfs/system/bin ]
 rmdir "$stage"/rootfs/system/app 2>/dev/null || [ ! -d "$stage"/rootfs/system/app ]
 rmdir "$stage"/rootfs/storage/self 2>/dev/null || [ ! -d "$stage"/rootfs/storage/self ]
 rmdir "$stage"/rootfs/storage/emulated 2>/dev/null || [ ! -d "$stage"/rootfs/storage/emulated ]
 rmdir "$stage"/rootfs/mnt/asec 2>/dev/null || [ ! -d "$stage"/rootfs/mnt/asec ]
 rmdir "$stage"/rootfs/dev/socket 2>/dev/null || [ ! -d "$stage"/rootfs/dev/socket ]
 rmdir "$stage"/rootfs/dev/pts 2>/dev/null || [ ! -d "$stage"/rootfs/dev/pts ]
 rmdir "$stage"/rootfs/dev/input 2>/dev/null || [ ! -d "$stage"/rootfs/dev/input ]
 rmdir "$stage"/rootfs/dev/block 2>/dev/null || [ ! -d "$stage"/rootfs/dev/block ]
 rmdir "$stage"/rootfs/dev/__properties__ 2>/dev/null || [ ! -d "$stage"/rootfs/dev/__properties__ ]
 rmdir "$stage"/rootfs/data/user 2>/dev/null || [ ! -d "$stage"/rootfs/data/user ]
 rmdir "$stage"/rootfs/data/system_de 2>/dev/null || [ ! -d "$stage"/rootfs/data/system_de ]
 rmdir "$stage"/rootfs/data/system_ce 2>/dev/null || [ ! -d "$stage"/rootfs/data/system_ce ]
 rmdir "$stage"/rootfs/data/system 2>/dev/null || [ ! -d "$stage"/rootfs/data/system ]
 rmdir "$stage"/rootfs/data/misc 2>/dev/null || [ ! -d "$stage"/rootfs/data/misc ]
 rmdir "$stage"/rootfs/data/media 2>/dev/null || [ ! -d "$stage"/rootfs/data/media ]
 rmdir "$stage"/rootfs/data/local 2>/dev/null || [ ! -d "$stage"/rootfs/data/local ]
 rmdir "$stage"/rootfs/data/dalvik-cache 2>/dev/null || [ ! -d "$stage"/rootfs/data/dalvik-cache ]
 rmdir "$stage"/rootfs/data/cache 2>/dev/null || [ ! -d "$stage"/rootfs/data/cache ]
 rmdir "$stage"/rootfs/data/app-private 2>/dev/null || [ ! -d "$stage"/rootfs/data/app-private ]
 rmdir "$stage"/rootfs/data/app 2>/dev/null || [ ! -d "$stage"/rootfs/data/app ]
 rmdir "$stage"/rootfs/tmp 2>/dev/null || [ ! -d "$stage"/rootfs/tmp ]
 rmdir "$stage"/rootfs/tests 2>/dev/null || [ ! -d "$stage"/rootfs/tests ]
 rmdir "$stage"/rootfs/system 2>/dev/null || [ ! -d "$stage"/rootfs/system ]
 rmdir "$stage"/rootfs/storage 2>/dev/null || [ ! -d "$stage"/rootfs/storage ]
 rmdir "$stage"/rootfs/sdcard 2>/dev/null || [ ! -d "$stage"/rootfs/sdcard ]
 rmdir "$stage"/rootfs/proc 2>/dev/null || [ ! -d "$stage"/rootfs/proc ]
 rmdir "$stage"/rootfs/mnt 2>/dev/null || [ ! -d "$stage"/rootfs/mnt ]
 rmdir "$stage"/rootfs/dev 2>/dev/null || [ ! -d "$stage"/rootfs/dev ]
 rmdir "$stage"/rootfs/data 2>/dev/null || [ ! -d "$stage"/rootfs/data ]
 rmdir "$stage"/rootfs/capture-out 2>/dev/null || [ ! -d "$stage"/rootfs/capture-out ]
 rmdir "$stage"/rootfs/cache 2>/dev/null || [ ! -d "$stage"/rootfs/cache ]
 rmdir "$stage"/rootfs 2>/dev/null || [ ! -d "$stage"/rootfs ]
 rmdir "$stage"/frame-capture 2>/dev/null || [ ! -d "$stage"/frame-capture ]
 extras=$(find "$stage" -type f -print)
 [ -z "$extras" ] || { echo "ART_CLEANUP_REFUSED_NONEMPTY=$stage" >&2; return 91; }
 find "$stage" -depth -type d -exec rmdir {} \;
 echo "ART_CLEANUP_COMPLETE=$stage" >&2
}
trap cleanup EXIT
if [ "${2:-}" = --cleanup-only ];then exit 0;fi
vnd_id=$(stat -Lc '%t:%T' /dev/vndbinder)
for fd in /proc/[0-9]*/fd/*;do
 link=$(readlink "$fd" 2>/dev/null)||continue
 case "$link" in *binder*)
  id=$(stat -Lc '%t:%T' "$fd" 2>/dev/null)||continue
  [ "$id" != "$vnd_id" ] || { echo "VNDBINDER_BUSY=$fd" >&2;exit 42; };;esac
done
if [ "${2:-}" = --hwtcon-display-probe ] || [ "${2:-}" = --android-session-probe ];then
 run_hwtcon_recovery --snapshot-full >&2
 if ! touch "$stage/hwtcon-backup-ready";then
  run_hwtcon_recovery --restore-full >&2 || { echo "HWTCON_RESTORE_FAILED=$stage; preserving backup and stage" >&2; exit 92; }
  exit 93
 fi
 echo HWTCON_FULL_FRAME_SNAPSHOT_READY=1 >&2
fi
trap 'exit 130' HUP INT TERM
if [ "${2:-}" = --hwtcon-display-probe ] || [ "${2:-}" = --android-session-probe ];then
 touch_state=$(cat /proc/touch)
 case "$touch_state" in
  "Touch is locked")
   touch_was_locked=1
   : > "$stage/touch-lock-guard"
   nohup sh -c 'sleep 14460; if [ -f "$1" ]; then echo lock > /proc/touch; rm -f "$1"; fi' sh "$stage/touch-lock-guard" >/dev/null 2>&1 </dev/null &
   touch_guard_pid=$!
   touch_probe_active=1
   echo unlock > /proc/touch
   [ "$(cat /proc/touch)" = "Touch is unlocked" ] || { echo TOUCH_UNLOCK_FAILED=1 >&2; exit 46; }
   echo TOUCH_UNLOCK_TEMPORARY=1 >&2 ;;
  "Touch is unlocked") touch_probe_active=1; echo TOUCH_ALREADY_UNLOCKED=1 >&2 ;;
  *) echo "TOUCH_STATE_UNKNOWN=$touch_state" >&2; exit 47 ;;
 esac
fi
mkdir -p "$stage/rootfs/proc" "$stage/rootfs/data" "$stage/rootfs/tmp"
KINDLE_CONTAINER_ROOT="$stage/rootfs"
KINDLE_CONTAINER_STAGE="$stage"
export KINDLE_CONTAINER_ROOT KINDLE_CONTAINER_STAGE
set +e
(
 ulimit -v 1048576
 cat >"$stage/container-run.sh" <<'KINDLELAB_CONTAINER_RUN_EOF'
 set -eu
 root=${KINDLE_CONTAINER_ROOT:?ANDROID_CONTAINER_ROOT_NOT_SET}
 stage=${KINDLE_CONTAINER_STAGE:?ANDROID_CONTAINER_STAGE_NOT_SET}
 export KINDLE_ANDROID_SESSION=1
 if [ -f /mnt/us/kindle-android/autolaunch-weread ]; then export KINDLELAB_AUTOLAUNCH_WEREAD=1; export KINDLELAB_TEST_WEREAD_DATA_UPDATE=1; echo ANDROID_WEREAD_AUTOLAUNCH_TEST=1 weread_data_update_experiment=1; fi
 echo "ANDROID_CONTAINER_ROOT_ARGUMENT=$root" >&2
 mount -t proc -o ro,nosuid,nodev,noexec proc "$root/proc"
 mount -t tmpfs -o size=4m,mode=0755,nosuid tmpfs "$root/dev"
 mkdir -p "$root/dev/pts"
 mount -t devpts -o newinstance,ptmxmode=0666,mode=0620,gid=5 devpts "$root/dev/pts"
 ln -sf pts/ptmx "$root/dev/ptmx"
 for dev in null urandom zero random;do touch "$root/dev/$dev";done
 mkdir -p "$root/cache" "$root/data/cache" "$root/data/misc/net" "$root/data/misc/wifi"
 mount --bind "$root/data/cache" "$root/cache"
 mkdir -p "$root/dev/input"
 for dev in null urandom zero random;do mount --bind "/dev/$dev" "$root/dev/$dev";done
 hex_major=$(stat -Lc %t /dev/vndbinder)
 hex_minor=$(stat -Lc %T /dev/vndbinder)
 binder_major=$((0x$hex_major))
 binder_minor=$((0x$hex_minor))
 mknod "$root/dev/binder" c "$binder_major" "$binder_minor"
 mknod "$root/dev/vndbinder" c "$binder_major" "$binder_minor"
 chmod 0666 "$root/dev/binder" "$root/dev/vndbinder"
 echo PRIVATE_BINDER_NODES=$binder_major:$binder_minor

 if [ "${KINDLE_ANDROID_SESSION:-0}" = 1 ];then
  data_image=/mnt/us/kindle-android/data.img
  [ -f "$data_image" ] && [ "$(stat -c '%s' "$data_image")" = 1610612736 ] || { echo ANDROID_DATA_IMAGE_INVALID=1 expected=1610612736 >&2; exit 48; }
  data_loop=$(losetup -f)
  losetup "$data_loop" "$data_image"
  echo "$data_loop" > "$stage/data-loop-device"
  mount -t ext4 -o rw,nosuid,nodev "$data_loop" "$root/data"
  export KINDLE_ANDROID_DATA_PERSISTENT=1
  echo "ANDROID_DATA_IMAGE_MOUNTED=1 device=$data_loop size_bytes=1610612736" >&2
 else
  mount -t tmpfs -o size=640m,mode=0755,nodev,nosuid tmpfs "$root/data"
 fi
 mkdir -p "$root/data/dalvik-cache/arm" "$root/data/local/tmp" "$root/data/system" "$root/data/system_ce/0" "$root/data/system_de/0" "$root/data/misc/shared_relro" "$root/data/misc_ce/0" "$root/data/misc_de/0" "$root/data/misc/user/0" "$root/data/misc/zoneinfo/current" "$root/data/app" "$root/data/app-private" "$root/data/system/users/0" "$root/data/user_de/0" "$root/data/anr" "$root/system/app" "$root/system/priv-app" "$root/system/vendor/app" "$root/mnt/asec" "$root/storage" "$root/sdcard"
 chmod 0711 "$root/data/user_de" "$root/data/user_de/0"
 chmod 01771 "$root/data/misc_ce" "$root/data/misc_de"
 chmod 0771 "$root/data/misc/shared_relro" "$root/data/misc_ce/0" "$root/data/misc_de/0"
 chown 1037:1037 "$root/data/misc/shared_relro" || echo WEBVIEW_SHARED_RELRO_OWNER_WARNING=1
 chown 1000:9998 "$root/data/misc_ce" "$root/data/misc_de" 2>/dev/null || :
 chown 1000:1000 "$root/data/misc_ce/0" "$root/data/misc_de/0" 2>/dev/null || :
 chown 1000:1000 "$root/data/anr"
 chmod 0770 "$root/data/anr"
 mkdir -p "$root/data/media/0"
 chmod 0770 "$root/data/media" "$root/data/media/0"
 chown 1023:1023 "$root/data/media" "$root/data/media/0"
 [ -d "$root/data" ] && [ -w "$root/data" ] || { echo ANDROID_DATA_MOUNT_UNAVAILABLE=1 >&2; exit 48; }
 [ -e "$root/data/.layout_version" ] || printf 2 > "$root/data/.layout_version"
 mount -t tmpfs -o size=4m,mode=0755,uid=0,gid=1000,nodev,nosuid tmpfs "$root/mnt"
 mount -t tmpfs -o size=4m,mode=0755,nodev,nosuid tmpfs "$root/storage"
 mkdir -p "$root/mnt/asec" "$root/mnt/expand" "$root/mnt/media_rw" "$root/mnt/user/0" "$root/mnt/runtime/default/self" "$root/mnt/runtime/default/emulated/0" "$root/mnt/runtime/read/self" "$root/mnt/runtime/read/emulated/0" "$root/mnt/runtime/write/self" "$root/mnt/runtime/write/emulated/0" "$root/mnt/runtime/full/self" "$root/storage/self" "$root/storage/emulated" "$root/data/anr"
 chmod 0771 "$root/data/dalvik-cache" "$root/data/dalvik-cache/arm"
 chown 0:1000 "$root/data/dalvik-cache" "$root/data/dalvik-cache/arm"
 chmod 0777 "$root/data/local/tmp"
 mount --bind "$root/data/local/tmp" "$root/tmp"
 mount --bind "$root/system" "$root/system"
 mount -o remount,bind,ro "$root/system"
 touch_hex_major=$(stat -Lc %t /dev/input/event1)
 touch_hex_minor=$(stat -Lc %T /dev/input/event1)
 touch_major=$((0x$touch_hex_major))
 touch_minor=$((0x$touch_hex_minor))
 touch_name=$(cat /sys/class/input/event1/device/name)
 [ "$touch_name" = pt_mt ] || { echo "TOUCH_DEVICE_NAME_MISMATCH=$touch_name"; exit 45; }
 mknod "$root/dev/input/event1" c "$touch_major" "$touch_minor"
 chown 1000:1000 "$root/dev/input/event1"
 chmod 0600 "$root/dev/input/event1"
 echo "TOUCH_DEVICE_NODE_CREATED=1 name=$touch_name major=$touch_major minor=$touch_minor owner=1000:1000 mode=0600"
 mkdir -p "$stage/frame-capture" "$root/capture-out"
 mount --bind "$stage/frame-capture" "$root/capture-out"
 touch "$root/dev/fb0"
 mount --bind /dev/fb0 "$root/dev/fb0"
 export KINDLE_HWTCON_CAPTURE=1
 export KINDLE_HWTCON_FULL_REFRESH=1
 export KINDLE_ANDROID_SESSION=1
 export KINDLELAB_EXCLUSIVE_TOUCH=1
 echo "ANDROID_DATA_SETUP_BEGIN root=$root"
 ls -ld "$root/data" 2>&1 || :
 awk -v p="$root/data" '$2==p {print "ANDROID_DATA_MOUNTINFO=" $0; found=1} END {if (!found) print "ANDROID_DATA_MOUNTINFO=missing"}' /proc/mounts
 mkdir -p "$root/data/user"
 if [ -L "$root/data/data" ]; then
  legacy_target=$(readlink "$root/data/data")
  [ "$legacy_target" = /data/user/0 ] || { echo ANDROID_DATA_LEGACY_LINK_UNEXPECTED=1 >&2; exit 50; }
  if [ -L "$root/data/user/0" ]; then
   user0_target=$(readlink "$root/data/user/0")
   [ "$user0_target" = /data/data ] || { echo ANDROID_DATA_USER0_LINK_UNEXPECTED=1 >&2; exit 50; }
   rm -f "$root/data/user/0"
  elif [ -e "$root/data/user/0" ]; then
   [ -d "$root/data/user/0" ] || { echo ANDROID_DATA_USER0_TYPE_UNEXPECTED=1 >&2; exit 50; }
   rm -f "$root/data/data"
   mv "$root/data/user/0" "$root/data/data"
   echo ANDROID_DATA_LEGACY_CONTENTS_MIGRATED=1
  fi
  [ ! -L "$root/data/data" ] || rm -f "$root/data/data"
 fi
 if [ ! -e "$root/data/data" ]; then
  if [ -d "$root/data/user/0" ] && [ ! -L "$root/data/user/0" ]; then
   mv "$root/data/user/0" "$root/data/data"
   echo ANDROID_DATA_USER0_CONTENTS_MIGRATED=1
  else
   mkdir "$root/data/data"
  fi
 elif [ -L "$root/data/data" ] || [ ! -d "$root/data/data" ]; then
  echo ANDROID_DATA_DIRECTORY_TYPE_UNEXPECTED=1 >&2
  exit 50
 elif [ -d "$root/data/user/0" ] && [ ! -L "$root/data/user/0" ]; then
  echo ANDROID_DATA_BOTH_LAYOUTS_CONTAIN_DIRECTORIES=1 >&2
  exit 50
 fi
 if [ -L "$root/data/user/0" ]; then
  [ "$(readlink "$root/data/user/0")" = /data/data ] || { echo ANDROID_DATA_USER0_SYMLINK_MISMATCH=1 >&2; exit 49; }
 elif [ -e "$root/data/user/0" ]; then
  echo ANDROID_DATA_USER0_PATH_CONFLICT=1 >&2
  exit 49
 else
  ln -s /data/data "$root/data/user/0"
 fi
 mkdir -p "$root/data/user_de" "$root/data/user_de/0"
 if ! mkdir -p "$root/data/data/com.android.providers.settings" "$root/data/data/com.android.providers.settings/databases"; then
  echo ANDROID_DATA_DIRECTORY_CREATE_FAILED=1 >&2
  ls -ld "$root" "$root/data" "$root/data/data" "$root/data/user/0" 2>&1 || :
  awk -v p="$root/data" '$2==p || index($2,p "/")==1 {print "ANDROID_DATA_MOUNTINFO=" $0}' /proc/mounts
  exit 50
 fi
 chmod 0771 "$root/data/data"
 chown 1000:1000 "$root/data/data"
 chmod 0711 "$root/data/user" "$root/data/user_de" "$root/data/user_de/0"
 chown 1000:1000 "$root/data/user" "$root/data/user_de"
 mkdir -p "$root/mnt/runtime/default/emulated" "$root/mnt/runtime/read/emulated" "$root/mnt/runtime/write/emulated" "$root/mnt/user/0" "$root/storage/self" "$root/storage/emulated" "$root/data/media/0"
 chmod 0755 "$root/mnt"
 chown 0:1000 "$root/mnt"
 chmod 0700 "$root/mnt/runtime"
 chown 0:0 "$root/mnt/runtime"
 chmod 0755 "$root/mnt/runtime/default" "$root/mnt/runtime/default/self" "$root/mnt/runtime/read" "$root/mnt/runtime/read/self" "$root/mnt/runtime/write" "$root/mnt/runtime/write/self" "$root/mnt/user" "$root/mnt/user/0"
 chown 0:0 "$root/mnt/runtime/default" "$root/mnt/runtime/default/self" "$root/mnt/runtime/read" "$root/mnt/runtime/read/self" "$root/mnt/runtime/write" "$root/mnt/runtime/write/self" "$root/mnt/user" "$root/mnt/user/0"
 rmdir "$root/sdcard" 2>/dev/null || :
 rmdir "$root/mnt/sdcard" 2>/dev/null || :
 ln -sfn /mnt/runtime/default/emulated/0 "$root/mnt/user/0/primary"
 ln -sfn /mnt/user/0/primary "$root/storage/self/primary"
 ln -sfn /mnt/runtime/default/emulated/0 "$root/storage/emulated/0"
 ln -sfn /storage/emulated/0 "$root/sdcard"
 ln -sfn /storage/emulated/0 "$root/mnt/sdcard"
 chmod 0770 "$root/data/media" "$root/data/media/0"
 chown 1023:1023 "$root/data/media" "$root/data/media/0"
 touch "$root/dev/fuse"
 mount --bind /dev/fuse "$root/dev/fuse"
 echo ANDROID_FUSE_DEVICE_BOUND=1
 echo ANDROID_DATA_LAYOUT_READY=data/data,data/user/0
 echo ANDROID_MNT_LAYOUT_READY=mode-0755-uid-0-gid-1000
 echo ANDROID_SDCARD_DAEMON_START=vold-volume-mount
 mount --rbind "$root" "$root"
 mount --make-rshared "$root"
 [ -c /dev/vndbinder ] || exit 44
 [ -c "$root/dev/binder" ] || mknod -m 0666 "$root/dev/binder" c 10 121
 exec "$stage/rootfs/system/bin/zygote-namespace-enter" --chroot-zygote-system-server-apk-probe "$root" /system/bin/toybox env ANDROID_ROOT=/system ANDROID_DATA=/data ANDROID_STORAGE=/storage EXTERNAL_STORAGE=/sdcard ASEC_MOUNTPOINT=/mnt/asec SYSTEMSERVERCLASSPATH=/system/framework/services.jar:/system/framework/ethernet-service.jar:/system/framework/wifi-service.jar LD_LIBRARY_PATH=/system/lib LD_PRELOAD=/system/lib/libkindleashmem.so KINDLE_ZYGOTE_HEADLESS_SF=1 KINDLE_AUDIO_PROBE=1 KINDLE_NETD_PROBE=1 KINDLE_VOLD_PROBE=1 KINDLE_KEYSTORE_PROBE=1 BOOTCLASSPATH=/system/framework/core-oj.jar:/system/framework/core-libart.jar:/system/framework/conscrypt.jar:/system/framework/okhttp.jar:/system/framework/core-junit.jar:/system/framework/bouncycastle.jar:/system/framework/ext.jar:/system/framework/framework.jar:/system/framework/telephony-common.jar:/system/framework/voip-common.jar:/system/framework/ims-common.jar:/system/framework/apache-xml.jar:/system/framework/org.apache.http.legacy.boot.jar /system/bin/art-guard --zygote-system-server-probe < /dev/null
KINDLELAB_CONTAINER_RUN_EOF
chmod 0700 "$stage/container-run.sh"
unshare -m -p -i -u -n -f --propagation private sh "$stage/container-run.sh"
) >"$stage/result.txt" 2>&1 &
probe_pid=$!
while kill -0 "$probe_pid" 2>/dev/null;do
 if grep -q 'APK_ACTIVITY_PROCESS_LAUNCH_PASS=1' "$stage/result.txt";then echo ANDROID_SESSION_ACTIVITY_PROCESS_STARTED=1 >&2;break;fi
 sleep 1
done
wait "$probe_pid"
rc=$?
set -e
printf '%s\n' "$rc" >"$stage/exit-code.txt"
awk '$2=="/" {print}' /proc/mounts >>"$stage/result.txt"
free -m >>"$stage/result.txt" 2>&1
 if [ -f "$stage/frame-capture/frame.rgba" ];then
  tar -cf - -C "$stage" result.txt exit-code.txt frame-capture/frame.rgba
 else
  tar -cf - -C "$stage" result.txt exit-code.txt
 fi
printf 'ART_STAGING_CLEANUP=%s\n' "$stage" >&2
