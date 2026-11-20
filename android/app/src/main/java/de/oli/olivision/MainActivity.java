package de.oli.olivision;

import android.Manifest;
import android.app.Activity;
import android.content.ClipData;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.content.res.ColorStateList;
import android.graphics.Color;
import android.graphics.Insets;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowInsets;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/** The OliVision screen: same questions and cards as on the PC. */
public final class MainActivity extends Activity implements Jobs.Listener {
    static final int GREEN = Color.parseColor("#39d353");
    static final int NAVY = Color.parseColor("#0d1b2a");
    static final int HEADER = Color.parseColor("#0f1f2e");
    static final int PANEL = Color.parseColor("#16293d");
    static final int RAISED = Color.parseColor("#1a3550");
    static final int LINE = Color.parseColor("#2a4a6a");
    static final int TEXT = Color.parseColor("#e8eef4");
    static final int MUTED = Color.parseColor("#a0b8cc");
    static final int HINT = Color.parseColor("#8fb0cf");
    static final int YELLOW = Color.parseColor("#ffc107");
    static final int RED = Color.parseColor("#ff6b6b");

    private static final int PICK = 1;

    private Settings settings;
    private TextView gpuLabel;
    private TextView emptyLabel;
    private LinearLayout cardsBox;
    private TextView startButton;
    private TextView pauseButton;
    private TextView abortButton;
    private final List<List<TextView>> choiceGroups = new ArrayList<>();
    private final Map<Job, Card> cards = new HashMap<>();

