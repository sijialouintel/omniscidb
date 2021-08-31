package com.mapd;

import java.io.IOException;

public class CiderJNI {

    private static long ptr = 0;

    static {
        System.loadLibrary("ciderjni");
        if (ptr == 0) ptr = init();
    }

    public static native int processBlocks(long dbePtr, String sql, String schema,
                                           long[] dataBuffers, long[] dataNulls,
                                           long[] resultBuffers, long[] resultNulls,
                                           int rowCount);

    public static native long init();

    public static native void close(long dbePtr);

    public static long getPtr() {
        return ptr;
    }
}
