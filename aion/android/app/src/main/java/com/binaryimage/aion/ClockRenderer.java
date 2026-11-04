package com.binaryimage.aion;

import android.graphics.Bitmap;
import android.graphics.BlurMaskFilter;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.LinearGradient;
import android.graphics.Matrix;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RadialGradient;
import android.graphics.Rect;
import android.graphics.RectF;
import android.graphics.Shader;
import android.graphics.SweepGradient;
import android.graphics.Typeface;

import java.time.LocalDate;
import java.time.format.TextStyle;
import java.time.temporal.IsoFields;
import java.util.Locale;
import java.util.Random;

/**
 * Draws a clock face (everything that does not move) and the three hands, each as a square
 * bitmap the same size, with the pivot in the middle. AnalogClock rotates the hand bitmaps.
 */
public final class ClockRenderer {

    final ClockStyle s;
    final int size;
    final float c;      // centre
    final float R;      // outer radius of the case
    final float u;      // face radius
    final LocalDate date;

    public ClockRenderer(ClockStyle style, int size, LocalDate date) {
        this.s = style;
        this.size = size;
        this.c = size / 2f;
        this.R = size * 0.5f * 0.95f;
        this.u = R * (1f - bezelWidth(style.bezel));
        this.date = date;
    }

    static float bezelWidth(int b) {
        switch (b) {
            case 1: return 0.025f;
            case 2: return 0.07f;
            case 3: return 0.06f;
            case 4: return 0.085f;
            case 5: return 0.075f;
            case 6: return 0.16f;
            case 7: return 0.15f;
            case 8: return 0.16f;
            case 9: return 0.10f;
            case 10: return 0.10f;
            case 11: return 0.05f;
            case 12: return 0.14f;
            case 13: return 0.075f;
            case 14: return 0.15f;
            default: return 0f;
        }
    }

    // ================================================================== shape geometry

    /** Distance from centre to the outline at angle a (0 = 12 o'clock, clockwise), in units of the radius. */
    float shapeR(double a) {
        double sx = Math.abs(Math.sin(a)), cy = Math.abs(Math.cos(a));
        switch (s.shape) {
            case 1: return superR(sx, cy, 6, 1);
            case 2: return superR(sx, cy, 4, 1);
            case 3: return superR(sx, cy, 2.6, 1);
            case 4: return (float) (1.0 / Math.max(sx, cy));
            case 5: return polyR(a, 8, 1);
            case 6: return polyR(a, 6, (float) Math.cos(Math.PI / 6));
            case 7: return polyR(a, 12, 1);
            case 8: return (float) (0.965 + 0.035 * Math.abs(Math.cos(12 * a)));
            case 9: return (float) (1.0 / (sx + cy));
            case 10: return (float) (0.86 + 0.14 * (0.5 + 0.5 * Math.cos(8 * a)));
            case 11: return superR(sx / 0.84, cy, 3, 1);
            default: return 1f;
        }
    }

    static float superR(double sx, double cy, double n, double k) {
        return (float) (k * Math.pow(Math.pow(sx, n) + Math.pow(cy, n), -1.0 / n));
    }

    static float polyR(double a, int k, float apothem) {
        double seg = 2 * Math.PI / k;
        double phi = ((a + seg / 2) % seg + seg) % seg - seg / 2;
        return (float) (apothem / Math.cos(phi));
    }

    /** Placement factor for marks: follows the case outline if the style asks for it. */
    float f(double a) { return s.followShape ? Math.min(shapeR(a), 1.45f) : 1f; }

    Path shapePath(float r) {
        Path p = new Path();
        if (s.shape == 0) { p.addCircle(c, c, r, Path.Direction.CW); return p; }
        if (s.shape == 4) { p.addRect(c - r, c - r, c + r, c + r, Path.Direction.CW); return p; }
        int n = 720;
        for (int i = 0; i < n; i++) {
            double a = 2 * Math.PI * i / n;
            float rr = r * shapeR(a);
            float x = c + (float) (rr * Math.sin(a)), y = c - (float) (rr * Math.cos(a));
            if (i == 0) p.moveTo(x, y); else p.lineTo(x, y);
        }
        p.close();
        return p;
    }

    float px(double a, float r) { return c + (float) (r * Math.sin(a)); }
    float py(double a, float r) { return c - (float) (r * Math.cos(a)); }
    static float deg(double a) { return (float) Math.toDegrees(a); }

    static Paint fill(int color) {
        Paint p = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
        p.setStyle(Paint.Style.FILL);
        p.setColor(color);
        return p;
    }

    static Paint stroke(int color, float w) {
        Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
        p.setStyle(Paint.Style.STROKE);
        p.setColor(color);
        p.setStrokeWidth(w);
        p.setStrokeCap(Paint.Cap.ROUND);
        p.setStrokeJoin(Paint.Join.ROUND);
        return p;
    }

    // ================================================================== dial

    public Bitmap dial() {
        Bitmap bmp = Bitmap.createBitmap(size, size, Bitmap.Config.ARGB_8888);
        Canvas cv = new Canvas(bmp);
        Path outer = shapePath(R);
        Path face = shapePath(u);

        boolean solidCase = s.pattern != 18 || s.bezel != 0;
        if (s.depth && solidCase) {
            Paint sh = fill(0x66000000);
            sh.setMaskFilter(new BlurMaskFilter(R * 0.035f, BlurMaskFilter.Blur.NORMAL));
            cv.save();
            cv.translate(0, R * 0.018f);
            cv.drawPath(s.bezel != 0 ? outer : face, sh);
            cv.restore();
        }

        // face
        cv.save();
        cv.clipPath(face);
        drawPattern(cv);
        if (s.vignette && s.pattern != 18) {
            Paint v = fill(0xFFFFFFFF);
            v.setShader(new RadialGradient(c, c, u * 1.05f, new int[]{0, 0, 0x70000000}, new float[]{0, 0.62f, 1f}, Shader.TileMode.CLAMP));
            cv.drawRect(0, 0, size, size, v);
        }
        if (s.depth && s.pattern != 18) {
            Paint in = stroke(0x99000000, u * 0.06f);
            in.setMaskFilter(new BlurMaskFilter(u * 0.04f, BlurMaskFilter.Blur.NORMAL));
            cv.drawPath(face, in);
        }
        cv.restore();

        if (s.bezel != 0) drawBezel(cv, outer, face);

        drawTicks(cv);
        drawNumerals(cv);
        drawLettering(cv);
        drawComplications(cv);

        if (s.gloss) {
            cv.save();
            cv.clipPath(face);
            Paint g = fill(0xFFFFFFFF);
            g.setShader(new LinearGradient(0, c - u, 0, c + u * 0.1f, 0x40FFFFFF, 0x00FFFFFF, Shader.TileMode.CLAMP));
            cv.drawOval(new RectF(c - u * 1.1f, c - u * 1.2f, c + u * 1.1f, c + u * 0.05f), g);
            cv.restore();
        }
        return bmp;
    }

    // ------------------------------------------------------------------ patterns

