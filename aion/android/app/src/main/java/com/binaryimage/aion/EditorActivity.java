package com.binaryimage.aion;

import android.app.Activity;
import android.app.AlertDialog;
import android.appwidget.AppWidgetManager;
import android.content.ComponentName;
import android.content.Intent;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.text.Editable;
import android.text.InputType;
import android.text.TextWatcher;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.HorizontalScrollView;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.SeekBar;
import android.widget.Switch;
import android.widget.TextView;
import android.widget.Toast;

import java.io.File;
import java.io.FileOutputStream;
import java.time.LocalDate;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.List;
import java.util.Random;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.function.BiConsumer;
import java.util.function.Consumer;
import java.util.function.Function;
import java.util.function.ObjIntConsumer;
import java.util.function.Predicate;
import java.util.function.ToIntFunction;

/** The clock designer: live preview on top, tabs of options below. Also the widget's configure screen. */
public class EditorActivity extends Activity {

    static final int BG = 0xFF0E0F12, CARD = 0xFF1A1C22, CARD2 = 0xFF252832, TEXT = 0xFFECEFF4, DIM = 0xFF8A919E, ACCENT = 0xFFFFB454;
    static final String[] TABS = {"Presets", "Face", "Case", "Marks", "Numerals", "Hands", "Lettering", "Extras"};

    interface FGet { float get(ClockStyle s); }
    interface FSet { void set(ClockStyle s, float v); }

    /** A thumbnail to render: what the style looks like with one option changed. */
    static final class Thumb {
        final ImageView view; final Function<ClockStyle, ClockStyle> variant; final int kind; final int px;
        Thumb(ImageView v, Function<ClockStyle, ClockStyle> f, int kind, int px) { view = v; variant = f; this.kind = kind; this.px = px; }
    }

    int widgetId = AppWidgetManager.INVALID_APPWIDGET_ID;
    boolean configMode;
    ClockStyle style;
    final ArrayDeque<String> undo = new ArrayDeque<>();

    final Handler ui = new Handler(Looper.getMainLooper());
    final ExecutorService renderExec = Executors.newSingleThreadExecutor();
    final ExecutorService thumbExec = Executors.newSingleThreadExecutor();
    volatile int renderGen = 0, thumbGen = 0;
    final List<Thumb> thumbs = new ArrayList<>();
    final List<Runnable> refreshers = new ArrayList<>();
    List<ClockStyle> ideas = new ArrayList<>();
    final Random rnd = new Random();

    PreviewView preview;
    FrameLayout previewBox;
    int previewBg = 0;
    LinearLayout panel, tabBar;
    ScrollView scroll;
    TextView subtitle;
    int tab = 0;

