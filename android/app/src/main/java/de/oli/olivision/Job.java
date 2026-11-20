package de.oli.olivision;

import android.net.Uri;

/** One video or photo in the list. */
final class Job {
    enum State { WAITING, RUNNING, PAUSED, DONE, FAILED, ABORTED }

    final Uri uri;
    final String name;
    final boolean isVideo;
    final long inBytes;

    // Stored size of the picture (video: before rotation)
    final int width;
    final int height;
    final int rotation;
    final long durationUs;
    final double frameRate;

    State state = State.WAITING;
    double progress;            // 0..1
    String detail = "";         // e.g. "Bild 120 von 900 · noch ca. 5 Min."
    Uri outputUri;
    String outputName;
    long outputBytes;

    Job(Uri uri, String name, boolean isVideo, long inBytes, int width, int height, int rotation,
        long durationUs, double frameRate) {
        this.uri = uri;
        this.name = name;
        this.isVideo = isVideo;
        this.inBytes = inBytes;
        this.width = width;
        this.height = height;
        this.rotation = rotation;
        this.durationUs = durationUs;
        this.frameRate = frameRate;
    }

    long frameCount() {
        if (!isVideo) {
            return 1;
        }
        return Math.max(1, Math.round(durationUs / 1e6 * frameRate));
    }

    /** All sizes of one run, calculated like on the PC. */
    static final class Plan {
        int prescaleWidth, prescaleHeight;   // 0 = no shrinking before the AI
        int aiWidth, aiHeight;               // what the AI produces
        int finalWidth, finalHeight;         // stored result
        String modelKey;
    }

    static final int SCALE = 4;
    // Photos: the AI result must not get too large for the phone memory
    private static final long MAX_AI_PIXELS = 80L * 1000 * 1000;

    Plan plan(Settings s) {
        Plan p = new Plan();
        p.modelKey = s.modelName();

        int maxW, maxH;
        switch (s.targetSize) {
            case 1: maxW = 1920; maxH = 1080; break;
            case 2: maxW = 2560; maxH = 1440; break;
            case 3: maxW = 3840; maxH = 2160; break;
            default: maxW = width; maxH = height; break;
        }

        int aiW = width * SCALE, aiH = height * SCALE;
        int[] fit = fit(aiW, aiH, maxW, maxH);
        p.finalWidth = fit[0];
        p.finalHeight = fit[1];
        p.aiWidth = aiW;
        p.aiHeight = aiH;

        if (p.finalWidth != aiW || p.finalHeight != aiH) {
            int idealW = Math.max(2, p.finalWidth / SCALE);
            int idealH = Math.max(2, p.finalHeight / SCALE);
            double ratio = (double) idealW / width;
            boolean shrink = ratio < 1.0 && (s.speed >= 2 || (s.speed == 1 && ratio >= 0.7));
            if (!shrink && ratio < 1.0 && (long) aiW * aiH > MAX_AI_PIXELS) {
                shrink = true;
            }
            if (shrink) {
                p.prescaleWidth = idealW;
                p.prescaleHeight = idealH;
                p.aiWidth = idealW * SCALE;
                p.aiHeight = idealH * SCALE;
                p.finalWidth = p.aiWidth;
                p.finalHeight = p.aiHeight;
            }
        }
        return p;
    }

    /** Fit into the box (the box is turned for portrait pictures), even numbers. */
    static int[] fit(int w, int h, int maxW, int maxH) {
        if (maxW <= 0 || maxH <= 0) {
            return new int[] {w, h};
        }
        if ((h > w) != (maxH > maxW)) {
            int t = maxW;
            maxW = maxH;
            maxH = t;
        }
        if (w <= maxW && h <= maxH) {
            return new int[] {w & ~1, h & ~1};
        }
        double f = Math.min((double) maxW / w, (double) maxH / h);
        int fw = Math.max(2, (int) Math.floor(w * f / 2.0) * 2);
        int fh = Math.max(2, (int) Math.floor(h * f / 2.0) * 2);
        return new int[] {fw, fh};
    }

    /** Display size (videos may be stored turned). */
    int[] shownSize(int w, int h) {
        if (rotation == 90 || rotation == 270) {
            return new int[] {h, w};
        }
        return new int[] {w, h};
    }

    /** Bit rate for the phone video chip (HEVC), same formula as on the PC. */
    static double bitrate(int w, int h, double fps, double factor) {
        if (fps <= 0) {
            fps = 30;
        }
        double pixels = (double) w * h;
        double b = 25e6 * Math.pow(pixels / (3840.0 * 2160.0), 0.83) * (fps / 30.0) * factor;
        return Math.max(2e6, Math.min(b, 120e6));
    }

    long estimatedBytes(Plan p) {
        if (!isVideo) {
            boolean png = name.toLowerCase().endsWith(".png");
            return (long) ((double) p.finalWidth * p.finalHeight * (png ? 2.0 : 0.45));
        }
        double seconds = durationUs / 1e6;
        return (long) ((bitrate(p.finalWidth, p.finalHeight, frameRate, 1.3) + 192e3) * seconds / 8);
    }

    /** Seconds, or -1 if the phone has not measured this model yet. */
    double estimatedSeconds(android.content.Context c, Plan p) {
        double speed = Settings.learnedSpeed(c, p.modelKey);
        if (speed <= 0) {
            return -1;
        }
        return (double) p.aiWidth * p.aiHeight * frameCount() / speed;
    }
}
