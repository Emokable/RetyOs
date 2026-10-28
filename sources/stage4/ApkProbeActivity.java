package org.kindlelab.apkprobe;

import android.app.Activity;
import android.app.ActivityManager;
import android.content.Intent;
import android.content.ComponentName;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.content.pm.ResolveInfo;
import android.os.UserManager;
import android.os.UserHandle;
import android.os.Process;
import java.lang.reflect.Method;
import android.content.pm.ApplicationInfo;
import android.content.pm.ActivityInfo;
import android.graphics.Color;
import android.graphics.Typeface;
import android.os.Bundle;
import android.util.Log;
import android.view.Gravity;
import android.view.View;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;

import java.io.FileOutputStream;
import android.net.LocalSocket;
import android.net.LocalSocketAddress;
import java.nio.charset.StandardCharsets;
import java.util.List;

/** Minimal eink-friendly HOME shell for the isolated Kindle Android session. */
public final class ApkProbeActivity extends Activity {
    private static final String TAG = "KindleAndroidDesktop";
    private static final String WEREAD_PACKAGE = "com.tencent.weread.eink";
    private TextView status;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON |
                WindowManager.LayoutParams.FLAG_FULLSCREEN);
        getWindow().getDecorView().setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION |
                View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY | View.SYSTEM_UI_FLAG_LAYOUT_STABLE);

        LinearLayout page = new LinearLayout(this);
        page.setOrientation(LinearLayout.VERTICAL);
        page.setGravity(Gravity.CENTER_HORIZONTAL);
        page.setPadding(28, 24, 28, 18);
        page.setBackgroundColor(Color.WHITE);

        TextView title = new TextView(this);
        title.setText("Android \u9605\u8bfb\u684c\u9762");
        title.setTextColor(Color.BLACK);
        title.setTextSize(30.0f);
        title.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        title.setGravity(Gravity.CENTER);
        page.addView(title, new LinearLayout.LayoutParams(-1, 105));

        TextView subtitle = new TextView(this);
        subtitle.setText("Kindle ColorSoft  ·  Android 7.1");
        subtitle.setTextColor(Color.DKGRAY);
        subtitle.setTextSize(17.0f);
        subtitle.setGravity(Gravity.CENTER);
        page.addView(subtitle, new LinearLayout.LayoutParams(-1, 70));

        status = new TextView(this);
        status.setTextColor(Color.BLACK);
        status.setTextSize(18.0f);
        status.setGravity(Gravity.CENTER);
        status.setPadding(12, 16, 12, 16);
        status.setBackgroundColor(0xffeeeeee);
        page.addView(status, new LinearLayout.LayoutParams(-1, 110));

        Button reader = new Button(this);
        reader.setText("\u5fae\u4fe1\u8bfb\u4e66\u58a8\u6c34\u5c4f\u7248");
        reader.setTextSize(23.0f);
        reader.setAllCaps(false);
        LinearLayout.LayoutParams readerParams = new LinearLayout.LayoutParams(-1, 150);
        readerParams.setMargins(0, 48, 0, 12);
        page.addView(reader, readerParams);
        reader.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View view) { openWeRead(); }
        });

        TextView hint = new TextView(this);
        hint.setText("\u70b9\u6309\u4e0a\u65b9\u6309\u94ae\u6253\u5f00\u5fae\u4fe1\u8bfb\u4e66\u3002\n\u5e94\u7528\u8fd0\u884c\u5728\u9694\u79bb Android \u4f1a\u8bdd\u4e2d\u3002");
        hint.setTextColor(Color.DKGRAY);
        hint.setTextSize(16.0f);
        hint.setGravity(Gravity.CENTER);
        page.addView(hint, new LinearLayout.LayoutParams(-1, 0, 1.0f));

        Button exit = new Button(this);
        exit.setText("\u9000\u51fa Android\uff0c\u8fd4\u56de Kindle");
        exit.setTextSize(19.0f);
        exit.setAllCaps(false);
        exit.setTextColor(Color.WHITE);
        exit.setBackgroundColor(0xffa62028);
        page.addView(exit, new LinearLayout.LayoutParams(-1, 120));
        exit.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View view) { requestContainerExit(); }
        });

        setContentView(page);
        updateReaderStatus();
        try (FileOutputStream out = createDeviceProtectedStorageContext()
                .openFileOutput("probe.txt", MODE_PRIVATE)) {
            out.write("ANDROID_DESKTOP_ACTIVITY_CREATED\n".getBytes(StandardCharsets.UTF_8));
            Log.i(TAG, "ANDROID_DESKTOP_ACTIVITY_CREATED files=" +
                    createDeviceProtectedStorageContext().getFilesDir());
        } catch (Exception e) {
            Log.e(TAG, "ANDROID_DESKTOP_ACTIVITY_FAILED", e);
            status.setText("Android 桌面已启动；应用数据目录暂不可写");
        }
        if (getIntent().getBooleanExtra("kindlelab-autolaunch-weread", false)) {
            Log.i(TAG, "WEREAD_AUTO_LAUNCH_FROM_DESKTOP=1");
            openWeRead();
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        updateReaderStatus();
        sendControl("showbar");
    }

    @Override
    protected void onPause() {
        sendControl("hidebar");
        super.onPause();
    }

    private void sendControl(final String command) {
        new Thread(new Runnable() {
            @Override public void run() {
                LocalSocket socket = new LocalSocket();
                try {
                    socket.connect(new LocalSocketAddress("kindle-controls",
                            LocalSocketAddress.Namespace.ABSTRACT));
                    socket.getOutputStream().write((command + "\n").getBytes("UTF-8"));
                    socket.getOutputStream().flush();
                } catch (Exception error) {
                    Log.w(TAG, "CONTROL_REQUEST_FAILED=" + command + ":" + error);
                } finally {
                    try { socket.close(); } catch (Exception ignored) { }
                }
            }
        }, "kindle-control-" + command).start();
    }

    private void updateReaderStatus() {
        try {
            PackageInfo info = getPackageManager().getPackageInfo(WEREAD_PACKAGE, 0);
            status.setText("\u5fae\u4fe1\u8bfb\u4e66\u58a8\u6c34\u5c4f\u7248\u5df2\u5b89\u88c5  \u00b7  " + info.versionName);
        } catch (PackageManager.NameNotFoundException e) {
            status.setText("\u5fae\u4fe1\u8bfb\u4e66\u58a8\u6c34\u5c4f\u7248\u5c1a\u672a\u5b89\u88c5");
        }
    }

    private void openWeRead() {
        PackageManager pm = getPackageManager();
        UserManager userManager = (UserManager) getSystemService(USER_SERVICE);
        Log.i(TAG, "ANDROID_PROCESS_UID=" + Process.myUid() +
                " process_user=" + (Process.myUid() / 100000));
        try {
            Method currentUser = ActivityManager.class.getDeclaredMethod("getCurrentUser");
            currentUser.setAccessible(true);
            Log.i(TAG, "ANDROID_ACTIVITY_MANAGER_CURRENT_USER=" +
                    currentUser.invoke(null));
        } catch (Exception error) {
            Log.i(TAG, "ANDROID_ACTIVITY_MANAGER_CURRENT_USER_QUERY_ERROR=" +
                    error.getClass().getSimpleName() + ":" + error.getMessage());
        }
        Log.i(TAG, "ANDROID_USER_UNLOCKED=" +
                (userManager != null && userManager.isUserUnlocked()));
        try {
            Method userHandleOf = UserHandle.class.getDeclaredMethod("of", Integer.TYPE);
            userHandleOf.setAccessible(true);
            Object processUser = userHandleOf.invoke(null,
                    Integer.valueOf(Process.myUid() / 100000));
            Method userRunning = UserManager.class.getDeclaredMethod(
                    "isUserRunning", UserHandle.class);
            Method userUnlocked = UserManager.class.getDeclaredMethod(
                    "isUserUnlocked", UserHandle.class);
            userRunning.setAccessible(true);
            userUnlocked.setAccessible(true);
            Log.i(TAG, "ANDROID_PROCESS_USER_RUNNING=" +
                    userRunning.invoke(userManager, processUser));
            Log.i(TAG, "ANDROID_PROCESS_USER_UNLOCKED=" +
                    userUnlocked.invoke(userManager, processUser));
        } catch (Exception error) {
            Log.i(TAG, "ANDROID_PROCESS_USER_STATE_QUERY_ERROR=" +
                    error.getClass().getSimpleName() + ":" + error.getMessage());
        }
        try {
            Class<?> storageManager = Class.forName("android.os.storage.StorageManager");
            Method keyUnlocked = storageManager.getDeclaredMethod(
                    "isUserKeyUnlocked", Integer.TYPE);
            keyUnlocked.setAccessible(true);
            Log.i(TAG, "ANDROID_STORAGE_USER_KEY_UNLOCKED=" +
                    keyUnlocked.invoke(null, Integer.valueOf(0)));
        } catch (Exception error) {
            Log.i(TAG, "ANDROID_STORAGE_USER_KEY_UNLOCKED_QUERY_ERROR=" +
                    error.getClass().getSimpleName() + ":" + error.getMessage());
        }
        Log.i(TAG, "WEREAD_STORAGE_PERMISSION_READ=" +
                pm.checkPermission("android.permission.READ_EXTERNAL_STORAGE", WEREAD_PACKAGE));
        Log.i(TAG, "WEREAD_STORAGE_PERMISSION_WRITE=" +
                pm.checkPermission("android.permission.WRITE_EXTERNAL_STORAGE", WEREAD_PACKAGE));
        ComponentName launcherComponent = new ComponentName(
                WEREAD_PACKAGE, "com.tencent.weread.LauncherActivity");
        int[] infoFlags = new int[] {0, PackageManager.GET_ACTIVITIES,
                PackageManager.MATCH_DISABLED_COMPONENTS,
                PackageManager.MATCH_UNINSTALLED_PACKAGES,
                PackageManager.GET_ACTIVITIES | PackageManager.MATCH_DISABLED_COMPONENTS,
                PackageManager.GET_ACTIVITIES | PackageManager.MATCH_UNINSTALLED_PACKAGES,
                PackageManager.GET_ACTIVITIES | PackageManager.MATCH_DISABLED_COMPONENTS |
                        PackageManager.MATCH_UNINSTALLED_PACKAGES,
                0x40000, 0x80000, 0xC0000,
                PackageManager.GET_ACTIVITIES | 0x40000,
                PackageManager.GET_ACTIVITIES | 0x80000,
                PackageManager.GET_ACTIVITIES | 0xC0000,
                PackageManager.GET_ACTIVITIES | PackageManager.MATCH_DISABLED_COMPONENTS |
                        PackageManager.MATCH_UNINSTALLED_PACKAGES | 0xC0000};
        for (int flags : infoFlags) {
            try {
                PackageInfo packageInfo = pm.getPackageInfo(WEREAD_PACKAGE, flags);
                ApplicationInfo app = packageInfo.applicationInfo;
                Log.i(TAG, "WEREAD_PACKAGE_INFO flags=0x" + Integer.toHexString(flags) +
                        " appEnabled=" + app.enabled + " appFlags=0x" +
                        Integer.toHexString(app.flags) + " installed=" +
                        ((app.flags & ApplicationInfo.FLAG_INSTALLED) != 0) +
                        " targetSdk=" + app.targetSdkVersion + " sourceDir=" + app.sourceDir +
                        " activities=" + (packageInfo.activities == null ? "null" :
                        Integer.toString(packageInfo.activities.length)));
            } catch (PackageManager.NameNotFoundException e) {
                Log.e(TAG, "WEREAD_PACKAGE_INFO_MISSING flags=0x" +
                        Integer.toHexString(flags), e);
            }
        }
        int[] componentFlags = new int[] {0, PackageManager.MATCH_DISABLED_COMPONENTS,
                PackageManager.MATCH_UNINSTALLED_PACKAGES,
                PackageManager.MATCH_DISABLED_COMPONENTS |
                        PackageManager.MATCH_UNINSTALLED_PACKAGES,
                0x40000, 0x80000, 0xC0000,
                PackageManager.MATCH_DISABLED_COMPONENTS | 0xC0000,
                PackageManager.MATCH_UNINSTALLED_PACKAGES | 0xC0000};
        for (int flags : componentFlags) {
            try {
                ActivityInfo launcher = pm.getActivityInfo(launcherComponent, flags);
                Log.i(TAG, "WEREAD_COMPONENT_FOUND flags=0x" +
                        Integer.toHexString(flags) + " enabled=" + launcher.enabled +
                        " exported=" + launcher.exported + " name=" + launcher.name);
            } catch (PackageManager.NameNotFoundException e) {
                Log.e(TAG, "WEREAD_COMPONENT_MISSING flags=0x" +
                        Integer.toHexString(flags));
            }
        }
        if (getIntent().getBooleanExtra("kindlelab-autolaunch-weread", false)) {
            inspectWeReadManifest(pm);
        }
        Intent query = new Intent(Intent.ACTION_MAIN);
        query.addCategory(Intent.CATEGORY_LAUNCHER);
        query.setPackage(WEREAD_PACKAGE);
        int[] queryFlags = new int[] {0, PackageManager.MATCH_DISABLED_COMPONENTS,
                PackageManager.MATCH_UNINSTALLED_PACKAGES,
                PackageManager.MATCH_DISABLED_COMPONENTS |
                        PackageManager.MATCH_UNINSTALLED_PACKAGES,
                0x40000, 0x80000, 0xC0000};
        for (int flags : queryFlags) {
            List<ResolveInfo> matches = pm.queryIntentActivities(query, flags);
            Log.i(TAG, "WEREAD_LAUNCHER_QUERY flags=0x" + Integer.toHexString(flags) +
                    " count=" + matches.size());
            for (ResolveInfo match : matches) {
                if (match.activityInfo != null)
                    Log.i(TAG, "WEREAD_LAUNCHER_ACTIVITY=" + match.activityInfo.packageName + "/" +
                            match.activityInfo.name + " exported=" + match.activityInfo.exported +
                            " enabled=" + match.activityInfo.enabled);
            }
        }
        Intent launch = pm.getLaunchIntentForPackage(WEREAD_PACKAGE);
        if (launch == null) {
            Log.e(TAG, "WEREAD_LAUNCH_INTENT_NULL package=" + WEREAD_PACKAGE);
            launch = new Intent(Intent.ACTION_MAIN);
            launch.setComponent(new ComponentName(WEREAD_PACKAGE,
                    "com.tencent.weread.LauncherActivity"));
            Log.i(TAG, "WEREAD_EXPLICIT_LAUNCH_FALLBACK=1");
        }
        try {
            Log.i(TAG, "WEREAD_LAUNCH_INTENT=" + launch);
            startActivity(launch);
            Log.i(TAG, "WEREAD_START_ACTIVITY_RETURNED=1");
        } catch (Exception e) {
            Log.e(TAG, "WEREAD_LAUNCH_FAILED", e);
            Toast.makeText(this, "\u5fae\u4fe1\u8bfb\u4e66\u542f\u52a8\u5931\u8d25: " + e.getMessage(), Toast.LENGTH_LONG).show();
        }
    }

    private void inspectWeReadManifest(PackageManager pm) {
        try {
            ApplicationInfo app = pm.getApplicationInfo(WEREAD_PACKAGE, 0);
            Class<?> parserClass = Class.forName("android.content.pm.PackageParser");
            Object parser = parserClass.getConstructor().newInstance();
            java.lang.reflect.Method parse = parserClass.getMethod(
                    "parsePackage", java.io.File.class, Integer.TYPE);
            Object parsed = parse.invoke(parser, new java.io.File(app.sourceDir), 0);
            java.lang.reflect.Field nameField = parsed.getClass().getField("packageName");
            java.lang.reflect.Field activitiesField = parsed.getClass().getField("activities");
            List<?> activities = (List<?>) activitiesField.get(parsed);
            Log.i(TAG, "WEREAD_RAW_PARSE_PACKAGE=" + nameField.get(parsed) +
                    " activityCount=" + (activities == null ? -1 : activities.size()));
            if (activities != null) {
                for (Object activity : activities) {
                    java.lang.reflect.Field infoField = activity.getClass().getField("info");
                    java.lang.reflect.Field intentsField = activity.getClass().getField("intents");
                    ActivityInfo info = (ActivityInfo) infoField.get(activity);
                    if ("com.tencent.weread.LauncherActivity".equals(info.name)) {
                        List<?> intents = (List<?>) intentsField.get(activity);
                        Log.i(TAG, "WEREAD_RAW_LAUNCHER enabled=" + info.enabled +
                                " exported=" + info.exported + " intentFilters=" +
                                (intents == null ? -1 : intents.size()));
                    }
                }
            }
        } catch (java.lang.reflect.InvocationTargetException e) {
            Throwable cause = e.getCause() == null ? e : e.getCause();
            Log.e(TAG, "WEREAD_RAW_PARSE_FAILED=" + cause.getClass().getName() + ":" +
                    cause.getMessage(), cause);
        } catch (Exception e) {
            Log.e(TAG, "WEREAD_RAW_PARSE_FAILED=" + e.getClass().getName() + ":" +
                    e.getMessage(), e);
        }
    }

    private void requestContainerExit() {
        try (FileOutputStream out = createDeviceProtectedStorageContext()
                .openFileOutput("kindle-android-exit", MODE_PRIVATE)) {
            out.write("exit\n".getBytes(StandardCharsets.US_ASCII));
            out.getFD().sync();
            status.setText("\u6b63\u5728\u6062\u590d Kindle\uff0c\u8bf7\u7a0d\u5019\u2026");
            Log.i(TAG, "KINDLE_ANDROID_EXIT_REQUESTED=1");
        } catch (Exception e) {
            Log.e(TAG, "KINDLE_ANDROID_EXIT_REQUEST_FAILED", e);
            status.setText("\u9000\u51fa\u8bf7\u6c42\u5931\u8d25\uff0c\u8bf7\u4fdd\u6301\u5c4f\u5e55\u4e0d\u52a8");
        }
    }
}