    int dp(float v) { return Math.round(TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, v, getResources().getDisplayMetrics())); }

    // ================================================================== lifecycle

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        Fonts.init(this);
        Intent it = getIntent();
        if (it.getBooleanExtra("ci_gallery", false)) { runGallery(); return; }

        widgetId = it.getIntExtra(AppWidgetManager.EXTRA_APPWIDGET_ID, AppWidgetManager.INVALID_APPWIDGET_ID);
        configMode = AppWidgetManager.ACTION_APPWIDGET_CONFIGURE.equals(it.getAction());
        if (configMode) setResult(RESULT_CANCELED, new Intent().putExtra(AppWidgetManager.EXTRA_APPWIDGET_ID, widgetId));
        style = widgetId != AppWidgetManager.INVALID_APPWIDGET_ID ? Store.forWidget(this, widgetId) : Store.getDefault(this);
        for (int i = 0; i < 9; i++) ideas.add(Shuffler.all(rnd));

        buildUi();
        String startTab = it.getStringExtra("tab");
        int t = 0;
        if (startTab != null) for (int i = 0; i < TABS.length; i++) if (TABS[i].equalsIgnoreCase(startTab)) t = i;
        if (it.hasExtra("preset")) {
            List<ClockStyle> ps = Presets.all();
            style = ps.get(Math.max(0, Math.min(ps.size() - 1, it.getIntExtra("preset", 0)))).copy();
        }
        showTab(t);
        preview.post(this::requestRender);
        if (it.getBooleanExtra("ci_pin", false)) ui.postDelayed(this::pinWidget, 1500);
        if (it.getBooleanExtra("ci_apply", false)) ui.postDelayed(this::apply, 2500);
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        renderExec.shutdownNow();
        thumbExec.shutdownNow();
    }

    // ================================================================== layout

    GradientDrawable rounded(int color, float radiusDp, int strokeColor, float strokeDp) {
        GradientDrawable g = new GradientDrawable();
        g.setColor(color);
        g.setCornerRadius(dp(radiusDp));
        if (strokeDp > 0) g.setStroke(dp(strokeDp), strokeColor);
        return g;
    }

    TextView label(String s, float sp, int color, boolean bold) {
        TextView t = new TextView(this);
        t.setText(s);
        t.setTextSize(sp);
        t.setTextColor(color);
        if (bold) t.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
        return t;
    }

    void buildUi() {
        getWindow().setStatusBarColor(BG);
        getWindow().setNavigationBarColor(BG);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(BG);
        root.setFitsSystemWindows(true);

        // header
        LinearLayout head = new LinearLayout(this);
        head.setOrientation(LinearLayout.HORIZONTAL);
        head.setGravity(Gravity.CENTER_VERTICAL);
        head.setPadding(dp(18), dp(10), dp(12), dp(4));
        LinearLayout titles = new LinearLayout(this);
        titles.setOrientation(LinearLayout.VERTICAL);
        TextView title = label("AION", 22, TEXT, true);
        title.setLetterSpacing(0.3f);
        titles.addView(title);
        subtitle = label(widgetId != AppWidgetManager.INVALID_APPWIDGET_ID ? "Designing this clock" : "Designing your clocks", 12, DIM, false);
        titles.addView(subtitle);
        head.addView(titles, new LinearLayout.LayoutParams(0, -2, 1));
        if (!configMode) {
            Button pin = smallButton("+ Home screen", false);
            pin.setOnClickListener(v -> pinWidget());
            head.addView(pin);
        }
        root.addView(head);

        // preview
        int sw = getResources().getDisplayMetrics().widthPixels, sh = getResources().getDisplayMetrics().heightPixels;
        int ph = (int) Math.min(sw * 0.68f, sh * 0.36f);
        previewBox = new FrameLayout(this);
        preview = new PreviewView(this);
        previewBox.addView(preview, new FrameLayout.LayoutParams(ph, ph, Gravity.CENTER));
        previewBox.setPadding(0, dp(6), 0, dp(6));
        previewBox.setOnClickListener(v -> { previewBg = (previewBg + 1) % 4; paintPreviewBg(); });
        paintPreviewBg();
        root.addView(previewBox, new LinearLayout.LayoutParams(-1, ph + dp(12)));

        // tabs
        HorizontalScrollView hs = new HorizontalScrollView(this);
        hs.setHorizontalScrollBarEnabled(false);
        tabBar = new LinearLayout(this);
        tabBar.setOrientation(LinearLayout.HORIZONTAL);
        tabBar.setPadding(dp(12), dp(4), dp(12), dp(6));
        for (int i = 0; i < TABS.length; i++) {
            final int idx = i;
            TextView t = label(TABS[i], 14, TEXT, true);
            t.setPadding(dp(14), dp(8), dp(14), dp(8));
            t.setOnClickListener(v -> showTab(idx));
            LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(-2, -2);
            lp.rightMargin = dp(6);
            tabBar.addView(t, lp);
        }
        hs.addView(tabBar);
        root.addView(hs);

        // options
        scroll = new ScrollView(this);
        panel = new LinearLayout(this);
        panel.setOrientation(LinearLayout.VERTICAL);
        panel.setPadding(dp(14), dp(4), dp(14), dp(24));
        scroll.addView(panel);
        root.addView(scroll, new LinearLayout.LayoutParams(-1, 0, 1));

        // bottom bar
        LinearLayout bar = new LinearLayout(this);
        bar.setOrientation(LinearLayout.HORIZONTAL);
        bar.setPadding(dp(10), dp(8), dp(10), dp(10));
        bar.setBackgroundColor(0xFF121318);
        Button bUndo = smallButton("↶ Undo", false);
        bUndo.setOnClickListener(v -> doUndo());
        Button bShuffle = smallButton("🎲 Shuffle", false);
        bShuffle.setOnClickListener(v -> change(() -> style = keepText(Shuffler.all(rnd))));
        Button bSave = smallButton("♥ Save", false);
        bSave.setOnClickListener(v -> {
            Store.addSaved(this, style);
            Toast.makeText(this, "Saved to My styles", Toast.LENGTH_SHORT).show();
            if (tab == 0) rebuildPanel();
        });
        Button bApply = smallButton(widgetId != AppWidgetManager.INVALID_APPWIDGET_ID ? "Apply" : "Apply to all", true);
        bApply.setOnClickListener(v -> apply());
        bar.addView(bUndo, new LinearLayout.LayoutParams(0, -2, 1));
        bar.addView(bShuffle, new LinearLayout.LayoutParams(0, -2, 1.2f));
        bar.addView(bSave, new LinearLayout.LayoutParams(0, -2, 1));
        bar.addView(bApply, new LinearLayout.LayoutParams(0, -2, 1.4f));
        root.addView(bar);

        setContentView(root);
    }

    /** Shuffling everything keeps the user's own brand text if they typed one. */
    ClockStyle keepText(ClockStyle n) { return n; }

    void paintPreviewBg() {
        int[][] bgs = {{0xFF1B1E26, 0xFF0B0C10}, {0xFFE9E6DF, 0xFFBFBAB0}, {0xFF3A1C71, 0xFFD76D77}, {0xFF134E5E, 0xFF71B280}};
        GradientDrawable g = new GradientDrawable(GradientDrawable.Orientation.TL_BR, bgs[previewBg]);
        previewBox.setBackground(g);
    }

    Button smallButton(String text, boolean primary) {
        Button b = new Button(this);
        b.setText(text);
        b.setAllCaps(false);
        b.setTextSize(14);
        b.setTextColor(primary ? 0xFF1A1206 : TEXT);
        b.setBackground(rounded(primary ? ACCENT : CARD2, 12, 0, 0));
        b.setMinHeight(dp(44));
        b.setMinimumHeight(dp(44));
        b.setPadding(dp(10), 0, dp(10), 0);
        ViewGroup.MarginLayoutParams lp = new ViewGroup.MarginLayoutParams(-2, -2);
        b.setLayoutParams(lp);
        b.setStateListAnimator(null);
        return b;
    }

    void showTab(int t) {
        tab = t;
        for (int i = 0; i < tabBar.getChildCount(); i++) {
            TextView tv = (TextView) tabBar.getChildAt(i);
            boolean on = i == t;
            tv.setTextColor(on ? 0xFF1A1206 : TEXT);
            tv.setBackground(rounded(on ? ACCENT : CARD, 18, 0, 0));
        }
        rebuildPanel();
        scroll.scrollTo(0, 0);
    }

    void rebuildPanel() {
        int y = scroll.getScrollY();
        panel.removeAllViews();
        thumbs.clear();
        refreshers.clear();
        switch (tab) {
            case 0: buildPresets(); break;
            case 1: buildFace(); break;
            case 2: buildCase(); break;
            case 3: buildMarks(); break;
            case 4: buildNumerals(); break;
            case 5: buildHands(); break;
            case 6: buildLettering(); break;
            default: buildExtras(); break;
        }
        scroll.post(() -> scroll.scrollTo(0, y));
        refreshThumbs();
    }

    // ================================================================== state changes

    void pushUndo() {
        undo.push(style.toString());
        while (undo.size() > 60) undo.removeLast();
    }

    void change(Runnable r) {
        pushUndo();
        r.run();
        afterChange(true);
    }

    void afterChange(boolean rebuild) {
        requestRender();
        if (rebuild) rebuildPanel();
        else {
            for (Runnable r : refreshers) r.run();
            scheduleThumbs();
        }
    }

    void doUndo() {
        if (undo.isEmpty()) { Toast.makeText(this, "Nothing to undo", Toast.LENGTH_SHORT).show(); return; }
        style = ClockStyle.fromJson(undo.pop());
        afterChange(true);
    }

    final Runnable thumbRunnable = this::refreshThumbs;

    void scheduleThumbs() {
        ui.removeCallbacks(thumbRunnable);
        ui.postDelayed(thumbRunnable, 350);
    }

    void requestRender() {
        final int gen = ++renderGen;
        final ClockStyle snap = style.copy();
        final int size = preview.renderSize();
        renderExec.execute(() -> {
            if (gen != renderGen) return;
            try {
                ClockRenderer r = new ClockRenderer(snap, size, LocalDate.now());
                Bitmap d = r.dial(), h = r.hourHand(), m = r.minuteHand();
                Bitmap s = snap.seconds ? r.secondHand() : null;
                ui.post(() -> { if (gen == renderGen) preview.set(d, h, m, s); });
            } catch (Throwable t) {
                ui.post(() -> Toast.makeText(this, "Render error: " + t, Toast.LENGTH_LONG).show());
            }
        });
    }

    void refreshThumbs() {
        final int gen = ++thumbGen;
        final ClockStyle snap = style.copy();
        final List<Thumb> jobs = new ArrayList<>(thumbs);
        thumbExec.execute(() -> {
            LocalDate today = LocalDate.now();
            for (Thumb t : jobs) {
                if (gen != thumbGen) return;
                try {
                    ClockStyle v = t.variant.apply(snap.copy());
                    Bitmap bmp = t.kind == 2 ? handsThumb(v, t.px) : ClockRenderer.composite(v, t.px, today, 10, 9, 36);
                    ui.post(() -> { if (gen == thumbGen) t.view.setImageBitmap(bmp); });
                } catch (Throwable ignored) { }
            }
        });
    }

    static Bitmap handsThumb(ClockStyle v, int px) {
        ClockStyle s = v.copy();
        s.handLength = 1.12f;
        s.handWidth = Math.max(1.1f, s.handWidth * 1.25f);
        ClockRenderer r = new ClockRenderer(s, px, LocalDate.now());
        Bitmap out = Bitmap.createBitmap(px, px, Bitmap.Config.ARGB_8888);
        Canvas cv = new Canvas(out);
        cv.drawColor(Col.alpha(s.faceA, 255));
        Paint p = new Paint(Paint.FILTER_BITMAP_FLAG | Paint.ANTI_ALIAS_FLAG);
        float c = px / 2f;
        ClockRenderer.drawRot(cv, r.hourHand(), -50, c, p);
        ClockRenderer.drawRot(cv, r.minuteHand(), 50, c, p);
        if (s.seconds) ClockRenderer.drawRot(cv, r.secondHand(), 180, c, p);
        return out;
    }

    // ================================================================== controls

    void section(String title) {
        TextView t = label(title.toUpperCase(), 11, DIM, true);
        t.setLetterSpacing(0.15f);
        t.setPadding(dp(4), dp(18), 0, dp(6));
        panel.addView(t);
    }

    void tabShuffle(String what, Consumer<ClockStyle> f) {
        Button b = smallButton("🎲 Shuffle " + what, false);
        b.setOnClickListener(v -> change(() -> f.accept(style)));
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(-2, -2);
        lp.topMargin = dp(8);
        panel.addView(b, lp);
    }

    /** A row of options; kind 0 = text chips, 1 = clock thumbnails, 2 = hand thumbnails. */
    void choice(String title, String[] names, ToIntFunction<ClockStyle> get, ObjIntConsumer<ClockStyle> set, int kind) {
        section(title);
        HorizontalScrollView hs = new HorizontalScrollView(this);
        hs.setHorizontalScrollBarEnabled(false);
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        final List<View> chips = new ArrayList<>();
        int thumbPx = dp(kind == 0 ? 0 : 68);
        for (int i = 0; i < names.length; i++) {
            final int idx = i;
            View chip;
            if (kind == 0) {
                TextView t = label(names[i], 13, TEXT, false);
                t.setPadding(dp(12), dp(8), dp(12), dp(8));
                chip = t;
            } else {
                LinearLayout col = new LinearLayout(this);
                col.setOrientation(LinearLayout.VERTICAL);
                col.setGravity(Gravity.CENTER_HORIZONTAL);
                col.setPadding(dp(6), dp(6), dp(6), dp(6));
                ImageView iv = new ImageView(this);
                col.addView(iv, new LinearLayout.LayoutParams(thumbPx, thumbPx));
                TextView t = label(names[i], 11, TEXT, false);
                t.setGravity(Gravity.CENTER);
                t.setMaxLines(2);
                t.setWidth(thumbPx + dp(6));
                col.addView(t);
                chip = col;
                thumbs.add(new Thumb(iv, s -> { set.accept(s, idx); return s; }, kind, thumbPx));
            }
            chip.setOnClickListener(v -> {
                if (get.applyAsInt(style) == idx) return;
                pushUndo();
                set.accept(style, idx);
                afterChange(false);
            });
            LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(-2, -2);
            lp.rightMargin = dp(6);
            row.addView(chip, lp);
            chips.add(chip);
        }
        Runnable refresh = () -> {
            int sel = get.applyAsInt(style);
            for (int i = 0; i < chips.size(); i++)
                chips.get(i).setBackground(rounded(i == sel ? CARD2 : CARD, 12, ACCENT, i == sel ? 2 : 0));
        };
        refresh.run();
        refreshers.add(refresh);
        hs.addView(row);
        panel.addView(hs);
        // bring the selected chip into view
        hs.post(() -> {
            int sel = get.applyAsInt(style);
            if (sel >= 0 && sel < chips.size()) hs.scrollTo(Math.max(0, chips.get(sel).getLeft() - dp(40)), 0);
        });
    }

    void fontChoice(String title, Function<ClockStyle, String> get, BiConsumer<ClockStyle, String> set) {
        section(title);
        HorizontalScrollView hs = new HorizontalScrollView(this);
        hs.setHorizontalScrollBarEnabled(false);
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        final List<String> names = Fonts.names();
        final List<View> chips = new ArrayList<>();
        for (String n : names) {
            LinearLayout col = new LinearLayout(this);
            col.setOrientation(LinearLayout.VERTICAL);
            col.setGravity(Gravity.CENTER_HORIZONTAL);
            col.setPadding(dp(10), dp(6), dp(10), dp(6));
            TextView big = label("Aa 12", 20, TEXT, false);
            big.setTypeface(Fonts.get(n, 0));
            big.setGravity(Gravity.CENTER);
            col.addView(big);
            TextView small = label(n, 10, DIM, false);
            small.setGravity(Gravity.CENTER);
            col.addView(small);
            col.setOnClickListener(v -> {
                if (n.equals(get.apply(style))) return;
                pushUndo();
                set.accept(style, n);
                afterChange(false);
            });
            LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(-2, -2);
            lp.rightMargin = dp(6);
            row.addView(col, lp);
            chips.add(col);
        }
        Runnable refresh = () -> {
            String sel = get.apply(style);
            for (int i = 0; i < chips.size(); i++) {
                boolean on = names.get(i).equals(sel);
                chips.get(i).setBackground(rounded(on ? CARD2 : CARD, 12, ACCENT, on ? 2 : 0));
            }
        };
        refresh.run();
        refreshers.add(refresh);
        hs.addView(row);
        panel.addView(hs);
        hs.post(() -> {
            int sel = names.indexOf(get.apply(style));
            if (sel >= 0) hs.scrollTo(Math.max(0, chips.get(sel).getLeft() - dp(40)), 0);
        });
    }

    static final int[] SWATCHES = {
        0xFFFFFFFF, 0xFFF5F5F2, 0xFFF7F3E8, 0xFFE8ECF2, 0xFFC9CED6, 0xFF8D8D8D, 0xFF4A4A4A, 0xFF1A1A1A, 0xFF000000,
        0xFFD4AF37, 0xFFF5D76E, 0xFFE0A899, 0xFFC87533, 0xFFB8BEC6,
        0xFFFF3B30, 0xFFE63946, 0xFFB3121D, 0xFF5A0F1E, 0xFFFF6B00, 0xFFFFB703, 0xFFFFD60A,
        0xFF39FF14, 0xFFB6FF3B, 0xFF00F5D4, 0xFF2A9D8F, 0xFF0B3D2E, 0xFF3B4A2F,
        0xFF00B4D8, 0xFF4CC9F0, 0xFFA9D6EA, 0xFF2F5FB3, 0xFF1E3A8A, 0xFF0A1F3D, 0xFF1B3E91,
        0xFF7B2CBF, 0xFFB967FF, 0xFFD7C9F0, 0xFF240046, 0xFFFF00A8, 0xFFFF71CE, 0xFFF4A896,
        0xFF7A3E1D, 0xFFD8C3A5, 0xFF4A2C2A, 0x00000000
    };

    void color(String title, ToIntFunction<ClockStyle> get, ObjIntConsumer<ClockStyle> set) {
        section(title);
        HorizontalScrollView hs = new HorizontalScrollView(this);
        hs.setHorizontalScrollBarEnabled(false);
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);
        final List<View> sw = new ArrayList<>();
        // custom first, showing the current colour
        final TextView custom = label("✎", 16, TEXT, true);
        custom.setGravity(Gravity.CENTER);
        custom.setOnClickListener(v -> colorDialog(get.applyAsInt(style), c -> {
            pushUndo();
            set.accept(style, c);
            afterChange(false);
        }));
        row.addView(custom, swatchLp());
        for (int col : SWATCHES) {
            View v = new View(this);
            v.setOnClickListener(x -> {
                if (get.applyAsInt(style) == col) return;
                pushUndo();
                set.accept(style, col);
                afterChange(false);
            });
            row.addView(v, swatchLp());
            sw.add(v);
        }
        Runnable refresh = () -> {
            int cur = get.applyAsInt(style);
            GradientDrawable g = new GradientDrawable();
            g.setShape(GradientDrawable.OVAL);
            g.setColor(cur);
            g.setStroke(dp(2), ACCENT);
            custom.setBackground(g);
            custom.setTextColor(Col.alpha(cur, 255) == 0xFF000000 || Color.alpha(cur) < 60 ? TEXT : Col.contrast(cur));
            for (int i = 0; i < sw.size(); i++) {
                int c = SWATCHES[i];
                GradientDrawable d = new GradientDrawable();
                d.setShape(GradientDrawable.OVAL);
                d.setColor(c);
                boolean on = c == cur;
                d.setStroke(dp(on ? 3 : 1), on ? ACCENT : 0x40FFFFFF);
                if (Color.alpha(c) == 0) d.setStroke(dp(on ? 3 : 1), on ? ACCENT : 0xAAFFFFFF, dp(3), dp(2));
                sw.get(i).setBackground(d);
            }
        };
        refresh.run();
        refreshers.add(refresh);
        hs.addView(row);
        panel.addView(hs);
    }

    LinearLayout.LayoutParams swatchLp() {
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(dp(36), dp(36));
        lp.rightMargin = dp(8);
        return lp;
    }

    void colorDialog(int start, Consumer<Integer> done) {
        float[] hsv = new float[3];
        Color.colorToHSV(start, hsv);
        final int[] alpha = {Color.alpha(start) == 0 ? 255 : Color.alpha(start)};
        LinearLayout box = new LinearLayout(this);
        box.setOrientation(LinearLayout.VERTICAL);
        box.setPadding(dp(20), dp(12), dp(20), dp(4));
        View sample = new View(this);
        box.addView(sample, new LinearLayout.LayoutParams(-1, dp(56)));
        EditText hex = new EditText(this);
        hex.setSingleLine();
        hex.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_CAP_CHARACTERS);
        final boolean[] updating = {false};
        final Runnable[] redraw = new Runnable[1];
        String[] labels = {"Hue", "Saturation", "Brightness", "Opacity"};
        int[] max = {360, 100, 100, 255};
        SeekBar[] bars = new SeekBar[4];
        for (int i = 0; i < 4; i++) {
            final int k = i;
            box.addView(label(labels[i], 12, 0xFF666666, false));
            SeekBar sb = new SeekBar(this);
            sb.setMax(max[i]);
            sb.setProgress(i == 0 ? Math.round(hsv[0]) : i == 1 ? Math.round(hsv[1] * 100) : i == 2 ? Math.round(hsv[2] * 100) : alpha[0]);
            sb.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
                @Override public void onProgressChanged(SeekBar s, int p, boolean user) {
                    if (!user) return;
                    if (k == 0) hsv[0] = p; else if (k == 1) hsv[1] = p / 100f; else if (k == 2) hsv[2] = p / 100f; else alpha[0] = p;
                    redraw[0].run();
                }
                @Override public void onStartTrackingTouch(SeekBar s) { }
                @Override public void onStopTrackingTouch(SeekBar s) { }
            });
            bars[i] = sb;
            box.addView(sb);
        }
        box.addView(hex);
        redraw[0] = () -> {
            int c = Col.alpha(Color.HSVToColor(hsv), alpha[0]);
            sample.setBackground(rounded(c, 10, 0xFF888888, 1));
            updating[0] = true;
            hex.setText(String.format("#%08X", c));
            updating[0] = false;
        };
        hex.addTextChangedListener(new TextWatcher() {
            @Override public void beforeTextChanged(CharSequence s, int a, int b, int c) { }
            @Override public void onTextChanged(CharSequence s, int a, int b, int c) { }
            @Override public void afterTextChanged(Editable e) {
                if (updating[0]) return;
                try {
                    int c = Color.parseColor(e.toString().trim());
                    Color.colorToHSV(c, hsv);
                    alpha[0] = Color.alpha(c);
                    bars[0].setProgress(Math.round(hsv[0]));
                    bars[1].setProgress(Math.round(hsv[1] * 100));
                    bars[2].setProgress(Math.round(hsv[2] * 100));
                    bars[3].setProgress(alpha[0]);
                    sample.setBackground(rounded(c, 10, 0xFF888888, 1));
                } catch (Exception ignored) { }
            }
        });
        redraw[0].run();
        new AlertDialog.Builder(this)
                .setTitle("Pick a colour")
                .setView(box)
                .setPositiveButton("Use", (d, w) -> done.accept(Col.alpha(Color.HSVToColor(hsv), alpha[0])))
                .setNegativeButton("Cancel", null)
                .show();
    }

    void slider(String title, float min, float max, FGet get, FSet set) {
        section(title);
        SeekBar sb = new SeekBar(this);
        sb.setMax(1000);
        sb.setProgress(Math.round((get.get(style) - min) / (max - min) * 1000));
        sb.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(SeekBar s, int p, boolean user) {
                if (!user) return;
                set.set(style, min + (max - min) * p / 1000f);
                requestRender();
            }
            @Override public void onStartTrackingTouch(SeekBar s) { pushUndo(); }
            @Override public void onStopTrackingTouch(SeekBar s) { scheduleThumbs(); }
        });
        panel.addView(sb);
    }

    void toggle(String title, Predicate<ClockStyle> get, BiConsumer<ClockStyle, Boolean> set) {
        Switch sw = new Switch(this);
        sw.setText(title);
        sw.setTextColor(TEXT);
        sw.setTextSize(15);
        sw.setChecked(get.test(style));
        sw.setPadding(dp(4), dp(10), dp(4), dp(10));
        sw.setOnCheckedChangeListener((b, on) -> {
            if (on == get.test(style)) return;
            pushUndo();
            set.accept(style, on);
            afterChange(false);
        });
        refreshers.add(() -> { if (sw.isChecked() != get.test(style)) sw.setChecked(get.test(style)); });
        panel.addView(sw);
    }

    void textField(String title, Function<ClockStyle, String> get, BiConsumer<ClockStyle, String> set) {
        section(title);
        EditText e = new EditText(this);
        e.setSingleLine();
        e.setText(get.apply(style));
        e.setTextColor(TEXT);
        e.setHintTextColor(DIM);
        e.setHint("(empty = none)");
        e.setBackground(rounded(CARD, 10, 0, 0));
        e.setPadding(dp(12), dp(10), dp(12), dp(10));
        e.setOnFocusChangeListener((v, has) -> { if (has) pushUndo(); });
        e.addTextChangedListener(new TextWatcher() {
            @Override public void beforeTextChanged(CharSequence s, int a, int b, int c) { }
            @Override public void onTextChanged(CharSequence s, int a, int b, int c) { }
            @Override public void afterTextChanged(Editable s) {
                set.accept(style, s.toString());
                requestRender();
            }
        });
        panel.addView(e);
    }

    void buttonRow(String text, Runnable r) {
        Button b = smallButton(text, false);
        b.setOnClickListener(v -> r.run());
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(-2, -2);
        lp.topMargin = dp(10);
        panel.addView(b, lp);
    }

    // ================================================================== tabs

    void grid(String title, List<ClockStyle> list, boolean deletable) {
        section(title);
        int cols = 3;
        int cell = (getResources().getDisplayMetrics().widthPixels - dp(28) - dp(8) * (cols - 1)) / cols;
        LinearLayout row = null;
        for (int i = 0; i < list.size(); i++) {
            if (i % cols == 0) {
                row = new LinearLayout(this);
                row.setOrientation(LinearLayout.HORIZONTAL);
                panel.addView(row);
            }
            final ClockStyle cs = list.get(i);
            final int idx = i;
            LinearLayout c = new LinearLayout(this);
            c.setOrientation(LinearLayout.VERTICAL);
            c.setGravity(Gravity.CENTER_HORIZONTAL);
            c.setPadding(dp(4), dp(6), dp(4), dp(6));
            c.setBackground(rounded(CARD, 14, 0, 0));
            ImageView iv = new ImageView(this);
            int px = cell - dp(8);
            c.addView(iv, new LinearLayout.LayoutParams(px, px));
            TextView t = label(cs.name, 12, TEXT, false);
            t.setGravity(Gravity.CENTER);
            t.setSingleLine();
            c.addView(t);
            thumbs.add(new Thumb(iv, s -> cs.copy(), 1, px));
            c.setOnClickListener(v -> change(() -> style = cs.copy()));
            if (deletable) c.setOnLongClickListener(v -> {
                new AlertDialog.Builder(this).setMessage("Delete \"" + cs.name + "\" from My styles?")
                        .setPositiveButton("Delete", (d, w) -> {
                            List<ClockStyle> l = Store.saved(this);
                            if (idx < l.size()) l.remove(idx);
                            Store.setSaved(this, l);
                            rebuildPanel();
                        }).setNegativeButton("Keep", null).show();
                return true;
            });
            LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(cell, -2);
            lp.rightMargin = (i % cols == cols - 1) ? 0 : dp(8);
            lp.bottomMargin = dp(8);
            row.addView(c, lp);
        }
    }

    void buildPresets() {
        buttonRow("🎲 Surprise me", () -> change(() -> style = Shuffler.all(rnd)));
        List<ClockStyle> saved = Store.saved(this);
        if (!saved.isEmpty()) grid("My styles (long-press to delete)", saved, true);
        grid("Presets", Presets.all(), false);
        section("Ideas");
        buttonRow("↻ New ideas", () -> {
            ideas.clear();
            for (int i = 0; i < 9; i++) ideas.add(Shuffler.all(rnd));
            rebuildPanel();
        });
        grid("", ideas, false);
    }

    void buildFace() {
        tabShuffle("face", s -> { Shuffler.face(s, rnd); Shuffler.colours(s, rnd); });
        choice("Shape", Names.SHAPES, s -> s.shape, (s, v) -> s.shape = v, 1);
        choice("Pattern", Names.PATTERNS, s -> s.pattern, (s, v) -> s.pattern = v, 1);
        color("Face colour", s -> s.faceA, (s, v) -> s.faceA = v);
        color("Second face colour", s -> s.faceB, (s, v) -> s.faceB = v);
        color("Accent colour", s -> s.accent, (s, v) -> s.accent = v);
        slider("Pattern detail", 0f, 1f, s -> s.detail, (s, v) -> s.detail = v);
        buttonRow("✦ Reroll pattern randomness", () -> change(() -> style.seed = rnd.nextInt(100000)));
        section("Finish");
        toggle("Vignette", s -> s.vignette, (s, v) -> s.vignette = v);
        toggle("Depth + case shadow", s -> s.depth, (s, v) -> s.depth = v);
        toggle("Crystal reflection", s -> s.gloss, (s, v) -> s.gloss = v);
    }

    void buildCase() {
        tabShuffle("case", s -> { s.bezel = rnd.nextInt(Names.BEZELS.length); s.bezelColor = SWATCHES[rnd.nextInt(14)]; });
        choice("Bezel", Names.BEZELS, s -> s.bezel, (s, v) -> s.bezel = v, 1);
        color("Bezel colour", s -> s.bezelColor, (s, v) -> s.bezelColor = v);
        color("Bezel insert / second colour", s -> s.bezelAccent, (s, v) -> s.bezelAccent = v);
        choice("Shape", Names.SHAPES, s -> s.shape, (s, v) -> s.shape = v, 1);
    }

    void buildMarks() {
        tabShuffle("marks", s -> Shuffler.marks(s, rnd));
        choice("Hour + minute marks", Names.TICKS, s -> s.ticks, (s, v) -> s.ticks = v, 1);
        choice("12 o'clock marker", Names.MARKER12, s -> s.marker12, (s, v) -> s.marker12 = v, 1);
        color("Mark colour", s -> s.tickColor, (s, v) -> s.tickColor = v);
        slider("Mark size", 0.5f, 1.8f, s -> s.tickSize, (s, v) -> s.tickSize = v);
        toggle("Marks follow the case shape", s -> s.followShape, (s, v) -> s.followShape = v);
        toggle("Hide marks where numerals are", s -> s.skipUnderNumerals, (s, v) -> s.skipUnderNumerals = v);
    }

    void buildNumerals() {
        tabShuffle("numerals", s -> Shuffler.numerals(s, rnd));
        choice("Numerals", Names.NUMERALS, s -> s.numerals, (s, v) -> s.numerals = v, 1);
        fontChoice("Font", s -> s.numeralFont, (s, v) -> s.numeralFont = v);
        choice("Weight", Names.WEIGHTS, s -> s.numeralWeight, (s, v) -> s.numeralWeight = v, 0);
        color("Numeral colour", s -> s.numeralColor, (s, v) -> s.numeralColor = v);
        slider("Size", 0.4f, 2.0f, s -> s.numeralSize, (s, v) -> s.numeralSize = v);
        slider("Distance from edge", -1f, 1f, s -> -s.numeralInset, (s, v) -> s.numeralInset = -v);
        choice("Orientation", Names.ORIENT, s -> s.numeralOrient, (s, v) -> s.numeralOrient = v, 1);
        choice("Effect", Names.EFFECTS, s -> s.numeralEffect, (s, v) -> s.numeralEffect = v, 1);
        color("Effect colour (outline / double)", s -> s.numeralAccent, (s, v) -> s.numeralAccent = v);
    }

    void buildHands() {
        tabShuffle("hands", s -> Shuffler.hands(s, rnd));
        choice("Hour hand", Names.HANDS, s -> s.hourHand, (s, v) -> s.hourHand = v, 2);
        choice("Minute hand", Names.HANDS, s -> s.minuteHand, (s, v) -> s.minuteHand = v, 2);
        buttonRow("Match minute hand to hour hand", () -> change(() -> style.minuteHand = style.hourHand));
        choice("Finish", Names.FINISHES, s -> s.handFinish, (s, v) -> s.handFinish = v, 2);
        color("Hand colour", s -> s.handColor, (s, v) -> s.handColor = v);
        color("Lume / tip colour", s -> s.handAccent, (s, v) -> s.handAccent = v);
        toggle("Seconds hand", s -> s.seconds, (s, v) -> s.seconds = v);
        choice("Seconds hand", Names.SECONDS, s -> s.secondHand, (s, v) -> s.secondHand = v, 2);
        color("Seconds colour", s -> s.secondColor, (s, v) -> s.secondColor = v);
        choice("Centre cap", Names.CAPS, s -> s.cap, (s, v) -> s.cap = v, 2);
        slider("Length", 0.7f, 1.25f, s -> s.handLength, (s, v) -> s.handLength = v);
        slider("Width", 0.5f, 2.0f, s -> s.handWidth, (s, v) -> s.handWidth = v);
        toggle("Hand shadows", s -> s.handShadow, (s, v) -> s.handShadow = v);
    }

    void buildLettering() {
        tabShuffle("lettering", s -> Shuffler.text(s, rnd));
        textField("Name on the dial", s -> s.brand, (s, v) -> s.brand = v);
        fontChoice("Name font", s -> s.brandFont, (s, v) -> s.brandFont = v);
        choice("Name weight", Names.WEIGHTS, s -> s.brandWeight, (s, v) -> s.brandWeight = v, 0);
        choice("Name layout", new String[]{"Straight", "Curved along the top"}, s -> s.brandLayout, (s, v) -> s.brandLayout = v, 0);
        slider("Name size", 0.4f, 2.2f, s -> s.brandSize, (s, v) -> s.brandSize = v);
        slider("Name position", 0f, 1f, s -> s.brandPos, (s, v) -> s.brandPos = v);
        choice("Logo", Names.LOGOS, s -> s.logo, (s, v) -> s.logo = v, 0);
        textField("Second line", s -> s.subtitle, (s, v) -> s.subtitle = v);
        fontChoice("Second line font", s -> s.subFont, (s, v) -> s.subFont = v);
        choice("Second line weight", Names.WEIGHTS, s -> s.subWeight, (s, v) -> s.subWeight = v, 0);
        choice("Second line layout", new String[]{"Straight", "Curved along the bottom"}, s -> s.subLayout, (s, v) -> s.subLayout = v, 0);
        slider("Second line size", 0.4f, 2.2f, s -> s.subSize, (s, v) -> s.subSize = v);
        slider("Second line position", 0f, 1f, s -> s.subPos, (s, v) -> s.subPos = v);
        color("Lettering colour", s -> s.textColor, (s, v) -> s.textColor = v);
        slider("Letter spacing", 0f, 0.7f, s -> s.letterSpacing, (s, v) -> s.letterSpacing = v);
    }

    void buildExtras() {
        tabShuffle("extras", s -> Shuffler.extras(s, rnd));
        TextView note = label("Dates, moon phase and calendar dials update by themselves every midnight.", 12, DIM, false);
        note.setPadding(dp(4), dp(10), dp(4), 0);
        panel.addView(note);
        choice("At 12 o'clock", Names.COMPS, s -> s.comp12, (s, v) -> s.comp12 = v, 0);
        choice("At 3 o'clock", Names.COMPS, s -> s.comp3, (s, v) -> s.comp3 = v, 0);
        choice("At 6 o'clock", Names.COMPS, s -> s.comp6, (s, v) -> s.comp6 = v, 0);
        choice("At 9 o'clock", Names.COMPS, s -> s.comp9, (s, v) -> s.comp9 = v, 0);
        color("Window / dial text", s -> s.compColor, (s, v) -> s.compColor = v);
        color("Window / dial background", s -> s.compBg, (s, v) -> s.compBg = v);
    }

    // ================================================================== apply / pin

    void apply() {
        final ClockStyle snap = style.copy();
        final boolean one = widgetId != AppWidgetManager.INVALID_APPWIDGET_ID;
        Store.setDefault(this, snap);
        final int[] ids = one ? new int[]{widgetId} : Widgets.ids(this);
        for (int id : ids) Store.setForWidget(this, id, snap);
        final android.content.Context app = getApplicationContext();
        new Thread(() -> {
            for (int id : ids) Widgets.update(app, id, true);
            ui.post(() -> {
                if (configMode) {
                    setResult(RESULT_OK, new Intent().putExtra(AppWidgetManager.EXTRA_APPWIDGET_ID, widgetId));
                    finish();
                } else if (ids.length == 0) {
                    Toast.makeText(this, "Saved. Add the clock to your home screen to see it.", Toast.LENGTH_LONG).show();
                } else {
                    Toast.makeText(this, ids.length == 1 ? "Clock updated" : ids.length + " clocks updated", Toast.LENGTH_SHORT).show();
                }
            });
        }, "aion-apply").start();
    }

    void pinWidget() {
        Store.setDefault(this, style);
        AppWidgetManager m = getSystemService(AppWidgetManager.class);
        if (m != null && m.isRequestPinAppWidgetSupported()) {
            m.requestPinAppWidget(new ComponentName(this, ClockWidget.class), null, null);
        } else {
            Toast.makeText(this, "Long-press your home screen → Widgets → Aion", Toast.LENGTH_LONG).show();
        }
    }

    // ================================================================== CI gallery

    void runGallery() {
        TextView t = label("Rendering gallery…", 18, TEXT, false);
        t.setGravity(Gravity.CENTER);
        t.setBackgroundColor(BG);
        setContentView(t);
        final File out = new File(getExternalFilesDir(null), "gallery");
        new Thread(() -> {
            try {
                out.mkdirs();
                File[] old = out.listFiles();
                if (old != null) for (File f : old) f.delete();
                LocalDate day = LocalDate.of(2026, 11, 4);
                List<ClockStyle> ps = Presets.all();
                List<Bitmap> tiles = new ArrayList<>();
                List<String> names = new ArrayList<>();
                for (ClockStyle s : ps) { tiles.add(ClockRenderer.composite(s, 360, day, 10, 9, 36)); names.add(s.name); }
                sheet(tiles, names, new File(out, "presets.png"), 6);
                Random r = new Random(2026);
                tiles.clear(); names.clear();
                for (int i = 0; i < 36; i++) { tiles.add(ClockRenderer.composite(Shuffler.all(r), 360, day, 10, 9, 36)); names.add("shuffle " + (i + 1)); }
                sheet(tiles, names, new File(out, "shuffles.png"), 6);
                ClockStyle base = ps.get(1).copy();
                variants(out, "patterns", base, Names.PATTERNS, (s, v) -> { s.pattern = v; s.bezel = 1; }, day, false);
                variants(out, "shapes", ps.get(16), Names.SHAPES, (s, v) -> s.shape = v, day, false);
                variants(out, "bezels", ps.get(16), Names.BEZELS, (s, v) -> s.bezel = v, day, false);
                variants(out, "ticks", ps.get(0), Names.TICKS, (s, v) -> { s.ticks = v; s.numerals = 0; s.comp6 = 0; }, day, false);
                variants(out, "marker12", ps.get(0), Names.MARKER12, (s, v) -> { s.ticks = 1; s.marker12 = v; s.numerals = 0; }, day, false);
                variants(out, "numerals", ps.get(0), Names.NUMERALS, (s, v) -> { s.numerals = v; s.comp6 = 0; }, day, false);
                variants(out, "effects", ps.get(5), Names.EFFECTS, (s, v) -> { s.numerals = 1; s.numeralEffect = v; s.numeralFont = "Sans"; s.numeralColor = 0xFFFFD166; s.numeralAccent = 0xFFFF2BD6; }, day, false);
                variants(out, "hands", ps.get(19), Names.HANDS, (s, v) -> { s.hourHand = v; s.minuteHand = v; }, day, true);
                variants(out, "seconds", ps.get(19), Names.SECONDS, (s, v) -> s.secondHand = v, day, true);
                variants(out, "finishes", ps.get(6), Names.FINISHES, (s, v) -> { s.handFinish = v; s.hourHand = 3; s.minuteHand = 3; s.handAccent = 0xFF7FFFD4; }, day, true);
                variants(out, "caps", ps.get(19), Names.CAPS, (s, v) -> s.cap = v, day, true);
                variants(out, "complications", ps.get(0), Names.COMPS, (s, v) -> { s.comp6 = v; s.comp3 = v; s.numerals = 0; s.subtitle = ""; }, day, false);
                String[] fonts = Fonts.names().toArray(new String[0]);
                ClockStyle fb = ps.get(0).copy();
                tiles.clear(); names.clear();
                for (String f : fonts) {
                    ClockStyle s = fb.copy();
                    s.numerals = 1; s.numeralFont = f; s.brandFont = f; s.brand = "Aion"; s.subtitle = ""; s.comp6 = 0;
                    tiles.add(ClockRenderer.composite(s, 300, day, 10, 9, 36));
                    names.add(f);
                }
                sheet(tiles, names, new File(out, "fonts.png"), 8);
                write(new File(out, "fonts.txt"), String.join("\n", fonts));
                write(new File(out, "done.txt"), "ok " + ps.size() + " presets, " + fonts.length + " fonts");
            } catch (Throwable e) {
                StringBuilder sb = new StringBuilder(e.toString());
                for (StackTraceElement el : e.getStackTrace()) sb.append("\n  at ").append(el);
                try { write(new File(out, "done.txt"), "FAILED " + sb); } catch (Exception ignored) { }
            }
            ui.post(this::finish);
        }, "aion-gallery").start();
    }

    void variants(File out, String name, ClockStyle base, String[] labels, ObjIntConsumer<ClockStyle> f, LocalDate day, boolean handsOnly) throws Exception {
        List<Bitmap> tiles = new ArrayList<>();
        List<String> names = new ArrayList<>();
        for (int i = 0; i < labels.length; i++) {
            ClockStyle s = base.copy();
            f.accept(s, i);
            tiles.add(ClockRenderer.composite(s, 300, day, 10, 9, 36));
            names.add(i + " " + labels[i]);
        }
        sheet(tiles, names, new File(out, name + ".png"), 7);
    }

    static void sheet(List<Bitmap> tiles, List<String> names, File f, int cols) throws Exception {
        if (tiles.isEmpty()) return;
        int t = tiles.get(0).getWidth(), lab = 34;
        int rows = (tiles.size() + cols - 1) / cols;
        Bitmap s = Bitmap.createBitmap(cols * t, rows * (t + lab), Bitmap.Config.ARGB_8888);
        Canvas cv = new Canvas(s);
        cv.drawColor(0xFF5A6070);
        Paint p = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
        Paint tp = new Paint(Paint.ANTI_ALIAS_FLAG);
        tp.setColor(0xFFFFFFFF);
        tp.setTextSize(22);
        tp.setTextAlign(Paint.Align.CENTER);
        for (int i = 0; i < tiles.size(); i++) {
            int x = (i % cols) * t, y = (i / cols) * (t + lab);
            cv.drawBitmap(tiles.get(i), x, y, p);
            cv.drawText(names.get(i), x + t / 2f, y + t + 25, tp);
            tiles.get(i).recycle();
        }
        try (FileOutputStream o = new FileOutputStream(f)) { s.compress(Bitmap.CompressFormat.PNG, 100, o); }
        s.recycle();
    }

    static void write(File f, String text) throws Exception {
        try (FileOutputStream o = new FileOutputStream(f)) { o.write(text.getBytes("UTF-8")); }
    }
}
