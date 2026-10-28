package android.net;
/** Compile-only declarations; Android's boot class supplies the implementation. */
public abstract class NetworkAgent extends android.os.Handler {
    public NetworkAgent(android.os.Looper looper,android.content.Context context,String tag,
        NetworkInfo info,NetworkCapabilities caps,LinkProperties links,int score,NetworkMisc misc) {}
    protected abstract void unwanted();
}
