package de.oli.olivision;

import android.content.Context;
import android.content.SharedPreferences;

/** The four choices, saved like on the PC. */
final class Settings {
    int targetSize = 3;    // 0 = same, 1 = Full HD, 2 = QHD, 3 = 4K
    int speed = 1;         // 2 = fast, 1 = balanced, 0 = best
    boolean denoise;
    boolean cartoon;

    static Settings load(Context c) {
        SharedPreferences p = c.getSharedPreferences("olivision-simple", Context.MODE_PRIVATE);
        Settings s = new Settings();
        s.targetSize = p.getInt("targetSize", 3);
        s.speed = p.getInt("speed", 1);
        s.denoise = p.getBoolean("denoise", false);
        s.cartoon = p.getBoolean("cartoon", false);
        return s;
    }

    void save(Context c) {
        c.getSharedPreferences("olivision-simple", Context.MODE_PRIVATE).edit()
                .putInt("targetSize", targetSize)
                .putInt("speed", speed)
                .putBoolean("denoise", denoise)
                .putBoolean("cartoon", cartoon)
                .apply();
    }

    Settings copy() {
        Settings s = new Settings();
        s.targetSize = targetSize;
        s.speed = speed;
        s.denoise = denoise;
        s.cartoon = cartoon;
        return s;
    }

    String modelName() {
        if (cartoon) {
            return "realesr-animevideov3-x4";
        }
        return denoise ? "realesr-generalv3-wdn-x4" : "realesr-generalv3-x4";
    }

    /** Learned speed of the phone in AI output pixels per second, per model. */
    static double learnedSpeed(Context c, String model) {
        return c.getSharedPreferences("olivision-speed", Context.MODE_PRIVATE).getFloat(model, 0f);
    }

    static void learnSpeed(Context c, String model, double pixelsPerSecond) {
        SharedPreferences p = c.getSharedPreferences("olivision-speed", Context.MODE_PRIVATE);
        double old = p.getFloat(model, 0f);
        double value = old > 0 ? (old + pixelsPerSecond) / 2.0 : pixelsPerSecond;
        p.edit().putFloat(model, (float) value).apply();
    }
}
