package com.binaryimage.aion;

import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;

/**
 * Hand outlines. Every path is built around the pivot at (0,0) pointing up (towards -y):
 * L = length to the tip, W = widest width, T = tail length behind the pivot.
 */
public final class HandShapes {
    private HandShapes() { }

    static Path poly(float... xy) {
        Path p = new Path();
        p.moveTo(xy[0], xy[1]);
        for (int i = 2; i < xy.length; i += 2) p.lineTo(xy[i], xy[i + 1]);
        p.close();
        return p;
    }

    static Path rect(float l, float t, float r, float b) {
        Path p = new Path();
        p.addRect(Math.min(l, r), Math.min(t, b), Math.max(l, r), Math.max(t, b), Path.Direction.CW);
        return p;
    }

    static Path rrect(float l, float t, float r, float b, float rad) {
        Path p = new Path();
        p.addRoundRect(new RectF(Math.min(l, r), Math.min(t, b), Math.max(l, r), Math.max(t, b)), rad, rad, Path.Direction.CW);
        return p;
    }

    static Path circle(float x, float y, float r) {
        Path p = new Path();
        p.addCircle(x, y, r, Path.Direction.CW);
        return p;
    }

    static Path oval(float x, float y, float rx, float ry) {
        Path p = new Path();
        p.addOval(new RectF(x - rx, y - ry, x + rx, y + ry), Path.Direction.CW);
        return p;
    }

    static Path union(Path... ps) {
        Path out = new Path(ps[0]);
        for (int i = 1; i < ps.length; i++) out.op(ps[i], Path.Op.UNION);
        return out;
    }

    static Path minus(Path a, Path... bs) {
        Path out = new Path(a);
        for (Path b : bs) out.op(b, Path.Op.DIFFERENCE);
        return out;
    }

    static Path star(float x, float y, float ro, float ri, int points) {
        Path p = new Path();
        for (int i = 0; i < points * 2; i++) {
            double a = Math.PI * i / points;
            float r = (i % 2 == 0) ? ro : ri;
            float px = x + (float) (r * Math.sin(a)), py = y - (float) (r * Math.cos(a));
            if (i == 0) p.moveTo(px, py); else p.lineTo(px, py);
        }
        p.close();
        return p;
    }

    static Path heart(float x, float y, float s) {
        Path p = new Path();
        p.moveTo(x, y + s * 0.9f);
        p.cubicTo(x - s * 1.4f, y - s * 0.1f, x - s * 0.7f, y - s * 1.1f, x, y - s * 0.45f);
        p.cubicTo(x + s * 0.7f, y - s * 1.1f, x + s * 1.4f, y - s * 0.1f, x, y + s * 0.9f);
        p.close();
        return p;
    }

    /** A polyline stroked to width w, turned into a fillable outline. */
    static Path strokeOf(Path line, float w) {
        Paint sp = new Paint(Paint.ANTI_ALIAS_FLAG);
        sp.setStyle(Paint.Style.STROKE);
        sp.setStrokeWidth(w);
        sp.setStrokeJoin(Paint.Join.MITER);
        sp.setStrokeCap(Paint.Cap.ROUND);
        Path out = new Path();
        sp.getFillPath(line, out);
        return out;
    }

    // -------------------------------------------------------------- hour / minute

