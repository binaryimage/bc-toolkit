package com.binaryimage.mneme;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.ContentValues;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.graphics.Typeface;
import android.media.MediaPlayer;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.provider.MediaStore;
import android.text.Editable;
import android.text.InputType;
import android.text.SpannableStringBuilder;
import android.text.Spanned;
import android.text.TextPaint;
import android.text.TextUtils;
import android.text.TextWatcher;
import android.text.format.DateFormat;
import android.text.method.LinkMovementMethod;
import android.text.style.ClickableSpan;
import android.text.style.ForegroundColorSpan;
import android.text.style.RelativeSizeSpan;
import android.text.style.StyleSpan;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.HorizontalScrollView;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import java.io.File;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.regex.Matcher;

/** One song (or the Ideas page): WRITE, CHECK, SING and CLIPS tabs over the MIC / HUM bar. */
public final class SongActivity extends Activity implements CaptureBar.Host {
    static final String[] TABS = {"WRITE", "CHECK", "SING", "CLIPS"};
    private static final int[] SPEEDS = {8, 12, 17, 23, 30, 39, 50, 64, 80};

    private Store.Song song;
    private final Handler h = new Handler(Looper.getMainLooper());
    private final TextView[] tabButtons = new TextView[TABS.length];
    private final View[] pages = new View[TABS.length];
    private int tab;
    private TextView titleView;
    private EditText editor;
    private LinearLayout checkList, clipList;
    private ScrollView singScroll;
    private TextView singText, scrollBtn, speedLabel;
    private CaptureBar bar;
    private boolean suppressWatch;
    private MediaPlayer player;
    private String playing;
    private boolean scrolling;
    private float scrollAcc;
    private int speedIdx = 2, singSize = 28;
    private SharedPreferences prefs;

    private final Runnable saver = () -> {
        song.updated = System.currentTimeMillis();
        Store.save(this, song);
    };

    private final Runnable scroller = new Runnable() {
        @Override public void run() {
            if (!scrolling) return;
            scrollAcc += SPEEDS[speedIdx] * getResources().getDisplayMetrics().density / 60f;
            int step = (int) scrollAcc;
            if (step > 0) {
                scrollAcc -= step;
                singScroll.scrollBy(0, step);
                View child = singScroll.getChildAt(0);
                if (child != null && singScroll.getScrollY() + singScroll.getHeight() >= child.getHeight()) {
                    setScrolling(false);
                    return;
                }
            }
            h.postDelayed(this, 16);
        }
    };

    @Override protected void onCreate(Bundle b) {
        super.onCreate(b);
        prefs = getSharedPreferences("mneme", MODE_PRIVATE);
        speedIdx = prefs.getInt("speed", 2);
        singSize = prefs.getInt("singSize", 28);
        String id = getIntent().getStringExtra("song");
        song = Store.load(this, id == null ? Store.IDEAS : id);
        if (song == null) {
            finish();
            return;
        }
        Dict.load(this, null);

        LinearLayout root = Ui.col(this);
        root.setBackgroundColor(Ui.BG);

        // ---- top bar
        LinearLayout top = Ui.row(this);
        int p = Ui.dp(this, 12);
        top.setPadding(p, Ui.dp(this, 10), p, Ui.dp(this, 6));
        TextView back = Ui.button(this, "←");
        back.setOnClickListener(v -> finish());
        top.addView(back, Ui.lp(Ui.dp(this, 52), -2));
        titleView = Ui.text(this, song.title, 21, Ui.FG);
        titleView.setTypeface(Typeface.DEFAULT_BOLD);
        titleView.setSingleLine(true);
        titleView.setEllipsize(TextUtils.TruncateAt.END);
        titleView.setPadding(Ui.dp(this, 12), 0, Ui.dp(this, 8), 0);
        if (!song.isIdeas()) titleView.setOnClickListener(v -> rename(this, song, () -> titleView.setText(song.title)));
        top.addView(titleView, Ui.lp(0, -2, 1));
        TextView share = Ui.button(this, "SHARE");
        share.setOnClickListener(v -> shareMenu());
        top.addView(share);
        root.addView(top);

        // ---- tabs
        LinearLayout tabs = Ui.row(this);
        tabs.setPadding(p, Ui.dp(this, 4), p, Ui.dp(this, 8));
        for (int i = 0; i < TABS.length; i++) {
            final int k = i;
            TextView t = Ui.button(this, TABS[i]);
            t.setMinHeight(Ui.dp(this, 40));
            t.setPadding(0, Ui.dp(this, 8), 0, Ui.dp(this, 8));
            t.setOnClickListener(v -> showTab(k));
            tabButtons[i] = t;
            tabs.addView(t, Ui.margins(Ui.lp(0, -2, 1), this, i == 0 ? 0 : 4, 0, i == TABS.length - 1 ? 0 : 4, 0));
        }
        root.addView(tabs);
        root.addView(Ui.divider(this));

        FrameLayout frame = new FrameLayout(this);
        pages[0] = buildWrite();
        pages[1] = buildCheck();
        pages[2] = buildSing();
        pages[3] = buildClips();
        for (View v : pages) frame.addView(v, new FrameLayout.LayoutParams(-1, -1));
        root.addView(frame, Ui.lp(-1, 0, 1));

        bar = new CaptureBar(this, this);
        root.addView(bar, Ui.lp(-1, -2));
        setContentView(root);

        String want = getIntent().getStringExtra("tab");
        int start = 0;
        if (want != null) for (int i = 0; i < TABS.length; i++) if (TABS[i].equalsIgnoreCase(want)) start = i;
        showTab(start);
        ci(getIntent());
    }

