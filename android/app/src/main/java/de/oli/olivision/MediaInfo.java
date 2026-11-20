package de.oli.olivision;

import android.content.ContentResolver;
import android.content.Context;
import android.database.Cursor;
import android.graphics.BitmapFactory;
import android.media.ExifInterface;
import android.media.MediaExtractor;
import android.media.MediaFormat;
import android.media.MediaMetadataRetriever;
import android.net.Uri;
import android.provider.OpenableColumns;

import java.io.InputStream;

/** Reads name, size and picture dimensions of a chosen file. */
final class MediaInfo {
    private MediaInfo() {
    }

    static Job read(Context context, Uri uri) {
        ContentResolver cr = context.getContentResolver();
        String name = "Datei";
        long bytes = 0;
        try (Cursor c = cr.query(uri, new String[] {OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE},
                null, null, null)) {
            if (c != null && c.moveToFirst()) {
                if (!c.isNull(0)) {
                    name = c.getString(0);
                }
                if (!c.isNull(1)) {
                    bytes = c.getLong(1);
                }
            }
        } catch (Exception ignored) {
        }

        String type = cr.getType(uri);
        boolean video = type != null ? type.startsWith("video/") : name.toLowerCase().matches(".*\\.(mp4|mov|mkv|3gp|webm|m4v)$");
        try {
            return video ? readVideo(context, uri, name, bytes) : readImage(context, uri, name, bytes);
        } catch (Exception e) {
            return null;
        }
    }

    private static Job readVideo(Context context, Uri uri, String name, long bytes) throws Exception {
        MediaMetadataRetriever r = new MediaMetadataRetriever();
        int w, h, rot;
        long durMs;
        long frames = -1;
        try {
            r.setDataSource(context, uri);
            w = Integer.parseInt(r.extractMetadata(MediaMetadataRetriever.METADATA_KEY_VIDEO_WIDTH));
            h = Integer.parseInt(r.extractMetadata(MediaMetadataRetriever.METADATA_KEY_VIDEO_HEIGHT));
            String rs = r.extractMetadata(MediaMetadataRetriever.METADATA_KEY_VIDEO_ROTATION);
            rot = rs != null ? Integer.parseInt(rs) : 0;
            durMs = Long.parseLong(r.extractMetadata(MediaMetadataRetriever.METADATA_KEY_DURATION));
            String fc = r.extractMetadata(MediaMetadataRetriever.METADATA_KEY_VIDEO_FRAME_COUNT);
            if (fc != null) {
                frames = Long.parseLong(fc);
            }
        } finally {
            r.release();
        }

        double fps = 30;
        MediaExtractor ex = new MediaExtractor();
        try {
            ex.setDataSource(context, uri, null);
            for (int i = 0; i < ex.getTrackCount(); i++) {
                MediaFormat f = ex.getTrackFormat(i);
                String mime = f.getString(MediaFormat.KEY_MIME);
                if (mime != null && mime.startsWith("video/") && f.containsKey(MediaFormat.KEY_FRAME_RATE)) {
                    try {
                        fps = f.getInteger(MediaFormat.KEY_FRAME_RATE);
                    } catch (ClassCastException e) {
                        fps = f.getFloat(MediaFormat.KEY_FRAME_RATE);
                    }
                    break;
                }
            }
        } finally {
            ex.release();
        }
        if (frames > 0 && durMs > 0) {
            fps = frames * 1000.0 / durMs;
        }
        if (fps <= 0 || fps > 240) {
            fps = 30;
        }
        return new Job(uri, name, true, bytes, w, h, rot, durMs * 1000, fps);
    }

    private static Job readImage(Context context, Uri uri, String name, long bytes) throws Exception {
        BitmapFactory.Options o = new BitmapFactory.Options();
        o.inJustDecodeBounds = true;
        try (InputStream in = context.getContentResolver().openInputStream(uri)) {
            BitmapFactory.decodeStream(in, null, o);
        }
        if (o.outWidth <= 0 || o.outHeight <= 0) {
            return null;
        }
        int w = o.outWidth, h = o.outHeight;
        // Photos are turned when they are decoded, so the job keeps the shown size
        try (InputStream in = context.getContentResolver().openInputStream(uri)) {
            int orientation = new ExifInterface(in).getAttributeInt(ExifInterface.TAG_ORIENTATION,
                    ExifInterface.ORIENTATION_NORMAL);
            if (orientation == ExifInterface.ORIENTATION_ROTATE_90
                    || orientation == ExifInterface.ORIENTATION_ROTATE_270
                    || orientation == ExifInterface.ORIENTATION_TRANSPOSE
                    || orientation == ExifInterface.ORIENTATION_TRANSVERSE) {
                int t = w;
                w = h;
                h = t;
            }
        } catch (Exception ignored) {
        }
        return new Job(uri, name, false, bytes, w, h, 0, 0, 0);
    }
}
