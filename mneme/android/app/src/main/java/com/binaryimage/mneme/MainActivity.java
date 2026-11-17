package com.binaryimage.mneme;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.provider.OpenableColumns;
import android.widget.Toast;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import android.graphics.Typeface;
import android.os.Bundle;
import android.text.format.DateFormat;
import android.view.Gravity;
import android.view.View;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputMethodManager;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.util.List;

/** Home: quick capture into Ideas, plus the list of songs. */
public final class MainActivity extends Activity implements CaptureBar.Host {
    private LinearLayout list;
    private TextView ideasInfo;
    private CaptureBar bar;
    private EditText quick;

    @Override protected void onCreate(Bundle b) {
        super.onCreate(b);
        Store.seed(this);
        Dict.load(this, null);

        LinearLayout root = Ui.col(this);
        root.setBackgroundColor(Ui.BG);
        int p = Ui.dp(this, 18);

        LinearLayout content = Ui.col(this);
        content.setPadding(p, Ui.dp(this, 22), p, p);

        TextView title = Ui.text(this, "MNEME", 30, Ui.FG);
        title.setTypeface(Typeface.DEFAULT_BOLD);
        title.setLetterSpacing(0.3f);
        content.addView(title);
        TextView sub = Ui.text(this, "lyrics · ideas · melodies", 13, Ui.DIM);
        content.addView(sub, Ui.margins(Ui.lp(-2, -2), this, 0, 0, 0, 18));

        // ---- quick line into Ideas
        LinearLayout qrow = Ui.row(this);
        quick = Ui.field(this, "Quick line…");
        quick.setSingleLine(true);
        quick.setImeOptions(EditorInfo.IME_ACTION_DONE);
        quick.setOnEditorActionListener((v, id, e) -> {
            addQuick();
            return true;
        });
        TextView add = Ui.solid(this, "ADD");
        add.setOnClickListener(v -> addQuick());
        qrow.addView(quick, Ui.lp(0, -2, 1));
        qrow.addView(add, Ui.margins(Ui.lp(-2, -2), this, 10, 0, 0, 0));
        content.addView(qrow);

        // ---- Ideas card
        LinearLayout card = Ui.col(this);
        card.setBackground(Ui.ripple(this, Ui.box(this, Ui.CARD, Ui.FG, 14)));
        card.setPadding(p, Ui.dp(this, 14), p, Ui.dp(this, 14));
        card.setClickable(true);
        TextView ct = Ui.text(this, "IDEAS", 18, Ui.FG);
        ct.setTypeface(Typeface.DEFAULT_BOLD);
        ct.setLetterSpacing(0.12f);
        card.addView(ct);
        ideasInfo = Ui.text(this, "", 13, Ui.DIM);
        card.addView(ideasInfo);
        card.setOnClickListener(v -> open(Store.IDEAS, null));
        content.addView(card, Ui.margins(Ui.lp(-1, -2), this, 0, 14, 0, 22));

        // ---- songs header
        LinearLayout sh = Ui.row(this);
        TextView sl = Ui.label(this, "SONGS");
        sh.addView(sl, Ui.lp(0, -2, 1));
        TextView imp = Ui.button(this, "IMPORT .TXT");
        imp.setOnClickListener(v -> pickTxt());
        sh.addView(imp, Ui.margins(Ui.lp(-2, -2), this, 0, 0, 8, 0));
        TextView neu = Ui.button(this, "+ NEW SONG");
        neu.setOnClickListener(v -> newSong());
        sh.addView(neu);
        content.addView(sh, Ui.margins(Ui.lp(-1, -2), this, 0, 0, 0, 8));

        list = Ui.col(this);
        content.addView(list);

        ScrollView sv = new ScrollView(this);
        sv.setFillViewport(true);
        sv.addView(content);
        root.addView(sv, Ui.lp(-1, 0, 1));

        bar = new CaptureBar(this, this);
        bar.setHint("MIC and HUM here go to IDEAS");
        root.addView(bar, Ui.lp(-1, -2));
        setContentView(root);

        ci(getIntent());
    }

    @Override protected void onResume() {
        super.onResume();
        refresh();
    }

    @Override protected void onPause() {
        super.onPause();
        bar.pause();
    }

    @Override protected void onDestroy() {
        bar.destroy();
        super.onDestroy();
    }

    @Override public void onRequestPermissionsResult(int code, String[] perms, int[] res) {
        if (code == CaptureBar.PERM_REQUEST) bar.onPermission(res.length > 0 && res[0] == 0);
    }

    private void refresh() {
        Store.Song ideas = Store.load(this, Store.IDEAS);
        int lines = ideas.lineCount();
        ideasInfo.setText(lines + (lines == 1 ? " line" : " lines") + "  ·  " + ideas.clips.size()
                + (ideas.clips.size() == 1 ? " clip" : " clips") + "   →");

        list.removeAllViews();
        List<Store.Song> songs = Store.songs(this);
        if (songs.isEmpty()) {
            TextView e = Ui.text(this, "No songs yet. Tap + NEW SONG.", 15, Ui.DIM);
            e.setPadding(0, Ui.dp(this, 12), 0, 0);
            list.addView(e);
        }
        for (Store.Song s : songs) {
            LinearLayout row = Ui.col(this);
            row.setPadding(0, Ui.dp(this, 14), 0, Ui.dp(this, 14));
            row.setBackground(Ui.ripple(this, Ui.box(this, Ui.BG, 0, 0)));
            row.setClickable(true);
            TextView t = Ui.text(this, s.title, 19, Ui.FG);
            t.setTypeface(Typeface.DEFAULT_BOLD);
            row.addView(t);
            int n = s.lineCount();
            TextView i = Ui.text(this, n + " lines  ·  " + s.clips.size() + " clips  ·  "
                    + DateFormat.format("MMM d, h:mm a", s.updated), 13, Ui.DIM);
            row.addView(i);
            row.setOnClickListener(v -> open(s.id, null));
            row.setOnLongClickListener(v -> {
                songMenu(s);
                return true;
            });
            list.addView(row);
            list.addView(Ui.divider(this));
        }
        TextView tip = Ui.text(this, "Long-press a song to rename or delete it.", 12, Ui.FAINT);
        tip.setGravity(Gravity.CENTER);
        tip.setPadding(0, Ui.dp(this, 16), 0, 0);
        if (!songs.isEmpty()) list.addView(tip);
    }

