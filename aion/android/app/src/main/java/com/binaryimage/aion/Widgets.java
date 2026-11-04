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

import java.io.File;
import java.io.FileOutputStream;
import java.time.LocalDate;
import java.time.LocalDateTime;
import java.time.ZoneId;

/**
 * Renders a widget's dial + hands to PNG files and hands them to the launcher's AnalogClock
 * through content URIs (a bitmap that big would not fit through the widget IPC).
 * The hands then move on their own, without this app running.
 */
public final class Widgets {
    private Widgets() { }

    public static final String AUTHORITY = "com.binaryimage.aion.clock";
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

    public static synchronized void update(Context c, int id, boolean forceRender) {
        Fonts.init(c);
        ClockStyle s = Store.forWidget(c, id);
        int size = sizeFor(c, id);
        LocalDate today = LocalDate.now();
        String key = s.toString().hashCode() + "_" + size + "_" + (usesDate(s) ? today.toString() : "") + "_v1";
        String last = Store.prefs(c).getString("key_" + id, "");
        int ver = Store.prefs(c).getInt("ver_" + id, 0);
        String base = "w" + id + "_" + ver;
        boolean have = new File(dir(c), base + "_dial.png").exists();
        if (forceRender || !key.equals(last) || !have) {
            ver++;
            base = "w" + id + "_" + ver;
            try {
                ClockRenderer r = new ClockRenderer(s, size, today);
                save(r.dial(), new File(dir(c), base + "_dial.png"));
                save(r.hourHand(), new File(dir(c), base + "_hour.png"));
                save(r.minuteHand(), new File(dir(c), base + "_min.png"));
                if (s.seconds) save(r.secondHand(), new File(dir(c), base + "_sec.png"));
            } catch (Exception e) {
                return;
            }
            Store.prefs(c).edit().putString("key_" + id, key).putInt("ver_" + id, ver).apply();
            cleanup(c, id, base);
        }
        push(c, id, s, base);
        scheduleMidnight(c);
    }

    static void save(Bitmap b, File f) throws Exception {
        File tmp = new File(f.getPath() + ".tmp");
        try (FileOutputStream o = new FileOutputStream(tmp)) {
            b.compress(Bitmap.CompressFormat.PNG, 100, o);
        }
        b.recycle();
        if (!tmp.renameTo(f)) throw new Exception("rename failed");
    }

    /** Keeps the current files and the previous set (the launcher may still be reading those). */
    static void cleanup(Context c, int id, String keepBase) {
        File[] fs = dir(c).listFiles();
        if (fs == null) return;
        String prefix = "w" + id + "_";
        int keepVer = Integer.parseInt(keepBase.substring(keepBase.lastIndexOf('_') + 1));
        for (File f : fs) {
            String n = f.getName();
            if (!n.startsWith(prefix)) continue;
            try {
                String rest = n.substring(prefix.length());
                int v = Integer.parseInt(rest.substring(0, rest.indexOf('_')));
                if (v < keepVer - 1) f.delete();
            } catch (Exception e) {
                f.delete();
            }
        }
    }

    public static void removeFiles(Context c, int id) {
        File[] fs = dir(c).listFiles();
        if (fs == null) return;
        for (File f : fs) if (f.getName().startsWith("w" + id + "_")) f.delete();
    }

    static Icon icon(String name) {
        return Icon.createWithContentUri(Uri.parse("content://" + AUTHORITY + "/" + name));
    }

    static void push(Context c, int id, ClockStyle s, String base) {
        RemoteViews rv = new RemoteViews(c.getPackageName(), s.seconds ? R.layout.widget_clock_s : R.layout.widget_clock);
        rv.setIcon(R.id.clock, "setDial", icon(base + "_dial.png"));
        rv.setIcon(R.id.clock, "setHourHand", icon(base + "_hour.png"));
        rv.setIcon(R.id.clock, "setMinuteHand", icon(base + "_min.png"));
        if (s.seconds) rv.setIcon(R.id.clock, "setSecondHand", icon(base + "_sec.png"));
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
