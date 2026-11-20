package de.oli.olivision;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.ImageDecoder;
import android.net.Uri;

import java.io.OutputStream;

/** Photos: decode, AI, save as JPEG (PNG stays PNG). */
final class ImageProcessor {
    private ImageProcessor() {
    }

    static void run(Context context, Job job, Job.Plan plan) throws Exception {
        ImageDecoder.Source source = ImageDecoder.createSource(context.getContentResolver(), job.uri);
        Bitmap in = ImageDecoder.decodeBitmap(source, (decoder, info, src) -> {
            decoder.setAllocator(ImageDecoder.ALLOCATOR_SOFTWARE);
            decoder.setMutableRequired(true);
            decoder.setTargetColorSpace(android.graphics.ColorSpace.get(android.graphics.ColorSpace.Named.SRGB));
        });
        if (in.getConfig() != Bitmap.Config.ARGB_8888) {
            Bitmap copy = in.copy(Bitmap.Config.ARGB_8888, false);
            in.recycle();
            in = copy;
        }

        job.detail = "KI rechnet …";
        Jobs.get().changed();
        long start = System.nanoTime();
        Bitmap out = Bitmap.createBitmap(plan.finalWidth, plan.finalHeight, Bitmap.Config.ARGB_8888);
        boolean ok = Native.processBitmap(in, out, plan.prescaleWidth, plan.prescaleHeight);
        in.recycle();
        if (!ok) {
            out.recycle();
            throw new IllegalStateException("Die KI konnte das Foto nicht bearbeiten.");
        }
        double seconds = (System.nanoTime() - start) / 1e9;
        if (seconds > 0.2) {
            Settings.learnSpeed(context, plan.modelKey, (double) plan.aiWidth * plan.aiHeight / seconds);
        }

        boolean png = job.name.toLowerCase().endsWith(".png");
        String name = Output.resultName(job.name, png ? "png" : "jpg");
        Uri target = Output.create(context, false, name, png ? "image/png" : "image/jpeg");
        if (target == null) {
            out.recycle();
            throw new IllegalStateException("Speichern nicht möglich.");
        }
        try (OutputStream os = context.getContentResolver().openOutputStream(target)) {
            out.compress(png ? Bitmap.CompressFormat.PNG : Bitmap.CompressFormat.JPEG, 95, os);
        } catch (Exception e) {
            Output.delete(context, target);
            throw e;
        } finally {
            out.recycle();
        }
        Output.publish(context, target);
        job.outputUri = target;
        job.outputName = "Bilder/OliVision/" + name;
        job.outputBytes = Output.size(context, target);
        job.progress = 1;
    }
}
