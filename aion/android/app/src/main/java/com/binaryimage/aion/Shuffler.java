package com.binaryimage.aion;

import java.util.List;
import java.util.Random;

/** Random but readable clocks: picks from colour palettes so the result always hangs together. */
public final class Shuffler {
    private Shuffler() { }

    // face A, face B, ink, accent, metal
    static final int[][] PALETTES = {
        {0xFF0D1B2A, 0xFF1B263B, 0xFFE0E1DD, 0xFFFFB703, 0xFFC0C7D1},
        {0xFFF7F3E8, 0xFFE4DCC8, 0xFF1A1A1A, 0xFF1F4E9C, 0xFFD4AF37},
        {0xFF023E8A, 0xFF03045E, 0xFFFFFFFF, 0xFFFF6B35, 0xFFC9CED6},
        {0xFF0B3D2E, 0xFF051F17, 0xFFF1E9D2, 0xFFD4AF37, 0xFFD4AF37},
        {0xFF5A0F1E, 0xFF2B060D, 0xFFF4E3C1, 0xFFE0B354, 0xFFD4AF37},
        {0xFFE8F4FA, 0xFFA9D6EA, 0xFF0D3B66, 0xFF00B4D8, 0xFFDDE6EE},
        {0xFF0A0014, 0xFF1F0036, 0xFF00F5D4, 0xFFFF00A8, 0xFF7B2CBF},
        {0xFFD8C3A5, 0xFFB79B73, 0xFF2E2416, 0xFFB5442E, 0xFF5A4630},
        {0xFFF4A896, 0xFFE07A5F, 0xFF2B1B17, 0xFF1D3557, 0xFFC9CED6},
        {0xFF3B4A2F, 0xFF242E1C, 0xFFEDE6C8, 0xFFE76F51, 0xFF8A8F7A},
        {0xFFFF7E5F, 0xFF6A0572, 0xFFFFFFFF, 0xFFFFD166, 0xFFD4AF37},
        {0xFFCDEAC0, 0xFF8FC0A9, 0xFF1F3A2D, 0xFFFF6F59, 0xFFC9CED6},
        {0xFFD7C9F0, 0xFF8E7DBE, 0xFF221A38, 0xFFFFC857, 0xFFC9CED6},
        {0xFF3D4451, 0xFF22262E, 0xFFF2F2F2, 0xFF4CC9F0, 0xFFAAB2BD},
        {0xFF7A3E1D, 0xFF3E1C0B, 0xFFF6D7B0, 0xFF5BC0BE, 0xFFC87533},
        {0xFFF6E1E1, 0xFFE8B4B8, 0xFF5B2333, 0xFFC9A227, 0xFFE0A899},
        {0xFF0F1A0A, 0xFF1E3311, 0xFFB6FF3B, 0xFFFFE600, 0xFF4D6B2E},
        {0xFF1B1464, 0xFF0B0833, 0xFFF5D76E, 0xFFE84855, 0xFFD4AF37},
        {0xFFFFFFFF, 0xFFEDEDED, 0xFF000000, 0xFFE63946, 0xFF222222},
        {0xFF000000, 0xFF141414, 0xFFFFFFFF, 0xFFFFD60A, 0xFF8D8D8D},
        {0xFFFF71CE, 0xFF01CDFE, 0xFF1A1A2E, 0xFFFFFB96, 0xFFB967FF},
        {0xFF00A6A6, 0xFF005F73, 0xFFFFFFFF, 0xFFEE9B00, 0xFFC9CED6},
        {0xFFB80C09, 0xFF6B0504, 0xFFFFFFFF, 0xFF0B4F6C, 0xFFC9CED6},
        {0xFF132A13, 0xFF31572C, 0xFFECF39E, 0xFFFFB4A2, 0xFF90A955},
        {0xFF5C7A99, 0xFF2F4858, 0xFFFFFFFF, 0xFFFFC300, 0xFFC9CED6},
        {0xFF4A2C2A, 0xFF2A1715, 0xFFF3E9DC, 0xFFC08552, 0xFFD4AF37},
        {0xFFF2B134, 0xFFED553B, 0xFF2D1E0F, 0xFF068587, 0xFF4F3B14},
        {0xFFBEE9E8, 0xFF62B6CB, 0xFF1B4965, 0xFFFF5D8F, 0xFFDDE6EE},
        {0xFF240046, 0xFF10002B, 0xFFFFFFFF, 0xFFFF9E00, 0xFFC77DFF},
        {0xFF111111, 0xFF2B2B2B, 0xFFFF2E63, 0xFF08D9D6, 0xFFEAEAEA},
        {0xFFFEF9EF, 0xFFFFE8D6, 0xFF3D405B, 0xFFE07A5F, 0xFF81B29A},
        {0xFF14213D, 0xFF000000, 0xFFFCA311, 0xFFE5E5E5, 0xFFFCA311},
    };

    static int pick(Random r, int n) { return r.nextInt(n); }

    static String font(Random r) {
        List<String> f = Fonts.names();
        return f.isEmpty() ? "Sans" : f.get(r.nextInt(f.size()));
    }