    public static Path hand(int style, float L, float W, float T) {
        float h = W * 0.5f;
        switch (style) {
            default:
            case 0: { // baton
                float w2 = W * 0.34f;
                return rrect(-w2, T * 0.5f, w2, -L, w2 * 0.35f);
            }
            case 1: // dauphine
                return poly(0, -L, h, -L * 0.04f, W * 0.22f, T * 0.6f, -W * 0.22f, T * 0.6f, -h, -L * 0.04f);
            case 2: { // leaf
                Path p = new Path();
                p.moveTo(0, T * 0.5f);
                p.quadTo(W * 0.95f, -L * 0.45f, 0, -L);
                p.quadTo(-W * 0.95f, -L * 0.45f, 0, T * 0.5f);
                p.close();
                return union(p, circle(0, 0, W * 0.3f));
            }
            case 3: // sword
                return poly(-W * 0.17f, T * 0.5f, W * 0.17f, T * 0.5f, h, -L * 0.8f, 0, -L, -h, -L * 0.8f);
            case 4: { // alpha
                Path p = new Path();
                p.moveTo(0, T * 0.45f);
                p.cubicTo(W * 0.8f, -L * 0.12f, W * 0.32f, -L * 0.62f, 0, -L);
                p.cubicTo(-W * 0.32f, -L * 0.62f, -W * 0.8f, -L * 0.12f, 0, T * 0.45f);
                p.close();
                return union(p, circle(0, 0, W * 0.3f));
            }
            case 5: { // breguet (pomme)
                float cy = -L * 0.72f;
                Path shaft = poly(-W * 0.12f, T * 0.6f, W * 0.12f, T * 0.6f, W * 0.07f, cy, -W * 0.07f, cy);
                Path tip = poly(-W * 0.12f, cy - W * 0.45f, W * 0.12f, cy - W * 0.45f, 0, -L);
                Path out = union(shaft, circle(0, cy, W * 0.55f), tip, circle(0, 0, W * 0.28f));
                return minus(out, circle(0, cy, W * 0.33f));
            }
            case 6: { // spade
                Path spade = new Path();
                spade.moveTo(0, -L);
                spade.cubicTo(W * 0.3f, -L * 0.88f, W * 0.85f, -L * 0.8f, W * 0.48f, -L * 0.64f);
                spade.quadTo(W * 0.22f, -L * 0.56f, 0, -L * 0.65f);
                spade.quadTo(-W * 0.22f, -L * 0.56f, -W * 0.48f, -L * 0.64f);
                spade.cubicTo(-W * 0.85f, -L * 0.8f, -W * 0.3f, -L * 0.88f, 0, -L);
                spade.close();
                return union(spade, poly(-W * 0.12f, T * 0.6f, W * 0.12f, T * 0.6f, W * 0.08f, -L * 0.62f, -W * 0.08f, -L * 0.62f),
                        circle(0, 0, W * 0.26f));
            }
            case 7: { // cathedral
                Path body = poly(-W * 0.28f, T * 0.5f, W * 0.28f, T * 0.5f, W * 0.48f, -L * 0.52f, W * 0.26f, -L * 0.82f,
                        0, -L, -W * 0.26f, -L * 0.82f, -W * 0.48f, -L * 0.52f);
                return minus(body, oval(0, -L * 0.25f, W * 0.13f, L * 0.06f), oval(0, -L * 0.48f, W * 0.2f, L * 0.08f),
                        poly(0, -L * 0.62f, W * 0.13f, -L * 0.72f, 0, -L * 0.85f, -W * 0.13f, -L * 0.72f));
            }
            case 8: // arrow
                return union(poly(-W * 0.09f, T * 0.6f, W * 0.09f, T * 0.6f, W * 0.09f, -L * 0.74f, -W * 0.09f, -L * 0.74f),
                        poly(0, -L, W * 0.55f, -L * 0.72f, 0, -L * 0.78f, -W * 0.55f, -L * 0.72f),
                        poly(-W * 0.3f, T * 0.9f, W * 0.3f, T * 0.9f, W * 0.09f, T * 0.4f, -W * 0.09f, T * 0.4f),
                        circle(0, 0, W * 0.24f));
            case 9: // needle
                return union(poly(-W * 0.17f, T * 0.6f, W * 0.17f, T * 0.6f, W * 0.035f, -L, -W * 0.035f, -L), circle(0, 0, W * 0.24f));
            case 10: { // lollipop
                float cy = -L * 0.76f;
                return union(rect(-W * 0.08f, T * 0.7f, W * 0.08f, -L), circle(0, cy, W * 0.42f), circle(0, 0, W * 0.24f));
            }
            case 11: { // mercedes
                float cy = -L * 0.7f, ro = W * 0.62f, ri = W * 0.46f, sw = W * 0.07f;
                Path base = union(poly(-W * 0.14f, T * 0.6f, W * 0.14f, T * 0.6f, W * 0.1f, cy, -W * 0.1f, cy),
                        circle(0, cy, ro), poly(-W * 0.12f, cy - ro * 0.9f, W * 0.12f, cy - ro * 0.9f, 0, -L), circle(0, 0, W * 0.28f));
                base = minus(base, circle(0, cy, ri));
                Path spokes = new Path();
                for (int k = 0; k < 3; k++) {
                    double a = Math.PI * 2 * k / 3;
                    float ex = (float) (Math.sin(a) * ri * 1.05), ey = cy - (float) (Math.cos(a) * ri * 1.05);
                    Path line = new Path();
                    line.moveTo(0, cy);
                    line.lineTo(ex, ey);
                    spokes.op(strokeOf(line, sw), Path.Op.UNION);
                }
                return union(base, spokes);
            }
            case 12: { // snowflake
                float cy = -L * 0.7f, d = W * 0.55f;
                return union(rect(-W * 0.11f, T * 0.6f, W * 0.11f, cy),
                        poly(0, cy - d, d, cy, 0, cy + d, -d, cy),
                        poly(-W * 0.11f, cy - d * 0.8f, W * 0.11f, cy - d * 0.8f, 0, -L),
                        circle(0, 0, W * 0.28f));
            }
            case 13: // plongeur
                return union(rect(-W * 0.42f, T * 0.4f, W * 0.42f, -L * 0.8f), poly(-W * 0.42f, -L * 0.8f, W * 0.42f, -L * 0.8f, 0, -L));
            case 14: // pencil
                return poly(-W * 0.28f, T * 0.5f, W * 0.28f, T * 0.5f, W * 0.28f, -L * 0.8f, 0, -L, -W * 0.28f, -L * 0.8f);
            case 15: // syringe
                return union(rrect(-W * 0.26f, T * 0.5f, W * 0.26f, -L * 0.56f, W * 0.08f),
                        poly(-W * 0.26f, -L * 0.56f, W * 0.26f, -L * 0.56f, W * 0.05f, -L * 0.64f, W * 0.035f, -L,
                                -W * 0.035f, -L, -W * 0.05f, -L * 0.64f));
            case 16: // broad arrow
                return union(rect(-W * 0.11f, T * 0.6f, W * 0.11f, -L * 0.68f),
                        poly(0, -L, W * 0.62f, -L * 0.6f, 0, -L * 0.72f, -W * 0.62f, -L * 0.6f), circle(0, 0, W * 0.26f));
            case 17: // teardrop
                return union(circle(0, 0, h), poly(-W * 0.45f, -W * 0.2f, W * 0.45f, -W * 0.2f, 0, -L));
            case 18: // paddle
                return union(rect(-W * 0.08f, T * 0.6f, W * 0.08f, -L * 0.46f), rrect(-W * 0.3f, -L * 0.42f, W * 0.3f, -L, W * 0.3f),
                        circle(0, 0, W * 0.22f));
            case 19: // wire
                return union(rrect(-W * 0.07f, T, W * 0.07f, -L, W * 0.07f), circle(0, 0, W * 0.2f));
            case 20: { // feuille (slim leaf with a tail)
                Path p = new Path();
                p.moveTo(0, T * 0.9f);
                p.quadTo(W * 0.62f, -L * 0.4f, 0, -L);
                p.quadTo(-W * 0.62f, -L * 0.4f, 0, T * 0.9f);
                p.close();
                return p;
            }
            case 21: // obelisk
                return poly(-W * 0.36f, T * 0.5f, W * 0.36f, T * 0.5f, W * 0.22f, -L * 0.9f, 0, -L, -W * 0.22f, -L * 0.9f);
            case 22: // lancet
                return poly(0, T * 0.7f, W * 0.34f, -L * 0.45f, 0, -L, -W * 0.34f, -L * 0.45f);
            case 23: { // skeleton
                Path outer = poly(-W * 0.2f, T * 0.5f, W * 0.2f, T * 0.5f, h, -L * 0.8f, 0, -L, -h, -L * 0.8f);
                Path inner = poly(-W * 0.08f, -L * 0.12f, W * 0.08f, -L * 0.12f, W * 0.32f, -L * 0.78f, 0, -L * 0.9f, -W * 0.32f, -L * 0.78f);
                return union(minus(outer, inner), circle(0, 0, W * 0.3f));
            }
            case 24: { // flame
                Path p = new Path();
                p.moveTo(0, T * 0.4f);
                p.cubicTo(W * 0.85f, -L * 0.18f, -W * 0.2f, -L * 0.5f, W * 0.32f, -L * 0.72f);
                p.quadTo(W * 0.28f, -L * 0.9f, 0, -L);
                p.quadTo(-W * 0.45f, -L * 0.78f, -W * 0.12f, -L * 0.56f);
                p.cubicTo(-W * 0.65f, -L * 0.36f, -W * 0.6f, -L * 0.08f, 0, T * 0.4f);
                p.close();
                return union(p, circle(0, 0, W * 0.3f));
            }
            case 25: { // key
                Path out = union(rect(-W * 0.1f, T * 0.4f, W * 0.1f, -L), circle(0, T * 0.4f + W * 0.35f, W * 0.48f),
                        rect(0, -L * 0.96f, W * 0.42f, -L * 0.9f), rect(0, -L * 0.86f, W * 0.32f, -L * 0.8f), circle(0, 0, W * 0.22f));
                return minus(out, circle(0, T * 0.4f + W * 0.35f, W * 0.24f));
            }
            case 26: { // trident
                Path line = new Path();
                line.moveTo(-W * 0.45f, -L * 0.9f);
                line.lineTo(-W * 0.45f, -L * 0.72f);
                line.lineTo(W * 0.45f, -L * 0.72f);
                line.lineTo(W * 0.45f, -L * 0.9f);
                return union(rect(-W * 0.09f, T * 0.6f, W * 0.09f, -L * 0.88f), strokeOf(line, W * 0.14f),
                        poly(-W * 0.16f, -L * 0.86f, W * 0.16f, -L * 0.86f, 0, -L),
                        poly(-W * 0.6f, -L * 0.88f, -W * 0.3f, -L * 0.88f, -W * 0.45f, -L * 0.98f),
                        poly(W * 0.3f, -L * 0.88f, W * 0.6f, -L * 0.88f, W * 0.45f, -L * 0.98f), circle(0, 0, W * 0.24f));
            }
            case 27: // bubble
                return union(rect(-W * 0.07f, T * 0.6f, W * 0.07f, -L * 0.9f), circle(0, -L * 0.34f, W * 0.3f),
                        circle(0, -L * 0.6f, W * 0.38f), circle(0, -L * 0.86f, W * 0.46f), circle(0, 0, W * 0.24f));
        }
    }

