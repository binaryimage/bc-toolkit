package com.binaryimage.mneme;

import android.content.Context;
import android.content.SharedPreferences;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/** Songs live as one JSON file each in the app's private storage; clips are audio files next to them. */
final class Store {
    static final String IDEAS = "ideas";
    static final String[] SECTIONS = {"Intro", "Verse", "Pre-Chorus", "Chorus", "Bridge", "Breakdown", "Solo", "Outro", "End"};
    private static final Pattern TAG = Pattern.compile("^\\s*\\[(.+?)\\]\\s*$");

    static final class Clip {
        String file, name, section;
        long created;
        int durMs;
    }

    static final class Song {
        String id, title, text = "";
        long created, updated;
        final List<Clip> clips = new ArrayList<>();

        boolean isIdeas() { return IDEAS.equals(id); }

        int lineCount() {
            int n = 0;
            for (String l : text.split("\n", -1)) if (!l.trim().isEmpty() && tagOf(l) == null) n++;
            return n;
        }
    }

    private Store() {}

    /** "[Chorus]" -> "Chorus", anything else -> null. */
    static String tagOf(String line) {
        Matcher m = TAG.matcher(line);
        return m.matches() ? m.group(1).trim() : null;
    }

    /** The section a character position sits in (the closest [Tag] above it). */
    static String sectionAt(String text, int pos) {
        if (pos < 0) pos = 0;
        if (pos > text.length()) pos = text.length();
        String[] lines = text.substring(0, pos).split("\n", -1);
        for (int i = lines.length - 1; i >= 0; i--) {
            String t = tagOf(lines[i]);
            if (t != null) return t;
        }
        return "";
    }

    /** Every section tag in a song, in order, no duplicates. */
    static List<String> sectionsIn(String text) {
        List<String> out = new ArrayList<>();
        for (String l : text.split("\n", -1)) {
            String t = tagOf(l);
            if (t != null && !out.contains(t)) out.add(t);
        }
        return out;
    }

    static File songsDir(Context c) {
        File d = new File(c.getFilesDir(), "songs");
        d.mkdirs();
        return d;
    }

    static File clipsDir(Context c) {
        File d = new File(c.getFilesDir(), "clips");
        d.mkdirs();
        return d;
    }

    static File clipFile(Context c, Clip k) { return new File(clipsDir(c), k.file); }

    static List<Song> songs(Context c) {
        List<Song> out = new ArrayList<>();
        File[] fs = songsDir(c).listFiles();
        if (fs != null) for (File f : fs) {
            if (!f.getName().endsWith(".json")) continue;
            Song s = read(f);
            if (s != null && !s.isIdeas()) out.add(s);
        }
        Collections.sort(out, (a, b) -> Long.compare(b.updated, a.updated));
        return out;
    }

    static Song load(Context c, String id) {
        Song s = read(new File(songsDir(c), id + ".json"));
        if (s == null && IDEAS.equals(id)) {
            s = new Song();
            s.id = IDEAS;
            s.title = "Ideas";
            s.created = s.updated = System.currentTimeMillis();
            save(c, s);
        }
        return s;
    }

    static Song create(Context c, String title) {
        Song s = new Song();
        s.id = "s" + System.currentTimeMillis();
        s.title = title.trim().isEmpty() ? "Untitled" : title.trim();
        s.created = s.updated = System.currentTimeMillis();
        s.text = "[Verse]\n";
        save(c, s);
        return s;
    }

    static void save(Context c, Song s) {
        try {
            JSONObject o = new JSONObject();
            o.put("id", s.id);
            o.put("title", s.title);
            o.put("text", s.text);
            o.put("created", s.created);
            o.put("updated", s.updated);
            JSONArray a = new JSONArray();
            for (Clip k : s.clips) {
                JSONObject j = new JSONObject();
                j.put("file", k.file);
                j.put("name", k.name);
                j.put("section", k.section == null ? "" : k.section);
                j.put("created", k.created);
                j.put("durMs", k.durMs);
                a.put(j);
            }
            o.put("clips", a);
            File f = new File(songsDir(c), s.id + ".json");
            File tmp = new File(songsDir(c), s.id + ".json.tmp");
            try (FileOutputStream out = new FileOutputStream(tmp)) {
                out.write(o.toString().getBytes(StandardCharsets.UTF_8));
                out.getFD().sync();
            }
            if (!tmp.renameTo(f)) {
                try (FileOutputStream out = new FileOutputStream(f)) {
                    out.write(o.toString().getBytes(StandardCharsets.UTF_8));
                }
                tmp.delete();
            }
        } catch (Exception ignored) {
        }
    }

    static void delete(Context c, Song s) {
        for (Clip k : s.clips) clipFile(c, k).delete();
        new File(songsDir(c), s.id + ".json").delete();
    }

    private static Song read(File f) {
        if (!f.exists()) return null;
        try (FileInputStream in = new FileInputStream(f)) {
            byte[] b = new byte[(int) f.length()];
            int off = 0;
            while (off < b.length) {
                int r = in.read(b, off, b.length - off);
                if (r < 0) break;
                off += r;
            }
            JSONObject o = new JSONObject(new String(b, 0, off, StandardCharsets.UTF_8));
            Song s = new Song();
            s.id = o.getString("id");
            s.title = o.optString("title", "Untitled");
            s.text = o.optString("text", "");
            s.created = o.optLong("created");
            s.updated = o.optLong("updated");
            JSONArray a = o.optJSONArray("clips");
            if (a != null) for (int i = 0; i < a.length(); i++) {
                JSONObject j = a.getJSONObject(i);
                Clip k = new Clip();
                k.file = j.getString("file");
                k.name = j.optString("name", "Clip");
                k.section = j.optString("section", "");
                k.created = j.optLong("created");
                k.durMs = j.optInt("durMs");
                s.clips.add(k);
            }
            return s;
        } catch (IOException | org.json.JSONException e) {
            return null;
        }
    }

    /** First run: make sure the Ideas page exists. */
    static void seed(Context c) {
        SharedPreferences p = c.getSharedPreferences("mneme", Context.MODE_PRIVATE);
        if (p.getBoolean("seeded", false)) return;
        load(c, IDEAS);
        p.edit().putBoolean("seeded", true).apply();
    }

    /**
     * A .txt becomes a song. The title comes from the file name ("X - Lyrics.txt" -> "X"); if the file starts with
     * that title (like Mneme's own exports), the heading lines down to the first blank line are dropped.
     */
    static Song importText(Context c, String fileName, String body) {
        String title = fileName == null ? "Imported" : fileName.replaceAll("(?i)\\.txt$", "").replaceAll("(?i)\\s*-\\s*lyrics$", "").trim();
        if (title.isEmpty()) title = "Imported";
        String t = body.replace("\r\n", "\n").replace('\r', '\n');
        String[] lines = t.split("\n", -1);
        int i = 0;
        while (i < lines.length && lines[i].trim().isEmpty()) i++;
        if (i < lines.length && lines[i].trim().equalsIgnoreCase(title)) {
            while (i < lines.length && !lines[i].trim().isEmpty()) i++;
            StringBuilder sb = new StringBuilder();
            for (int k = i; k < lines.length; k++) sb.append(lines[k]).append(k < lines.length - 1 ? "\n" : "");
            t = sb.toString();
        }
        Song s = new Song();
        s.id = "s" + System.currentTimeMillis();
        s.title = title;
        s.text = t.replaceAll("^\\s+", "");
        s.created = s.updated = System.currentTimeMillis();
        save(c, s);
        return s;
    }
}
