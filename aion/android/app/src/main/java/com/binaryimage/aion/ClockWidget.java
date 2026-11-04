package com.binaryimage.aion;

import android.appwidget.AppWidgetManager;
import android.appwidget.AppWidgetProvider;
import android.content.Context;
import android.content.Intent;
import android.os.Bundle;

/** The home-screen widget. Rendering happens off the main thread. */
public class ClockWidget extends AppWidgetProvider {

    interface Job { void run(Context c); }

    void async(Context ctx, Job job) {
        final PendingResult pr = goAsync();
        final Context app = ctx.getApplicationContext();
        new Thread(() -> {
            try { job.run(app); } catch (Throwable ignored) { } finally { pr.finish(); }
        }, "aion-widget").start();
    }

    @Override
    public void onUpdate(Context ctx, AppWidgetManager mgr, int[] ids) {
        async(ctx, c -> { for (int id : ids) Widgets.update(c, id, false); });
    }

    @Override
    public void onAppWidgetOptionsChanged(Context ctx, AppWidgetManager mgr, int id, Bundle opts) {
        async(ctx, c -> Widgets.update(c, id, false));
    }

    @Override
    public void onDeleted(Context ctx, int[] ids) {
        for (int id : ids) {
            Store.removeWidget(ctx, id);
            Widgets.removeFiles(ctx, id);
        }
    }

    @Override
    public void onEnabled(Context ctx) { Widgets.scheduleMidnight(ctx); }

    @Override
    public void onDisabled(Context ctx) { Widgets.cancelMidnight(ctx); }

    @Override
    public void onReceive(Context ctx, Intent intent) {
        String a = intent.getAction();
        if (Widgets.ACTION_MIDNIGHT.equals(a) || Intent.ACTION_TIME_CHANGED.equals(a)
                || Intent.ACTION_TIMEZONE_CHANGED.equals(a) || Intent.ACTION_LOCALE_CHANGED.equals(a)
                || Intent.ACTION_BOOT_COMPLETED.equals(a) || Intent.ACTION_MY_PACKAGE_REPLACED.equals(a)) {
            async(ctx, Widgets::updateAll);
            return;
        }
        super.onReceive(ctx, intent);
    }
}
