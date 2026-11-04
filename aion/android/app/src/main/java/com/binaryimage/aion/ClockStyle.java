package com.binaryimage.aion;

import org.json.JSONObject;

import java.lang.reflect.Field;
import java.lang.reflect.Modifier;

/** Every choice that makes up one clock. Plain public fields, saved as JSON by reflection. */
public class ClockStyle {
    public String name = "Custom";

    // ---- case + face
    public int shape = 0;            // Names.SHAPES
    public int pattern = 0;          // Names.PATTERNS
    public int faceA = 0xFF101418;
    public int faceB = 0xFF2A3440;
    public float detail = 0.5f;      // pattern density / scale
    public int seed = 7;             // random patterns (stars, aurora, brushing)
    public boolean gloss = false;    // crystal reflection
    public boolean vignette = true;
    public boolean depth = true;     // inner shadow + case shadow

    // ---- bezel
    public int bezel = 1;            // Names.BEZELS
    public int bezelColor = 0xFFC9CED6;
    public int bezelAccent = 0xFF1B2330;

    // ---- marks
    public int ticks = 1;            // Names.TICKS
    public int tickColor = 0xFFE8ECF2;
    public float tickSize = 1f;
    public int marker12 = 0;         // Names.MARKER12
    public boolean followShape = true;
    public boolean skipUnderNumerals = false;

    // ---- numerals
    public int numerals = 0;         // Names.NUMERALS
    public int numeralColor = 0xFFE8ECF2;
    public String numeralFont = "Sans";
    public int numeralWeight = 0;    // 0 regular, 1 bold, 2 italic, 3 bold italic
    public float numeralSize = 1f;
    public float numeralInset = 0f;  // -1..1: move numerals in or out
    public int numeralOrient = 0;    // Names.ORIENT
    public int numeralEffect = 0;    // Names.EFFECTS
    public int numeralAccent = 0xFF000000;

    // ---- lettering
    public String brand = "AION";
    public String subtitle = "";
    public String brandFont = "Sans";
    public String subFont = "Sans";
    public int textColor = 0xFFE8ECF2;
    public float brandSize = 1f;
    public float subSize = 1f;
    public int brandLayout = 0;      // 0 straight, 1 curved
    public int subLayout = 0;
    public int brandWeight = 1;
    public int subWeight = 0;
    public float brandPos = 0.5f;    // 0 near centre .. 1 near edge
    public float subPos = 0.5f;
    public int logo = 0;             // Names.LOGOS
    public float letterSpacing = 0.12f;

    // ---- complications (12, 3, 6, 9 o'clock)
    public int comp12 = 0, comp3 = 0, comp6 = 0, comp9 = 0;   // Names.COMPS
    public int compColor = 0xFFE8ECF2;
    public int compBg = 0xFF0B0E12;

    // ---- hands
    public int hourHand = 0;         // Names.HANDS
    public int minuteHand = 0;
    public int secondHand = 0;       // Names.SECONDS
    public int handFinish = 0;       // Names.FINISHES
    public int handColor = 0xFFF2F4F7;
    public int handAccent = 0xFFB8FFD8;
    public int secondColor = 0xFFFF4A3D;
    public float handLength = 1f;
    public float handWidth = 1f;
    public boolean handShadow = true;
    public boolean seconds = true;
    public int cap = 1;              // Names.CAPS

    public int accent = 0xFFFF4A3D;

    // ------------------------------------------------------------------

    public JSONObject toJson() {
        JSONObject o = new JSONObject();
        try {
            for (Field f : ClockStyle.class.getFields()) {
                if (Modifier.isStatic(f.getModifiers())) continue;
                Class<?> t = f.getType();
                if (t == int.class) o.put(f.getName(), f.getInt(this));
                else if (t == float.class) o.put(f.getName(), (double) f.getFloat(this));
                else if (t == boolean.class) o.put(f.getName(), f.getBoolean(this));
                else if (t == String.class) o.put(f.getName(), f.get(this));
            }
        } catch (Exception ignored) { }
        return o;
    }

    public static ClockStyle fromJson(String s) {
        ClockStyle c = new ClockStyle();
        if (s == null || s.isEmpty()) return c;
        try {
            JSONObject o = new JSONObject(s);
            for (Field f : ClockStyle.class.getFields()) {
                if (Modifier.isStatic(f.getModifiers()) || !o.has(f.getName())) continue;
                Class<?> t = f.getType();
                if (t == int.class) f.setInt(c, o.getInt(f.getName()));
                else if (t == float.class) f.setFloat(c, (float) o.getDouble(f.getName()));
                else if (t == boolean.class) f.setBoolean(c, o.getBoolean(f.getName()));
                else if (t == String.class) f.set(c, o.getString(f.getName()));
            }
        } catch (Exception ignored) { }
        return c;
    }

    public ClockStyle copy() { return fromJson(toJson().toString()); }

    @Override public String toString() { return toJson().toString(); }
}
