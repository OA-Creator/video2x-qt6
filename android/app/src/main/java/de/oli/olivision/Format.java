package de.oli.olivision;

import java.util.Locale;

/** Texts for people, like on the PC. */
final class Format {
    private Format() {
    }

    static String sizeName(int w, int h) {
        int s = Math.min(w, h);
        String name;
        if (s >= 2000) {
            name = "4K";
        } else if (s >= 1300) {
            name = "QHD";
        } else if (s >= 1000) {
            name = "Full HD";
        } else if (s >= 700) {
            name = "HD";
        } else {
            name = "SD";
        }
        return name + " (" + w + " × " + h + ")";
    }

    static String bytes(long b) {
        if (b <= 0) {
            return "?";
        }
        if (b >= 1024L * 1024 * 1024) {
            return String.format(Locale.GERMANY, "%.1f GB", b / (1024.0 * 1024 * 1024));
        }
        if (b >= 1024L * 1024) {
            return String.format(Locale.GERMANY, "%.0f MB", b / (1024.0 * 1024));
        }
        return String.format(Locale.GERMANY, "%.0f KB", Math.max(1, b / 1024.0));
    }

    static String duration(double seconds) {
        long s = Math.round(seconds);
        if (s < 60) {
            return "1 Min.";
        }
        long m = (s + 30) / 60;
        if (m < 60) {
            return m + " Min.";
        }
        long h = m / 60;
        m = m % 60;
        if (h >= 24) {
            long d = h / 24;
            h = h % 24;
            return d + (d == 1 ? " Tag " : " Tage ") + h + " Std.";
        }
        return h + " Std. " + m + " Min.";
    }

    /** Length of a video, e.g. "45 Sek." or "3:20 Min." */
    static String length(double seconds) {
        long s = Math.round(seconds);
        if (s < 60) {
            return s + " Sek.";
        }
        if (s < 3600) {
            return String.format(Locale.GERMANY, "%d:%02d Min.", s / 60, s % 60);
        }
        return String.format(Locale.GERMANY, "%d:%02d:%02d Std.", s / 3600, (s / 60) % 60, s % 60);
    }
}