    private static final class Card {
        LinearLayout root;
        TextView name;
        TextView remove;
        TextView status;
        TextView sizes;
        ProgressBar progress;
        LinearLayout resultRow;
        TextView result;
    }

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        settings = Settings.load(this);
        getWindow().setStatusBarColor(HEADER);
        getWindow().setNavigationBarColor(NAVY);
        setContentView(buildUi());
        Jobs.get().addListener(this);
        loadGpuName();
        handleShare(getIntent());
        onJobsChanged();
    }

    @Override
    protected void onDestroy() {
        Jobs.get().removeListener(this);
        super.onDestroy();
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        handleShare(intent);
    }

    // ---------------------------------------------------------------- building the screen

    private int dp(float v) {
        return Math.round(TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, v, getResources().getDisplayMetrics()));
    }

    private GradientDrawable box(int fill, int stroke, float radius) {
        GradientDrawable d = new GradientDrawable();
        d.setColor(fill);
        d.setCornerRadius(dp(radius));
        if (stroke != 0) {
            d.setStroke(dp(1), stroke);
        }
        return d;
    }

    private TextView label(String text, float sp, int color, boolean bold) {
        TextView t = new TextView(this);
        t.setText(text);
        t.setTextSize(sp);
        t.setTextColor(color);
        if (bold) {
            t.setTypeface(Typeface.DEFAULT_BOLD);
        }
        return t;
    }

    private LinearLayout.LayoutParams fullWidth(int top) {
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT);
        lp.topMargin = dp(top);
        return lp;
    }

    private View buildUi() {
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(NAVY);
        root.setOnApplyWindowInsetsListener((v, insets) -> {
            Insets bars = insets.getInsets(WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout());
            v.setPadding(0, 0, 0, bars.bottom);
            v.findViewWithTag("header").setPadding(dp(16), bars.top + dp(12), dp(16), dp(12));
            return WindowInsets.CONSUMED;
        });

        // Header: green dot, name, graphics chip
        LinearLayout header = new LinearLayout(this);
        header.setTag("header");
        header.setOrientation(LinearLayout.HORIZONTAL);
        header.setGravity(Gravity.CENTER_VERTICAL);
        header.setBackgroundColor(HEADER);
        header.setPadding(dp(16), dp(12), dp(16), dp(12));
        TextView dot = label("●", 18, GREEN, false);
        header.addView(dot);
        TextView title = label("OliVision", 22, TEXT, true);
        title.setPadding(dp(8), 0, 0, 0);
        header.addView(title, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));
        gpuLabel = label("", 12, MUTED, false);
        gpuLabel.setGravity(Gravity.END);
        header.addView(gpuLabel);
        root.addView(header);

        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        LinearLayout content = new LinearLayout(this);
        content.setOrientation(LinearLayout.VERTICAL);
        content.setPadding(dp(14), dp(14), dp(14), dp(14));
        scroll.addView(content);
        root.addView(scroll, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1));

        // Add files
        TextView add = label("＋  Videos oder Fotos auswählen", 16, GREEN, true);
        add.setGravity(Gravity.CENTER);
        add.setPadding(dp(12), dp(22), dp(12), dp(22));
        GradientDrawable addBg = box(PANEL, 0, 12);
        addBg.setStroke(dp(2), GREEN, dp(8), dp(6));
        add.setBackground(addBg);
        add.setOnClickListener(v -> pickFiles());
        content.addView(add, fullWidth(0));

        emptyLabel = label("Noch keine Videos oder Fotos.", 14, MUTED, false);
        emptyLabel.setGravity(Gravity.CENTER);
        emptyLabel.setPadding(0, dp(14), 0, dp(4));
        content.addView(emptyLabel, fullWidth(0));

        cardsBox = new LinearLayout(this);
        cardsBox.setOrientation(LinearLayout.VERTICAL);
        content.addView(cardsBox, fullWidth(4));

        // The four questions
        content.addView(question("Wie groß soll es werden?",
                new String[] {"Gleich\n(nur verbessern)", "Full HD", "QHD", "4K"},
                settings.targetSize, i -> settings.targetSize = i, null), fullWidth(14));
        content.addView(question("Was ist dir wichtiger?",
                new String[] {"Schnell", "Ausgewogen", "Beste\nQualität"},
                2 - settings.speed, i -> settings.speed = 2 - i, null), fullWidth(10));
        content.addView(question("Rauschunterdrückung?", new String[] {"Aus", "An"},
                settings.denoise ? 1 : 0, i -> settings.denoise = i == 1,
                "An: für alte oder dunkle Aufnahmen mit Bildrauschen. Das Bild wird ruhiger, "
                        + "sehr feine Details etwas weicher."), fullWidth(10));
        content.addView(question("Zeichentrick?", new String[] {"Nein", "Ja"},
                settings.cartoon ? 1 : 0, i -> settings.cartoon = i == 1, null), fullWidth(10));

        // Buttons at the bottom
        LinearLayout bar = new LinearLayout(this);
        bar.setOrientation(LinearLayout.HORIZONTAL);
        bar.setPadding(dp(14), dp(10), dp(14), dp(12));
        bar.setBackgroundColor(HEADER);
        startButton = label("▶  Start", 17, NAVY, true);
        startButton.setGravity(Gravity.CENTER);
        startButton.setPadding(0, dp(14), 0, dp(14));
        startButton.setOnClickListener(v -> start());
        bar.addView(startButton, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 2));
        pauseButton = secondaryButton("Pause");
        pauseButton.setOnClickListener(v -> {
            Jobs.get().paused = !Jobs.get().paused;
            Jobs.get().changed();
        });
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1);
        lp.leftMargin = dp(8);
        bar.addView(pauseButton, lp);
        abortButton = secondaryButton("Abbrechen");
        abortButton.setOnClickListener(v -> {
            Jobs.get().abort = true;
            Jobs.get().paused = false;
            Jobs.get().changed();
        });
        LinearLayout.LayoutParams lp2 = new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1);
        lp2.leftMargin = dp(8);
        bar.addView(abortButton, lp2);
        root.addView(bar);
        return root;
    }

    private TextView secondaryButton(String text) {
        TextView b = label(text, 15, TEXT, false);
        b.setGravity(Gravity.CENTER);
        b.setPadding(0, dp(14), 0, dp(14));
        b.setBackground(box(PANEL, LINE, 10));
        return b;
    }

    private interface Choice {
        void chosen(int index);
    }

    private View question(String title, String[] options, int selected, Choice choice, String hint) {
        LinearLayout panel = new LinearLayout(this);
        panel.setOrientation(LinearLayout.VERTICAL);
        panel.setPadding(dp(14), dp(12), dp(14), dp(14));
        panel.setBackground(box(RAISED, LINE, 12));
        panel.addView(label(title, 15, TEXT, true));

        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        List<TextView> group = new ArrayList<>();
        for (int i = 0; i < options.length; i++) {
            TextView b = label(options[i], 14, TEXT, false);
            b.setGravity(Gravity.CENTER);
            b.setMinHeight(dp(48));
            b.setPadding(dp(4), dp(8), dp(4), dp(8));
            final int index = i;
            b.setOnClickListener(v -> {
                if (Jobs.get().running) {
                    return;
                }
                choice.chosen(index);
                settings.save(this);
                markGroup(group, index);
                onJobsChanged();
            });
            LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.MATCH_PARENT, 1);
            if (i > 0) {
                lp.leftMargin = dp(6);
            }
            row.addView(b, lp);
            group.add(b);
        }
        markGroup(group, selected);
        choiceGroups.add(group);
        panel.addView(row, fullWidth(10));
        if (hint != null) {
            TextView h = label(hint, 12, HINT, false);
            panel.addView(h, fullWidth(8));
        }
        return panel;
    }

    private void markGroup(List<TextView> group, int selected) {
        boolean locked = Jobs.get().running;
        for (int i = 0; i < group.size(); i++) {
            TextView b = group.get(i);
            boolean on = i == selected;
            b.setTag(on);
            if (on) {
                b.setBackground(box(locked ? Color.parseColor("#2b8a3e") : GREEN, 0, 10));
                b.setTextColor(NAVY);
                b.setTypeface(Typeface.DEFAULT_BOLD);
            } else {
                b.setBackground(box(PANEL, locked ? Color.parseColor("#1f3a55") : LINE, 10));
                b.setTextColor(locked ? Color.parseColor("#5c7690") : TEXT);
                b.setTypeface(Typeface.DEFAULT);
            }
        }
    }

    // ---------------------------------------------------------------- cards

    private Card createCard(Job job) {
        Card c = new Card();
        c.root = new LinearLayout(this);
        c.root.setOrientation(LinearLayout.VERTICAL);
        c.root.setPadding(dp(14), dp(12), dp(14), dp(12));
        c.root.setBackground(box(PANEL, LINE, 12));

        LinearLayout top = new LinearLayout(this);
        top.setOrientation(LinearLayout.HORIZONTAL);
        top.setGravity(Gravity.CENTER_VERTICAL);
        c.name = label((job.isVideo ? "🎬  " : "🖼  ") + job.name, 15, TEXT, true);
        c.name.setSingleLine(true);
        c.name.setEllipsize(android.text.TextUtils.TruncateAt.MIDDLE);
        top.addView(c.name, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));
        c.remove = label("✕", 18, MUTED, false);
        c.remove.setPadding(dp(12), dp(2), dp(4), dp(2));
        c.remove.setOnClickListener(v -> {
            if (job.state == Job.State.RUNNING) {
                return;
            }
            synchronized (Jobs.get()) {
                Jobs.get().list.remove(job);
            }
            onJobsChanged();
        });
        top.addView(c.remove);
        c.root.addView(top);

        c.status = label("", 13, MUTED, false);
        c.root.addView(c.status, fullWidth(4));
        c.sizes = label("", 13, TEXT, false);
        c.sizes.setLineSpacing(dp(3), 1f);
        c.root.addView(c.sizes, fullWidth(6));

        c.progress = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        c.progress.setMax(1000);
        c.progress.setProgressTintList(ColorStateList.valueOf(GREEN));
        c.progress.setProgressBackgroundTintList(ColorStateList.valueOf(LINE));
        c.root.addView(c.progress, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(6)));
        ((LinearLayout.LayoutParams) c.progress.getLayoutParams()).topMargin = dp(8);

        c.resultRow = new LinearLayout(this);
        c.resultRow.setOrientation(LinearLayout.HORIZONTAL);
        c.resultRow.setGravity(Gravity.CENTER_VERTICAL);
        c.result = label("", 13, GREEN, false);
        c.resultRow.addView(c.result, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));
        TextView open = label("Öffnen", 14, TEXT, false);
        open.setPadding(dp(14), dp(8), dp(14), dp(8));
        open.setBackground(box(RAISED, LINE, 8));
        open.setOnClickListener(v -> openResult(job));
        LinearLayout.LayoutParams olp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.WRAP_CONTENT);
        olp.leftMargin = dp(8);
        c.resultRow.addView(open, olp);
        c.root.addView(c.resultRow, fullWidth(8));
        return c;
    }

    private void updateCard(Card c, Job job, Settings s) {
        String status;
        int color;
        switch (job.state) {
            case RUNNING:
                status = Jobs.get().paused ? "Pause" : (job.detail.isEmpty() ? "läuft" : job.detail);
                color = Jobs.get().paused ? YELLOW : GREEN;
                break;
            case DONE:
                status = "✓ Fertig";
                color = GREEN;
                break;
            case FAILED:
                status = "Fehlgeschlagen: " + job.detail;
                color = RED;
                break;
            case ABORTED:
                status = "Abgebrochen";
                color = RED;
                break;
            default:
                status = "wartet";
                color = MUTED;
                break;
        }
        c.status.setText(status);
        c.status.setTextColor(color);

        Job.Plan p = job.plan(s);
        int[] before = job.shownSize(job.width, job.height);
        int[] after = job.shownSize(p.finalWidth, p.finalHeight);
        StringBuilder sb = new StringBuilder();
        sb.append("Vorher: ").append(Format.sizeName(before[0], before[1]))
                .append("\n→ Nachher: ").append(Format.sizeName(after[0], after[1]));
        sb.append("\nDatei: ").append(Format.bytes(job.inBytes)).append("   →   ");
        if (job.state == Job.State.DONE && job.outputBytes > 0) {
            sb.append(Format.bytes(job.outputBytes));
        } else {
            sb.append("ca. ").append(Format.bytes(job.estimatedBytes(p)));
        }
        if (job.isVideo) {
            sb.append("\nLänge: ").append(Format.length(job.durationUs / 1e6));
        }
        if (job.state == Job.State.WAITING) {
            double secs = job.estimatedSeconds(this, p);
            sb.append("\nDauer: ").append(secs < 0
                    ? "wird beim ersten Mal ermittelt" : "ca. " + Format.duration(secs));
        }
        c.sizes.setText(sb.toString());

        boolean showProgress = job.state == Job.State.RUNNING || job.state == Job.State.DONE;
        c.progress.setVisibility(showProgress ? View.VISIBLE : View.GONE);
        c.progress.setProgress((int) (job.progress * 1000));
        c.remove.setVisibility(job.state == Job.State.RUNNING ? View.INVISIBLE : View.VISIBLE);

        boolean done = job.state == Job.State.DONE && job.outputUri != null;
        c.resultRow.setVisibility(done ? View.VISIBLE : View.GONE);
        if (done) {
            c.result.setText("Gespeichert: " + job.outputName);
        }
    }

    @Override
    public void onJobsChanged() {
        Jobs jobs = Jobs.get();
        Settings s = jobs.running && jobs.runSettings != null ? jobs.runSettings : settings;
        List<Job> list;
        synchronized (jobs) {
            list = new ArrayList<>(jobs.list);
        }
        cards.keySet().retainAll(list);
        cardsBox.removeAllViews();
        for (Job job : list) {
            Card c = cards.get(job);
            if (c == null) {
                c = createCard(job);
                cards.put(job, c);
            }
            updateCard(c, job, s);
            if (c.root.getParent() != null) {
                ((ViewGroup) c.root.getParent()).removeView(c.root);
            }
            LinearLayout.LayoutParams lp = fullWidth(cardsBox.getChildCount() == 0 ? 6 : 10);
            cardsBox.addView(c.root, lp);
        }
        emptyLabel.setVisibility(list.isEmpty() ? View.VISIBLE : View.GONE);

        boolean running = jobs.running;
        startButton.setBackground(box(running ? Color.parseColor("#2b8a3e") : GREEN, 0, 10));
        startButton.setText(running ? "Läuft …" : "▶  Start");
        pauseButton.setText(jobs.paused ? "Fortsetzen" : "Pause");
        pauseButton.setAlpha(running ? 1f : 0.4f);
        pauseButton.setEnabled(running);
        abortButton.setAlpha(running ? 1f : 0.4f);
        abortButton.setEnabled(running);
        for (List<TextView> g : choiceGroups) {
            int sel = 0;
            for (int i = 0; i < g.size(); i++) {
                if (Boolean.TRUE.equals(g.get(i).getTag())) {
                    sel = i;
                }
            }
            markGroup(g, sel);
        }
    }

    // ---------------------------------------------------------------- actions

    private void loadGpuName() {
        if (!Jobs.get().gpuName.isEmpty()) {
            gpuLabel.setText(Jobs.get().gpuName);
            return;
        }
        new Thread(() -> {
            String name;
            try {
                name = Native.gpuName();
            } catch (Throwable t) {
                name = "";
            }
            final String shown = name.isEmpty() ? "Keine Grafik gefunden" : name;
            Jobs.get().gpuName = name;
            runOnUiThread(() -> gpuLabel.setText(shown));
        }).start();
    }

    private void pickFiles() {
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        i.setType("*/*");
        i.putExtra(Intent.EXTRA_MIME_TYPES, new String[] {"video/*", "image/*"});
        i.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true);
        startActivityForResult(i, PICK);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != PICK || resultCode != RESULT_OK || data == null) {
            return;
        }
        List<Uri> uris = new ArrayList<>();
        ClipData clip = data.getClipData();
        if (clip != null) {
            for (int i = 0; i < clip.getItemCount(); i++) {
                uris.add(clip.getItemAt(i).getUri());
            }
        } else if (data.getData() != null) {
            uris.add(data.getData());
        }
        for (Uri u : uris) {
            try {
                getContentResolver().takePersistableUriPermission(u, Intent.FLAG_GRANT_READ_URI_PERMISSION);
            } catch (Exception ignored) {
            }
        }
        addFiles(uris);
    }

    private void handleShare(Intent intent) {
        if (intent == null) {
            return;
        }
        List<Uri> uris = new ArrayList<>();
        if (Intent.ACTION_SEND.equals(intent.getAction())) {
            Uri u = intent.getParcelableExtra(Intent.EXTRA_STREAM);
            if (u != null) {
                uris.add(u);
            }
        } else if (Intent.ACTION_SEND_MULTIPLE.equals(intent.getAction())) {
            ArrayList<Uri> list = intent.getParcelableArrayListExtra(Intent.EXTRA_STREAM);
            if (list != null) {
                uris.addAll(list);
            }
        }
        intent.setAction(null);
        addFiles(uris);
    }

    private void addFiles(List<Uri> uris) {
        if (uris.isEmpty()) {
            return;
        }
        new Thread(() -> {
            int bad = 0;
            for (Uri u : uris) {
                if (Jobs.get().contains(u)) {
                    continue;
                }
                Job job = MediaInfo.read(this, u);
                if (job == null) {
                    bad++;
                    continue;
                }
                synchronized (Jobs.get()) {
                    Jobs.get().list.add(job);
                }
                Jobs.get().changed();
            }
            if (bad > 0) {
                final int n = bad;
                runOnUiThread(() -> Toast.makeText(this,
                        n == 1 ? "Eine Datei kann nicht gelesen werden." : n + " Dateien können nicht gelesen werden.",
                        Toast.LENGTH_LONG).show());
            }
        }).start();
    }

    private void start() {
        Jobs jobs = Jobs.get();
        if (jobs.running) {
            return;
        }
        boolean any = false;
        synchronized (jobs) {
            for (Job j : jobs.list) {
                if (j.state == Job.State.FAILED || j.state == Job.State.ABORTED) {
                    j.state = Job.State.WAITING;
                    j.detail = "";
                    j.progress = 0;
                }
                any |= j.state == Job.State.WAITING;
            }
        }
        if (!any) {
            Toast.makeText(this, "Erst Videos oder Fotos auswählen.", Toast.LENGTH_SHORT).show();
            return;
        }
        if (Build.VERSION.SDK_INT >= 33
                && checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[] {Manifest.permission.POST_NOTIFICATIONS}, 2);
        }
        settings.save(this);
        jobs.runSettings = settings.copy();
        jobs.running = true;
        jobs.abort = false;
        jobs.paused = false;
        onJobsChanged();
        ProcessingService.start(this);
    }

    private void openResult(Job job) {
        if (job.outputUri == null) {
            return;
        }
        Intent i = new Intent(Intent.ACTION_VIEW);
        i.setDataAndType(job.outputUri, job.isVideo ? "video/*" : "image/*");
        i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        try {
            startActivity(i);
        } catch (Exception e) {
            Toast.makeText(this, "Keine App zum Öffnen gefunden.", Toast.LENGTH_SHORT).show();
        }
    }
}