    void drawPattern(Canvas cv) {
        int A = s.faceA, B = s.faceB;
        float E = u * 1.5f;
        float d = s.detail;
        Random rnd = new Random(s.seed * 7919L + 13);
        switch (s.pattern) {
            case 0: cv.drawColor(A); break;
            case 1: shade(cv, new LinearGradient(0, c - u, 0, c + u, A, B, Shader.TileMode.CLAMP)); break;
            case 2: shade(cv, new RadialGradient(c, c - u * 0.1f, u * 1.15f, A, B, Shader.TileMode.CLAMP)); break;
            case 3: { // sunburst
                shade(cv, new RadialGradient(c, c, u * 1.2f, A, B, Shader.TileMode.CLAMP));
                int rays = 60 + Math.round(d * 140);
                double step = 2 * Math.PI / rays;
                Paint p = fill(0xFFFFFFFF);
                for (int i = 0; i < rays; i++) {
                    double a0 = i * step, a1 = a0 + step;
                    double light = 0.5 + 0.5 * Math.cos(2 * (a0 - Math.PI / 4));
                    int al = (int) (10 + 34 * light);
                    p.setColor(i % 2 == 0 ? Col.alpha(0xFFFFFF, al) : Col.alpha(0x000000, al));
                    Path w = new Path();
                    w.moveTo(c, c);
                    w.lineTo(px(a0, E), py(a0, E));
                    w.lineTo(px(a1, E), py(a1, E));
                    w.close();
                    cv.drawPath(w, p);
                }
                break;
            }
            case 4: shade(cv, new SweepGradient(c, c, new int[]{A, B, A}, null)); break;
            case 5: { // guilloché rosette
                shade(cv, new RadialGradient(c, c, u, Col.lighten(A, 0.08f), Col.darken(A, 0.15f), Shader.TileMode.CLAMP));
                int n = 24 + Math.round(d * 56);
                Paint p = stroke(Col.alpha(B, 150), Math.max(1f, u * 0.0035f));
                float rr = u * 0.48f;
                for (int i = 0; i < n; i++) {
                    double a = 2 * Math.PI * i / n;
                    cv.drawCircle(px(a, rr), py(a, rr), rr, p);
                }
                break;
            }
            case 6: { // clous de paris
                float cell = u * (0.05f + 0.09f * (1 - d));
                cv.save();
                cv.rotate(45, c, c);
                Paint p = fill(0xFFFFFFFF);
                int[] tone = { Col.lighten(A, 0.28f), Col.darken(A, 0.12f), Col.darken(A, 0.38f), Col.lighten(A, 0.06f) };
                for (float y = c - E; y < c + E; y += cell) {
                    for (float x = c - E; x < c + E; x += cell) {
                        float mx = x + cell / 2, my = y + cell / 2;
                        float[][] corners = {{x, y}, {x + cell, y}, {x + cell, y + cell}, {x, y + cell}};
                        for (int k = 0; k < 4; k++) {
                            float[] p0 = corners[k], p1 = corners[(k + 1) % 4];
                            p.setColor(tone[k]);
                            Path t = new Path();
                            t.moveTo(mx, my);
                            t.lineTo(p0[0], p0[1]);
                            t.lineTo(p1[0], p1[1]);
                            t.close();
                            cv.drawPath(t, p);
                        }
                    }
                }
                cv.restore();
                break;
            }
            case 7: { // côtes de genève
                float band = u * (0.07f + 0.13f * (1 - d));
                cv.save();
                cv.rotate(-18, c, c);
                Paint p = fill(0xFFFFFFFF);
                for (float y = c - E; y < c + E; y += band) {
                    p.setShader(new LinearGradient(0, y, 0, y + band, new int[]{Col.darken(A, 0.1f), Col.lighten(B, 0.15f), Col.darken(A, 0.15f)},
                            new float[]{0, 0.45f, 1}, Shader.TileMode.CLAMP));
                    cv.drawRect(c - E, y, c + E, y + band + 1, p);
                }
                cv.restore();
                break;
            }
            case 8: { // carbon fibre
                float cell = u * (0.025f + 0.045f * (1 - d));
                int dark = Col.darken(A, 0.25f), light = Col.lighten(A, 0.16f);
                Paint p = fill(0xFFFFFFFF);
                int i = 0;
                for (float y = c - E; y < c + E; y += cell, i++) {
                    int j = 0;
                    for (float x = c - E; x < c + E; x += cell, j++) {
                        boolean vert = ((i / 2 + j / 2) % 2 == 0) == ((i + j) % 2 == 0);
                        p.setShader(vert ? new LinearGradient(x, 0, x + cell, 0, new int[]{dark, light, dark}, null, Shader.TileMode.CLAMP)
                                : new LinearGradient(0, y, 0, y + cell, new int[]{dark, light, dark}, null, Shader.TileMode.CLAMP));
                        cv.drawRect(x, y, x + cell + 0.5f, y + cell + 0.5f, p);
                    }
                }
                break;
            }
            case 9: { // galaxy
                shade(cv, new RadialGradient(c, c, u * 1.2f, Col.lighten(B, 0.05f), Col.darken(B, 0.6f), Shader.TileMode.CLAMP));
                int[] neb = { A, s.accent, Col.mix(A, s.accent, 0.5f), Col.lighten(A, 0.3f) };
                Paint p = fill(0xFFFFFFFF);
                for (int k = 0; k < 6; k++) {
                    float x = c + (rnd.nextFloat() - 0.5f) * u * 1.3f, y = c + (rnd.nextFloat() - 0.5f) * u * 1.3f;
                    float r = u * (0.3f + rnd.nextFloat() * 0.5f);
                    int col = neb[k % neb.length];
                    p.setShader(new RadialGradient(x, y, r, Col.alpha(col, 110), Col.alpha(col, 0), Shader.TileMode.CLAMP));
                    cv.drawCircle(x, y, r, p);
                }
                p.setShader(null);
                int stars = 150 + Math.round(d * 650);
                for (int k = 0; k < stars; k++) {
                    float x = c + (rnd.nextFloat() * 2 - 1) * E, y = c + (rnd.nextFloat() * 2 - 1) * E;
                    float r = u * (0.0015f + 0.005f * rnd.nextFloat() * rnd.nextFloat());
                    p.setColor(Col.alpha(0xFFFFFF, 80 + rnd.nextInt(175)));
                    cv.drawCircle(x, y, Math.max(0.6f, r), p);
                }
                Paint glint = stroke(0xCCFFFFFF, Math.max(1f, u * 0.003f));
                for (int k = 0; k < 7; k++) {
                    float x = c + (rnd.nextFloat() * 2 - 1) * u * 0.9f, y = c + (rnd.nextFloat() * 2 - 1) * u * 0.9f;
                    float r = u * (0.02f + 0.03f * rnd.nextFloat());
                    cv.drawLine(x - r, y, x + r, y, glint);
                    cv.drawLine(x, y - r, x, y + r, glint);
                    cv.drawCircle(x, y, Math.max(1f, u * 0.006f), fill(0xFFFFFFFF));
                }
                break;
            }
            case 10: { // vinyl grooves
                cv.drawColor(A);
                float step = u * (0.005f + 0.012f * (1 - d));
                Paint p = stroke(Col.darken(A, 0.25f), step * 0.45f);
                for (float r = step; r < E; r += step) cv.drawCircle(c, c, r, p);
                shade(cv, new SweepGradient(c, c, new int[]{0, 0x30FFFFFF, 0, 0, 0x30FFFFFF, 0, 0}, null));
                break;
            }
            case 11: { // brushed metal
                cv.drawColor(A);
                Paint p = stroke(0, Math.max(1f, u * 0.0025f));
                int n = 900 + Math.round(d * 1800);
                for (int k = 0; k < n; k++) {
                    double a = rnd.nextDouble() * Math.PI * 2;
                    p.setColor(rnd.nextBoolean() ? Col.alpha(0xFFFFFF, 8 + rnd.nextInt(20)) : Col.alpha(0, 8 + rnd.nextInt(24)));
                    cv.drawLine(px(a, u * 0.03f), py(a, u * 0.03f), px(a, E), py(a, E), p);
                }
                shade(cv, new SweepGradient(c, c, new int[]{0x22FFFFFF, 0x10000000, 0x28FFFFFF, 0x14000000, 0x22FFFFFF}, null));
                break;
            }
            case 12: { // honeycomb
                cv.drawColor(A);
                float r = u * (0.045f + 0.08f * (1 - d));
                float w = (float) (Math.sqrt(3) * r);
                Paint line = stroke(B, Math.max(1f, u * 0.006f));
                Paint in = fill(0xFFFFFFFF);
                int row = 0;
                for (float y = c - E; y < c + E; y += r * 1.5f, row++) {
                    for (float x = c - E + ((row % 2) * w / 2); x < c + E; x += w) {
                        Path h = new Path();
                        for (int k = 0; k < 6; k++) {
                            double a = Math.PI / 3 * k;
                            float hx = x + (float) (r * Math.sin(a)), hy = y - (float) (r * Math.cos(a));
                            if (k == 0) h.moveTo(hx, hy); else h.lineTo(hx, hy);
                        }
                        h.close();
                        in.setShader(new RadialGradient(x, y - r * 0.3f, r * 1.2f, Col.lighten(A, 0.12f), Col.darken(A, 0.2f), Shader.TileMode.CLAMP));
                        cv.drawPath(h, in);
                        cv.drawPath(h, line);
                    }
                }
                break;
            }
            case 13: { // tapisserie
                float cell = u * (0.05f + 0.08f * (1 - d));
                Paint p = fill(0xFFFFFFFF);
                for (float y = c - E; y < c + E; y += cell) {
                    for (float x = c - E; x < c + E; x += cell) {
                        p.setColor(Col.darken(A, 0.3f));
                        cv.drawRect(x, y, x + cell, y + cell, p);
                        float g = cell * 0.16f;
                        p.setColor(Col.lighten(A, 0.12f));
                        cv.drawRect(x + g, y + g, x + cell - g, y + cell - g, p);
                        p.setColor(Col.lighten(A, 0.35f));
                        cv.drawRect(x + g, y + g, x + cell - g, y + g * 1.6f, p);
                    }
                }
                break;
            }
            case 14: { // waves
                cv.drawColor(A);
                float gap = u * (0.03f + 0.06f * (1 - d));
                Paint p = stroke(Col.alpha(B, 170), Math.max(1f, u * 0.006f));
                for (float y = c - E; y < c + E; y += gap) {
                    Path w = new Path();
                    for (float x = c - E; x <= c + E; x += u * 0.02f) {
                        float yy = y + (float) Math.sin((x - c) / (u * 0.12f)) * gap * 0.55f;
                        if (x == c - E) w.moveTo(x, yy); else w.lineTo(x, yy);
                    }
                    cv.drawPath(w, p);
                }
                break;
            }
            case 15: { // aurora
                cv.drawColor(Col.darken(A, 0.55f));
                int[] cols = { A, B, s.accent, Col.mix(A, B, 0.5f), Col.mix(B, s.accent, 0.5f), Col.lighten(A, 0.3f) };
                Paint p = fill(0xFFFFFFFF);
                for (int k = 0; k < 7; k++) {
                    float x = c + (rnd.nextFloat() - 0.5f) * u * 1.4f, y = c + (rnd.nextFloat() - 0.5f) * u * 1.4f;
                    float r = u * (0.45f + rnd.nextFloat() * 0.5f);
                    int col = cols[k % cols.length];
                    p.setShader(new RadialGradient(x, y, r, Col.alpha(col, 200), Col.alpha(col, 0), Shader.TileMode.CLAMP));
                    cv.drawCircle(x, y, r, p);
                }
                break;
            }
            case 16: { // pinstripe
                cv.drawColor(A);
                float gap = u * (0.02f + 0.05f * (1 - d));
                Paint p = stroke(Col.alpha(B, 150), Math.max(1f, u * 0.0035f));
                for (float x = c - E; x < c + E; x += gap) cv.drawLine(x, c - E, x, c + E, p);
                break;
            }
            case 17: { // smoked glass
                cv.drawColor(Col.alpha(A, 0x88));
                shade(cv, new RadialGradient(c - u * 0.4f, c - u * 0.5f, u * 1.2f, 0x30FFFFFF, 0x00FFFFFF, Shader.TileMode.CLAMP));
                break;
            }
            case 18: break; // see-through
            case 19: { // hypno spiral
                cv.drawColor(A);
                float turns = 5 + d * 12;
                float k = u * 1.5f / (float) (turns * 2 * Math.PI);
                Paint p = stroke(B, k * (float) Math.PI * 0.95f);
                p.setStrokeCap(Paint.Cap.BUTT);
                Path sp = new Path();
                int n = 2400;
                for (int i = 0; i <= n; i++) {
                    double a = turns * 2 * Math.PI * i / n;
                    float r = (float) (k * a);
                    if (i == 0) sp.moveTo(px(a, r), py(a, r)); else sp.lineTo(px(a, r), py(a, r));
                }
                cv.drawPath(sp, p);
                break;
            }
            case 20: { // pop rays
                cv.drawColor(A);
                int n = 12 + Math.round(d * 36);
                Paint p = fill(B);
                for (int i = 0; i < n; i += 2) {
                    double a0 = 2 * Math.PI * i / n, a1 = 2 * Math.PI * (i + 1) / n;
                    Path w = new Path();
                    w.moveTo(c, c);
                    w.lineTo(px(a0, E), py(a0, E));
                    w.lineTo(px(a1, E), py(a1, E));
                    w.close();
                    cv.drawPath(w, p);
                }
                break;
            }
            case 21: { // halftone
                cv.drawColor(A);
                float gap = u * (0.035f + 0.06f * (1 - d));
                Paint p = fill(B);
                int row = 0;
                for (float y = c - E; y < c + E; y += gap * 0.866f, row++) {
                    for (float x = c - E + (row % 2) * gap / 2; x < c + E; x += gap) {
                        float dist = (float) Math.hypot(x - c, y - c) / u;
                        float r = gap * 0.5f * Math.min(1f, dist * dist);
                        if (r > 0.4f) cv.drawCircle(x, y, r, p);
                    }
                }
                break;
            }
            case 22: shade(cv, new LinearGradient(c - u, c - u, c + u, c + u, new int[]{A, s.accent, B}, null, Shader.TileMode.CLAMP)); break;
            case 23: { // topographic
                cv.drawColor(A);
                Paint p = stroke(Col.alpha(B, 170), Math.max(1f, u * 0.005f));
                for (int cIdx = 0; cIdx < 3; cIdx++) {
                    float cx = c + (rnd.nextFloat() - 0.5f) * u * 1.2f, cy = c + (rnd.nextFloat() - 0.5f) * u * 1.2f;
                    double ph1 = rnd.nextDouble() * 6.28, ph2 = rnd.nextDouble() * 6.28;
                    int rings = 8 + Math.round(d * 14);
                    for (int k = 1; k <= rings; k++) {
                        float base = u * 0.09f * k;
                        Path ring = new Path();
                        for (int i = 0; i <= 180; i++) {
                            double a = 2 * Math.PI * i / 180;
                            float r = base * (float) (1 + 0.12 * Math.sin(3 * a + ph1 + k * 0.3) + 0.07 * Math.sin(5 * a + ph2));
                            float x = cx + (float) (r * Math.sin(a)), y = cy - (float) (r * Math.cos(a));
                            if (i == 0) ring.moveTo(x, y); else ring.lineTo(x, y);
                        }
                        cv.drawPath(ring, p);
                    }
                }
                break;
            }
            case 24: { // plasma
                int n = 96;
                Bitmap small = Bitmap.createBitmap(n, n, Bitmap.Config.ARGB_8888);
                int[] px = new int[n * n];
                float fq = 3 + d * 9;
                double ph = rnd.nextDouble() * 6;
                for (int y = 0; y < n; y++) for (int x = 0; x < n; x++) {
                    double xx = (double) x / n, yy = (double) y / n;
                    double v = Math.sin(xx * fq + ph) + Math.sin((yy * fq + ph) * 0.8) + Math.sin((xx + yy) * fq * 0.7)
                            + Math.sin(Math.hypot(xx - 0.5, yy - 0.5) * fq * 1.6 - ph);
                    float t = (float) (0.5 + 0.5 * Math.sin(v * Math.PI * 0.5));
                    px[y * n + x] = t < 0.5f ? Col.mix(A, s.accent, t * 2) : Col.mix(s.accent, B, (t - 0.5f) * 2);
                }
                small.setPixels(px, 0, n, 0, 0, n, n);
                cv.drawBitmap(small, null, new RectF(c - E, c - E, c + E, c + E), fill(0xFFFFFFFF));
                break;
            }
            case 25: { // checkerboard
                float cell = u * (0.08f + 0.17f * (1 - d));
                cv.drawColor(A);
                Paint p = fill(B);
                int i = 0;
                for (float y = c - cell * (float) Math.ceil(E / cell); y < c + E; y += cell, i++) {
                    int j = 0;
                    for (float x = c - cell * (float) Math.ceil(E / cell); x < c + E; x += cell, j++)
                        if ((i + j) % 2 == 0) cv.drawRect(x, y, x + cell, y + cell, p);
                }
                break;
            }
            case 26: { // crackle (voronoi edges)
                int n = 200, pts = 18 + Math.round(d * 40);
                float[] vx = new float[pts], vy = new float[pts];
                for (int k = 0; k < pts; k++) { vx[k] = rnd.nextFloat(); vy[k] = rnd.nextFloat(); }
                int[] px = new int[n * n];
                for (int y = 0; y < n; y++) for (int x = 0; x < n; x++) {
                    float fx = (float) x / n, fy = (float) y / n, d1 = 9, d2 = 9;
                    for (int k = 0; k < pts; k++) {
                        float dx = fx - vx[k], dy = fy - vy[k];
                        float dd = dx * dx + dy * dy;
                        if (dd < d1) { d2 = d1; d1 = dd; } else if (dd < d2) d2 = dd;
                    }
                    float e = (float) (Math.sqrt(d2) - Math.sqrt(d1));
                    float t = Math.min(1f, e * 28f);
                    px[y * n + x] = Col.mix(B, Col.mix(A, Col.lighten(A, 0.15f), (float) Math.sqrt(d1) * 3), t);
                }
                Bitmap small = Bitmap.createBitmap(n, n, Bitmap.Config.ARGB_8888);
                small.setPixels(px, 0, n, 0, 0, n, n);
                cv.drawBitmap(small, null, new RectF(c - E, c - E, c + E, c + E), fill(0xFFFFFFFF));
                break;
            }
            case 27: { // sector
                cv.drawColor(A);
                Path ring = new Path();
                ring.addCircle(c, c, u * 0.8f, Path.Direction.CW);
                ring.addCircle(c, c, u * 0.42f, Path.Direction.CCW);
                cv.drawPath(ring, fill(B));
                Paint l = stroke(Col.alpha(s.tickColor, 160), Math.max(1f, u * 0.005f));
                cv.drawCircle(c, c, u * 0.8f, l);
                cv.drawCircle(c, c, u * 0.42f, l);
                break;
            }
            default: cv.drawColor(A);
        }
    }

