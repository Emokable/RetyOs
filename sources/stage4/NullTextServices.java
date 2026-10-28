package org.kindlelab.textservices;

import android.os.Binder;
import android.os.Parcel;
import android.os.RemoteException;
import android.os.ServiceManager;

/** Headless-only AOSP TextServices contract stub; reports no spell checker. */
public final class NullTextServices extends Binder {
    private static final String DESCRIPTOR = "com.android.internal.textservice.ITextServicesManager";
    private static final int TRANSACTION_getCurrentSpellChecker = 1;

    @Override
    protected boolean onTransact(int code, Parcel data, Parcel reply, int flags)
            throws RemoteException {
        if (code == 1598968902) {
            reply.writeString(DESCRIPTOR);
            return true;
        }
        if (code == TRANSACTION_getCurrentSpellChecker) {
            data.enforceInterface(DESCRIPTOR);
            data.readInt();
            data.readString();
            reply.writeNoException();
            reply.writeInt(0);
            return true;
        }
        return super.onTransact(code, data, reply, flags);
    }

    public static void main(String[] args) {
        ServiceManager.addService("textservices", new NullTextServices());
        System.out.println("TEXTSERVICES_NULL_STUB_PUBLISHED=1");
        try {
            for (;;) Thread.sleep(60000);
        } catch (InterruptedException ignored) {
            Thread.currentThread().interrupt();
        }
    }
}
