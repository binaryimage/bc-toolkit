package com.binaryimage.aion;

import android.content.Context;
import android.content.SharedPreferences;

import org.json.JSONArray;

import java.util.ArrayList;
import java.util.List;

/** Saved styles: one per widget, the default for new widgets, and the user's favourites. */
public final class Store {
    private Store() { }

    static SharedPreferences prefs(Context c) { return c.getSharedPreferences("aion", Context.MODE_PRIVATE); }

    public static ClockStyle forWidget(Context c, int id) {
        String js = prefs(c).getString("w_" + id, null);
        return js != null ? ClockStyle.fromJson(js) : getDefault(c);
    }

    public static void setForWidget(Context c, int id, ClockStyle s) {
        prefs(c).edit().putString("w_" + id, s.toString()).apply();
    }

    public static void removeWidget(Context c, int id) {
        prefs(c).edit().remove("w_" + id).remove("key_" + id).apply();
    }

    public static ClockStyle getDefault(Context c) {
        String js = prefs(c).getString("default", null);
        if (js != null) return ClockStyle.fromJson(js);
        return Presets.all().get(1).copy();
    }

    public static void setDefault(Context c, ClockStyle s) {
        prefs(c).edit().putString("default", s.toString()).apply();
    }

    public static List<ClockStyle> saved(Context c) {
        List<ClockStyle> out = new ArrayList<>();
        try {
            JSONArray a = new JSONArray(prefs(c).getString("saved", "[]"));
            for (int i = 0; i < a.length(); i++) out.add(ClockStyle.fromJson(a.getString(i)));
        } catch (Exception ignored) { }
        return out;
    }

    public static void setSaved(Context c, List<ClockStyle> list) {
        JSONArray a = new JSONArray();
        for (ClockStyle s : list) a.put(s.toString());
        prefs(c).edit().putString("saved", a.toString()).apply();
    }

    public static void addSaved(Context c, ClockStyle s) {
        List<ClockStyle> l = saved(c);
        l.add(0, s.copy());
        setSaved(c, l);
    }
}