    void shade(Canvas cv, Shader sh) {
        Paint p = fill(0xFFFFFFFF);
        p.setShader(sh);
        cv.drawRect(0, 0, size, size, p);
    }

    // ------------------------------------------------------------------ bezel

    void drawBezel(Canvas cv, Path outer, Path face) {
        Path ring = new Path(outer);
        ring.op(face, Path.Op.DIFFERENCE);
        int bc = s.bezelColor, ba = s.bezelAccent;
        float bw = R - u, rm = (R + u) / 2;
        switch (s.bezel) {
            case 1: cv.drawPath(ring, fill(bc)); break;
            case 2: {
                Paint p = fill(0xFFFFFFFF);
                p.setShader(new LinearGradient(0, c - R, 0, c + R, Col.lighten(bc, 0.2f), Col.darken(bc, 0.25f), Shader.TileMode.CLAMP));
                cv.drawPath(ring, p);
                break;
            }
            case 3: {
                cv.drawPath(ring, fill(ba));
                Paint l = stroke(bc, bw * 0.22f);
                cv.drawPath(shapePath(R - bw * 0.11f), l);
                cv.drawPath(shapePath(u + bw * 0.11f), l);
                break;
            }
            case 4: case 13: {
                Paint p = fill(0xFFFFFFFF);
                p.setShader(new SweepGradient(c, c, new int[]{Col.lighten(bc, 0.55f), Col.darken(bc, 0.35f), Col.lighten(bc, 0.35f),
                        Col.darken(bc, 0.5f), Col.lighten(bc, 0.55f)}, null));
                cv.drawPath(ring, p);
                if (s.bezel == 13) { // rope
                    int n = Math.max(24, (int) (2 * Math.PI * rm / (bw * 0.55f)));
                    Paint t = stroke(Col.darken(bc, 0.45f), Math.max(1f, bw * 0.09f));
                    for (int i = 0; i < n; i++) {
                        double a = 2 * Math.PI * i / n, a2 = a + 2 * Math.PI / n * 0.9;
                        float k = shapeR(a);
                        cv.drawLine(px(a, (R - bw * 0.1f) * k), py(a, (R - bw * 0.1f) * k), px(a2, (u + bw * 0.1f) * k), py(a2, (u + bw * 0.1f) * k), t);
                    }
                }
                break;
            }
            case 5: {
                cv.drawPath(ring, fill(bc));
                Paint t = stroke(Col.darken(bc, 0.4f), Math.max(1f, R * 0.006f));
                for (int i = 0; i < 160; i++) {
                    double a = 2 * Math.PI * i / 160;
                    float k = shapeR(a);
                    cv.drawLine(px(a, R * k), py(a, R * k), px(a, (R - bw * 0.7f) * k), py(a, (R - bw * 0.7f) * k), t);
                }
                break;
            }
            case 6: { // diver
                cv.drawPath(ring, fill(ba));
                cv.drawPath(shapePath(R - bw * 0.04f), stroke(Col.alpha(bc, 120), bw * 0.06f));
                int tc = Col.contrast(ba) == 0xFF111111 ? Col.darken(bc, 0.6f) : bc;
                if (Col.lum(ba) > 0.55f) tc = 0xFF151515;
                Paint t = fill(tc);
                for (int i = 0; i < 60; i++) {
                    double a = 2 * Math.PI * i / 60;
                    float k = shapeR(a);
                    if (i == 0) {
                        Path tri = new Path();
                        float r0 = (R - bw * 0.15f) * k, r1 = (u + bw * 0.28f) * k;
                        tri.moveTo(px(0, r1), py(0, r1));
                        tri.lineTo(c - bw * 0.32f, py(0, r0));
                        tri.lineTo(c + bw * 0.32f, py(0, r0));
                        tri.close();
                        cv.drawPath(tri, t);
                        cv.drawCircle(c, py(0, (r0 + r1) / 2 + bw * 0.08f), bw * 0.09f, fill(s.handAccent));
                    } else if (i % 10 == 0) {
                        text(cv, String.valueOf(i), px(a, rm * k), py(a, rm * k), deg(a), bw * 0.42f, Fonts.get(s.numeralFont, 1), tc, 0, 0);
                    } else if (i % 5 == 0) {
                        cv.drawCircle(px(a, rm * k), py(a, rm * k), bw * 0.1f, t);
                    } else if (i < 15) {
                        radialLine(cv, a, (R - bw * 0.2f) * k, bw * 0.35f, bw * 0.06f, t);
                    }
                }
                break;
            }
            case 7: { // tachymeter
                cv.drawPath(ring, fill(ba));
                int tc = Col.lum(ba) > 0.55f ? 0xFF151515 : bc;
                Paint t = fill(tc);
                int[] speeds = {500, 400, 300, 250, 200, 180, 160, 140, 120, 110, 100, 90, 80, 75, 70, 65, 60};
                for (int v : speeds) {
                    double a = 2 * Math.PI * (3600.0 / v) / 60.0;
                    float k = shapeR(a);
                    text(cv, String.valueOf(v), px(a, (rm - bw * 0.06f) * k), py(a, (rm - bw * 0.06f) * k), deg(a), bw * 0.3f, Fonts.get(s.numeralFont, 1), tc, 0, 0);
                    radialLine(cv, a, (R - bw * 0.03f) * k, bw * 0.16f, bw * 0.04f, t);
                }
                text(cv, "TACHYMETER", c, py(0, rm), 0, bw * 0.26f, Fonts.get(s.numeralFont, 1), tc, 0, 0);
                break;
            }
            case 8: { // GMT two tone
                cv.save();
                cv.clipPath(ring);
                cv.drawRect(0, 0, size, c, fill(ba));
                cv.drawRect(0, c, size, size, fill(bc));
                cv.restore();
                for (int h = 0; h < 24; h += 2) {
                    double a = 2 * Math.PI * h / 24;
                    float k = shapeR(a);
                    int bg = (h > 6 && h < 18) ? bc : ba;
                    int tc = Col.contrast(bg);
                    if (h == 0) {
                        Path tri = new Path();
                        float r0 = (R - bw * 0.18f) * k, r1 = (u + bw * 0.2f) * k;
                        tri.moveTo(c, py(0, r1));
                        tri.lineTo(c - bw * 0.3f, py(0, r0));
                        tri.lineTo(c + bw * 0.3f, py(0, r0));
                        tri.close();
                        cv.drawPath(tri, fill(tc));
                    } else {
                        text(cv, String.valueOf(h), px(a, rm * k), py(a, rm * k), deg(a), bw * 0.42f, Fonts.get(s.numeralFont, 1), tc, 0, 0);
                    }
                    double a2 = a + 2 * Math.PI / 24;
                    cv.drawCircle(px(a2, rm * shapeR(a2)), py(a2, rm * shapeR(a2)), bw * 0.06f, fill(Col.contrast((h + 1 > 6 && h + 1 < 18) ? bc : ba)));
                }
                break;
            }
            case 9: { // fluted
                int n = 72;
                Paint p = fill(0xFFFFFFFF);
                cv.save();
                cv.clipPath(ring);
                for (int i = 0; i < n; i++) {
                    double a0 = 2 * Math.PI * i / n, a1 = 2 * Math.PI * (i + 1) / n, am = (a0 + a1) / 2;
                    float E = R * 1.5f;
                    p.setShader(new LinearGradient(px(a0, rm), py(a0, rm), px(a1, rm), py(a1, rm),
                            new int[]{Col.darken(bc, 0.4f), Col.lighten(bc, 0.5f), Col.darken(bc, 0.2f)}, new float[]{0, 0.45f, 1}, Shader.TileMode.CLAMP));
                    Path w = new Path();
                    w.moveTo(c, c);
                    w.lineTo(px(a0, E), py(a0, E));
                    w.lineTo(px(am, E), py(am, E));
                    w.lineTo(px(a1, E), py(a1, E));
                    w.close();
                    cv.drawPath(w, p);
                }
                cv.restore();
                break;
            }
            case 10: { // gem set
                cv.drawPath(ring, fill(Col.darken(bc, 0.3f)));
                float gr = bw * 0.36f;
                int n = Math.max(12, (int) (2 * Math.PI * rm / (gr * 2.3f)));
                Paint g = fill(0xFFFFFFFF);
                for (int i = 0; i < n; i++) {
                    double a = 2 * Math.PI * i / n;
                    float k = shapeR(a), x = px(a, rm * k), y = py(a, rm * k);
                    g.setShader(new RadialGradient(x - gr * 0.3f, y - gr * 0.3f, gr * 1.3f, new int[]{0xFFFFFFFF, Col.lighten(ba, 0.4f), ba, Col.darken(ba, 0.5f)},
                            new float[]{0, 0.25f, 0.6f, 1}, Shader.TileMode.CLAMP));
                    cv.drawCircle(x, y, gr, g);
                    cv.drawCircle(x, y, gr, stroke(Col.lighten(bc, 0.3f), Math.max(1f, gr * 0.15f)));
                }
                break;
            }
            case 11: { // neon
                Paint glow = stroke(s.accent, bw * 0.9f);
                glow.setMaskFilter(new BlurMaskFilter(bw * 0.9f, BlurMaskFilter.Blur.NORMAL));
                Path mid = shapePath(rm);
                cv.drawPath(mid, glow);
                cv.drawPath(mid, stroke(Col.lighten(s.accent, 0.6f), bw * 0.35f));
                break;
            }
            case 12: { // compass
                cv.drawPath(ring, fill(ba));
                int tc = Col.lum(ba) > 0.55f ? 0xFF151515 : bc;
                Paint t = fill(tc);
                String[] dirs = {"N", "E", "S", "W"};
                for (int i = 0; i < 72; i++) {
                    double a = 2 * Math.PI * i / 72;
                    float k = shapeR(a);
                    if (i % 18 == 0) {
                        text(cv, dirs[i / 18], px(a, rm * k), py(a, rm * k), deg(a), bw * 0.5f, Fonts.get(s.numeralFont, 1), i == 0 ? s.accent : tc, 0, 0);
                    } else if (i % 6 == 0) {
                        text(cv, String.valueOf(i * 5), px(a, rm * k), py(a, rm * k), deg(a), bw * 0.3f, Fonts.get(s.numeralFont, 0), tc, 0, 0);
                    } else {
                        radialLine(cv, a, (R - bw * 0.08f) * k, bw * (i % 2 == 0 ? 0.3f : 0.18f), bw * 0.05f, t);
                    }
                }
                break;
            }
            case 14: { // hour ring
                cv.drawPath(ring, fill(ba));
                int tc = Col.lum(ba) > 0.55f ? 0xFF151515 : bc;
                for (int h = 1; h <= 12; h++) {
                    double a = 2 * Math.PI * h / 12;
                    float k = shapeR(a);
                    text(cv, String.valueOf(h), px(a, rm * k), py(a, rm * k), 0, bw * 0.5f, Fonts.get(s.numeralFont, 1), tc, 0, 0);
                }
                break;
            }
            default: cv.drawPath(ring, fill(bc));
        }
    }