    private void open(String id, String tab) {
        Intent i = new Intent(this, SongActivity.class).putExtra("song", id);
        if (tab != null) i.putExtra("tab", tab);
        startActivity(i);
    }

    private void addQuick() {
        String t = quick.getText().toString().trim();
        if (t.isEmpty()) return;
        onDictatedLine(t);
        quick.setText("");
        InputMethodManager imm = getSystemService(InputMethodManager.class);
        if (imm != null) imm.hideSoftInputFromWindow(quick.getWindowToken(), 0);
        bar.setHint("✓ Added to Ideas");
    }

    private static final int PICK_TXT = 41;

    private void pickTxt() {
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("text/*");
        try {
            startActivityForResult(i, PICK_TXT);
        } catch (Exception e) {
            Toast.makeText(this, "No file picker on this phone", Toast.LENGTH_SHORT).show();
        }
    }

    @Override protected void onActivityResult(int code, int result, Intent data) {
        super.onActivityResult(code, result, data);
        if (code != PICK_TXT || result != RESULT_OK || data == null || data.getData() == null) return;
        Uri u = data.getData();
        String name = null;
        try (Cursor c = getContentResolver().query(u, new String[]{OpenableColumns.DISPLAY_NAME}, null, null, null)) {
            if (c != null && c.moveToFirst()) name = c.getString(0);
        } catch (Exception ignored) {
        }
        try (InputStream in = getContentResolver().openInputStream(u)) {
            if (in == null) throw new IllegalStateException();
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            byte[] buf = new byte[8192];
            int n;
            while ((n = in.read(buf)) > 0 && out.size() < 2_000_000) out.write(buf, 0, n);
            Store.Song s = Store.importText(this, name, out.toString(StandardCharsets.UTF_8.name()));
            refresh();
            open(s.id, "WRITE");
        } catch (Exception e) {
            Toast.makeText(this, "Couldn't read that file", Toast.LENGTH_SHORT).show();
        }
    }

    private void newSong() {
        EditText name = Ui.field(this, "Song title");
        name.setSingleLine(true);
        LinearLayout box = Ui.col(this);
        int p = Ui.dp(this, 20);
        box.setPadding(p, Ui.dp(this, 8), p, 0);
        box.addView(name);
        new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                .setTitle("New song")
                .setView(box)
                .setPositiveButton("Create", (d, w) -> {
                    Store.Song s = Store.create(this, name.getText().toString());
                    open(s.id, "WRITE");
                })
                .setNegativeButton("Cancel", null)
                .show();
        name.requestFocus();
    }

    private void songMenu(Store.Song s) {
        new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                .setTitle(s.title)
                .setItems(new String[]{"Open", "Rename", "Delete"}, (d, w) -> {
                    if (w == 0) open(s.id, null);
                    else if (w == 1) SongActivity.rename(this, s, this::refresh);
                    else confirmDelete(s);
                })
                .show();
    }

    private void confirmDelete(Store.Song s) {
        new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                .setTitle("Delete “" + s.title + "”?")
                .setMessage("The lyrics and its " + s.clips.size() + " clips are gone for good. Share or save the lyrics first if you want a copy.")
                .setPositiveButton("Delete", (d, w) -> {
                    Store.delete(this, s);
                    refresh();
                })
                .setNegativeButton("Cancel", null)
                .show();
    }

    // ---------------------------------------------------------------- capture into Ideas

    @Override public void onDictatedLine(String line) {
        Store.Song s = Store.load(this, Store.IDEAS);
        String t = s.text;
        if (!t.isEmpty() && !t.endsWith("\n")) t += "\n";
        s.text = t + line + "\n";
        s.updated = System.currentTimeMillis();
        Store.save(this, s);
        refresh();
    }

    @Override public void onDictatedTag(String tag) {
        onDictatedLine("[" + tag + "]");
    }

    @Override public Store.Song clipTarget() { return Store.load(this, Store.IDEAS); }

    @Override public String clipSection() { return ""; }

    @Override public void beforeCapture() {}

    @Override public void onClipAdded(Store.Clip clip) { refresh(); }

    // ---------------------------------------------------------------- CI screenshots only

    private void ci(Intent i) {
        if (i == null) return;
        if (i.getBooleanExtra("ci_seed", false)) {
            Ci.seed(this);
            refresh();
        }
        if (i.getBooleanExtra("ci_new", false)) list.post(this::newSong);
        if (i.getBooleanExtra("ci_menu", false)) list.post(() -> {
            List<Store.Song> s = Store.songs(this);
            if (!s.isEmpty()) songMenu(s.get(0));
        });
    }
}