    public static void colours(ClockStyle s, Random r) {
        int[] p = PALETTES[r.nextInt(PALETTES.length)];
        s.faceA = p[0]; s.faceB = p[1];
        s.tickColor = p[2]; s.numeralColor = p[2]; s.textColor = p[2];
        s.accent = p[3];
        s.bezelColor = p[4];
        s.bezelAccent = r.nextBoolean() ? Col.darken(p[1], 0.3f) : p[0];
        s.handColor = r.nextInt(3) == 0 ? p[4] : p[2];
        s.handAccent = r.nextBoolean() ? 0xFFEFFFEF : Col.lighten(p[3], 0.5f);
        s.secondColor = r.nextInt(4) == 0 ? p[2] : p[3];
        s.compColor = p[2];
        s.compBg = r.nextBoolean() ? Col.darken(p[0], 0.25f) : Col.lighten(p[0], 0.2f);
        s.numeralAccent = p[3];
        if (r.nextInt(6) == 0) s.handFinish = 7 + r.nextInt(2);
    }

    public static void face(ClockStyle s, Random r) {
        int[] shapes = {0, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
        s.shape = shapes[r.nextInt(shapes.length)];
        s.pattern = r.nextInt(Names.PATTERNS.length);
        if (s.pattern == 18 && r.nextBoolean()) s.pattern = 0;
        s.detail = 0.2f + r.nextFloat() * 0.7f;
        s.seed = r.nextInt(1000);
        s.vignette = r.nextInt(3) != 0;
        s.depth = r.nextInt(4) != 0;
        s.gloss = r.nextInt(4) == 0;
        s.bezel = r.nextInt(3) == 0 ? 0 : r.nextInt(Names.BEZELS.length);
    }

    public static void marks(ClockStyle s, Random r) {
        s.ticks = r.nextInt(Names.TICKS.length);
        s.tickSize = 0.75f + r.nextFloat() * 0.5f;
        s.marker12 = r.nextInt(3) == 0 ? r.nextInt(Names.MARKER12.length) : 0;
        s.followShape = r.nextInt(4) != 0;
    }

    public static void numerals(ClockStyle s, Random r) {
        s.numerals = r.nextInt(5) < 2 ? 0 : r.nextInt(Names.NUMERALS.length);
        s.numeralFont = font(r);
        s.numeralWeight = r.nextInt(3) == 0 ? r.nextInt(4) : 0;
        s.numeralSize = 0.75f + r.nextFloat() * 0.45f;
        s.numeralInset = 0;
        s.numeralOrient = r.nextInt(4) == 0 ? 1 + r.nextInt(2) : 0;
        s.numeralEffect = r.nextInt(3) == 0 ? r.nextInt(Names.EFFECTS.length) : 0;
        s.skipUnderNumerals = r.nextBoolean();
    }

    public static void hands(ClockStyle s, Random r) {
        s.hourHand = r.nextInt(Names.HANDS.length);
        s.minuteHand = r.nextInt(10) < 7 ? s.hourHand : r.nextInt(Names.HANDS.length);
        s.secondHand = r.nextInt(Names.SECONDS.length);
        s.handFinish = r.nextInt(Names.FINISHES.length);
        s.handLength = 0.9f + r.nextFloat() * 0.15f;
        s.handWidth = 0.75f + r.nextFloat() * 0.5f;
        s.handShadow = r.nextInt(4) != 0;
        s.cap = r.nextInt(Names.CAPS.length);
    }

    static final String[] BRANDS = {"AION", "Aion", "CHRONOS", "KAIROS", "HORA", "ÆON", "NOX", "LUMEN", "VOID", "ORBIT", "TEMPUS", "HALO"};
    static final String[] SUBS = {"", "", "AUTOMATIC", "CHRONOMETER", "SWISS MADE", "200M", "QUARTZ", "Since 2026", "MOONPHASE", "GMT", "Limited"};

    public static void text(ClockStyle s, Random r) {
        s.brand = r.nextInt(3) == 0 ? "" : BRANDS[r.nextInt(BRANDS.length)];
        s.subtitle = SUBS[r.nextInt(SUBS.length)];
        s.brandFont = font(r);
        s.subFont = r.nextBoolean() ? s.brandFont : font(r);
        s.brandLayout = r.nextInt(5) == 0 ? 1 : 0;
        s.subLayout = r.nextInt(5) == 0 ? 1 : 0;
        s.brandSize = 0.7f + r.nextFloat() * 0.5f;
        s.letterSpacing = r.nextFloat() * 0.35f;
        s.logo = r.nextInt(3) == 0 ? r.nextInt(Names.LOGOS.length) : 0;
        s.brandPos = 0.3f + r.nextFloat() * 0.3f;
        s.subPos = 0.3f + r.nextFloat() * 0.3f;
    }

    public static void extras(ClockStyle s, Random r) {
        s.comp12 = 0; s.comp3 = 0; s.comp6 = 0; s.comp9 = 0;
        int n = r.nextInt(5) < 2 ? 0 : 1 + r.nextInt(2);
        for (int i = 0; i < n; i++) {
            int kind = 1 + r.nextInt(Names.COMPS.length - 1);
            switch (r.nextInt(3)) {
                case 0: s.comp3 = kind; break;
                case 1: s.comp6 = kind; break;
                default: s.comp9 = kind; break;
            }
        }
        if (s.comp6 != 0 && !s.subtitle.isEmpty()) s.subtitle = "";
    }

    public static ClockStyle all(Random r) {
        ClockStyle s = new ClockStyle();
        s.name = "Shuffled";
        colours(s, r);
        face(s, r);
        marks(s, r);
        numerals(s, r);
        hands(s, r);
        text(s, r);
        extras(s, r);
        s.seconds = true;
        return s;
    }
}