    // ------------------------------------------------------------------ ticks

    /** How far the hour marks reach in from the edge (in px), so numerals can sit inside them. */
    float tickReach() {
        float t = s.tickSize;
        switch (s.ticks) {
            case 0: return u * 0.02f;
            case 1: return u * 0.11f * t;
            case 2: return u * 0.12f * t;
            case 3: return u * 0.055f * t;
            case 4: return u * 0.1f * t;
            case 5: return u * 0.1f * t;
            case 6: return u * 0.15f * t;
            case 7: return u * 0.12f * t;
            case 8: return u * 0.07f * t;
            case 9: return u * 0.09f * t;
            case 10: return u * 0.1f * t;
            case 11: return u * 0.16f * t;
            case 12: return u * 0.08f * t;
            case 13: return u * 0.1f * t;
            case 14: return u * 0.11f * t;
            case 15: return u * 0.08f * t;
            case 16: return u * 0.15f * t;
            case 17: return u * 0.05f * t;
            case 18: return u * 0.1f * t;
            case 19: return u * 0.08f * t;
            default: return u * 0.1f;
        }
    }

    float edge(double a) { return u * 0.93f * f(a); }

    void radialLine(Canvas cv, double a, float rOut, float len, float w, Paint p) {
        cv.save();
        cv.rotate(deg(a), c, c);
        cv.drawRect(c - w / 2, c - rOut, c + w / 2, c - rOut + len, p);
        cv.restore();
    }