    // -------------------------------------------------------------- seconds

    /** w = shaft width, u = face radius (for sizing decorations). */
    public static Path second(int style, float L, float w, float T, float u) {
        Path shaft = poly(-w * 0.6f, T, w * 0.6f, T, w * 0.35f, -L, -w * 0.35f, -L);
        Path hub = circle(0, 0, w * 1.6f);
        switch (style) {
            default:
            case 0: // needle with counterweight
                return union(shaft, rrect(-w * 1.3f, T * 0.45f, w * 1.3f, T, w), hub);
            case 1: // lollipop
                return union(shaft, circle(0, -L * 0.8f, u * 0.032f), hub);
            case 2: // arrow tip
                return union(shaft, poly(0, -L, u * 0.03f, -L * 0.86f, -u * 0.03f, -L * 0.86f), hub);
            case 3: { // lightning
                Path line = new Path();
                line.moveTo(0, T);
                line.lineTo(0, -L * 0.3f);
                line.lineTo(u * 0.035f, -L * 0.42f);
                line.lineTo(-u * 0.035f, -L * 0.56f);
                line.lineTo(u * 0.03f, -L * 0.7f);
                line.lineTo(0, -L * 0.8f);
                line.lineTo(0, -L);
                return union(strokeOf(line, w * 1.1f), poly(0, -L, u * 0.02f, -L * 0.88f, -u * 0.02f, -L * 0.88f), hub);
            }
            case 4: // paddle
                return union(shaft, rrect(-u * 0.016f, -L * 0.62f, u * 0.016f, -L * 0.88f, u * 0.016f), hub);
            case 5: // counterweight disc
                return union(shaft, minus(circle(0, T * 0.7f, u * 0.04f), circle(0, T * 0.7f, u * 0.018f)), hub);
            case 6: // railway disc
                return union(shaft, circle(0, -L * 0.68f, u * 0.06f), hub);
            case 7: // diver triangle
                return union(shaft, minus(poly(0, -L * 0.88f, u * 0.035f, -L * 0.7f, -u * 0.035f, -L * 0.7f),
                        poly(0, -L * 0.83f, u * 0.018f, -L * 0.74f, -u * 0.018f, -L * 0.74f)), hub);
            case 8: // hairline
                return union(rect(-w * 0.3f, 0, w * 0.3f, -L), circle(0, 0, w * 1.2f));
            case 9: // star tip
                return union(shaft, star(0, -L * 0.82f, u * 0.045f, u * 0.02f, 5), hub);
            case 10: // ring tip
                return union(shaft, minus(circle(0, -L * 0.82f, u * 0.035f), circle(0, -L * 0.82f, u * 0.022f)), hub);
            case 11: // heart
                return union(shaft, heart(0, -L * 0.8f, u * 0.03f), hub);
        }
    }
}
