package com.binaryimage.aion;

import android.app.AlarmManager;
import android.app.PendingIntent;
import android.appwidget.AppWidgetManager;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.graphics.Bitmap;
import android.graphics.drawable.Icon;
import android.net.Uri;
import android.os.Bundle;
import android.widget.RemoteViews;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.time.LocalDate;
import java.time.LocalDateTime;
import java.time.ZoneId;

/**
 * Renders a widget's dial + hands and hands them to the launcher's AnalogClock as compressed
 * image data. The hands then move on their own, without this app running.
 */
public final class Widgets {
    private Widgets() { }

    public static final String ACTION_MIDNIGHT = "com.binaryimage.aion.MIDNIGHT";

    static File dir(Context c) {
        File d = new File(c.getFilesDir(), "clock");
        if (!d.exists()) d.mkdirs();
        return d;
    }

    static int[] ids(Context c) {
        AppWidgetManager m = AppWidgetManager.getInstance(c);
        return m.getAppWidgetIds(new ComponentName(c, ClockWidget.class));
    }

    public static void updateAll(Context c) {
        for (int id : ids(c)) update(c, id, false);
    }

    /** Pixel size to render at, from the size the launcher reports. */
    static int sizeFor(Context c, int id) {
        Bundle o = AppWidgetManager.getInstance(c).getAppWidgetOptions(id);
        float dens = c.getResources().getDisplayMetrics().density;
        int minW = o.getInt(AppWidgetManager.OPTION_APPWIDGET_MIN_WIDTH, 0);
        int maxW = o.getInt(AppWidgetManager.OPTION_APPWIDGET_MAX_WIDTH, 0);
        int minH = o.getInt(AppWidgetManager.OPTION_APPWIDGET_MIN_HEIGHT, 0);
        int maxH = o.getInt(AppWidgetManager.OPTION_APPWIDGET_MAX_HEIGHT, 0);
        int dp = Math.max(Math.min(minW, maxH), Math.min(maxW, minH));
        if (dp <= 0) dp = 220;
        int px = Math.round(dp * dens);
        return Math.max(360, Math.min(1080, px));
    }

    static boolean usesDate(ClockStyle s) {
        return s.comp12 != 0 || s.comp3 != 0 || s.comp6 != 0 || s.comp9 != 0;
    }

    static final String[] PARTS = {"dial", "hour", "min", "sec"};

    public static synchronized void update(Context c, int id, boolean forceRender) {
        Fonts.init(c);
        ClockStyle s = Store.forWidget(c, id);
        int size = sizeFor(c, id);
        LocalDate today = LocalDate.now();
        String key = s.toString().hashCode() + "_" + size + "_" + (usesDate(s) ? today.toString() : "") + "_v2";
        String last = Store.prefs(c).getString("key_" + id, "");
        byte[][] img = new byte[4][];
        boolean have = key.equals(last) && !forceRender;
        if (have) {
            for (int k = 0; k < 4; k++) {
                if (k == 3 && !s.seconds) continue;
                img[k] = read(new File(dir(c), "w" + id + "_" + PARTS[k] + ".webp"));
                if (img[k] == null) have = false;
            }
        }
        if (!have) {
            try {
                ClockRenderer r = new ClockRenderer(s, size, today);
                img[0] = encode(r.dial());
                img[1] = encode(crop(r.hourHand()));
                img[2] = encode(crop(r.minuteHand()));
                img[3] = s.seconds ? encode(crop(r.secondHand())) : null;
                for (int k = 0; k < 4; k++) if (img[k] != null) write(new File(dir(c), "w" + id + "_" + PARTS[k] + ".webp"), img[k]);
            } catch (Exception e) {
                return;
            }
            Store.prefs(c).edit().putString("key_" + id, key).apply();
        }
        push(c, id, s, img);
        scheduleMidnight(c);
    }