    /** Draws path (built around (0,0), "inward" = +y) at angle a with its origin at radius r. */
    void placed(Canvas cv, double a, float r, Path p, Paint paint) {
        cv.save();
        cv.rotate(deg(a), c, c);
        cv.translate(c, c - r);
        cv.drawPath(p, paint);
        cv.restore();
    }

    void drawTicks(Canvas cv) {
        if (s.ticks == 0 && s.marker12 == 0) return;
        float t = s.tickSize;
        int col = s.tickColor;
        Paint p = fill(col);
        Paint pl = stroke(col, Math.max(1f, u * 0.006f * t));
        String[] nums = Names.numeralSet(s.numerals);

        if (s.ticks == 4 || s.ticks == 10) { // railroad / sector rings
            float w = Math.max(1f, u * 0.0045f * t);
            Paint ring = stroke(col, w);
            float band = s.ticks == 4 ? u * 0.045f * t : u * 0.08f * t;
            if (s.followShape) {
                cv.drawPath(shapePath(u * 0.93f), ring);
                cv.drawPath(shapePath(u * 0.93f - band), ring);
            } else {
                cv.drawCircle(c, c, u * 0.93f, ring);
                cv.drawCircle(c, c, u * 0.93f - band, ring);
            }
            if (s.ticks == 10) cv.drawCircle(c, c, u * 0.5f, ring);
        }

        int count = s.ticks == 18 ? 120 : 60;
        for (int i = 0; i < count; i++) {
            double a = 2 * Math.PI * i / count;
            int min = s.ticks == 18 ? i / 2 : i;
            boolean half = s.ticks == 18 && i % 2 == 1;
            boolean hour = !half && min % 5 == 0;
            int h = min / 5;
            float ro = edge(a);
            if (hour && s.skipUnderNumerals && nums != null && nums[(h + 11) % 12] != null) continue;
            if (hour && h == 0 && s.marker12 != 0) { drawMarker12(cv, ro, p); continue; }
            switch (s.ticks) {
                case 1:
                    if (hour) radialLine(cv, a, ro, u * 0.11f * t, u * 0.02f * t, p);
                    else radialLine(cv, a, ro, u * 0.045f * t, u * 0.007f * t, p);
                    break;
                case 2:
                    if (hour) radialLine(cv, a, ro, u * 0.12f * t, u * 0.024f * t, p);
                    break;
                case 3:
                    if (hour) cv.drawCircle(px(a, ro - u * 0.03f * t), py(a, ro - u * 0.03f * t), u * 0.024f * t, p);
                    else cv.drawCircle(px(a, ro - u * 0.02f * t), py(a, ro - u * 0.02f * t), u * 0.008f * t, p);
                    break;
                case 4:
                    if (hour) radialLine(cv, a, ro, u * 0.1f * t, u * 0.018f * t, p);
                    else radialLine(cv, a, ro, u * 0.045f * t, u * 0.006f * t, p);
                    break;
                case 5:
                    if (hour) placed(cv, a, ro, HandShapes.poly(-u * 0.035f * t, 0, u * 0.035f * t, 0, 0, u * 0.1f * t), p);
                    else cv.drawCircle(px(a, ro - u * 0.015f), py(a, ro - u * 0.015f), u * 0.006f * t, p);
                    break;
                case 6:
                    if (hour) {
                        float L = u * 0.15f * t, W = u * 0.045f * t;
                        Path bar = HandShapes.rrect(-W / 2, 0, W / 2, L, W * 0.2f);
                        placed(cv, a, ro, bar, p);
                        placed(cv, a, ro, bar, stroke(Col.darken(col, 0.4f), Math.max(1f, W * 0.08f)));
                        placed(cv, a, ro, HandShapes.rrect(-W * 0.2f, L * 0.12f, W * 0.2f, L * 0.88f, W * 0.1f), fill(s.handAccent));
                    } else radialLine(cv, a, ro, u * 0.03f * t, u * 0.006f * t, p);
                    break;
                case 7:
                    if (hour) placed(cv, a, ro, HandShapes.poly(0, 0, u * 0.028f * t, u * 0.06f * t, 0, u * 0.12f * t, -u * 0.028f * t, u * 0.06f * t), p);
                    else cv.drawCircle(px(a, ro - u * 0.015f), py(a, ro - u * 0.015f), u * 0.006f * t, p);
                    break;
                case 8:
                    if (hour) cv.drawCircle(px(a, ro - u * 0.035f * t), py(a, ro - u * 0.035f * t), u * 0.028f * t, stroke(col, u * 0.009f * t));
                    else radialLine(cv, a, ro, u * 0.035f * t, u * 0.006f * t, p);
                    break;
                case 9:
                    if (hour) placed(cv, a, ro, HandShapes.star(0, u * 0.045f * t, u * 0.045f * t, u * 0.019f * t, 5), p);
                    else cv.drawCircle(px(a, ro - u * 0.015f), py(a, ro - u * 0.015f), u * 0.005f * t, p);
                    break;
                case 10:
                    radialLine(cv, a, ro, u * 0.08f * t, (hour ? 0.014f : 0.005f) * u * t, p);
                    if (hour) radialLine(cv, a, ro - u * 0.08f * t, ro - u * 0.08f * t - u * 0.5f, u * 0.004f * t, p);
                    break;
                case 11:
                    if (hour) placed(cv, a, ro, HandShapes.poly(-u * 0.028f * t, 0, u * 0.028f * t, 0, u * 0.009f * t, u * 0.16f * t, -u * 0.009f * t, u * 0.16f * t), p);
                    else radialLine(cv, a, ro, u * 0.03f * t, u * 0.005f * t, p);
                    break;
                case 12:
                    if (hour) radialLine(cv, a, ro, ro - u * 0.33f, u * 0.008f * t, p);
                    else radialLine(cv, a, ro, u * 0.03f * t, u * 0.005f * t, p);
                    break;
                case 13:
                    if (hour) radialLine(cv, a, ro, u * 0.1f * t, u * 0.03f * t, p);
                    else cv.drawCircle(px(a, ro - u * 0.015f), py(a, ro - u * 0.015f), u * 0.006f * t, p);
                    break;
                case 14:
                    if (hour) {
                        cv.save();
                        cv.rotate(deg(a), c, c);
                        float w = u * 0.014f * t, g = u * 0.016f * t, L = u * 0.11f * t;
                        cv.drawRect(c - g - w / 2, c - ro, c - g + w / 2, c - ro + L, p);
                        cv.drawRect(c + g - w / 2, c - ro, c + g + w / 2, c - ro + L, p);
                        cv.restore();
                    } else radialLine(cv, a, ro, u * 0.035f * t, u * 0.006f * t, p);
                    break;
                case 15:
                    if (hour) {
                        Path ch = new Path();
                        ch.moveTo(-u * 0.035f * t, 0);
                        ch.lineTo(0, u * 0.06f * t);
                        ch.lineTo(u * 0.035f * t, 0);
                        placed(cv, a, ro - u * 0.01f, ch, stroke(col, u * 0.016f * t));
                    } else cv.drawCircle(px(a, ro - u * 0.015f), py(a, ro - u * 0.015f), u * 0.006f * t, p);
                    break;
                case 16:
                    if (hour) {
                        radialLine(cv, a, ro, u * 0.08f * t, u * 0.016f * t, p);
                        cv.drawCircle(px(a, ro - u * 0.125f * t), py(a, ro - u * 0.125f * t), u * 0.018f * t, p);
                    } else radialLine(cv, a, ro, u * 0.03f * t, u * 0.005f * t, p);
                    break;
                case 17:
                    if (hour && h % 3 == 0) radialLine(cv, a, ro, u * 0.14f * t, u * 0.035f * t, p);
                    break;
                case 18: {
                    float len = half ? 0.02f : (min % 15 == 0 ? 0.1f : (min % 5 == 0 ? 0.07f : 0.035f));
                    float w = half ? 0.004f : (min % 5 == 0 ? 0.012f : 0.006f);
                    radialLine(cv, a, ro, u * len * t, u * w * t, p);
                    break;
                }
                case 19:
                    if (hour) {
                        float r = u * 0.03f * t;
                        Path moon = HandShapes.minus(HandShapes.circle(0, r * 1.3f, r), HandShapes.circle(r * 0.45f, r * 1.1f, r * 0.85f));
                        placed(cv, a, ro, moon, p);
                    } else cv.drawCircle(px(a, ro - u * 0.015f), py(a, ro - u * 0.015f), u * 0.005f * t, p);
                    break;
                default:
                    break;
            }
        }
    }

