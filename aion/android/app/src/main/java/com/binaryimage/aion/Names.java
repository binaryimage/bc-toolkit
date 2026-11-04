package com.binaryimage.aion;

/** Display names for every option (the index is what gets saved). */
public final class Names {
    private Names() { }

    public static final String[] SHAPES = {
        "Round", "Rounded square", "Squircle", "Cushion", "Square", "Octagon", "Hexagon",
        "Dodecagon", "Scalloped", "Diamond", "Flower", "Tonneau"
    };

    public static final String[] PATTERNS = {
        "Solid", "Linear fade", "Radial glow", "Sunburst", "Conic", "Guilloché rosette",
        "Clous de Paris", "Côtes de Genève", "Carbon fibre", "Galaxy", "Vinyl grooves",
        "Brushed metal", "Honeycomb", "Tapisserie", "Waves", "Aurora", "Pinstripe",
        "Smoked glass", "None (see-through)", "Hypno spiral", "Pop rays", "Halftone",
        "Three-tone fade", "Topographic", "Plasma", "Checkerboard", "Crackle", "Sector"
    };

    public static final String[] BEZELS = {
        "None", "Thin line", "Thick ring", "Double line", "Polished", "Coin edge", "Diver 60",
        "Tachymeter", "GMT 24h two-tone", "Fluted", "Gem set", "Neon glow", "Compass", "Rope", "Hour ring"
    };

    public static final String[] TICKS = {
        "None", "Classic", "Hour lines", "Dots", "Railroad", "Triangles", "Applied bars", "Diamonds",
        "Hollow circles", "Stars", "Sector", "Wedges", "Long spokes", "Dots + bars", "Double bars",
        "Chevrons", "Stick + dot", "Quarters only", "Ruler", "Moons"
    };

    public static final String[] MARKER12 = {
        "Same as others", "Triangle", "Double bar", "Big dot", "Star", "Pilot triangle", "Diamond", "Crown"
    };

    public static final String[] NUMERALS = {
        "None", "Arabic", "Arabic quarters", "Roman", "Roman quarters", "Roman IIII", "Big 12",
        "Minutes 05-60", "Eastern Arabic", "Kanji", "Devanagari", "Thai", "Words", "Binary",
        "Hex", "Even hours", "Zodiac", "Greek", "Runes", "Dots count", "24 hour", "Military 00-22"
    };

    public static final String[] ORIENT = { "Upright", "Follow the dial", "Follow, flip bottom" };

    public static final String[] EFFECTS = {
        "Plain", "Hollow outline", "Outlined", "Drop shadow", "Glow", "Embossed", "Double"
    };

    public static final String[] WEIGHTS = { "Regular", "Bold", "Italic", "Bold italic" };

    public static final String[] LOGOS = {
        "None", "✦", "♛", "✧", "◆", "⚜", "☾", "⚡", "✪", "♠", "☀", "❖", "✠", "∞", "Ω", "▲", "❄", "☯", "♾", "✺"
    };

    public static final String[] COMPS = {
        "None", "Date window", "Weekday window", "Day + date", "Month window", "Week number",
        "Big date", "Weekday dial", "Date dial", "Month dial", "Moon phase", "Emblem", "Year", "Day/night disc"
    };

    public static final String[] HANDS = {
        "Baton", "Dauphine", "Leaf", "Sword", "Alpha", "Breguet", "Spade", "Cathedral", "Arrow",
        "Needle", "Lollipop", "Mercedes", "Snowflake", "Plongeur", "Pencil", "Syringe", "Broad arrow",
        "Teardrop", "Paddle", "Wire", "Feuille", "Obelisk", "Lancet", "Skeleton", "Flame", "Key", "Trident", "Bubble"
    };

    public static final String[] SECONDS = {
        "Needle", "Lollipop", "Arrow tip", "Lightning", "Paddle", "Counterweight", "Railway disc",
        "Diver triangle", "Hairline", "Star tip", "Ring tip", "Heart"
    };

    public static final String[] FINISHES = {
        "Solid", "Polished metal", "Faceted", "Lume filled", "Outline", "Neon", "Tip fade", "Gold", "Silver"
    };

    public static final String[] CAPS = { "None", "Dot", "Ring", "Jewel", "Screw", "Big disc", "Flower" };

    // ------------------------------------------------------------ numeral sets

    /** Labels for hours 1..12 (index 0 = 1 o'clock); null = leave that hour blank. */
    public static String[] numeralSet(int set) {
        switch (set) {
            case 1: return seq("1 2 3 4 5 6 7 8 9 10 11 12");
            case 2: return seq("_ _ 3 _ _ 6 _ _ 9 _ _ 12");
            case 3: return seq("I II III IV V VI VII VIII IX X XI XII");
            case 4: return seq("_ _ III _ _ VI _ _ IX _ _ XII");
            case 5: return seq("I II III IIII V VI VII VIII IX X XI XII");
            case 6: return seq("_ _ _ _ _ _ _ _ _ _ _ 12");
            case 8: return seq("١ ٢ ٣ ٤ ٥ ٦ ٧ ٨ ٩ ١٠ ١١ ١٢");
            case 9: return seq("一 二 三 四 五 六 七 八 九 十 十一 十二");
            case 10: return seq("१ २ ३ ४ ५ ६ ७ ८ ९ १० ११ १२");
            case 11: return seq("๑ ๒ ๓ ๔ ๕ ๖ ๗ ๘ ๙ ๑๐ ๑๑ ๑๒");
            case 12: return seq("ONE TWO THREE FOUR FIVE SIX SEVEN EIGHT NINE TEN ELEVEN TWELVE");
            case 13: return seq("1 10 11 100 101 110 111 1000 1001 1010 1011 1100");
            case 14: return seq("1 2 3 4 5 6 7 8 9 A B C");
            case 15: return seq("_ 2 _ 4 _ 6 _ 8 _ 10 _ 12");
            case 16: return seq("♉ ♊ ♋ ♌ ♍ ♎ ♏ ♐ ♑ ♒ ♓ ♈");
            case 17: return seq("Α Β Γ Δ Ε ΣΤ Ζ Η Θ Ι ΙΑ ΙΒ");
            case 18: return seq("ᚠ ᚢ ᚦ ᚨ ᚱ ᚲ ᚷ ᚹ ᚺ ᚾ ᛁ ᛃ");
            case 19: return seq("• •• ••• :: ⁙ ⠿ ⁘ ⁂ ⁜ ⁞ ⁝ ⁑");
            case 20: return seq("13 14 15 16 17 18 19 20 21 22 23 24");
            case 21: return seq("02 04 06 08 10 12 14 16 18 20 22 00");
            default: return null;
        }
    }

    private static String[] seq(String s) {
        String[] a = s.split(" ");
        for (int i = 0; i < a.length; i++) if (a[i].equals("_")) a[i] = null;
        return a;
    }
}
