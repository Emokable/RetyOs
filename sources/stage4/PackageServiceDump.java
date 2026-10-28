package org.kindlelab.diagnostics;

import android.os.IBinder;
import android.content.ComponentName;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageInfo;

import java.io.FileDescriptor;
import java.lang.reflect.Method;
import java.lang.reflect.InvocationTargetException;

/** Invoke the package service's built-in dump from the isolated root process. */
public final class PackageServiceDump {
    private PackageServiceDump() {}

    public static void main(String[] args) throws Exception {
        String packageName = args.length == 0 ? "com.tencent.weread.eink" : args[0];
        System.out.println("WEREAD_PACKAGE_SERVICE_DUMP_BEGIN package=" + packageName);
        Class<?> serviceManager = Class.forName("android.os.ServiceManager");
        Method getService = serviceManager.getDeclaredMethod("getService", String.class);
        getService.setAccessible(true);
        IBinder service = (IBinder) getService.invoke(null, "package");
        if (service == null) {
            System.out.println("WEREAD_PACKAGE_SERVICE_DUMP_NO_SERVICE=1");
            System.exit(2);
            return;
        }
        if (args.length > 1 && "api-only-check".equals(args[1])) {
            boolean visible = isDefaultLauncherVisible(service, packageName);
            System.out.println("ROOT_PMS_DEFAULT_LAUNCHER_VISIBLE=" + visible);
            if (!visible) System.exit(27);
            return;
        }
        dumpPackageManagerApi(service, packageName);
        if (args.length < 2 || !"api-only".equals(args[1]))
            service.dump(FileDescriptor.out, new String[] { packageName });
        System.out.println("WEREAD_PACKAGE_SERVICE_DUMP_END=1");
    }

    private static boolean isDefaultLauncherVisible(IBinder binder, String packageName) {
        try {
            Class<?> stub = Class.forName("android.content.pm.IPackageManager$Stub");
            Method asInterface = stub.getMethod("asInterface", IBinder.class);
            Object packageManager = asInterface.invoke(null, binder);
            Class<?> api = Class.forName("android.content.pm.IPackageManager");
            Method getActivityInfo = api.getMethod(
                    "getActivityInfo", ComponentName.class, Integer.TYPE, Integer.TYPE);
            ComponentName component = new ComponentName(packageName,
                    "com.tencent.weread.LauncherActivity");
            ActivityInfo info = (ActivityInfo) getActivityInfo.invoke(
                    packageManager, component, 0, 0);
            Method query = api.getMethod("queryIntentActivities", Intent.class,
                    String.class, Integer.TYPE, Integer.TYPE);
            Intent intent = new Intent(Intent.ACTION_MAIN);
            intent.addCategory(Intent.CATEGORY_LAUNCHER);
            intent.setPackage(packageName);
            Object slice = query.invoke(packageManager, intent, null, 0, 0);
            Method getList = slice.getClass().getMethod("getList");
            Object list = getList.invoke(slice);
            return info != null && !((java.util.List<?>) list).isEmpty();
        } catch (Exception error) {
            System.out.println("ROOT_PMS_DEFAULT_LAUNCHER_CHECK_ERROR=" + error);
            return false;
        }
    }

