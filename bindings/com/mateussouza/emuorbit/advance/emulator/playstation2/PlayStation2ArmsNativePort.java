// SPDX-License-Identifier: GPL-3.0-or-later
// Native declarations extracted verbatim from the product, visibility made public.
// This is an ABI reference, NOT the application's frontend/session implementation.
package com.mateussouza.emuorbit.advance.emulator.playstation2;
import android.view.Surface;
import java.io.IOException;
import java.nio.ByteBuffer;
public final class PlayStation2ArmsNativePort {
    private PlayStation2ArmsNativePort() {}
    public static native long openNative(Surface surface, int width, int height, String library, String system,
            String resources, String biosDirectory, String biosName, String game) throws IOException;
    public static native void pauseNative(long handle, boolean paused) throws IOException;
    public static native void replaceNative(long handle, Surface surface, int width, int height) throws IOException;
    public static native void frameNative(long handle, ByteBuffer pcm, long[] metadata,
            int buttons, int lx, int ly, int rx, int ry) throws IOException;
    public static native long pausedCyclesNative(long handle) throws IOException;
    public static native boolean stateFileNative(long handle, String nativeScratch, boolean loading) throws IOException;
    public static native boolean terminalNative(long handle) throws IOException;
    public static native void closeNative(long handle) throws IOException;
    public static native boolean idleNative();
}
