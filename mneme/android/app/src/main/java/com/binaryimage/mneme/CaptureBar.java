package com.binaryimage.mneme;

import android.Manifest;
import android.app.Activity;
import android.content.pm.PackageManager;
import android.media.MediaMetadataRetriever;
import android.media.MediaRecorder;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.text.TextUtils;
import android.view.Gravity;
import android.view.View;
import android.view.WindowManager;
import android.widget.LinearLayout;
import android.widget.TextView;

import java.io.File;

/** The bar at the bottom of every screen: MIC types what you say, HUM records a melody clip. */
final class CaptureBar extends LinearLayout implements Dictation.Sink {
    interface Host {
        /** A spoken line (already tidied) to put in the lyrics. */
        void onDictatedLine(String line);
        /** A spoken section name, e.g. "Chorus". */
        void onDictatedTag(String tag);
        /** The song a new clip goes into. */
        Store.Song clipTarget();
        /** The section the cursor is in, for naming the clip. */
        String clipSection();
        /** Stop any playback before the mic opens. */
        void beforeCapture();
        void onClipAdded(Store.Clip clip);
    }

    static final int PERM_REQUEST = 77;

    private final Activity act;
    private final Host host;
    private final TextView status, mic, hum;
    private final Dictation dict;
    private final Handler h = new Handler(Looper.getMainLooper());
    private MediaRecorder rec;
    private File recFile;
    private long recStart;
    private Runnable pending;
    private final Runnable tick = new Runnable() {
        @Override public void run() {
            if (rec == null) return;
            status.setText("●  RECORDING  " + Ui.time((int) (SystemClock.elapsedRealtime() - recStart)) + "   tap HUM to stop");
            h.postDelayed(this, 250);
        }
    };

    CaptureBar(Activity a, Host host) {
        super(a);
        this.act = a;
        this.host = host;
        setOrientation(VERTICAL);
        setBackgroundColor(Ui.BG);
        int p = Ui.dp(a, 12);
        setPadding(p, Ui.dp(a, 6), p, Ui.dp(a, 12));
        addView(Ui.divider(a));

        status = Ui.text(a, "MIC types what you say  ·  HUM records a melody", 13, Ui.DIM);
        status.setGravity(Gravity.CENTER);
        status.setMaxLines(2);
        status.setEllipsize(TextUtils.TruncateAt.START);
        status.setPadding(0, Ui.dp(a, 8), 0, Ui.dp(a, 8));
        addView(status, Ui.lp(LayoutParams.MATCH_PARENT, LayoutParams.WRAP_CONTENT));

        LinearLayout row = Ui.row(a);
        mic = Ui.solid(a, "🎙  MIC");
        hum = Ui.button(a, "●  HUM");
        mic.setMinHeight(Ui.dp(a, 52));
        hum.setMinHeight(Ui.dp(a, 52));
        row.addView(mic, Ui.margins(Ui.lp(0, LayoutParams.WRAP_CONTENT, 1), a, 0, 0, 6, 0));
        row.addView(hum, Ui.margins(Ui.lp(0, LayoutParams.WRAP_CONTENT, 1), a, 6, 0, 0, 0));
        addView(row, Ui.lp(LayoutParams.MATCH_PARENT, LayoutParams.WRAP_CONTENT));

        mic.setContentDescription("MIC button");
        hum.setContentDescription("HUM button");
        dict = new Dictation(a, this);
        mic.setOnClickListener(v -> toggleMic());
        hum.setOnClickListener(v -> toggleHum());
    }

    // ---------------------------------------------------------------- permission

    private void withMic(Runnable r) {
        if (act.checkSelfPermission(Manifest.permission.RECORD_AUDIO) == PackageManager.PERMISSION_GRANTED) {
            r.run();
        } else {
            pending = r;
            act.requestPermissions(new String[]{Manifest.permission.RECORD_AUDIO}, PERM_REQUEST);
        }
    }

    void onPermission(boolean granted) {
        Runnable r = pending;
        pending = null;
        if (granted && r != null) r.run();
        else if (!granted) status.setText("Mneme needs the mic for MIC and HUM - allow it in Settings > Apps > Mneme");
    }

    // ---------------------------------------------------------------- speech to text

    private void toggleMic() {
        if (dict.isOn()) {
            dict.stop();
            return;
        }
        if (rec != null) stopHum();
        withMic(() -> {
            if (!Dictation.available(act)) {
                status.setText("No voice typing service on this phone - install/enable Google voice typing");
                return;
            }
            host.beforeCapture();
            dict.start();
        });
    }

    @Override public void onPartial(String text) {
        status.setTextColor(Ui.FG);
        status.setText("“" + text + "”");
    }

