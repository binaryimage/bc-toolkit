package com.binaryimage.aion;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;
import android.view.View;

import java.time.LocalTime;

/** Live, ticking preview: the dial bitmap plus the three hand bitmaps rotated to the current time. */
public class PreviewView extends View {
    private Bitmap dial, hour, min, sec;
    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
    private final RectF dst = new RectF();

    public PreviewView(Context c) { super(c); }

    public void set(Bitmap dial, Bitmap hour, Bitmap min, Bitmap sec) {
        this.dial = dial;
        this.hour = hour;
        this.min = min;
        this.sec = sec;
        invalidate();
    }

    public int renderSize() { return Math.max(200, Math.min(getWidth(), getHeight())); }

    @Override
    protected void onMeasure(int w, int h) {
        int sw = MeasureSpec.getSize(w), sh = MeasureSpec.getSize(h);
        int s = Math.min(sw, sh);
        setMeasuredDimension(s, s);
    }

    @Override
    protected void onDraw(Canvas cv) {
        if (dial == null) { postInvalidateDelayed(200); return; }
        float s = Math.min(getWidth(), getHeight());
        dst.set(0, 0, s, s);
        float cx = s / 2f;
        cv.drawBitmap(dial, null, dst, paint);
        LocalTime t = LocalTime.now();
        int h = t.getHour(), m = t.getMinute(), sc = t.getSecond();
        draw(cv, hour, ((h % 12) + m / 60f) * 30f, cx);
        draw(cv, min, (m + sc / 60f) * 6f, cx);
        if (sec != null) draw(cv, sec, sc * 6f, cx);
        long ms = System.currentTimeMillis() % 1000;
        postInvalidateDelayed(1000 - ms + 5);
    }

    private void draw(Canvas cv, Bitmap b, float deg, float cx) {
        if (b == null) return;
        cv.save();
        cv.rotate(deg, cx, cx);
        cv.drawBitmap(b, null, dst, paint);
        cv.restore();
    }
}
