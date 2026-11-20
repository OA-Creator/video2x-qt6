package de.oli.olivision;

import android.app.Activity;
import android.app.ActivityManager;
import android.app.AlertDialog;
import android.app.ApplicationExitInfo;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.SharedPreferences;
import android.os.Build;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileWriter;
import java.io.InputStream;
import java.io.PrintWriter;
import java.io.StringWriter;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.List;
import java.util.Locale;

/** Remembers why the app stopped and shows it at the next start. */
final class CrashReport {
    private static File logFile;

    private CrashReport() {
    }

    static void install(Context context) {
        logFile = new File(context.getFilesDir(), "ablauf.txt");
        Thread.UncaughtExceptionHandler old = Thread.getDefaultUncaughtExceptionHandler();
        Thread.setDefaultUncaughtExceptionHandler((t, e) -> {
            StringWriter sw = new StringWriter();
            e.printStackTrace(new PrintWriter(sw));
            step("ABSTURZ (Java) in " + t.getName() + ":\n" + sw);
            if (old != null) {
                old.uncaughtException(t, e);
            }
        });
    }

    /** Writes one line, so we know how far the work got. */
    static synchronized void step(String text) {
        if (logFile == null) {
            return;
        }
        try {
            if (logFile.length() > 200_000) {
                logFile.delete();
            }
            try (FileWriter w = new FileWriter(logFile, true)) {
                w.write(new SimpleDateFormat("HH:mm:ss", Locale.GERMANY).format(new Date()) + "  " + text + "\n");
            }
        } catch (Exception ignored) {
        }
    }

    static void showIfCrashed(Activity activity) {
        if (Build.VERSION.SDK_INT < 30) {
            return;
        }
        ActivityManager am = activity.getSystemService(ActivityManager.class);
        List<ApplicationExitInfo> list = am.getHistoricalProcessExitReasons(activity.getPackageName(), 0, 1);
        if (list.isEmpty()) {
            return;
        }
        ApplicationExitInfo info = list.get(0);
        SharedPreferences p = activity.getSharedPreferences("olivision-crash", Context.MODE_PRIVATE);
        if (info.getTimestamp() <= p.getLong("shown", 0)) {
            return;
        }
        p.edit().putLong("shown", info.getTimestamp()).apply();
        int r = info.getReason();
        boolean bad = r == ApplicationExitInfo.REASON_CRASH || r == ApplicationExitInfo.REASON_CRASH_NATIVE
                || r == ApplicationExitInfo.REASON_ANR || r == ApplicationExitInfo.REASON_LOW_MEMORY
                || r == ApplicationExitInfo.REASON_SIGNALED || r == ApplicationExitInfo.REASON_INITIALIZATION_FAILURE
                || r == ApplicationExitInfo.REASON_EXCESSIVE_RESOURCE_USAGE;
        if (!bad) {
            return;
        }

        StringBuilder sb = new StringBuilder();
        sb.append("Grund: ").append(reasonName(r)).append(" (").append(info.getStatus()).append(")\n");
        if (info.getDescription() != null) {
            sb.append(info.getDescription()).append('\n');
        }
        sb.append("Speicher: ").append(info.getPss() / 1024).append(" MB\n");
        sb.append("Gerät: ").append(Build.MANUFACTURER).append(' ').append(Build.MODEL)
                .append(" · Android ").append(Build.VERSION.RELEASE).append('\n');
        sb.append("Grafik: ").append(Jobs.get().gpuName).append("\n\n");

        sb.append("--- Ablauf ---\n").append(lastLines(30)).append('\n');

        if (r == ApplicationExitInfo.REASON_CRASH_NATIVE) {
            sb.append("--- Stelle ---\n").append(nativeTrace(info));
        }
        String text = sb.toString();

        TextView tv = new TextView(activity);
        tv.setText(text);
        tv.setTextIsSelectable(true);
        tv.setTextSize(11);
        tv.setTextColor(MainActivity.TEXT);
        int pad = (int) (16 * activity.getResources().getDisplayMetrics().density);
        tv.setPadding(pad, pad, pad, pad);
        ScrollView sv = new ScrollView(activity);
        sv.addView(tv);
        new AlertDialog.Builder(activity, android.R.style.Theme_Material_Dialog_Alert)
                .setTitle("OliVision ist abgestürzt")
                .setView(sv)
                .setPositiveButton("Kopieren", (d, w) -> {
                    ClipboardManager cm = activity.getSystemService(ClipboardManager.class);
                    cm.setPrimaryClip(ClipData.newPlainText("OliVision", text));
                    Toast.makeText(activity, "Kopiert", Toast.LENGTH_SHORT).show();
                })
                .setNegativeButton("Schließen", null)
                .show();
    }

    private static String lastLines(int count) {
        try {
            List<String> lines = Files.readAllLines(logFile.toPath(), StandardCharsets.UTF_8);
            int from = Math.max(0, lines.size() - count);
            return String.join("\n", lines.subList(from, lines.size()));
        } catch (Exception e) {
            return "(leer)";
        }
    }

    /** The native report is binary, the readable parts (functions, libraries) are enough. */
    private static String nativeTrace(ApplicationExitInfo info) {
        try (InputStream in = info.getTraceInputStream()) {
            if (in == null) {
                return "(keine Daten)";
            }
            ByteArrayOutputStream bo = new ByteArrayOutputStream();
            byte[] buf = new byte[8192];
            int n;
            while ((n = in.read(buf)) > 0 && bo.size() < 2_000_000) {
                bo.write(buf, 0, n);
            }
            byte[] data = bo.toByteArray();
            StringBuilder out = new StringBuilder();
            StringBuilder run = new StringBuilder();
            int lines = 0;
            for (byte b : data) {
                if (b >= 32 && b < 127) {
                    run.append((char) b);
                    continue;
                }
                String s = run.toString();
                run.setLength(0);
                if (s.length() >= 6 && (s.contains("olivision") || s.contains("ncnn") || s.contains("Real")
                        || s.contains("SIG") || s.contains("signal") || s.contains("Abort") || s.contains("abort")
                        || s.contains("vulkan") || s.contains("Vulkan") || s.contains("/vendor/")
                        || s.contains("_Z") || s.contains("Cause") || s.contains("fault"))) {
                    out.append(s).append('\n');
                    if (++lines >= 60) {
                        break;
                    }
                }
            }
            return out.length() > 0 ? out.toString() : "(nichts Lesbares)";
        } catch (Exception e) {
            return "(nicht lesbar: " + e.getMessage() + ")";
        }
    }

    private static String reasonName(int r) {
        switch (r) {
            case ApplicationExitInfo.REASON_CRASH: return "Fehler im Java-Teil";
            case ApplicationExitInfo.REASON_CRASH_NATIVE: return "Fehler im KI-Teil (C++)";
            case ApplicationExitInfo.REASON_ANR: return "App hing fest";
            case ApplicationExitInfo.REASON_LOW_MEMORY: return "Speicher voll";
            case ApplicationExitInfo.REASON_SIGNALED: return "vom System beendet";
            case ApplicationExitInfo.REASON_EXCESSIVE_RESOURCE_USAGE: return "zu viel Last";
            case ApplicationExitInfo.REASON_INITIALIZATION_FAILURE: return "Start fehlgeschlagen";
            default: return "Code " + r;
        }
    }
}