    @Override public void onLine(String text) {
        String tag = Dictation.spokenTag(text);
        if (tag != null) {
            host.onDictatedTag(tag);
            status.setText("[" + tag + "]  added - keep talking");
        } else {
            String line = Dictation.tidy(text);
            host.onDictatedLine(line);
            status.setText("✓ " + line);
        }
        status.setTextColor(Ui.DIM);
    }

    @Override public void onState(boolean listening, String message) {
        Ui.setSolid(act, mic, true, listening ? Ui.REC : Ui.FG);
        mic.setText(listening ? "■  STOP MIC" : "🎙  MIC");
        mic.setTextColor(listening ? Ui.FG : Ui.BG);
        keepScreenOn(listening || rec != null);
        if (message != null) {
            status.setTextColor(listening ? Ui.FG : Ui.DIM);
            status.setText(listening ? message + "  say a line, pause, say the next.  Say \"chorus\" for a tag." : message);
        } else if (!listening) {
            status.setTextColor(Ui.DIM);
            status.setText("Mic off");
        }
    }

    // ---------------------------------------------------------------- melody clips

    private void toggleHum() {
        if (rec != null) {
            stopHum();
            return;
        }
        if (dict.isOn()) dict.stop();
        withMic(this::startHum);
    }

    private void startHum() {
        host.beforeCapture();
        Store.Song s = host.clipTarget();
        recFile = new File(Store.clipsDir(act), s.id + "-" + System.currentTimeMillis() + ".m4a");
        try {
            rec = new MediaRecorder(act);
            rec.setAudioSource(MediaRecorder.AudioSource.MIC);
            rec.setOutputFormat(MediaRecorder.OutputFormat.MPEG_4);
            rec.setAudioEncoder(MediaRecorder.AudioEncoder.AAC);
            rec.setAudioSamplingRate(44100);
            rec.setAudioEncodingBitRate(128000);
            rec.setAudioChannels(1);
            rec.setOutputFile(recFile.getAbsolutePath());
            rec.prepare();
            rec.start();
        } catch (Exception e) {
            if (rec != null) rec.release();
            rec = null;
            recFile.delete();
            status.setText("Couldn't open the mic for recording");
            return;
        }
        recStart = SystemClock.elapsedRealtime();
        Ui.setSolid(act, hum, true, Ui.REC);
        hum.setText("■  STOP HUM");
        hum.setTextColor(Ui.FG);
        status.setTextColor(Ui.REC);
        keepScreenOn(true);
        h.post(tick);
    }

    void stopHum() {
        if (rec == null) return;
        h.removeCallbacks(tick);
        boolean ok = true;
        try {
            rec.stop();
        } catch (RuntimeException e) {
            ok = false;   // stopped before any audio arrived
        }
        rec.release();
        rec = null;
        Ui.setSolid(act, hum, false, 0);
        hum.setText("●  HUM");
        keepScreenOn(dict.isOn());
        status.setTextColor(Ui.DIM);
        if (!ok || !recFile.exists() || recFile.length() < 1000) {
            recFile.delete();
            status.setText("Too short - nothing saved");
            return;
        }
        Store.Song s = host.clipTarget();
        Store.Clip k = new Store.Clip();
        k.file = recFile.getName();
        k.section = host.clipSection();
        k.created = System.currentTimeMillis();
        k.durMs = duration(recFile);
        int n = s.clips.size() + 1;
        k.name = (k.section == null || k.section.isEmpty() ? "Idea " : k.section + " idea ") + n;
        s.clips.add(k);
        s.updated = System.currentTimeMillis();
        Store.save(act, s);
        host.onClipAdded(k);
        status.setText("✓ Saved “" + k.name + "” (" + Ui.time(k.durMs) + ") to " + s.title);
    }

    static int duration(File f) {
        MediaMetadataRetriever m = new MediaMetadataRetriever();
        try {
            m.setDataSource(f.getAbsolutePath());
            String d = m.extractMetadata(MediaMetadataRetriever.METADATA_KEY_DURATION);
            return d == null ? 0 : Integer.parseInt(d);
        } catch (Exception e) {
            return 0;
        } finally {
            try { m.release(); } catch (Exception ignored) {}
        }
    }

    private void keepScreenOn(boolean on) {
        if (on) act.getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        else act.getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
    }

    boolean busy() { return rec != null || dict.isOn(); }

    /** Call from the activity's onPause: a clip in progress is saved, the mic is closed. */
    void pause() {
        if (rec != null) stopHum();
        if (dict.isOn()) dict.stop();
    }

    void destroy() {
        pause();
        dict.destroy();
        h.removeCallbacksAndMessages(null);
    }

    void setHint(String s) {
        if (!busy()) {
            status.setTextColor(Ui.DIM);
            status.setText(s);
        }
    }

    @Override protected void onDetachedFromWindow() {
        super.onDetachedFromWindow();
        h.removeCallbacksAndMessages(null);
    }

    View micButton() { return mic; }
    View humButton() { return hum; }
}