    void drawMarker12(Canvas cv, float ro, Paint p) {
        float t = s.tickSize;
        switch (s.marker12) {
            case 1: placed(cv, 0, ro, HandShapes.poly(-u * 0.05f * t, 0, u * 0.05f * t, 0, 0, u * 0.12f * t), p); break;
            case 2: {
                float w = u * 0.02f * t, g = u * 0.022f * t, L = u * 0.13f * t;
                cv.drawRect(c - g - w / 2, c - ro, c - g + w / 2, c - ro + L, p);
                cv.drawRect(c + g - w / 2, c - ro, c + g + w / 2, c - ro + L, p);
                break;
            }
            case 3: cv.drawCircle(c, c - ro + u * 0.045f * t, u * 0.04f * t, p); break;
            case 4: placed(cv, 0, ro, HandShapes.star(0, u * 0.06f * t, u * 0.06f * t, u * 0.025f * t, 5), p); break;
            case 5: {
                placed(cv, 0, ro, HandShapes.poly(-u * 0.055f * t, 0, u * 0.055f * t, 0, 0, u * 0.13f * t), p);
                cv.drawCircle(c - u * 0.085f * t, c - ro + u * 0.02f * t, u * 0.014f * t, p);
                cv.drawCircle(c + u * 0.085f * t, c - ro + u * 0.02f * t, u * 0.014f * t, p);
                break;
            }
            case 6: placed(cv, 0, ro, HandShapes.poly(0, 0, u * 0.04f * t, u * 0.075f * t, 0, u * 0.15f * t, -u * 0.04f * t, u * 0.075f * t), p); break;
            case 7: { // crown
                float w = u * 0.06f * t, hgt = u * 0.08f * t;
                Path cr = HandShapes.poly(-w, hgt, -w, hgt * 0.15f, -w * 0.5f, hgt * 0.55f, 0, 0, w * 0.5f, hgt * 0.55f, w, hgt * 0.15f, w, hgt);
                placed(cv, 0, ro - u * 0.005f, cr, p);
                break;
            }
            default: break;
        }
    }

    // ------------------------------------------------------------------ text

    final Rect tb = new Rect();

    /** Centred text at (x,y) rotated by rot degrees, with an effect. */
    void text(Canvas cv, String str, float x, float y, float rot, float sizePx, Typeface tf, int color, int effect, int accentCol) {
        if (str == null || str.isEmpty()) return;
        Paint p = fill(color);
        p.setTypeface(tf);
        p.setTextSize(sizePx);
        p.setTextAlign(Paint.Align.CENTER);
        p.getTextBounds(str, 0, str.length(), tb);
        float by = y - tb.exactCenterY();
        cv.save();
        cv.rotate(rot, x, y);
        float k = sizePx * 0.04f;
        switch (effect) {
            case 1: {
                Paint o = new Paint(p);
                o.setStyle(Paint.Style.STROKE);
                o.setStrokeWidth(Math.max(1f, sizePx * 0.05f));
                cv.drawText(str, x, by, o);
                break;
            }
            case 2: {
                Paint o = new Paint(p);
                o.setStyle(Paint.Style.STROKE);
                o.setStrokeWidth(Math.max(1.5f, sizePx * 0.1f));
                o.setStrokeJoin(Paint.Join.ROUND);
                o.setColor(accentCol);
                cv.drawText(str, x, by, o);
                cv.drawText(str, x, by, p);
                break;
            }
            case 3: {
                Paint sh = new Paint(p);
                sh.setColor(0x99000000);
                sh.setMaskFilter(new BlurMaskFilter(Math.max(1f, sizePx * 0.06f), BlurMaskFilter.Blur.NORMAL));
                cv.drawText(str, x + k, by + k * 1.5f, sh);
                cv.drawText(str, x, by, p);
                break;
            }
            case 4: {
                Paint g = new Paint(p);
                g.setMaskFilter(new BlurMaskFilter(Math.max(1f, sizePx * 0.18f), BlurMaskFilter.Blur.NORMAL));
                cv.drawText(str, x, by, g);
                cv.drawText(str, x, by, g);
                Paint core = new Paint(p);
                core.setColor(Col.lighten(color, 0.5f));
                cv.drawText(str, x, by, core);
                break;
            }
            case 5: {
                Paint hi = new Paint(p);
                hi.setColor(0x88FFFFFF);
                cv.drawText(str, x - k, by - k, hi);
                Paint lo = new Paint(p);
                lo.setColor(0x99000000);
                cv.drawText(str, x + k, by + k, lo);
                cv.drawText(str, x, by, p);
                break;
            }
            case 6: {
                Paint d = new Paint(p);
                d.setColor(accentCol);
                cv.drawText(str, x + k * 2, by + k * 2, d);
                cv.drawText(str, x, by, p);
                break;
            }
            default: cv.drawText(str, x, by, p);
        }
        cv.restore();
    }

    float orientRot(double a) {
        float d = deg(a);
        switch (s.numeralOrient) {
            case 1: return d;
            case 2: return (d > 90 && d < 270) ? d + 180 : d;
            default: return 0;
        }
    }

    void drawNumerals(Canvas cv) {
        if (s.numerals == 0) return;
        Typeface tf = Fonts.get(s.numeralFont, s.numeralWeight);
        float reach = tickReach();
        if (s.numerals == 7) { // minutes around the outside
            float size = u * 0.075f * s.numeralSize;
            for (int m = 5; m <= 60; m += 5) {
                double a = 2 * Math.PI * m / 60;
                float r = edge(a) - reach - size * 0.7f + s.numeralInset * u * 0.25f;
                text(cv, String.format(Locale.US, "%02d", m), px(a, r), py(a, r), orientRot(a), size, tf, s.numeralColor, s.numeralEffect, s.numeralAccent);
            }
            return;
        }
        String[] set = Names.numeralSet(s.numerals == 20 ? 1 : s.numerals);
        if (set == null) return;
        float base = u * 0.17f * s.numeralSize;
        if (s.numerals == 12) base = u * 0.07f * s.numeralSize;
        if (s.numerals == 13) base = u * 0.08f * s.numeralSize;
        if (s.numerals == 6) base = u * 0.26f * s.numeralSize;
        if (s.numerals == 19) base = u * 0.12f * s.numeralSize;
        Paint meas = fill(0xFFFFFFFF);
        meas.setTypeface(tf);
        for (int h = 1; h <= 12; h++) {
            String lab = set[h - 1];
            if (lab == null) continue;
            double a = 2 * Math.PI * h / 12;
            float size = base;
            meas.setTextSize(size);
            float maxW = (s.numeralOrient == 0) ? u * 0.32f : u * 0.55f;
            float w = meas.measureText(lab);
            if (w > maxW) size *= maxW / w;
            float rot = orientRot(a);
            // how much room this label needs measured towards the centre
            float ext;
            if (s.numeralOrient == 0) {
                meas.setTextSize(size);
                float lw = meas.measureText(lab) / 2, lh = size * 0.36f;
                ext = (float) (Math.abs(Math.sin(a)) * lw + Math.abs(Math.cos(a)) * lh);
            } else ext = size * 0.38f;
            float r = edge(a) - reach - ext - u * 0.03f + s.numeralInset * u * 0.25f;
            text(cv, lab, px(a, r), py(a, r), rot, size, tf, s.numeralColor, s.numeralEffect, s.numeralAccent);
            if (s.numerals == 20) {
                float r2 = r - size * 0.95f;
                text(cv, String.valueOf(h + 12), px(a, r2), py(a, r2), rot, size * 0.42f, tf, Col.alphaMul(s.numeralColor, 0.75f), 0, 0);
            }
        }
    }

    void drawLettering(Canvas cv) {
        if (s.brand != null && !s.brand.trim().isEmpty()) {
            Typeface tf = Fonts.get(s.brandFont, s.brandWeight);
            float size = u * 0.1f * s.brandSize;
            if (s.brandLayout == 1) {
                arcText(cv, s.brand, u * (0.3f + 0.42f * s.brandPos), true, size, tf, s.textColor);
            } else {
                float y = c - u * (0.16f + 0.3f * s.brandPos);
                spaced(cv, s.brand, c, y, size, tf, s.textColor);
                if (s.logo > 0) text(cv, Names.glyph(s.logo), c, y - size * 1.25f, 0, size * 1.15f, Typeface.DEFAULT, s.textColor, 0, 0);
            }
            if (s.brandLayout == 1 && s.logo > 0)
                text(cv, Names.glyph(s.logo), c, c - u * (0.16f + 0.18f * s.brandPos), 0, size * 1.15f, Typeface.DEFAULT, s.textColor, 0, 0);
        } else if (s.logo > 0) {
            text(cv, Names.glyph(s.logo), c, c - u * (0.16f + 0.3f * s.brandPos), 0, u * 0.13f * s.brandSize, Typeface.DEFAULT, s.textColor, 0, 0);
        }
        if (s.subtitle != null && !s.subtitle.trim().isEmpty()) {
            Typeface tf = Fonts.get(s.subFont, s.subWeight);
            float size = u * 0.058f * s.subSize;
            if (s.subLayout == 1) arcText(cv, s.subtitle, u * (0.3f + 0.42f * s.subPos), false, size, tf, Col.alphaMul(s.textColor, 0.85f));
            else spaced(cv, s.subtitle, c, c + u * (0.16f + 0.3f * s.subPos), size, tf, Col.alphaMul(s.textColor, 0.85f));
        }
    }

    void spaced(Canvas cv, String str, float x, float y, float size, Typeface tf, int color) {
        Paint p = fill(color);
        p.setTypeface(tf);
        p.setTextSize(size);
        p.setLetterSpacing(s.letterSpacing);
        p.setTextAlign(Paint.Align.CENTER);
        float maxW = u * 1.1f;
        float w = p.measureText(str);
        if (w > maxW) { p.setTextSize(size * maxW / w); }
        p.getTextBounds(str, 0, str.length(), tb);
        cv.drawText(str, x, y - tb.exactCenterY(), p);
    }

