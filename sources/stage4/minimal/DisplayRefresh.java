package org.kindlelab.apkprobe;

/**
 * 页面进入前台后请求一次"整屏刷新"。
 *
 * 2026-11-06：壳里默认开的是 fast + 只刷脏区（kindle-display-partial-region），
 * 窗口切换时没变化的区域不会重画，屏幕上会留下上一页的残影甚至整块黑/半截旧页脚。
 * 实测：刷新必须发生在**本 Activity 第一帧真正画出来之后**——onCreate 里按时间延迟
 * （450/900/2000ms）都太早，因为 interpret-only 下首帧要 5-7 秒，那些请求全被过渡帧吃掉。
 * 所以这里挂 OnPreDrawListener，等第一次 pre-draw（首帧即将上屏）再延 600ms 补刷。
 *
 * KindleSystemControls 在单独的 system dex 里、不在本 APK 的 classpath，所以直接写触发文件：
 * /data/local/tmp 是 0777，应用（uid 10009）可写；gralloc 消费后自己 unlink。
 */
final class DisplayRefresh {
    private static final String TRIGGER = "/data/local/tmp/kindle-display-full-refresh";

    private DisplayRefresh() {
    }

    /** 首帧上屏后再补一次整屏刷新；另留一个 4s 兜底，防止某些页面不触发 pre-draw。 */
    static void onVisible(android.app.Activity activity) {
        final android.view.View decor = activity.getWindow().getDecorView();
        decor.getViewTreeObserver().addOnPreDrawListener(new android.view.ViewTreeObserver.OnPreDrawListener() {
            public boolean onPreDraw() {
                final android.view.ViewTreeObserver observer = decor.getViewTreeObserver();
                if (observer.isAlive()) observer.removeOnPreDrawListener(this);
                decor.postDelayed(new Runnable() {
                    public void run() {
                        request(decor);
                    }
                }, 600);
                return true;
            }
        });
        decor.postDelayed(new Runnable() {
            public void run() {
                request(decor);
            }
        }, 4000);
    }

    /** 写触发文件并让这个 View 再画一帧（gralloc 在帧开始时读取该标记）。 */
    static void request(final android.view.View view) {
        try (java.io.FileOutputStream out = new java.io.FileOutputStream(TRIGGER)) {
            out.write("1\n".getBytes("UTF-8"));
        } catch (Exception ignored) {
            return;
        }
        if (view != null) view.postInvalidateOnAnimation();
    }
}
