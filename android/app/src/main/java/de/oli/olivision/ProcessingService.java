package de.oli.olivision;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.os.Build;
import android.os.IBinder;
import android.os.PowerManager;
import android.os.SystemClock;

/** Works through the list in the background, also when the phone is locked. */
public final class ProcessingService extends Service {
    private static final String CHANNEL = "work";
    private static final String CHANNEL_DONE = "done";
    private static final int ID = 1;
    private static final int ID_DONE = 2;
    private static long lastUpdate;

    private PowerManager.WakeLock wakeLock;
    private Thread worker;

    static void start(Context context) {
        Intent i = new Intent(context, ProcessingService.class);
        context.startForegroundService(i);
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        createChannels(this);
        Notification n = build(this, "Startet …", 0, true);
        if (Build.VERSION.SDK_INT >= 35) {
            startForeground(ID, n, ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PROCESSING);
        } else if (Build.VERSION.SDK_INT >= 29) {
            startForeground(ID, n, ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC);
        } else {
            startForeground(ID, n);
        }
        if (worker == null) {
            PowerManager pm = (PowerManager) getSystemService(POWER_SERVICE);
            wakeLock = pm.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "OliVision:work");
            wakeLock.acquire(24L * 60 * 60 * 1000);
            worker = new Thread(this::work, "OliVision");
            worker.setPriority(Thread.NORM_PRIORITY);
            worker.start();
        }
        return START_NOT_STICKY;
    }

    @Override
    public void onTimeout(int startId, int fgsType) {
        // Android stops long background work after some hours
        Jobs.get().abort = true;
    }

    private void work() {
        Jobs jobs = Jobs.get();
        jobs.running = true;
        jobs.abort = false;
        jobs.paused = false;
        jobs.changed();
        Settings settings = jobs.runSettings != null ? jobs.runSettings : Settings.load(this);
        int done = 0;
        int failed = 0;
        String lastError = null;

        Job job;
        while (!jobs.abort && (job = jobs.nextWaiting()) != null) {
            Job.Plan plan = job.plan(settings);
            job.state = Job.State.RUNNING;
            job.progress = 0;
            job.detail = "KI wird geladen …";
            jobs.changed();
            updateNotification(this);
            try {
                if (!Output.loadModel(this, plan.modelKey)) {
                    throw new IllegalStateException("Die KI lässt sich auf diesem Handy nicht starten.");
                }
                if (job.isVideo) {
                    new VideoProcessor(this, job, plan).run();
                } else {
                    ImageProcessor.run(this, job, plan);
                }
                job.state = Job.State.DONE;
                job.detail = "";
                done++;
            } catch (InterruptedException e) {
                job.state = Job.State.ABORTED;
                job.detail = "";
                job.progress = 0;
            } catch (Throwable e) {
                job.state = jobs.abort ? Job.State.ABORTED : Job.State.FAILED;
                String msg = e.getMessage();
                job.detail = msg != null && !msg.isEmpty() ? msg : e.getClass().getSimpleName();
                lastError = job.detail;
                if (!jobs.abort) {
                    failed++;
                }
            }
            jobs.changed();
        }

        jobs.running = false;
        jobs.paused = false;
        jobs.changed();
        showDone(done, failed, lastError);
        if (wakeLock != null && wakeLock.isHeld()) {
            wakeLock.release();
        }
        stopForeground(STOP_FOREGROUND_REMOVE);
        stopSelf();
    }

    private void showDone(int done, int failed, String error) {
        if (done == 0 && failed == 0) {
            return;
        }
        String text = done == 1 ? "1 Datei fertig" : done + " Dateien fertig";
        if (failed > 0) {
            text += " · " + failed + " fehlgeschlagen";
        }
        Notification.Builder b = new Notification.Builder(this, CHANNEL_DONE)
                .setSmallIcon(R.drawable.ic_notify)
                .setContentTitle("OliVision")
                .setContentText(text)
                .setColor(0xFF39D353)
                .setAutoCancel(true)
                .setContentIntent(openApp(this));
        if (failed > 0 && error != null) {
            b.setStyle(new Notification.BigTextStyle().bigText(text + "\n" + error));
        }
        getSystemService(NotificationManager.class).notify(ID_DONE, b.build());
    }

    static void updateNotification(Context context) {
        long now = SystemClock.elapsedRealtime();
        if (now - lastUpdate < 1000) {
            return;
        }
        lastUpdate = now;
        Jobs jobs = Jobs.get();
        Job current = null;
        int index = 0;
        int count;
        synchronized (jobs) {
            count = jobs.list.size();
            for (int i = 0; i < count; i++) {
                if (jobs.list.get(i).state == Job.State.RUNNING) {
                    current = jobs.list.get(i);
                    index = i + 1;
                }
            }
        }
        if (current == null) {
            return;
        }
        String title = current.name + " (" + index + " von " + count + ")";
        String text = jobs.paused ? "Pause" : current.detail;
        NotificationManager nm = context.getSystemService(NotificationManager.class);
        nm.notify(ID, build(context, title + "\n" + text, (int) (current.progress * 1000), false));
    }

    private static Notification build(Context c, String text, int progress, boolean indeterminate) {
        String[] parts = text.split("\n", 2);
        Notification.Builder b = new Notification.Builder(c, CHANNEL)
                .setSmallIcon(R.drawable.ic_notify)
                .setContentTitle(parts.length > 1 ? parts[0] : "OliVision")
                .setContentText(parts.length > 1 ? parts[1] : parts[0])
                .setColor(0xFF39D353)
                .setOngoing(true)
                .setOnlyAlertOnce(true)
                .setProgress(1000, progress, indeterminate)
                .setContentIntent(openApp(c));
        if (Build.VERSION.SDK_INT >= 31) {
            b.setForegroundServiceBehavior(Notification.FOREGROUND_SERVICE_IMMEDIATE);
        }
        return b.build();
    }

    private static PendingIntent openApp(Context c) {
        Intent i = new Intent(c, MainActivity.class).addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP);
        return PendingIntent.getActivity(c, 0, i, PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
    }

    private static void createChannels(Context c) {
        NotificationManager nm = c.getSystemService(NotificationManager.class);
        NotificationChannel work = new NotificationChannel(CHANNEL, "Bearbeitung", NotificationManager.IMPORTANCE_LOW);
        work.setShowBadge(false);
        nm.createNotificationChannel(work);
        nm.createNotificationChannel(
                new NotificationChannel(CHANNEL_DONE, "Fertig", NotificationManager.IMPORTANCE_DEFAULT));
    }
}
