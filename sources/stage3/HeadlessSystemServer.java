import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;

/** Reproduce AOSP's shared SystemServer class-loader namespace in the root probe. */
public final class HeadlessSystemServer {
    public static void main(String[] args) throws Throwable {
        String classpath = System.getenv("SYSTEMSERVERCLASSPATH");
        if (classpath == null || classpath.isEmpty()) throw new IllegalStateException("missing server classpath");
        Class<?> factory = Class.forName("com.android.internal.os.PathClassLoaderFactory");
        Method create = factory.getDeclaredMethod("createClassLoader", String.class, String.class,
                String.class, ClassLoader.class, int.class, boolean.class);
        ClassLoader loader = (ClassLoader) create.invoke(null, classpath,
                System.getProperty("java.library.path"), null,
                ClassLoader.getSystemClassLoader(), 25, true);
        Thread.currentThread().setContextClassLoader(loader);
        System.err.println("SYSTEMSERVER_SHARED_NAMESPACE_READY");
        try {
            // A normal SystemServer inherits these caches from Zygote. This
            // direct diagnostic entry must initialize them before AssetAtlas.
            Method preload = Class.forName("com.android.internal.os.ZygoteInit")
                    .getDeclaredMethod("preloadResources");
            preload.setAccessible(true);
            preload.invoke(null);
            System.err.println("SYSTEMSERVER_RESOURCE_PRELOAD_COMPLETE");
            Class.forName("com.android.server.SystemServer", true, loader)
                    .getMethod("main", String[].class).invoke(null, (Object) new String[0]);
        } catch (InvocationTargetException exception) {
            throw exception.getCause();
        }
    }
}
