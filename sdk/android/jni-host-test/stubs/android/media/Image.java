package android.media;

import java.nio.ByteBuffer;

public abstract class Image implements AutoCloseable {
    public abstract static class Plane {
        public abstract int getRowStride();
        public abstract int getPixelStride();
        public abstract ByteBuffer getBuffer();
    }
    public abstract int getFormat();
    public abstract int getWidth();
    public abstract int getHeight();
    public abstract Plane[] getPlanes();
    @Override public void close() {}
}
