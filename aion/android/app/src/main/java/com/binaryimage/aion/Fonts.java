package com.binaryimage.aion;

import android.content.Context;
import android.graphics.Typeface;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/** Built-in Android families plus every .ttf/.otf bundled under assets/fonts. */
public final class Fonts {
    private Fonts() { }

    private static final Map<String, Typeface> map = new LinkedHashMap<>();
    private static List<String> names;

    public static synchronized void init(Context ctx) {
        if (names != null) return;
        sys("Sans", "sans-serif", Typeface.NORMAL);
        sys("Sans Light", "sans-serif-light", Typeface.NORMAL);
        sys("Sans Thin", "sans-serif-thin", Typeface.NORMAL);
        sys("Sans Medium", "sans-serif-medium", Typeface.NORMAL);
        sys("Sans Black", "sans-serif-black", Typeface.NORMAL);
        sys("Condensed", "sans-serif-condensed", Typeface.NORMAL);
        sys("Condensed Light", "sans-serif-condensed-light", Typeface.NORMAL);
        sys("Serif", "serif", Typeface.NORMAL);
        sys("Mono", "monospace", Typeface.NORMAL);
        sys("Serif Mono", "serif-monospace", Typeface.NORMAL);
        sys("Casual", "casual", Typeface.NORMAL);
        sys("Cursive", "cursive", Typeface.NORMAL);
        sys("Small Caps", "sans-serif-smallcaps", Typeface.NORMAL);
        try {
            String[] files = ctx.getAssets().list("fonts");
            if (files != null) {
                Arrays.sort(files);
                for (String f : files) {
                    String lower = f.toLowerCase();
                    if (!lower.endsWith(".ttf") && !lower.endsWith(".otf")) continue;
                    try {
                        Typeface t = Typeface.createFromAsset(ctx.getAssets(), "fonts/" + f);
                        map.put(prettyName(f), t);
                    } catch (Exception ignored) { }
                }
            }
        } catch (Exception ignored) { }
        names = new ArrayList<>(map.keySet());
    }

    private static void sys(String name, String family, int style) {
        map.put(name, Typeface.create(family, style));
    }

    /** "PlayfairDisplay.ttf" -> "Playfair Display" */
    static String prettyName(String file) {
        String base = file.replaceAll("\\.(ttf|otf|TTF|OTF)$", "").replace('_', ' ').replace('-', ' ');
        StringBuilder b = new StringBuilder();
        for (int i = 0; i < base.length(); i++) {
            char c = base.charAt(i);
            if (i > 0 && Character.isUpperCase(c) && Character.isLowerCase(base.charAt(i - 1))) b.append(' ');
            b.append(c);
        }
        return b.toString().replaceAll("\\s+", " ").trim();
    }

    public static List<String> names() { return names == null ? new ArrayList<>() : names; }

    public static Typeface get(String name, int weight) {
        Typeface base = map.get(name);
        if (base == null) base = Typeface.SANS_SERIF;
        int style;
        switch (weight) {
            case 1: style = Typeface.BOLD; break;
            case 2: style = Typeface.ITALIC; break;
            case 3: style = Typeface.BOLD_ITALIC; break;
            default: return base;
        }
        return Typeface.create(base, style);
    }
}