    private static void dumpPackageManagerApi(IBinder binder, String packageName)
            throws Exception {
        Class<?> stub = Class.forName("android.content.pm.IPackageManager$Stub");
        Method asInterface = stub.getMethod("asInterface", IBinder.class);
        Object packageManager = asInterface.invoke(null, binder);
        Class<?> packageManagerInterface = Class.forName("android.content.pm.IPackageManager");
        Method getPackageInfo = packageManagerInterface.getMethod(
                "getPackageInfo", String.class, Integer.TYPE, Integer.TYPE);
        Method getApplicationInfo = packageManagerInterface.getMethod(
                "getApplicationInfo", String.class, Integer.TYPE, Integer.TYPE);
        Method getActivityInfo = packageManagerInterface.getMethod(
                "getActivityInfo", ComponentName.class, Integer.TYPE, Integer.TYPE);
        Method activitySupportsIntent = packageManagerInterface.getMethod(
                "activitySupportsIntent", ComponentName.class, Intent.class, String.class);
        Method getComponentEnabledSetting = packageManagerInterface.getMethod(
                "getComponentEnabledSetting", ComponentName.class, Integer.TYPE);
        Method getApplicationEnabledSetting = packageManagerInterface.getMethod(
                "getApplicationEnabledSetting", String.class, Integer.TYPE);
        int[] flags = new int[] {0, 1, 0x200, 0x2000, 0x2201,
                0x40000, 0x80000, 0xC0000,
                0x40001, 0x80001, 0xC0001, 0xC0201, 0xC2201};
        String[] packages = new String[] {packageName, "org.kindlelab.apkprobe"};
        String[][] activities = new String[][] {{"com.tencent.weread.LauncherActivity",
                "com.tencent.weread.LaunchExternalSchema"},
                {"org.kindlelab.apkprobe.ApkProbeActivity"}};
        for (int packageIndex = 0; packageIndex < packages.length; packageIndex++) {
            String name = packages[packageIndex];
            ComponentName component = new ComponentName(name, activities[packageIndex][0]);
            try {
                ApplicationInfo info = (ApplicationInfo) getApplicationInfo.invoke(
                        packageManager, name, 0, 0);
                System.out.println("ROOT_PMS_APPLICATION_INFO package=" + name +
                        " state=" + (info == null ? "null" : "present") +
                        (info == null ? "" : " enabled=" + info.enabled +
                        " installed=" + ((info.flags & ApplicationInfo.FLAG_INSTALLED) != 0) +
                        " sourceDir=" + info.sourceDir));
            } catch (InvocationTargetException error) {
                System.out.println("ROOT_PMS_APPLICATION_INFO_ERROR package=" + name +
                        " cause=" + error.getCause());
            }
            Intent main = new Intent(Intent.ACTION_MAIN);
            main.addCategory(Intent.CATEGORY_LAUNCHER);
            try {
                System.out.println("ROOT_PMS_ACTIVITY_SUPPORTS_MAIN package=" + name +
                        " component=" + component.flattenToShortString() + " result=" +
                        activitySupportsIntent.invoke(packageManager, component, main, null));
            } catch (InvocationTargetException error) {
                System.out.println("ROOT_PMS_ACTIVITY_SUPPORTS_MAIN_ERROR package=" + name +
                        " cause=" + error.getCause());
            }
            try {
                System.out.println("ROOT_PMS_COMPONENT_ENABLED_SETTING package=" + name +
                        " component=" + component.flattenToShortString() + " value=" +
                        getComponentEnabledSetting.invoke(packageManager, component, 0));
                System.out.println("ROOT_PMS_APPLICATION_ENABLED_SETTING package=" + name +
                        " value=" + getApplicationEnabledSetting.invoke(packageManager, name, 0));
            } catch (InvocationTargetException error) {
                System.out.println("ROOT_PMS_ENABLED_SETTING_ERROR package=" + name +
                        " cause=" + error.getCause());
            }
            for (int value : flags) {
                try {
                    PackageInfo info = (PackageInfo) getPackageInfo.invoke(
                            packageManager, name, value, 0);
                    System.out.println("ROOT_PMS_PACKAGE_INFO package=" + name +
                            " flags=0x" + Integer.toHexString(value) + " activities=" +
                            (info == null ? "package-null" : info.activities == null ?
                            "null" : info.activities.length));
                } catch (InvocationTargetException error) {
                    Throwable cause = error.getCause();
                    System.out.println("ROOT_PMS_PACKAGE_INFO_ERROR package=" + name +
                            " flags=0x" + Integer.toHexString(value) + " cause=" + cause);
                }
                try {
                    ActivityInfo info = (ActivityInfo) getActivityInfo.invoke(
                            packageManager, component, value, 0);
                    if (info == null)
                        System.out.println("ROOT_PMS_ACTIVITY_NULL package=" + name +
                                " flags=0x" + Integer.toHexString(value));
                    else
                        System.out.println("ROOT_PMS_ACTIVITY package=" + name +
                                " flags=0x" + Integer.toHexString(value) + " name=" +
                                info.name + " enabled=" + info.enabled +
                                " exported=" + info.exported);
                } catch (InvocationTargetException error) {
                    Throwable cause = error.getCause();
                    System.out.println("ROOT_PMS_ACTIVITY_ERROR package=" + name +
                            " flags=0x" + Integer.toHexString(value) + " cause=" + cause);
                }
            }
            for (int activityIndex = 1; activityIndex < activities[packageIndex].length;
                    activityIndex++) {
                ComponentName extra = new ComponentName(name,
                        activities[packageIndex][activityIndex]);
                try {
                    ActivityInfo info = (ActivityInfo) getActivityInfo.invoke(
                            packageManager, extra, 0x2201, 0);
                    System.out.println("ROOT_PMS_EXTRA_ACTIVITY package=" + name +
                            " component=" + extra.flattenToShortString() + " result=" +
                            (info == null ? "null" : info.name + ":" + info.enabled));
                } catch (InvocationTargetException error) {
                    System.out.println("ROOT_PMS_EXTRA_ACTIVITY_ERROR package=" + name +
                            " component=" + extra.flattenToShortString() + " cause=" +
                            error.getCause());
                }
            }
            for (int type = 1; type <= 8; type <<= 1) {
                try {
                    PackageInfo info = (PackageInfo) getPackageInfo.invoke(
                            packageManager, name, type, 0);
                    int count = info == null ? -1 : type == 1 ?
                            (info.activities == null ? -1 : info.activities.length) :
                            type == 2 ? (info.receivers == null ? -1 : info.receivers.length) :
                            type == 4 ? (info.services == null ? -1 : info.services.length) :
                            (info.providers == null ? -1 : info.providers.length);
                    System.out.println("ROOT_PMS_COMPONENT_ARRAY package=" + name +
                            " type=0x" + Integer.toHexString(type) + " count=" + count);
                } catch (InvocationTargetException error) {
                    System.out.println("ROOT_PMS_COMPONENT_ARRAY_ERROR package=" + name +
                            " type=0x" + Integer.toHexString(type) + " cause=" +
                            error.getCause());
                }
            }
        }
        dumpRootIntentQuery(packageManagerInterface, packageManager, packageName);
        dumpRootIntentQuery(packageManagerInterface, packageManager, packageName, 0x40000);
        dumpRootIntentQuery(packageManagerInterface, packageManager, packageName, 0x80000);
        dumpRootIntentQuery(packageManagerInterface, packageManager, packageName, 0xC0000);
    }

    private static void dumpRootIntentQuery(Class<?> api, Object packageManager,
                                            String packageName) {
        dumpRootIntentQuery(api, packageManager, packageName, 0);
    }

    private static void dumpRootIntentQuery(Class<?> api, Object packageManager,
                                            String packageName, int flags) {
        try {
            Method query = api.getMethod("queryIntentActivities", Intent.class,
                    String.class, Integer.TYPE, Integer.TYPE);
            Intent intent = new Intent(Intent.ACTION_MAIN);
            intent.addCategory(Intent.CATEGORY_LAUNCHER);
            intent.setPackage(packageName);
            Object slice = query.invoke(packageManager, intent, null, flags, 0);
            Method getList = slice.getClass().getMethod("getList");
            Object list = getList.invoke(slice);
            System.out.println("ROOT_PMS_LAUNCHER_QUERY package=" + packageName +
                    " flags=0x" + Integer.toHexString(flags) + " count=" +
                    ((java.util.List<?>) list).size());
        } catch (Exception error) {
            System.out.println("ROOT_PMS_LAUNCHER_QUERY_ERROR=" + error);
        }
    }
}