    @Override protected void onPause() {
        super.onPause();
        bar.pause();
        setScrolling(false);
        stopPlayback();
        h.removeCallbacks(saver);
        if (song != null) Store.save(this, song);
    }

    @Override protected void onDestroy() {
        if (bar != null) bar.destroy();
        h.removeCallbacksAndMessages(null);
        super.onDestroy();
    }

    @Override public void onRequestPermissionsResult(int code, String[] perms, int[] res) {
        if (code == CaptureBar.PERM_REQUEST) bar.onPermission(res.length > 0 && res[0] == 0);
    }

    private void showTab(int k) {
        tab = k;
        for (int i = 0; i < TABS.length; i++) {
            pages[i].setVisibility(i == k ? View.VISIBLE : View.GONE);
            Ui.setSolid(this, tabButtons[i], i == k, Ui.FG);
        }
        if (k != 2) setScrolling(false);
        if (k == 1) renderCheck();
        if (k == 2) renderSing();
        if (k == 3) renderClips();
        if (k == 2) getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        else if (!bar.busy()) getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        bar.setHint(k == 3 ? "HUM records a melody clip into this song"
                : song.isIdeas() ? "MIC adds lines here  ·  HUM records a melody"
                : "MIC types at the cursor  ·  say \"chorus\" for a tag  ·  HUM records");
    }

    // ================================================================ WRITE

