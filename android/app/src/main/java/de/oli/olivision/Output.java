package de.oli.olivision;

import android.content.ContentResolver;
import android.content.ContentValues;
import android.content.Context;
import android.net.Uri;
import android.os.Environment;
import android.provider.MediaStore;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;

/** Models and result files. */
final class Output {
    private Output() {
    }

    /** Copies the model from the app into the phone memory once and loads it. */
    static boolean loadModel(Context context, String model) {
        File dir = new File(context.getFilesDir(), "models");
        if (!dir.exists() && !dir.mkdirs()) {
            return false;
        }
        File param = new File(dir, model + ".param");
        File bin = new File(dir, model + ".bin");
        try {
            copyAsset(context, "models/" + model + ".param", param);
            copyAsset(context, "models/" + model + ".bin", bin);
        } catch (Exception e) {
            return false;
        }
        return Native.init(param.getAbsolutePath(), bin.getAbsolutePath());
    }

    private static void copyAsset(Context context, String asset, File target) throws Exception {
        long size;
        try (android.content.res.AssetFileDescriptor fd = context.getAssets().openFd(asset)) {
            size = fd.getLength();
        } catch (Exception e) {
            size = -1;   // compressed asset, length unknown
        }
        if (target.exists() && (size < 0 ? target.length() > 0 : target.length() == size)) {
            return;
        }
        File tmp = new File(target.getPath() + ".tmp");
        try (InputStream in = context.getAssets().open(asset); OutputStream out = new FileOutputStream(tmp)) {
            byte[] buf = new byte[1 << 16];
            int n;
            while ((n = in.read(buf)) > 0) {
                out.write(buf, 0, n);
            }
        }
        if (!tmp.renameTo(target)) {
            throw new IllegalStateException("rename");
        }
    }

    static String resultName(String inputName, String extension) {
        String base = inputName;
        int dot = base.lastIndexOf('.');
        if (dot > 0) {
            base = base.substring(0, dot);
        }
        return base + "_OliVision." + extension;
    }

    /** New entry in Filme/OliVision or Bilder/OliVision, hidden until it is finished. */
    static Uri create(Context context, boolean video, String displayName, String mime) {
        ContentValues v = new ContentValues();
        v.put(MediaStore.MediaColumns.DISPLAY_NAME, displayName);
        v.put(MediaStore.MediaColumns.MIME_TYPE, mime);
        v.put(MediaStore.MediaColumns.RELATIVE_PATH,
                (video ? Environment.DIRECTORY_MOVIES : Environment.DIRECTORY_PICTURES) + "/OliVision");
        v.put(MediaStore.MediaColumns.IS_PENDING, 1);
        Uri collection = video
                ? MediaStore.Video.Media.getContentUri(MediaStore.VOLUME_EXTERNAL_PRIMARY)
                : MediaStore.Images.Media.getContentUri(MediaStore.VOLUME_EXTERNAL_PRIMARY);
        return context.getContentResolver().insert(collection, v);
    }

    static void publish(Context context, Uri uri) {
        ContentValues v = new ContentValues();
        v.put(MediaStore.MediaColumns.IS_PENDING, 0);
        context.getContentResolver().update(uri, v, null, null);
    }

    static void delete(Context context, Uri uri) {
        if (uri == null) {
            return;
        }
        try {
            context.getContentResolver().delete(uri, null, null);
        } catch (Exception ignored) {
        }
    }

    static long size(Context context, Uri uri) {
        ContentResolver cr = context.getContentResolver();
        try (android.os.ParcelFileDescriptor fd = cr.openFileDescriptor(uri, "r")) {
            return fd != null ? fd.getStatSize() : 0;
        } catch (Exception e) {
            return 0;
        }
    }
}
