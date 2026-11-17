package com.binaryimage.mneme;

import android.app.Activity;
import android.app.AlertDialog;
import android.graphics.Typeface;
import android.text.SpannableStringBuilder;
import android.text.Spanned;
import android.text.TextPaint;
import android.text.method.LinkMovementMethod;
import android.text.style.ClickableSpan;
import android.text.style.ForegroundColorSpan;
import android.text.style.RelativeSizeSpan;
import android.text.style.StyleSpan;
import android.view.View;
import android.view.inputmethod.EditorInfo;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.util.List;

/** Pop-up rhyme finder. Tap a rhyme to drop it into the lyrics (when opened from WRITE). */
final class RhymeSheet {
    interface Pick { void pick(String word); }

    private RhymeSheet() {}

    static AlertDialog show(Activity a, String word, Pick pick) {
        LinearLayout box = Ui.col(a);
        int p = Ui.dp(a, 18);
        box.setPadding(p, p, p, Ui.dp(a, 6));
        box.setBackgroundColor(Ui.BG);

        LinearLayout top = Ui.row(a);
        EditText field = Ui.field(a, "Word to rhyme");
        field.setText(word == null ? "" : word);
        field.setSingleLine(true);
        field.setImeOptions(EditorInfo.IME_ACTION_SEARCH);
        TextView find = Ui.solid(a, "FIND");
        top.addView(field, Ui.lp(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));
        top.addView(find, Ui.margins(Ui.lp(LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT), a, 10, 0, 0, 0));
        box.addView(top);

        TextView out = Ui.text(a, "", 17, Ui.FG);
        out.setLineSpacing(Ui.dp(a, 4), 1f);
        out.setMovementMethod(LinkMovementMethod.getInstance());
        out.setHighlightColor(0x33FFFFFF);
        out.setPadding(0, Ui.dp(a, 14), 0, Ui.dp(a, 8));
        ScrollView sv = new ScrollView(a);
        sv.addView(out);
        box.addView(sv, Ui.lp(LinearLayout.LayoutParams.MATCH_PARENT, Ui.dp(a, 380)));

        AlertDialog d = new AlertDialog.Builder(a, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                .setView(box)
                .setNegativeButton("Close", null)
                .create();

        Runnable render = () -> out.setText(build(a, field.getText().toString(), pick, d));
        find.setOnClickListener(v -> render.run());
        field.setOnEditorActionListener((v, id, e) -> {
            render.run();
            return true;
        });
        if (Dict.ready()) render.run();
        else {
            out.setText("Loading the rhyme dictionary…");
            Dict.load(a, render);
        }
        d.show();
        return d;
    }

    private static CharSequence build(Activity a, String word, Pick pick, AlertDialog d) {
        SpannableStringBuilder sb = new SpannableStringBuilder();
        if (Dict.norm(word).isEmpty()) {
            sb.append("Type a word and tap FIND.");
            return sb;
        }
        Dict.Rhymes r = Dict.rhymes(word);
        int s0 = sb.length();
        sb.append(r.word.toUpperCase());
        sb.setSpan(new StyleSpan(Typeface.BOLD), s0, sb.length(), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
        s0 = sb.length();
        sb.append("   ").append(String.valueOf(r.syl)).append(r.syl == 1 ? " syllable" : " syllables").append("\n");
        sb.setSpan(new ForegroundColorSpan(Ui.DIM), s0, sb.length(), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
        sb.setSpan(new RelativeSizeSpan(0.8f), s0, sb.length(), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
        if (!r.known) {
            sb.append("\nThat word isn't in the dictionary. Try the base word (sing for singing).");
            return sb;
        }
        section(sb, "PERFECT", r.perfect, pick, d);
        section(sb, "NEAR  (same vowel sound)", r.near, pick, d);
        if (!r.rare.isEmpty()) section(sb, "RARE WORDS + NAMES", r.rare, pick, d);
        if (pick != null) {
            int s1 = sb.length();
            sb.append("\nTap a word to put it in your lyrics.");
            sb.setSpan(new ForegroundColorSpan(Ui.DIM), s1, sb.length(), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
            sb.setSpan(new RelativeSizeSpan(0.75f), s1, sb.length(), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
        }
        return sb;
    }

    private static void section(SpannableStringBuilder sb, String title, List<String> words, Pick pick, AlertDialog d) {
        int s0 = sb.length();
        sb.append("\n").append(title).append("\n");
        sb.setSpan(new ForegroundColorSpan(Ui.DIM), s0, sb.length(), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
        sb.setSpan(new RelativeSizeSpan(0.7f), s0, sb.length(), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
        sb.setSpan(new StyleSpan(Typeface.BOLD), s0, sb.length(), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
        if (words.isEmpty()) {
            sb.append("none\n");
            return;
        }
        for (int i = 0; i < words.size(); i++) {
            final String w = words.get(i);
            int a = sb.length();
            sb.append(w);
            if (pick != null) {
                sb.setSpan(new ClickableSpan() {
                    @Override public void onClick(View v) {
                        pick.pick(w);
                        d.dismiss();
                    }

                    @Override public void updateDrawState(TextPaint tp) {
                        tp.setColor(Ui.FG);
                        tp.setUnderlineText(false);
                    }
                }, a, sb.length(), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
            }
            if (i < words.size() - 1) {
                int b = sb.length();
                sb.append("  ·  ");
                sb.setSpan(new ForegroundColorSpan(Ui.FAINT), b, sb.length(), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
            }
        }
        sb.append("\n");
    }
}
