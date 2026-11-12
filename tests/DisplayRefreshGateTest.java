package org.kindlelab.apkprobe;

public final class DisplayRefreshGateTest {
    public static void main(String[] args) {
        DisplayRefreshGate gate = new DisplayRefreshGate();
        if (!gate.claim()) throw new AssertionError("first request must pass");
        if (gate.claim()) throw new AssertionError("second request must be suppressed");
        gate.release();
        if (!gate.claim()) throw new AssertionError("failed request must be retryable");
        if (gate.claim()) throw new AssertionError("retry must still be one-shot");
    }
}