    void arcText(Canvas cv, String str, float r, boolean top, float size, Typeface tf, int color) {
        Paint p = fill(color);
        p.setTypeface(tf);
        p.setTextSize(size);
        p.setLetterSpacing(s.letterSpacing);
        p.setTextAlign(Paint.Align.LEFT);
        Paint.FontMetrics fm = p.getFontMetrics();
        float capH = -fm.ascent * 0.7f;
        Path arc = new Path();
        float rr = top ? r - capH / 2 : r - capH / 2;
        RectF ov = new RectF(c - rr, c - rr, c + rr, c + rr);
        if (top) arc.addArc(ov, 180, 180);
        else arc.addArc(ov, 180, -180);
        float len = (float) (Math.PI * rr);
        float w = p.measureText(str);
        if (w > len * 0.9f) { p.setTextSize(size * len * 0.9f / w); w = p.measureText(str); }
        float off = (len - w) / 2;
        float v = top ? capH / 2 : capH / 2;
        cv.drawTextOnPath(str, arc, off, top ? capH / 2 : v, p);
    }

    // ------------------------------------------------------------------ complications

    void drawComplications(Canvas cv) {
        int[] slots = {s.comp12, s.comp3, s.comp6, s.comp9};
        for (int i = 0; i < 4; i++) if (slots[i] != 0) drawComp(cv, slots[i], i * Math.PI / 2);
    }

    void drawComp(Canvas cv, int kind, double a) {
        Locale loc = Locale.getDefault();
        Typeface tf = Fonts.get(s.numeralFont, 1);
        float wr = u * 0.56f;            // windows
        float sr = u * 0.2f, sd = u * 0.46f;   // subdials
        float wx = px(a, wr), wy = py(a, wr);
        float sx = px(a, sd), sy = py(a, sd);
        switch (kind) {
            case 1: window(cv, wx, wy, String.valueOf(date.getDayOfMonth()), tf); break;
            case 2: window(cv, wx, wy, date.getDayOfWeek().getDisplayName(TextStyle.SHORT, loc).toUpperCase(loc), tf); break;
            case 3: window(cv, px(a, wr * 0.92f), py(a, wr * 0.92f),
                    date.getDayOfWeek().getDisplayName(TextStyle.SHORT, loc).toUpperCase(loc) + " " + date.getDayOfMonth(), tf); break;
            case 4: window(cv, wx, wy, date.getMonth().getDisplayName(TextStyle.SHORT, loc).toUpperCase(loc), tf); break;
            case 5: window(cv, wx, wy, "W" + date.get(IsoFields.WEEK_OF_WEEK_BASED_YEAR), tf); break;
            case 6: { // big date
                String dd = String.format(Locale.US, "%02d", date.getDayOfMonth());
                float sz = u * 0.12f;
                float bx = px(a, wr * 0.9f), by = py(a, wr * 0.9f);
                float bwid = sz * 0.8f, bh = sz * 1.15f, gap = sz * 0.08f;
                for (int k = 0; k < 2; k++) {
                    float cx = bx + (k == 0 ? -1 : 1) * (bwid / 2 + gap / 2);
                    box(cv, cx - bwid / 2, by - bh / 2, cx + bwid / 2, by + bh / 2);
                    text(cv, dd.substring(k, k + 1), cx, by, 0, sz, tf, s.compColor, 0, 0);
                }
                break;
            }
            case 7: { // weekday dial
                String[] lab = new String[7];
                for (int k = 0; k < 7; k++)
                    lab[k] = java.time.DayOfWeek.of(k + 1).getDisplayName(TextStyle.SHORT, loc);
                for (int k = 0; k < 7; k++) lab[k] = lab[k].substring(0, Math.min(2, lab[k].length())).toUpperCase(loc);
                subdial(cv, sx, sy, sr, lab, date.getDayOfWeek().getValue() - 1, 7);
                break;
            }
            case 8: { // date dial
                String[] lab = new String[31];
                for (int k = 0; k < 31; k++) lab[k] = ((k + 1) % 5 == 0 || k == 0) ? String.valueOf(k + 1) : null;
                lab[30] = "31";
                lab[29] = null;
                subdial(cv, sx, sy, sr, lab, date.getDayOfMonth() - 1, 31);
                break;
            }
            case 9: { // month dial
                String[] lab = new String[12];
                for (int k = 0; k < 12; k++) lab[k] = java.time.Month.of(k + 1).getDisplayName(TextStyle.NARROW, loc).toUpperCase(loc);
                subdial(cv, sx, sy, sr, lab, date.getMonthValue() - 1, 12);
                break;
            }
            case 10: moon(cv, sx, sy, sr); break;
            case 11: {
                String g = Names.glyph(s.logo > 0 ? s.logo : 1);
                text(cv, g, px(a, wr * 0.92f), py(a, wr * 0.92f), 0, u * 0.17f, Typeface.DEFAULT, s.compColor, 4, 0);
                break;
            }
            case 12: window(cv, wx, wy, String.valueOf(date.getYear()), tf); break;
            case 13: dayNight(cv, sx, sy, sr); break;
            default: break;
        }
    }

    void box(Canvas cv, float l, float t, float r, float b) {
        RectF rc = new RectF(l, t, r, b);
        float rad = (b - t) * 0.14f;
        cv.drawRoundRect(rc, rad, rad, fill(s.compBg));
        Paint in = fill(0xFFFFFFFF);
        in.setShader(new LinearGradient(0, t, 0, t + (b - t) * 0.4f, 0x66000000, 0, Shader.TileMode.CLAMP));
        cv.drawRoundRect(rc, rad, rad, in);
        cv.drawRoundRect(rc, rad, rad, stroke(Col.alphaMul(s.compColor, 0.55f), Math.max(1f, u * 0.006f)));
    }

    void window(Canvas cv, float x, float y, String str, Typeface tf) {
        float sz = u * 0.075f;
        Paint p = fill(0xFFFFFFFF);
        p.setTypeface(tf);
        p.setTextSize(sz);
        float w = p.measureText(str) + sz * 0.7f, h = sz * 1.45f;
        box(cv, x - w / 2, y - h / 2, x + w / 2, y + h / 2);
        text(cv, str, x, y, 0, sz, tf, s.compColor, 0, 0);
    }

    void subdial(Canvas cv, float x, float y, float r, String[] labels, int pos, int n) {
        Paint bg = fill(0xFFFFFFFF);
        bg.setShader(new RadialGradient(x, y, r, Col.alphaMul(s.compBg, 0.55f), Col.alphaMul(s.compBg, 0.9f), Shader.TileMode.CLAMP));
        cv.drawCircle(x, y, r, bg);
        cv.drawCircle(x, y, r, stroke(Col.alphaMul(s.compColor, 0.7f), Math.max(1f, u * 0.006f)));
        Typeface tf = Fonts.get(s.numeralFont, 1);
        Paint tick = fill(Col.alphaMul(s.compColor, 0.8f));
        for (int k = 0; k < n; k++) {
            double a = 2 * Math.PI * k / n;
            float tx = x + (float) (r * 0.9f * Math.sin(a)), ty = y - (float) (r * 0.9f * Math.cos(a));
            cv.drawCircle(tx, ty, Math.max(1f, r * 0.03f), tick);
            if (labels[k] != null) {
                float lr = r * 0.66f;
                text(cv, labels[k], x + (float) (lr * Math.sin(a)), y - (float) (lr * Math.cos(a)), 0, r * (n > 12 ? 0.2f : 0.24f), tf, s.compColor, 0, 0);
            }
        }
        double a = 2 * Math.PI * pos / n;
        cv.save();
        cv.rotate(deg(a), x, y);
        Path hand = HandShapes.poly(x - r * 0.05f, y + r * 0.15f, x + r * 0.05f, y + r * 0.15f, x + r * 0.015f, y - r * 0.85f, x - r * 0.015f, y - r * 0.85f);
        cv.drawPath(hand, fill(s.secondColor));
        cv.restore();
        cv.drawCircle(x, y, r * 0.07f, fill(s.secondColor));
    }

    void moon(Canvas cv, float x, float y, float r) {
        Paint sky = fill(0xFFFFFFFF);
        sky.setShader(new RadialGradient(x, y - r * 0.3f, r * 1.2f, 0xFF1C3A7A, 0xFF07122E, Shader.TileMode.CLAMP));
        cv.drawCircle(x, y, r, sky);
        Random rnd = new Random(5);
        Paint st = fill(0xDDFFFFFF);
        for (int k = 0; k < 18; k++) {
            float a = rnd.nextFloat() * 6.28f, d = r * (0.2f + 0.75f * rnd.nextFloat());
            cv.drawCircle(x + (float) Math.cos(a) * d, y + (float) Math.sin(a) * d, Math.max(0.7f, r * 0.02f * rnd.nextFloat() + r * 0.008f), st);
        }
        // phase: days since a known new moon
        double days = date.toEpochDay() + 0.5 - 10962.76;   // 2000-01-06 18:14 UTC
        double phase = ((days / 29.530588853) % 1 + 1) % 1;
        float mr = r * 0.58f;
        cv.drawCircle(x, y, mr, fill(0x40EDE3C0));
        Path lit = moonLit(x, y, mr, phase);
        Paint mp = fill(0xFFFFFFFF);
        mp.setShader(new RadialGradient(x - mr * 0.3f, y - mr * 0.3f, mr * 1.4f, 0xFFFFF6D8, 0xFFD9C27A, Shader.TileMode.CLAMP));
        cv.drawPath(lit, mp);
        cv.drawCircle(x, y, r, stroke(Col.alphaMul(s.compColor, 0.7f), Math.max(1f, u * 0.006f)));
    }