    /**
     * AnalogClock centres each hand drawable on the pivot, so a hand can be cut down to the
     * smallest box that is still symmetric around the centre. Keeps the widget update small.
     */
    static Bitmap crop(Bitmap full) {
        int w = full.getWidth(), h = full.getHeight();
        int[] px = new int[w * h];
        full.getPixels(px, 0, w, 0, 0, w, h);
        int cx = w / 2, cy = h / 2, maxDx = 1, maxDy = 1;
        for (int y = 0; y < h; y++) {
            int row = y * w;
            for (int x = 0; x < w; x++) {
                if ((px[row + x] >>> 24) > 2) {
                    int dx = Math.abs(x - cx), dy = Math.abs(y - cy);
                    if (dx > maxDx) maxDx = dx;
                    if (dy > maxDy) maxDy = dy;
                }
            }
        }
        int hw = Math.min(cx, maxDx + 2), hh = Math.min(cy, maxDy + 2);
        Bitmap out = Bitmap.createBitmap(full, cx - hw, cy - hh, hw * 2, hh * 2);
        if (out != full) full.recycle();
        return out;
    }

    static byte[] encode(Bitmap b) {
        ByteArrayOutputStream o = new ByteArrayOutputStream();
        b.compress(Bitmap.CompressFormat.WEBP_LOSSLESS, 75, o);
        b.recycle();
        return o.toByteArray();
    }

    static void write(File f, byte[] data) throws Exception {
        File tmp = new File(f.getPath() + ".tmp");
        try (FileOutputStream o = new FileOutputStream(tmp)) { o.write(data); }
        if (!tmp.renameTo(f)) throw new Exception("rename failed");
    }

    static byte[] read(File f) {
        if (!f.exists()) return null;
        try (FileInputStream in = new FileInputStream(f)) {
            ByteArrayOutputStream o = new ByteArrayOutputStream();
            byte[] buf = new byte[65536];
            int n;
            while ((n = in.read(buf)) > 0) o.write(buf, 0, n);
            return o.toByteArray();
        } catch (Exception e) {
            return null;
        }
    }

    public static void removeFiles(Context c, int id) {
        File[] fs = dir(c).listFiles();
        if (fs == null) return;
        for (File f : fs) if (f.getName().startsWith("w" + id + "_")) f.delete();
    }

    /** The images travel inside the widget update as compressed WebP data, so the launcher never has to call back into this app. */
    static Icon icon(byte[] data) { return Icon.createWithData(data, 0, data.length); }

    static void push(Context c, int id, ClockStyle s, byte[][] img) {
        RemoteViews rv = new RemoteViews(c.getPackageName(), s.seconds ? R.layout.widget_clock_s : R.layout.widget_clock);
        rv.setIcon(R.id.clock, "setDial", icon(img[0]));
        rv.setIcon(R.id.clock, "setHourHand", icon(img[1]));
        rv.setIcon(R.id.clock, "setMinuteHand", icon(img[2]));
        if (s.seconds && img[3] != null) rv.setIcon(R.id.clock, "setSecondHand", icon(img[3]));
        Intent edit = new Intent(c, EditorActivity.class)
                .putExtra(AppWidgetManager.EXTRA_APPWIDGET_ID, id)
                .setData(Uri.parse("aion://widget/" + id))
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TOP);
        rv.setOnClickPendingIntent(R.id.root, PendingIntent.getActivity(c, id, edit,
                PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT));
        AppWidgetManager.getInstance(c).updateAppWidget(id, rv);
    }

    // ---------------------------------------------------------------- date roll-over

    static void scheduleMidnight(Context c) {
        AlarmManager am = c.getSystemService(AlarmManager.class);
        if (am == null) return;
        LocalDateTime next = LocalDate.now().plusDays(1).atStartOfDay().plusSeconds(5);
        long at = next.atZone(ZoneId.systemDefault()).toInstant().toEpochMilli();
        am.setAndAllowWhileIdle(AlarmManager.RTC_WAKEUP, at, midnightIntent(c));
    }

    static void cancelMidnight(Context c) {
        AlarmManager am = c.getSystemService(AlarmManager.class);
        if (am != null) am.cancel(midnightIntent(c));
    }

    static PendingIntent midnightIntent(Context c) {
        Intent i = new Intent(c, ClockWidget.class).setAction(ACTION_MIDNIGHT);
        return PendingIntent.getBroadcast(c, 1, i, PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
    }
}
