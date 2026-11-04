package com.binaryimage.aion;

import android.graphics.Color;

/** Small colour helpers. */
public final class Col {
    private Col() { }

    public static int mix(int a, int b, float t) {
        t = Math.max(0f, Math.min(1f, t));
        int aa = Color.alpha(a), ar = Color.red(a), ag = Color.green(a), ab = Color.blue(a);
        int ba = Color.alpha(b), br = Color.red(b), bg = Color.green(b), bb = Color.blue(b);
        return Color.argb(Math.round(aa + (ba - aa) * t), Math.round(ar + (br - ar) * t),
                Math.round(ag + (bg - ag) * t), Math.round(ab + (bb - ab) * t));
    }

    public static int lighten(int c, float t) { return mix(c, (c & 0xFF000000) | 0xFFFFFF, t); }
    public static int darken(int c, float t) { return mix(c, c & 0xFF000000, t); }

    public static int alpha(int c, int a) { return (c & 0x00FFFFFF) | (Math.max(0, Math.min(255, a)) << 24); }

    public static int alphaMul(int c, float m) { return alpha(c, Math.round(Color.alpha(c) * m)); }

    public static float lum(int c) {
        return (0.2126f * Color.red(c) + 0.7152f * Color.green(c) + 0.0722f * Color.blue(c)) / 255f;
    }

    /** Black or white, whichever reads better on c. */
    public static int contrast(int c) { return lum(c) > 0.55f ? 0xFF111111 : 0xFFF5F5F5; }

    public static int hsv(float h, float s, float v) { return Color.HSVToColor(new float[]{ ((h % 360) + 360) % 360, s, v }); }

    public static float hue(int c) { float[] f = new float[3]; Color.colorToHSV(c, f); return f[0]; }
}