    static Path moonLit(float x, float y, float r, double phase) {
        boolean waxing = phase < 0.5;
        double k = Math.cos(2 * Math.PI * phase);   // 1 new .. -1 full
        Path half = new Path();
        RectF ov = new RectF(x - r, y - r, x + r, y + r);
        half.addArc(ov, waxing ? -90 : 90, 180);
        half.close();
        float ex = (float) Math.abs(k) * r;
        Path ell = new Path();
        if (ex > 0.5f) ell.addOval(new RectF(x - ex, y - r, x + ex, y + r), Path.Direction.CW);
        Path out = new Path(half);
        if (k > 0) out.op(ell, Path.Op.DIFFERENCE);   // crescent
        else out.op(ell, Path.Op.UNION);              // gibbous
        return out;
    }

    void dayNight(Canvas cv, float x, float y, float r) {
        Path ap = new Path();
        ap.addArc(new RectF(x - r, y - r, x + r, y + r), 180, 180);
        ap.close();
        Paint sky = fill(0xFFFFFFFF);
        sky.setShader(new LinearGradient(x - r, 0, x + r, 0, new int[]{0xFF0B1640, 0xFF3B6FD6, 0xFF8FC6FF, 0xFFFFB066, 0xFF2A1450}, null, Shader.TileMode.CLAMP));
        cv.drawPath(ap, sky);
        cv.drawCircle(x + r * 0.42f, y - r * 0.45f, r * 0.16f, fill(0xFFFFD34D));
        Path m = moonLit(x - r * 0.45f, y - r * 0.45f, r * 0.15f, 0.2);
        cv.drawPath(m, fill(0xFFF4ECCB));
        cv.drawPath(ap, stroke(Col.alphaMul(s.compColor, 0.7f), Math.max(1f, u * 0.006f)));
    }

    // ================================================================== hands

    float hourL() { return Math.min(u * 0.52f * s.handLength, u * 0.82f); }
    float minL() { return Math.min(u * 0.8f * s.handLength, u * 0.98f); }
    float secL() { return Math.min(u * 0.88f * s.handLength, u * 1.0f); }

    public Bitmap hourHand() {
        return handBitmap(HandShapes.hand(s.hourHand, hourL(), u * 0.085f * s.handWidth, u * 0.1f), false, false);
    }

    public Bitmap minuteHand() {
        return handBitmap(HandShapes.hand(s.minuteHand, minL(), u * 0.065f * s.handWidth, u * 0.13f), false, !s.seconds);
    }

    public Bitmap secondHand() {
        Path p = HandShapes.second(s.secondHand, secL(), Math.max(1.5f, u * 0.012f * s.handWidth), u * 0.2f, u);
        return handBitmap(p, true, true);
    }

    Bitmap handBitmap(Path local, boolean second, boolean cap) {
        Bitmap bmp = Bitmap.createBitmap(size, size, Bitmap.Config.ARGB_8888);
        Canvas cv = new Canvas(bmp);
        Path p = new Path(local);
        Matrix m = new Matrix();
        m.setTranslate(c, c);
        p.transform(m);
        RectF b = new RectF();
        p.computeBounds(b, true);

        if (s.handShadow) {
            Paint sh = fill(0x70000000);
            sh.setMaskFilter(new BlurMaskFilter(u * 0.018f, BlurMaskFilter.Blur.NORMAL));
            cv.save();
            cv.translate(0, u * 0.01f);
            cv.drawPath(p, sh);
            cv.restore();
        }

        if (second) {
            if (s.handFinish == 5) {
                Paint g = fill(s.secondColor);
                g.setMaskFilter(new BlurMaskFilter(u * 0.025f, BlurMaskFilter.Blur.NORMAL));
                cv.drawPath(p, g);
            }
            cv.drawPath(p, fill(s.secondColor));
        } else {
            finish(cv, p, b);
        }

        if (cap) drawCap(cv, second);
        return bmp;
    }

    void finish(Canvas cv, Path p, RectF b) {
        int h = s.handColor, acc = s.handAccent;
        float W = Math.max(2f, b.width());
        switch (s.handFinish) {
            case 1: {
                Paint pp = fill(0xFFFFFFFF);
                pp.setShader(new LinearGradient(b.left, 0, b.right, 0,
                        new int[]{Col.lighten(h, 0.5f), h, Col.darken(h, 0.45f), h, Col.lighten(h, 0.3f)}, null, Shader.TileMode.CLAMP));
                cv.drawPath(p, pp);
                break;
            }
            case 2: {
                cv.save();
                cv.clipRect(0, 0, c, size);
                cv.drawPath(p, fill(Col.lighten(h, 0.28f)));
                cv.restore();
                cv.save();
                cv.clipRect(c, 0, size, size);
                cv.drawPath(p, fill(Col.darken(h, 0.28f)));
                cv.restore();
                break;
            }
            case 3: {
                cv.drawPath(p, fill(acc));
                cv.drawPath(p, stroke(h, Math.max(2f, Math.min(W * 0.16f, u * 0.014f))));
                break;
            }
            case 4: cv.drawPath(p, stroke(h, Math.max(2f, Math.min(W * 0.12f, u * 0.012f)))); break;
            case 5: {
                Paint g = stroke(h, Math.max(3f, u * 0.025f));
                g.setMaskFilter(new BlurMaskFilter(u * 0.03f, BlurMaskFilter.Blur.NORMAL));
                cv.drawPath(p, g);
                cv.drawPath(p, fill(Col.alphaMul(h, 0.35f)));
                cv.drawPath(p, stroke(Col.lighten(h, 0.55f), Math.max(2f, u * 0.008f)));
                break;
            }
            case 6: {
                Paint pp = fill(0xFFFFFFFF);
                pp.setShader(new LinearGradient(0, c, 0, b.top, h, acc, Shader.TileMode.CLAMP));
                cv.drawPath(p, pp);
                break;
            }
            case 7: case 8: {
                int[] cols = s.handFinish == 7
                        ? new int[]{0xFFFFF1B8, 0xFFD4A537, 0xFF8A6418, 0xFFE9C766, 0xFFB8892B}
                        : new int[]{0xFFFFFFFF, 0xFFC4CAD2, 0xFF6E757E, 0xFFE3E7EC, 0xFF9AA1AA};
                Paint pp = fill(0xFFFFFFFF);
                pp.setShader(new LinearGradient(b.left, 0, b.right, 0, cols, null, Shader.TileMode.CLAMP));
                cv.drawPath(p, pp);
                break;
            }
            default: cv.drawPath(p, fill(h));
        }
    }

    void drawCap(Canvas cv, boolean onSecond) {
        int col = onSecond ? s.secondColor : s.handColor;
        switch (s.cap) {
            case 1: cv.drawCircle(c, c, u * 0.03f, fill(col)); break;
            case 2:
                cv.drawCircle(c, c, u * 0.036f, fill(col));
                cv.drawCircle(c, c, u * 0.015f, fill(Col.darken(col, 0.5f)));
                break;
            case 3: {
                Paint j = fill(0xFFFFFFFF);
                j.setShader(new RadialGradient(c - u * 0.01f, c - u * 0.01f, u * 0.04f, new int[]{0xFFFFFFFF, s.accent, Col.darken(s.accent, 0.6f)},
                        new float[]{0, 0.35f, 1}, Shader.TileMode.CLAMP));
                cv.drawCircle(c, c, u * 0.032f, j);
                cv.drawCircle(c, c, u * 0.032f, stroke(s.handColor, Math.max(1.5f, u * 0.007f)));
                break;
            }
            case 4: {
                Paint sc = fill(0xFFFFFFFF);
                sc.setShader(new LinearGradient(c - u * 0.03f, c - u * 0.03f, c + u * 0.03f, c + u * 0.03f, Col.lighten(col, 0.4f), Col.darken(col, 0.3f), Shader.TileMode.CLAMP));
                cv.drawCircle(c, c, u * 0.032f, sc);
                Paint sl = stroke(Col.darken(col, 0.55f), Math.max(1.5f, u * 0.008f));
                cv.drawLine(c - u * 0.022f, c + u * 0.008f, c + u * 0.022f, c - u * 0.008f, sl);
                break;
            }
            case 5:
                cv.drawCircle(c, c, u * 0.065f, fill(col));
                cv.drawCircle(c, c, u * 0.065f, stroke(Col.darken(col, 0.35f), Math.max(1.5f, u * 0.008f)));
                break;
            case 6: {
                Paint pp = fill(col);
                for (int k = 0; k < 6; k++) {
                    double a = Math.PI / 3 * k;
                    cv.drawCircle(px(a, u * 0.028f), py(a, u * 0.028f), u * 0.018f, pp);
                }
                cv.drawCircle(c, c, u * 0.02f, fill(Col.lighten(col, 0.5f)));
                break;
            }
            default: break;
        }
    }

    // ================================================================== preview

    /** Dial + hands at the given time, all in one bitmap. */
    public static Bitmap composite(ClockStyle s, int size, LocalDate date, int h, int m, int sec) {
        ClockRenderer r = new ClockRenderer(s, size, date);
        Bitmap out = r.dial();
        Canvas cv = new Canvas(out);
        Paint p = fill(0xFFFFFFFF);
        float hc = size / 2f;
        drawRot(cv, r.hourHand(), ((h % 12) + m / 60f) * 30f, hc, p);
        drawRot(cv, r.minuteHand(), (m + sec / 60f) * 6f, hc, p);
        if (s.seconds) drawRot(cv, r.secondHand(), sec * 6f, hc, p);
        return out;
    }

    static void drawRot(Canvas cv, Bitmap b, float deg, float hc, Paint p) {
        cv.save();
        cv.rotate(deg, hc, hc);
        cv.drawBitmap(b, 0, 0, p);
        cv.restore();
        b.recycle();
    }
}
