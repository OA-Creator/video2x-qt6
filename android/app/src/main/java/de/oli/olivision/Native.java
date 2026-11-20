package de.oli.olivision;

import android.graphics.Bitmap;

import java.nio.ByteBuffer;

/** Bridge to the AI part in C++ (Real-ESRGAN on the phone GPU). */
final class Native {
    static {
        System.loadLibrary("olivision");
    }

    private Native() {
    }

    static native boolean init(String paramPath, String binPath);

    static native String gpuName();

    static native boolean processBitmap(Bitmap in, Bitmap out, int prescaleWidth, int prescaleHeight);

    static native boolean processYuv(ByteBuffer inY, int inYRowStride, int inYPixelStride,
                                     ByteBuffer inU, ByteBuffer inV, int inUvRowStride, int inUvPixelStride,
                                     boolean inHighBit, int width, int height,
                                     int prescaleWidth, int prescaleHeight,
                                     ByteBuffer outY, int outYRowStride,
                                     ByteBuffer outU, ByteBuffer outV, int outUvRowStride, int outUvPixelStride,
                                     int finalWidth, int finalHeight);
}
