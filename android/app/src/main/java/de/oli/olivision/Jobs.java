package de.oli.olivision;

import android.os.Handler;
import android.os.Looper;

import java.util.ArrayList;
import java.util.List;

/** The list of videos and photos, shared by the screen and the background service. */
final class Jobs {
    interface Listener {
        void onJobsChanged();
    }

    private static final Jobs INSTANCE = new Jobs();

    final List<Job> list = new ArrayList<>();
    volatile boolean running;
    volatile boolean paused;
    volatile boolean abort;
    Settings runSettings;
    String gpuName = "";

    private final List<Listener> listeners = new ArrayList<>();
    private final Handler main = new Handler(Looper.getMainLooper());
    private boolean posted;

    static Jobs get() {
        return INSTANCE;
    }

    synchronized void addListener(Listener l) {
        listeners.add(l);
    }

    synchronized void removeListener(Listener l) {
        listeners.remove(l);
    }

    /** Tell the screen something changed (at most a few times per second is enough). */
    void changed() {
        synchronized (this) {
            if (posted) {
                return;
            }
            posted = true;
        }
        main.postDelayed(() -> {
            List<Listener> copy;
            synchronized (Jobs.this) {
                posted = false;
                copy = new ArrayList<>(listeners);
            }
            for (Listener l : copy) {
                l.onJobsChanged();
            }
        }, 150);
    }

    synchronized Job nextWaiting() {
        for (Job j : list) {
            if (j.state == Job.State.WAITING) {
                return j;
            }
        }
        return null;
    }

    synchronized boolean contains(android.net.Uri uri) {
        for (Job j : list) {
            if (j.uri.equals(uri) && j.state != Job.State.DONE) {
                return true;
            }
        }
        return false;
    }
}