    private View buildWrite() {
        LinearLayout col = Ui.col(this);
        HorizontalScrollView hs = new HorizontalScrollView(this);
        hs.setHorizontalScrollBarEnabled(false);
        LinearLayout chips = Ui.row(this);
        int p = Ui.dp(this, 12);
        chips.setPadding(p, Ui.dp(this, 10), p, Ui.dp(this, 6));
        TextView rhyme = Ui.solid(this, "RHYME");
        rhyme.setOnClickListener(v -> rhymeAtCursor());
        chips.addView(rhyme, Ui.margins(Ui.lp(-2, -2), this, 0, 0, 8, 0));
        for (String s : Store.SECTIONS) {
            TextView c = Ui.button(this, "+ " + s);
            c.setMinHeight(Ui.dp(this, 40));
            c.setTextSize(TypedValue.COMPLEX_UNIT_SP, 12);
            c.setOnClickListener(v -> insertTag(s));
            chips.addView(c, Ui.margins(Ui.lp(-2, -2), this, 0, 0, 8, 0));
        }
        hs.addView(chips);
        col.addView(hs);

        editor = new EditText(this);
        editor.setText(song.text);
        editor.setTextColor(Ui.FG);
        editor.setHintTextColor(Ui.FAINT);
        editor.setHint(song.isIdeas() ? "Ideas, lines, titles, anything…" : "Start writing…");
        editor.setTextSize(TypedValue.COMPLEX_UNIT_SP, 18);
        editor.setLineSpacing(Ui.dp(this, 3), 1f);
        editor.setGravity(Gravity.TOP | Gravity.START);
        editor.setBackground(null);
        editor.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_MULTI_LINE
                | InputType.TYPE_TEXT_FLAG_CAP_SENTENCES);
        editor.setHorizontallyScrolling(false);
        editor.setPadding(Ui.dp(this, 18), Ui.dp(this, 8), Ui.dp(this, 18), Ui.dp(this, 24));
        editor.addTextChangedListener(new TextWatcher() {
            @Override public void beforeTextChanged(CharSequence s, int a, int b, int c) {}
            @Override public void onTextChanged(CharSequence s, int a, int b, int c) {}
            @Override public void afterTextChanged(Editable e) {
                if (suppressWatch) return;
                song.text = e.toString();
                h.removeCallbacks(saver);
                h.postDelayed(saver, 700);
            }
        });
        ScrollView sv = new ScrollView(this);
        sv.setFillViewport(true);
        sv.addView(editor, new ViewGroup.LayoutParams(-1, -2));
        col.addView(sv, Ui.lp(-1, 0, 1));
        return col;
    }

    /** Puts a line on its own line at the cursor (replaces an empty line, else goes under the current one). */
    private void insertLine(String line) {
        Editable e = editor.getText();
        String s = e.toString();
        int pos = editor.getSelectionEnd();
        if (pos < 0 || pos > s.length()) pos = s.length();
        int ls = s.lastIndexOf('\n', pos - 1) + 1;
        int le = s.indexOf('\n', pos);
        if (le < 0) le = s.length();
        int caret;
        if (s.substring(ls, le).trim().isEmpty()) {
            e.replace(ls, le, line);
            caret = ls + line.length();
        } else {
            e.insert(le, "\n" + line);
            caret = le + 1 + line.length();
        }
        editor.setSelection(Math.min(caret, e.length()));
        song.text = e.toString();
        h.removeCallbacks(saver);
        h.postDelayed(saver, 300);
    }

    private void insertTag(String tag) {
        if (tab != 0) showTab(0);
        Editable e = editor.getText();
        String s = e.toString();
        int pos = editor.getSelectionEnd();
        if (pos < 0 || pos > s.length()) pos = s.length();
        int ls = s.lastIndexOf('\n', pos - 1) + 1;
        int le = s.indexOf('\n', pos);
        if (le < 0) le = s.length();
        String t = "[" + tag + "]";
        int caret;
        if (s.substring(ls, le).trim().isEmpty()) {
            boolean gap = false;
            if (ls > 0) {
                int ps = s.lastIndexOf('\n', ls - 2) + 1;
                gap = !s.substring(ps, ls - 1).trim().isEmpty();
            }
            String ins = (gap ? "\n" : "") + t + "\n";
            e.replace(ls, le, ins);
            caret = ls + ins.length();
        } else {
            String ins = "\n\n" + t + "\n";
            e.insert(le, ins);
            caret = le + ins.length();
        }
        editor.setSelection(Math.min(caret, e.length()));
        editor.requestFocus();
        song.text = e.toString();
        h.removeCallbacks(saver);
        h.postDelayed(saver, 300);
    }

    private void rhymeAtCursor() {
        String s = editor.getText().toString();
        int a = editor.getSelectionStart(), b = editor.getSelectionEnd();
        String word = "";
        if (a >= 0 && b > a) word = s.substring(a, b);
        else if (a >= 0) {
            int l = a, r = a;
            while (l > 0 && isWordChar(s.charAt(l - 1))) l--;
            while (r < s.length() && isWordChar(s.charAt(r))) r++;
            if (r > l) word = s.substring(l, r);
            else {   // cursor after a space: use the last word of the line
                int ls = s.lastIndexOf('\n', Math.max(0, a - 1)) + 1;
                Matcher m = Lines.WORD.matcher(s.substring(ls, a));
                while (m.find()) word = m.group();
            }
        }
        RhymeSheet.show(this, word.trim(), this::insertWord);
    }

    private static boolean isWordChar(char c) {
        return Character.isLetter(c) || c == '\'' || c == '’';
    }

    private void insertWord(String w) {
        Editable e = editor.getText();
        int a = editor.getSelectionStart(), b = editor.getSelectionEnd();
        if (a < 0) a = b = e.length();
        String ins = w;
        if (a > 0 && !Character.isWhitespace(e.charAt(a - 1))) ins = " " + ins;
        e.replace(a, b, ins);
        editor.setSelection(Math.min(a + ins.length(), e.length()));
    }

    // ================================================================ CHECK

    private View buildCheck() {
        ScrollView sv = new ScrollView(this);
        checkList = Ui.col(this);
        int p = Ui.dp(this, 18);
        checkList.setPadding(p, Ui.dp(this, 12), p, p);
        sv.addView(checkList);
        return sv;
    }

    private void renderCheck() {
        checkList.removeAllViews();
        TextView tip = Ui.text(this, "Number = syllables. Section headers show the pattern so you can match verses. "
                + "Tap any word for rhymes. Red = same word twice in a line.", 13, Ui.DIM);
        tip.setPadding(0, 0, 0, Ui.dp(this, 10));
        checkList.addView(tip);

        String[] lines = song.text.split("\n", -1);
        TextView header = null;
        List<Integer> pattern = new ArrayList<>();
        String headerName = null;
        int problems = 0;
        for (String raw : lines) {
            String tag = Store.tagOf(raw);
            if (tag != null) {
                if (header != null) setHeader(header, headerName, pattern);
                header = Ui.label(this, tag.toUpperCase(Locale.US));
                header.setPadding(0, Ui.dp(this, 18), 0, Ui.dp(this, 6));
                headerName = tag;
                pattern = new ArrayList<>();
                checkList.addView(header);
                continue;
            }
            if (raw.trim().isEmpty()) {
                View gap = new View(this);
                checkList.addView(gap, Ui.lp(-1, Ui.dp(this, 10)));
                continue;
            }
            Lines.Info info = Lines.check(raw);
            pattern.add(info.syllables);
            LinearLayout row = Ui.row(this);
            row.setGravity(Gravity.TOP);
            TextView lt = Ui.text(this, wordLinks(raw, info.repeats), 18, Ui.FG);
            lt.setMovementMethod(LinkMovementMethod.getInstance());
            lt.setHighlightColor(0x33FFFFFF);
            row.addView(lt, Ui.lp(0, -2, 1));
            TextView n = Ui.text(this, String.valueOf(info.syllables), 18, info.repeats.isEmpty() ? Ui.DIM : Ui.WARN);
            n.setGravity(Gravity.END);
            n.setTypeface(Typeface.MONOSPACE);
            row.addView(n, Ui.lp(Ui.dp(this, 44), -2));
            row.setPadding(0, Ui.dp(this, 4), 0, Ui.dp(this, 4));
            checkList.addView(row);
            if (!info.repeats.isEmpty()) {
                problems++;
                TextView w = Ui.text(this, "same word twice: " + TextUtils.join(", ", info.repeats), 12, Ui.WARN);
                w.setPadding(0, 0, 0, Ui.dp(this, 4));
                checkList.addView(w);
            }
        }
        if (header != null) setHeader(header, headerName, pattern);
        TextView sum = Ui.text(this, problems == 0 ? "✓ No repeated words in any line." :
                problems + (problems == 1 ? " line repeats" : " lines repeat") + " a word.", 13, problems == 0 ? Ui.DIM : Ui.WARN);
        sum.setPadding(0, Ui.dp(this, 20), 0, 0);
        checkList.addView(sum);
        if (!Dict.ready()) Dict.load(this, () -> { if (tab == 1) renderCheck(); });
    }

    private void setHeader(TextView header, String name, List<Integer> pattern) {
        StringBuilder sb = new StringBuilder(name.toUpperCase(Locale.US));
        if (!pattern.isEmpty()) {
            sb.append("     ");
            for (int i = 0; i < pattern.size(); i++) sb.append(i == 0 ? "" : " · ").append(pattern.get(i));
        }
        header.setText(sb.toString());
    }

    private CharSequence wordLinks(String line, List<String> repeats) {
        SpannableStringBuilder sb = new SpannableStringBuilder(line);
        Matcher m = Lines.WORD.matcher(line);
        while (m.find()) {
            final String w = m.group();
            boolean rep = repeats.contains(w.toLowerCase(Locale.US).replace('’', '\''));
            sb.setSpan(new ClickableSpan() {
                @Override public void onClick(View v) { RhymeSheet.show(SongActivity.this, w, null); }
                @Override public void updateDrawState(TextPaint tp) {
                    tp.setColor(rep ? Ui.WARN : Ui.FG);
                    tp.setUnderlineText(rep);
                }
            }, m.start(), m.end(), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
        }
        return sb;
    }

    // ================================================================ SING

    private View buildSing() {
        LinearLayout col = Ui.col(this);
        LinearLayout ctl = Ui.row(this);
        int p = Ui.dp(this, 12);
        ctl.setPadding(p, Ui.dp(this, 10), p, Ui.dp(this, 6));
        scrollBtn = Ui.solid(this, "▶  SCROLL");
        scrollBtn.setSingleLine(true);
        scrollBtn.setPadding(Ui.dp(this, 6), scrollBtn.getPaddingTop(), Ui.dp(this, 6), scrollBtn.getPaddingBottom());
        scrollBtn.setOnClickListener(v -> setScrolling(!scrolling));
        ctl.addView(scrollBtn, Ui.lp(0, -2, 1.6f));
        TextView slower = Ui.button(this, "−");
        slower.setOnClickListener(v -> setSpeed(speedIdx - 1));
        ctl.addView(slower, Ui.margins(Ui.lp(0, -2, 0.6f), this, 8, 0, 0, 0));
        speedLabel = Ui.text(this, "", 13, Ui.FG);
        speedLabel.setGravity(Gravity.CENTER);
        ctl.addView(speedLabel, Ui.lp(0, -2, 0.7f));
        TextView faster = Ui.button(this, "+");
        faster.setOnClickListener(v -> setSpeed(speedIdx + 1));
        ctl.addView(faster, Ui.lp(0, -2, 0.6f));
        TextView smaller = Ui.button(this, "A−");
        smaller.setOnClickListener(v -> setSingSize(singSize - 2));
        ctl.addView(smaller, Ui.margins(Ui.lp(0, -2, 0.7f), this, 8, 0, 0, 0));
        TextView bigger = Ui.button(this, "A+");
        bigger.setOnClickListener(v -> setSingSize(singSize + 2));
        ctl.addView(bigger, Ui.margins(Ui.lp(0, -2, 0.7f), this, 6, 0, 0, 0));
        col.addView(ctl);

        singScroll = new ScrollView(this);
        singText = Ui.text(this, "", singSize, Ui.FG);
        int q = Ui.dp(this, 22);
        singText.setPadding(q, Ui.dp(this, 16), q, Ui.dp(this, 400));
        singText.setLineSpacing(Ui.dp(this, 6), 1f);
        singScroll.addView(singText);
        singScroll.setOnTouchListener((v, ev) -> {
            if (scrolling && ev.getAction() == android.view.MotionEvent.ACTION_DOWN) scrollAcc = 0;
            return false;
        });
        col.addView(singScroll, Ui.lp(-1, 0, 1));
        setSpeed(speedIdx);
        return col;
    }

    private void renderSing() {
        SpannableStringBuilder sb = new SpannableStringBuilder();
        for (String raw : song.text.split("\n", -1)) {
            String tag = Store.tagOf(raw);
            int a = sb.length();
            if (tag != null) {
                sb.append(tag.toUpperCase(Locale.US)).append("\n");
                sb.setSpan(new ForegroundColorSpan(Ui.FAINT), a, sb.length(), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
                sb.setSpan(new RelativeSizeSpan(0.5f), a, sb.length(), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
                sb.setSpan(new StyleSpan(Typeface.BOLD), a, sb.length(), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
            } else {
                sb.append(raw).append("\n");
            }
        }
        singText.setTextSize(TypedValue.COMPLEX_UNIT_SP, singSize);
        singText.setText(sb.length() == 0 ? "No lyrics yet." : sb);
    }

    private void setScrolling(boolean on) {
        if (scrollBtn == null) return;
        scrolling = on;
        h.removeCallbacks(scroller);
        scrollBtn.setText(on ? "❚❚  PAUSE" : "▶  SCROLL");
        Ui.setSolid(this, scrollBtn, true, Ui.FG);
        if (on) {
            View child = singScroll.getChildAt(0);
            if (child != null && singScroll.getScrollY() + singScroll.getHeight() >= child.getHeight() - 2)
                singScroll.scrollTo(0, 0);
            scrollAcc = 0;
            h.post(scroller);
        }
    }

    private void setSpeed(int i) {
        speedIdx = Math.max(0, Math.min(SPEEDS.length - 1, i));
        speedLabel.setText("speed " + (speedIdx + 1));
        prefs.edit().putInt("speed", speedIdx).apply();
    }

    private void setSingSize(int sp) {
        singSize = Math.max(18, Math.min(48, sp));
        singText.setTextSize(TypedValue.COMPLEX_UNIT_SP, singSize);
        prefs.edit().putInt("singSize", singSize).apply();
    }

    // ================================================================ CLIPS

    private View buildClips() {
        ScrollView sv = new ScrollView(this);
        clipList = Ui.col(this);
        int p = Ui.dp(this, 18);
        clipList.setPadding(p, Ui.dp(this, 8), p, p);
        sv.addView(clipList);
        return sv;
    }

    private void renderClips() {
        clipList.removeAllViews();
        if (song.clips.isEmpty()) {
            TextView e = Ui.text(this, "No clips yet.\n\nTap HUM below and sing or hum the melody. "
                    + "Tap it again to stop. The clip is named after the section your cursor is in on WRITE.", 15, Ui.DIM);
            e.setPadding(0, Ui.dp(this, 16), 0, 0);
            clipList.addView(e);
            return;
        }
        for (int i = song.clips.size() - 1; i >= 0; i--) {
            final Store.Clip k = song.clips.get(i);
            LinearLayout row = Ui.row(this);
            row.setPadding(0, Ui.dp(this, 12), 0, Ui.dp(this, 12));
            boolean isPlaying = k.file.equals(playing);
            TextView play = isPlaying ? Ui.solid(this, "■") : Ui.button(this, "▶");
            play.setOnClickListener(v -> togglePlay(k));
            row.addView(play, Ui.lp(Ui.dp(this, 56), Ui.dp(this, 48)));
            LinearLayout info = Ui.col(this);
            info.setPadding(Ui.dp(this, 14), 0, Ui.dp(this, 8), 0);
            TextView name = Ui.text(this, k.name, 17, Ui.FG);
            name.setTypeface(Typeface.DEFAULT_BOLD);
            info.addView(name);
            String sec = k.section == null || k.section.isEmpty() ? "no section" : k.section;
            info.addView(Ui.text(this, sec + "  ·  " + Ui.time(k.durMs) + "  ·  "
                    + DateFormat.format("MMM d, h:mm a", k.created), 13, Ui.DIM));
            row.addView(info, Ui.lp(0, -2, 1));
            TextView more = Ui.button(this, "•••");
            more.setOnClickListener(v -> clipMenu(k));
            row.addView(more, Ui.lp(Ui.dp(this, 56), Ui.dp(this, 48)));
            clipList.addView(row);
            clipList.addView(Ui.divider(this));
        }
    }

    private void togglePlay(Store.Clip k) {
        if (k.file.equals(playing)) {
            stopPlayback();
            renderClips();
            return;
        }
        stopPlayback();
        File f = Store.clipFile(this, k);
        if (!f.exists()) {
            toast("That clip's audio file is missing");
            return;
        }
        try {
            player = new MediaPlayer();
            player.setDataSource(f.getAbsolutePath());
            player.setOnCompletionListener(mp -> {
                stopPlayback();
                if (tab == 3) renderClips();
            });
            player.prepare();
            player.start();
            playing = k.file;
        } catch (Exception e) {
            stopPlayback();
            toast("Couldn't play that clip");
        }
        renderClips();
    }

    private void stopPlayback() {
        if (player != null) {
            try { player.stop(); } catch (Exception ignored) {}
            player.release();
            player = null;
        }
        playing = null;
    }

    private void clipMenu(Store.Clip k) {
        new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                .setTitle(k.name)
                .setItems(new String[]{"Rename", "Set section", "Move to another song", "Share / send", "Delete"}, (d, w) -> {
                    switch (w) {
                        case 0: renameClip(k); break;
                        case 1: pickSection(k); break;
                        case 2: moveClip(k); break;
                        case 3: shareClip(k); break;
                        default: deleteClip(k);
                    }
                })
                .show();
    }

    private void renameClip(Store.Clip k) {
        EditText f = Ui.field(this, "Clip name");
        f.setText(k.name);
        f.setSingleLine(true);
        f.setSelection(f.length());
        new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                .setTitle("Rename clip")
                .setView(padded(f))
                .setPositiveButton("Save", (d, w) -> {
                    String n = f.getText().toString().trim();
                    if (!n.isEmpty()) k.name = n;
                    saveNow();
                    renderClips();
                })
                .setNegativeButton("Cancel", null)
                .show();
    }

    private void pickSection(Store.Clip k) {
        List<String> secs = Store.sectionsIn(song.text);
        for (String s : Store.SECTIONS) if (!secs.contains(s)) secs.add(s);
        secs.add(0, "(none)");
        String[] items = secs.toArray(new String[0]);
        new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                .setTitle("Section for " + k.name)
                .setItems(items, (d, w) -> {
                    k.section = w == 0 ? "" : items[w];
                    saveNow();
                    renderClips();
                })
                .show();
    }

    private void moveClip(Store.Clip k) {
        List<Store.Song> all = new ArrayList<>();
        if (!song.isIdeas()) all.add(Store.load(this, Store.IDEAS));
        for (Store.Song s : Store.songs(this)) if (!s.id.equals(song.id)) all.add(s);
        if (all.isEmpty()) {
            toast("No other songs yet");
            return;
        }
        String[] names = new String[all.size()];
        for (int i = 0; i < all.size(); i++) names[i] = all.get(i).title;
        new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                .setTitle("Move “" + k.name + "” to")
                .setItems(names, (d, w) -> {
                    Store.Song to = all.get(w);
                    if (k.file.equals(playing)) stopPlayback();
                    song.clips.remove(k);
                    to.clips.add(k);
                    to.updated = System.currentTimeMillis();
                    Store.save(this, to);
                    saveNow();
                    renderClips();
                    toast("Moved to " + to.title);
                })
                .show();
    }

    private void shareClip(Store.Clip k) {
        File f = Store.clipFile(this, k);
        if (!f.exists()) {
            toast("That clip's audio file is missing");
            return;
        }
        Uri u = ClipProvider.uriFor(f, song.title + " - " + k.name);
        Intent s = new Intent(Intent.ACTION_SEND)
                .setType(f.getName().endsWith(".wav") ? "audio/wav" : "audio/mp4")
                .putExtra(Intent.EXTRA_STREAM, u)
                .putExtra(Intent.EXTRA_SUBJECT, song.title + " - " + k.name)
                .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        s.setClipData(ClipData.newRawUri(k.name, u));
        startActivity(Intent.createChooser(s, "Send " + k.name));
    }

    private void deleteClip(Store.Clip k) {
        new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                .setTitle("Delete “" + k.name + "”?")
                .setMessage("The recording is gone for good.")
                .setPositiveButton("Delete", (d, w) -> {
                    if (k.file.equals(playing)) stopPlayback();
                    song.clips.remove(k);
                    Store.clipFile(this, k).delete();
                    saveNow();
                    renderClips();
                })
                .setNegativeButton("Cancel", null)
                .show();
    }

    // ================================================================ share / rename

    private String exportText() {
        return song.title + "\n\n" + song.text.trim() + "\n";
    }

    private void shareMenu() {
        new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                .setTitle(song.title)
                .setItems(new String[]{"Share lyrics", "Save .txt to Downloads", "Copy lyrics"}, (d, w) -> {
                    if (w == 0) {
                        Intent s = new Intent(Intent.ACTION_SEND).setType("text/plain")
                                .putExtra(Intent.EXTRA_SUBJECT, song.title)
                                .putExtra(Intent.EXTRA_TEXT, exportText());
                        startActivity(Intent.createChooser(s, "Share lyrics"));
                    } else if (w == 1) {
                        saveTxt();
                    } else {
                        ClipboardManager cm = getSystemService(ClipboardManager.class);
                        if (cm != null) cm.setPrimaryClip(ClipData.newPlainText(song.title, exportText()));
                        toast("Lyrics copied");
                    }
                })
                .show();
    }

    private void saveTxt() {
        String name = song.title.replaceAll("[\\\\/:*?\"<>|]", "_") + " - Lyrics.txt";
        ContentValues v = new ContentValues();
        v.put(MediaStore.Downloads.DISPLAY_NAME, name);
        v.put(MediaStore.Downloads.MIME_TYPE, "text/plain");
        v.put(MediaStore.Downloads.RELATIVE_PATH, "Download/Mneme");
        try {
            Uri u = getContentResolver().insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, v);
            if (u == null) throw new IllegalStateException();
            try (OutputStream out = getContentResolver().openOutputStream(u)) {
                if (out == null) throw new IllegalStateException();
                out.write(exportText().getBytes(StandardCharsets.UTF_8));
            }
            toast("Saved to Download/Mneme/" + name);
        } catch (Exception e) {
            toast("Couldn't save the file");
        }
    }

    static void rename(Activity a, Store.Song s, Runnable after) {
        EditText f = Ui.field(a, "Song title");
        f.setText(s.title);
        f.setSingleLine(true);
        f.setSelection(f.length());
        new AlertDialog.Builder(a, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                .setTitle("Rename song")
                .setView(padded(a, f))
                .setPositiveButton("Save", (d, w) -> {
                    String n = f.getText().toString().trim();
                    if (n.isEmpty()) return;
                    Store.Song fresh = Store.load(a, s.id);
                    if (fresh != null) {
                        fresh.title = n;
                        fresh.updated = System.currentTimeMillis();
                        Store.save(a, fresh);
                    }
                    s.title = n;
                    if (after != null) after.run();
                })
                .setNegativeButton("Cancel", null)
                .show();
    }

    private View padded(View v) { return padded(this, v); }

    private static View padded(Context c, View v) {
        LinearLayout box = Ui.col(c);
        int p = Ui.dp(c, 20);
        box.setPadding(p, Ui.dp(c, 8), p, 0);
        box.addView(v);
        return box;
    }

    private void saveNow() {
        h.removeCallbacks(saver);
        song.updated = System.currentTimeMillis();
        Store.save(this, song);
    }

    private void toast(String s) {
        Toast.makeText(this, s, Toast.LENGTH_SHORT).show();
    }

    // ================================================================ capture bar

    @Override public void onDictatedLine(String line) {
        if (tab != 0) showTab(0);
        insertLine(line);
    }

    @Override public void onDictatedTag(String tag) {
        insertTag(tag);
    }

    @Override public Store.Song clipTarget() {
        // keep any typing that hasn't hit disk yet before the clip is saved
        song.text = editor.getText().toString();
        return song;
    }

    @Override public String clipSection() {
        return Store.sectionAt(editor.getText().toString(), Math.max(0, editor.getSelectionStart()));
    }

    @Override public void beforeCapture() {
        stopPlayback();
        setScrolling(false);
        if (tab == 3) renderClips();
    }

    @Override public void onClipAdded(Store.Clip clip) {
        if (tab == 3) renderClips();
    }

    // ================================================================ CI screenshots only

    private void ci(Intent i) {
        int cursor = i.getIntExtra("ci_cursor", -1);
        if (cursor >= 0) {
            editor.requestFocus();
            editor.setSelection(Math.min(cursor, editor.length()));
        }
        String r = i.getStringExtra("ci_rhyme");
        if (r != null) Dict.load(this, () -> RhymeSheet.show(this, r, tab == 0 ? this::insertWord : null));
        if (i.getBooleanExtra("ci_share", false)) h.postDelayed(this::shareMenu, 500);
        if (i.getBooleanExtra("ci_clipmenu", false) && !song.clips.isEmpty())
            h.postDelayed(() -> clipMenu(song.clips.get(song.clips.size() - 1)), 500);
        if (i.getBooleanExtra("ci_scroll", false)) h.postDelayed(() -> setScrolling(true), 800);
        if (i.getBooleanExtra("ci_play", false) && !song.clips.isEmpty()) h.postDelayed(() -> {
            Store.Clip longest = song.clips.get(0);
            for (Store.Clip k : song.clips) if (k.durMs > longest.durMs) longest = k;
            togglePlay(longest);
        }, 300);
        String say = i.getStringExtra("ci_say");     // pretend the mic heard these ("/" between phrases, "_" for spaces)
        if (say != null) h.postDelayed(() -> {
            for (String s : say.replace('_', ' ').split("/")) {
                String tag = Dictation.spokenTag(s);
                if (tag != null) onDictatedTag(tag);
                else onDictatedLine(Dictation.tidy(s));
            }
        }, 600);
    }
}
