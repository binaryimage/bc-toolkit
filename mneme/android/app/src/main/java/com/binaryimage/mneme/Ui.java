package com.binaryimage.mneme;

import android.content.Context;
import android.content.res.ColorStateList;
import android.graphics.Typeface;
import android.graphics.drawable.Drawable;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.RippleDrawable;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.TextView;

/** Small helpers so every screen looks the same: black, white, one shade of grey. */
final class Ui {
    static final int BG = 0xFF000000;
    static final int FG = 0xFFFFFFFF;
    static final int DIM = 0xFF8E8E8E;
    static final int FAINT = 0xFF4A4A4A;
    static final int LINE = 0xFF2A2A2A;
    static final int CARD = 0xFF0E0E0E;
    static final int WARN = 0xFFFF6B6B;
    static final int REC = 0xFFFF3B30;

    private Ui() {}

    static int dp(Context c, float v) {
        return Math.round(TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, v, c.getResources().getDisplayMetrics()));
    }

    static TextView text(Context c, CharSequence s, float sp, int color) {
        TextView t = new TextView(c);
        t.setText(s);
        t.setTextSize(TypedValue.COMPLEX_UNIT_SP, sp);
        t.setTextColor(color);
        return t;
    }

    static TextView label(Context c, String s) {
        TextView t = text(c, s, 12, DIM);
        t.setTypeface(Typeface.DEFAULT_BOLD);
        t.setLetterSpacing(0.15f);
        return t;
    }

    static GradientDrawable box(Context c, int fill, int stroke, float radiusDp) {
        GradientDrawable g = new GradientDrawable();
        g.setColor(fill);
        if (stroke != 0) g.setStroke(dp(c, 1), stroke);
        g.setCornerRadius(dp(c, radiusDp));
        return g;
    }

    static Drawable ripple(Context c, Drawable content) {
        return new RippleDrawable(ColorStateList.valueOf(0x44FFFFFF), content, null);
    }

    /** An outlined button. */
    static TextView button(Context c, String label) {
        TextView b = text(c, label, 13, FG);
        b.setTypeface(Typeface.DEFAULT_BOLD);
        b.setLetterSpacing(0.08f);
        b.setGravity(Gravity.CENTER);
        b.setAllCaps(true);
        int h = dp(c, 14), v = dp(c, 10);
        b.setPadding(h, v, h, v);
        b.setMinHeight(dp(c, 44));
        b.setBackground(ripple(c, box(c, BG, FG, 22)));
        b.setClickable(true);
        b.setFocusable(true);
        return b;
    }

    /** A filled white button with black text. */
    static TextView solid(Context c, String label) {
        TextView b = button(c, label);
        b.setTextColor(BG);
        b.setBackground(ripple(c, box(c, FG, FG, 22)));
        return b;
    }

    static void setSolid(Context c, TextView b, boolean solid, int fill) {
        b.setTextColor(solid ? BG : FG);
        b.setBackground(ripple(c, solid ? box(c, fill, fill, 22) : box(c, BG, FG, 22)));
    }

    static EditText field(Context c, String hint) {
        EditText e = new EditText(c);
        e.setHint(hint);
        e.setHintTextColor(FAINT);
        e.setTextColor(FG);
        e.setTextSize(TypedValue.COMPLEX_UNIT_SP, 16);
        int p = dp(c, 12);
        e.setPadding(p, p, p, p);
        e.setBackground(box(c, CARD, LINE, 10));
        return e;
    }

    static View divider(Context c) {
        View v = new View(c);
        v.setBackgroundColor(LINE);
        v.setLayoutParams(new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, Math.max(1, dp(c, 1))));
        return v;
    }

    static LinearLayout.LayoutParams lp(int w, int h) {
        return new LinearLayout.LayoutParams(w, h);
    }

    static LinearLayout.LayoutParams lp(int w, int h, float weight) {
        return new LinearLayout.LayoutParams(w, h, weight);
    }

    static LinearLayout.LayoutParams margins(LinearLayout.LayoutParams p, Context c, float l, float t, float r, float b) {
        p.setMargins(dp(c, l), dp(c, t), dp(c, r), dp(c, b));
        return p;
    }

    static LinearLayout row(Context c) {
        LinearLayout l = new LinearLayout(c);
        l.setOrientation(LinearLayout.HORIZONTAL);
        l.setGravity(Gravity.CENTER_VERTICAL);
        return l;
    }

    static LinearLayout col(Context c) {
        LinearLayout l = new LinearLayout(c);
        l.setOrientation(LinearLayout.VERTICAL);
        return l;
    }

    static String time(int ms) {
        int s = Math.max(0, ms / 1000);
        return (s / 60) + ":" + (s % 60 < 10 ? "0" : "") + (s % 60);
    }
}
