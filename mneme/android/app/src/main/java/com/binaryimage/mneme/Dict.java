package com.binaryimage.mneme;

import android.content.Context;
import android.os.Handler;
import android.os.Looper;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Set;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * Offline pronunciation dictionary (CMU dict, ~125k words) used for syllable counts and rhymes.
 * Each word carries: syllables, its rhyme tail (sounds from the last stressed vowel on), the vowels of that
 * tail (for near rhymes), the kind of sound it ends on, and how common the word is.
 */
final class Dict {
    static final class Entry {
        final String word, rhyme, vowels;
        final int syl, rank;
        final char fin;

        Entry(String w, int syl, String rhyme, String vowels, char fin, int rank) {
            this.word = w; this.syl = syl; this.rhyme = rhyme; this.vowels = vowels; this.fin = fin; this.rank = rank;
        }

        int order() { return rank > 0 ? rank : 1_000_000; }
    }

    static final class Rhymes {
        String word;
        int syl = -1;
        boolean known;
        final List<String> perfect = new ArrayList<>();
        final List<String> near = new ArrayList<>();
    }

    private static final HashMap<String, Entry> WORDS = new HashMap<>(200_000);
    private static final HashMap<String, List<Entry>> BY_RHYME = new HashMap<>();
    private static final HashMap<String, List<Entry>> BY_VOWELS = new HashMap<>();
    private static volatile boolean ready, loading;
    private static final List<Runnable> WAITING = new ArrayList<>();
    private static final Pattern VOWELS = Pattern.compile("[aeiouy]{1,2}");

    private Dict() {}

    static boolean ready() { return ready; }

    /** Loads in the background the first time; runs {@code then} on the main thread once it's ready. */
    static synchronized void load(Context c, Runnable then) {
        if (ready) {
            if (then != null) then.run();
            return;
        }
        if (then != null) WAITING.add(then);
        if (loading) return;
        loading = true;
        final Context app = c.getApplicationContext();
        new Thread(() -> {
            try (BufferedReader r = new BufferedReader(new InputStreamReader(app.getAssets().open("words.tsv"), StandardCharsets.UTF_8), 1 << 16)) {
                String line;
                while ((line = r.readLine()) != null) {
                    String[] p = line.split("\t");
                    if (p.length < 6) continue;
                    Entry e = new Entry(p[0], Integer.parseInt(p[1]), p[2], p[3], p[4].charAt(0), Integer.parseInt(p[5]));
                    WORDS.put(e.word, e);
                    BY_RHYME.computeIfAbsent(e.rhyme, k -> new ArrayList<>()).add(e);
                    if (e.rank > 0) BY_VOWELS.computeIfAbsent(e.vowels, k -> new ArrayList<>()).add(e);
                }
            } catch (Exception ignored) {
            }
            ready = true;
            new Handler(Looper.getMainLooper()).post(() -> {
                List<Runnable> run;
                synchronized (Dict.class) {
                    run = new ArrayList<>(WAITING);
                    WAITING.clear();
                }
                for (Runnable x : run) x.run();
            });
        }, "mneme-dict").start();
    }

    static String norm(String w) {
        return w.toLowerCase(Locale.US).replace('’', '\'').replaceAll("[^a-z']", "").replaceAll("^'+|'+$", "");
    }

    static int syllables(String word) {
        String w = norm(word);
        if (w.isEmpty()) return 0;
        if (ready) {
            Entry e = WORDS.get(w);
            if (e != null) return e.syl;
        }
        return guess(w);
    }

    /** Rule-of-thumb count for words the dictionary doesn't know. */
    static int guess(String w) {
        w = w.replace("'", "");
        if (w.isEmpty()) return 0;
        if (w.length() <= 3) return 1;
        w = w.replaceAll("(?:[^laeiouy]es|[^laeiouy]ed|[^laeiouy]e)$", "");
        w = w.replaceAll("^y", "");
        Matcher m = VOWELS.matcher(w);
        int n = 0;
        while (m.find()) n++;
        return Math.max(1, n);
    }

    static Rhymes rhymes(String word) {
        Rhymes out = new Rhymes();
        out.word = norm(word);
        if (!ready || out.word.isEmpty()) return out;
        Entry e = WORDS.get(out.word);
        if (e == null) {
            out.syl = guess(out.word);
            return out;
        }
        out.known = true;
        out.syl = e.syl;
        Set<String> seen = new HashSet<>();
        seen.add(e.word);

        List<Entry> p = new ArrayList<>(BY_RHYME.getOrDefault(e.rhyme, Collections.emptyList()));
        p.sort((a, b) -> Integer.compare(a.order(), b.order()));
        int unranked = 0;
        for (Entry x : p) {
            if (out.perfect.size() >= 80) break;
            if (x.word.contains("'") || !seen.add(x.word)) continue;
            if (x.rank == 0 && ++unranked > 25) continue;
            out.perfect.add(x.word);
        }

        List<Entry> n = new ArrayList<>(BY_VOWELS.getOrDefault(e.vowels, Collections.emptyList()));
        final char fin = e.fin;
        n.sort((a, b) -> {
            int fa = a.fin == fin ? 0 : 1, fb = b.fin == fin ? 0 : 1;
            return fa != fb ? Integer.compare(fa, fb) : Integer.compare(a.order(), b.order());
        });
        for (Entry x : n) {
            if (out.near.size() >= 80) break;
            if (x.word.contains("'") || !seen.add(x.word)) continue;
            out.near.add(x.word);
        }
        return out;
    }
}
