package org.kindlelab.apkprobe;

/** Allows one successful display-refresh request, with retry after failure. */
final class DisplayRefreshGate {
    private boolean claimed;

    synchronized boolean claim() {
        if (claimed) return false;
        claimed = true;
        return true;
    }

    synchronized void release() {
        claimed = false;
    }
}
