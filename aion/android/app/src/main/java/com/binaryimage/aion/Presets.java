package com.binaryimage.aion;

import java.util.ArrayList;
import java.util.List;
import java.util.function.Consumer;

/** Hand-made starting points. */
public final class Presets {
    private Presets() { }

    static ClockStyle make(String name, Consumer<ClockStyle> f) {
        ClockStyle s = new ClockStyle();
        s.name = name;
        s.brand = "";
        f.accept(s);
        return s;
    }

    static final int GOLD = 0xFFD4AF37, SILVER = 0xFFC9CED6, ROSE = 0xFFE0A899, WHITE = 0xFFF5F5F2, BLACK = 0xFF0D0D0D;

    public static List<ClockStyle> all() {
        List<ClockStyle> l = new ArrayList<>();

        l.add(make("Heritage", s -> {
            s.pattern = 2; s.faceA = 0xFFFBF8F0; s.faceB = 0xFFE6DFCD; s.vignette = false;
            s.bezel = 4; s.bezelColor = SILVER;
            s.ticks = 4; s.tickColor = 0xFF1A1A1A;
            s.numerals = 3; s.numeralColor = 0xFF1A1A1A; s.numeralFont = "Cinzel"; s.numeralOrient = 1; s.numeralSize = 0.9f;
            s.brand = "AION"; s.brandFont = "Cinzel"; s.textColor = 0xFF1A1A1A; s.subtitle = "AUTOMATIC"; s.subFont = "Cormorant Garamond";
            s.hourHand = 5; s.minuteHand = 5; s.handFinish = 0; s.handColor = 0xFF1E3A8A; s.secondHand = 0; s.secondColor = 0xFF1E3A8A;
            s.cap = 1; s.comp6 = 10; s.compColor = 0xFF1A1A1A; s.compBg = 0xFFEDE6D3; s.subPos = 0.25f;
        }));
        l.add(make("Deep Diver", s -> {
            s.pattern = 2; s.faceA = 0xFF0A1F3D; s.faceB = 0xFF020812;
            s.bezel = 6; s.bezelColor = SILVER; s.bezelAccent = 0xFF0A1A33;
            s.ticks = 6; s.tickColor = WHITE; s.marker12 = 5; s.tickSize = 1.05f;
            s.hourHand = 12; s.minuteHand = 13; s.handFinish = 3; s.handColor = SILVER; s.handAccent = 0xFFE9FFE9;
            s.secondHand = 1; s.secondColor = 0xFFFF5A36; s.cap = 2;
            s.brand = "AION"; s.subtitle = "300M · 1000FT"; s.textColor = WHITE; s.brandFont = "Michroma"; s.subFont = "Rajdhani"; s.brandSize = 0.8f;
            s.comp3 = 1; s.compBg = WHITE; s.compColor = BLACK;
        }));
        l.add(make("Bauhaus", s -> {
            s.pattern = 0; s.faceA = 0xFFF2EFE6; s.vignette = false; s.depth = true;
            s.bezel = 1; s.bezelColor = 0xFF2B2B2B;
            s.ticks = 1; s.tickColor = 0xFF222222; s.tickSize = 0.8f;
            s.numerals = 1; s.numeralFont = "Josefin Sans"; s.numeralColor = 0xFF222222; s.numeralSize = 0.75f;
            s.hourHand = 0; s.minuteHand = 0; s.handColor = 0xFF222222; s.handWidth = 0.7f; s.handShadow = false;
            s.secondHand = 8; s.secondColor = 0xFFE63946; s.cap = 0;
            s.brand = "aion"; s.brandFont = "Josefin Sans"; s.brandWeight = 0; s.textColor = 0xFF222222; s.brandSize = 0.8f;
        }));
        l.add(make("Pilot", s -> {
            s.pattern = 0; s.faceA = 0xFF161616; s.vignette = true;
            s.bezel = 5; s.bezelColor = 0xFF8C8F93;
            s.ticks = 1; s.tickColor = WHITE; s.marker12 = 5;
            s.numerals = 2; s.numeralFont = "Oswald"; s.numeralColor = WHITE; s.numeralSize = 1.2f; s.skipUnderNumerals = true;
            s.hourHand = 9; s.minuteHand = 3; s.handFinish = 3; s.handColor = WHITE; s.handAccent = 0xFFF3E9C6;
            s.secondHand = 0; s.secondColor = WHITE; s.cap = 4;
            s.brand = "FLIEGER"; s.brandFont = "Oswald"; s.textColor = WHITE; s.brandSize = 0.7f; s.brandPos = 0.35f;
        }));
        l.add(make("Art Deco", s -> {
            s.shape = 1; s.pattern = 3; s.faceA = 0xFF1E1B16; s.faceB = 0xFF0B0A08; s.detail = 0.6f;
            s.bezel = 4; s.bezelColor = GOLD;
            s.ticks = 11; s.tickColor = GOLD;
            s.numerals = 1; s.numeralFont = "Poiret One"; s.numeralColor = GOLD; s.numeralSize = 0.9f;
            s.hourHand = 21; s.minuteHand = 21; s.handFinish = 7; s.secondHand = 8; s.secondColor = GOLD; s.cap = 3; s.accent = 0xFF1B998B;
            s.brand = "AION"; s.brandFont = "Poiret One"; s.textColor = GOLD; s.letterSpacing = 0.4f;
        }));
        l.add(make("Neon Night", s -> {
            s.pattern = 19; s.faceA = 0xFF07000F; s.faceB = 0xFF1A0033; s.detail = 0.4f; s.vignette = true;
            s.bezel = 11; s.accent = 0xFFFF2BD6;
            s.ticks = 3; s.tickColor = 0xFF00F5D4;
            s.numerals = 2; s.numeralFont = "Monoton"; s.numeralColor = 0xFF00F5D4; s.numeralEffect = 4;
            s.hourHand = 19; s.minuteHand = 19; s.handFinish = 5; s.handColor = 0xFF00F5D4; s.handWidth = 1.3f;
            s.secondHand = 8; s.secondColor = 0xFFFF2BD6; s.handFinish = 5; s.cap = 1;
        }));
        l.add(make("Galaxy", s -> {
            s.pattern = 9; s.faceA = 0xFF6A3CBC; s.faceB = 0xFF0B0620; s.accent = 0xFF2E86FF; s.detail = 0.7f;
            s.bezel = 4; s.bezelColor = 0xFFB9C0CC;
            s.ticks = 9; s.tickColor = 0xFFF7F0D6; s.tickSize = 0.9f;
            s.hourHand = 17; s.minuteHand = 17; s.handFinish = 8; s.secondHand = 9; s.secondColor = 0xFFFFD166; s.cap = 3;
            s.brand = "AION"; s.brandFont = "Cinzel Decorative"; s.textColor = 0xFFF7F0D6; s.logo = 1;
        }));
        l.add(make("Ice Glacier", s -> {
            s.pattern = 26; s.faceA = 0xFFDDF3FB; s.faceB = 0xFF6FB7D6; s.detail = 0.5f;
            s.bezel = 9; s.bezelColor = 0xFFE6F4FA;
            s.ticks = 7; s.tickColor = 0xFF0D3B66;
            s.numerals = 4; s.numeralFont = "Cinzel"; s.numeralColor = 0xFF0D3B66;
            s.hourHand = 1; s.minuteHand = 1; s.handFinish = 2; s.handColor = 0xFF5FA8D3; s.secondHand = 2; s.secondColor = 0xFF0D3B66;
            s.brand = "GLACIER"; s.brandFont = "Cinzel"; s.textColor = 0xFF0D3B66; s.brandSize = 0.7f; s.cap = 3; s.accent = 0xFF00B4D8;
        }));
        l.add(make("Racing", s -> {
            s.pattern = 8; s.faceA = 0xFF2A2A2A; s.detail = 0.6f;
            s.bezel = 7; s.bezelColor = WHITE; s.bezelAccent = 0xFF111111;
            s.ticks = 18; s.tickColor = WHITE; s.tickSize = 0.8f;
            s.numerals = 2; s.numeralFont = "Bebas Neue"; s.numeralColor = WHITE; s.numeralSize = 1.1f;
            s.hourHand = 0; s.minuteHand = 0; s.handFinish = 3; s.handColor = WHITE; s.handAccent = 0xFFFFD60A;
            s.secondHand = 3; s.secondColor = 0xFFFFD60A; s.cap = 2;
            s.comp9 = 7; s.comp3 = 8; s.compColor = WHITE; s.compBg = 0xFF111111; s.accent = 0xFFFFD60A;
        }));
        l.add(make("Railway", s -> {
            s.pattern = 0; s.faceA = WHITE; s.vignette = false;
            s.bezel = 2; s.bezelColor = 0xFF2B2B2B;
            s.ticks = 1; s.tickColor = BLACK; s.tickSize = 1.15f;
            s.hourHand = 0; s.minuteHand = 0; s.handColor = BLACK; s.handWidth = 1.2f; s.handShadow = true;
            s.secondHand = 6; s.secondColor = 0xFFD62828; s.cap = 0;
        }));
        l.add(make("Kanji Ink", s -> {
            s.pattern = 2; s.faceA = 0xFFF4EBDD; s.faceB = 0xFFD8C7A8;
            s.bezel = 1; s.bezelColor = 0xFF8B1E1E;
            s.ticks = 3; s.tickColor = 0xFF3B2A1A; s.tickSize = 0.8f;
            s.numerals = 9; s.numeralFont = "Serif"; s.numeralColor = 0xFF1A1208; s.numeralSize = 0.8f;
            s.hourHand = 2; s.minuteHand = 2; s.handColor = 0xFF1A1208; s.secondHand = 1; s.secondColor = 0xFFB3261E; s.cap = 1;
            s.logo = 0; s.comp6 = 2; s.compBg = 0xFF8B1E1E; s.compColor = 0xFFF4EBDD;
        }));
        l.add(make("Vaporwave", s -> {
            s.shape = 2; s.pattern = 22; s.faceA = 0xFFFF71CE; s.faceB = 0xFF01CDFE; s.accent = 0xFFB967FF;
            s.bezel = 3; s.bezelColor = 0xFFFFFB96; s.bezelAccent = 0xFF1A1A2E;
            s.ticks = 13; s.tickColor = 0xFF1A1A2E;
            s.numerals = 1; s.numeralFont = "Press Start 2P"; s.numeralColor = 0xFF1A1A2E; s.numeralSize = 0.55f;
            s.hourHand = 14; s.minuteHand = 14; s.handColor = 0xFFFFFB96; s.handFinish = 3; s.handAccent = 0xFFFFFFFF;
            s.secondHand = 11; s.secondColor = 0xFF1A1A2E; s.cap = 6;
            s.brand = "A E S T H E T I C"; s.brandFont = "VT323"; s.textColor = 0xFF1A1A2E; s.brandSize = 0.7f;
        }));
        l.add(make("Field", s -> {
            s.pattern = 0; s.faceA = 0xFF2F3A28; s.vignette = true;
            s.bezel = 1; s.bezelColor = 0xFF9DA38A;
            s.ticks = 4; s.tickColor = 0xFFEDE6C8;
            s.numerals = 20; s.numeralFont = "Share Tech Mono"; s.numeralColor = 0xFFEDE6C8; s.numeralSize = 0.85f;
            s.hourHand = 16; s.minuteHand = 16; s.handFinish = 3; s.handColor = 0xFFEDE6C8; s.handAccent = 0xFFFFF4D6;
            s.secondHand = 0; s.secondColor = 0xFFE76F51; s.cap = 4;
            s.brand = "FIELD"; s.brandFont = "Share Tech Mono"; s.textColor = 0xFFEDE6C8; s.brandSize = 0.6f; s.brandPos = 0.2f;
        }));
        l.add(make("Skeleton Gold", s -> {
            s.pattern = 18; s.bezel = 4; s.bezelColor = GOLD; s.depth = true;
            s.ticks = 6; s.tickColor = GOLD; s.handAccent = 0xFF111111;
            s.numerals = 3; s.numeralFont = "Cinzel"; s.numeralColor = GOLD; s.numeralEffect = 3;
            s.hourHand = 23; s.minuteHand = 23; s.handFinish = 7; s.secondHand = 8; s.secondColor = GOLD; s.cap = 3; s.accent = 0xFFB3261E;
        }));
        l.add(make("Moonphase", s -> {
            s.pattern = 5; s.faceA = 0xFF13254A; s.faceB = 0xFF7D93C8; s.detail = 0.6f;
            s.bezel = 4; s.bezelColor = ROSE;
            s.ticks = 9; s.tickColor = ROSE; s.tickSize = 0.8f;
            s.numerals = 3; s.numeralFont = "Cormorant Garamond"; s.numeralColor = ROSE; s.numeralOrient = 0;
            s.hourHand = 20; s.minuteHand = 20; s.handFinish = 1; s.handColor = ROSE; s.secondHand = 8; s.secondColor = ROSE;
            s.comp6 = 10; s.comp3 = 1; s.compColor = ROSE; s.compBg = 0xFF0B1630; s.cap = 1;
            s.brand = "AION"; s.brandFont = "Cormorant Garamond"; s.textColor = ROSE; s.subtitle = "Phase de lune"; s.subFont = "Great Vibes"; s.subSize = 1.4f;
            s.subPos = 0.05f;
        }));
        l.add(make("Breguet Blue", s -> {
            s.pattern = 5; s.faceA = 0xFFE9EDF5; s.faceB = 0xFF9AA9C7; s.detail = 0.8f; s.vignette = false;
            s.bezel = 13; s.bezelColor = SILVER;
            s.ticks = 4; s.tickColor = 0xFF1C2C5B;
            s.numerals = 1; s.numeralFont = "Cormorant Garamond"; s.numeralColor = 0xFF1C2C5B; s.numeralWeight = 2;
            s.hourHand = 5; s.minuteHand = 5; s.handColor = 0xFF1A3FA0; s.handFinish = 1; s.secondHand = 0; s.secondColor = 0xFF1A3FA0; s.cap = 1;
            s.brand = "Aion"; s.brandFont = "Great Vibes"; s.brandSize = 1.5f; s.textColor = 0xFF1C2C5B; s.brandWeight = 0;
        }));
        l.add(make("Sunburst Blue", s -> {
            s.pattern = 3; s.faceA = 0xFF2F5FB3; s.faceB = 0xFF0C1E44; s.detail = 0.5f;
            s.bezel = 4; s.bezelColor = SILVER;
            s.ticks = 6; s.tickColor = SILVER; s.marker12 = 2;
            s.hourHand = 0; s.minuteHand = 0; s.handFinish = 3; s.handColor = SILVER; s.handAccent = 0xFFF4FFF4;
            s.secondHand = 5; s.secondColor = SILVER; s.cap = 1;
            s.brand = "AION"; s.textColor = WHITE; s.subtitle = "CHRONOMETER"; s.comp3 = 3; s.compBg = WHITE; s.compColor = BLACK;
        }));
        l.add(make("Carbon Sport", s -> {
            s.shape = 5; s.pattern = 8; s.faceA = 0xFF333333; s.detail = 0.7f;
            s.bezel = 12; s.bezelColor = 0xFFFF6B00; s.bezelAccent = 0xFF151515;
            s.ticks = 13; s.tickColor = 0xFFFF6B00;
            s.numerals = 2; s.numeralFont = "Orbitron"; s.numeralColor = WHITE; s.numeralWeight = 1;
            s.hourHand = 13; s.minuteHand = 13; s.handFinish = 6; s.handColor = WHITE; s.handAccent = 0xFFFF6B00;
            s.secondHand = 2; s.secondColor = 0xFFFF6B00; s.cap = 2; s.accent = 0xFFFF6B00;
        }));
        l.add(make("Opera", s -> {
            s.shape = 8; s.pattern = 4; s.faceA = 0xFF5A0F1E; s.faceB = 0xFF1C0308;
            s.bezel = 10; s.bezelColor = GOLD; s.bezelAccent = 0xFFE8F0FF;
            s.ticks = 7; s.tickColor = GOLD;
            s.numerals = 5; s.numeralFont = "Playfair Display"; s.numeralColor = 0xFFF4E3C1; s.numeralOrient = 1;
            s.hourHand = 6; s.minuteHand = 6; s.handFinish = 7; s.secondHand = 8; s.secondColor = GOLD; s.cap = 6;
            s.brand = "AION"; s.brandFont = "Playfair Display"; s.textColor = 0xFFF4E3C1; s.logo = 2;
        }));
        l.add(make("Minimal White", s -> {
            s.pattern = 0; s.faceA = 0xFFFFFFFF; s.vignette = false; s.depth = true;
            s.bezel = 0; s.ticks = 17; s.tickColor = 0xFF111111;
            s.hourHand = 19; s.minuteHand = 19; s.handColor = 0xFF111111; s.handWidth = 1.4f;
            s.secondHand = 8; s.secondColor = 0xFFFF3B30; s.cap = 1;
        }));
        l.add(make("Minimal Black", s -> {
            s.pattern = 0; s.faceA = 0xFF000000; s.vignette = false;
            s.bezel = 0; s.ticks = 3; s.tickColor = 0xFF8D8D8D; s.tickSize = 0.7f;
            s.hourHand = 0; s.minuteHand = 0; s.handColor = 0xFFFFFFFF; s.handWidth = 0.8f;
            s.secondHand = 8; s.secondColor = 0xFFFFD60A; s.cap = 1;
        }));
        l.add(make("Floating", s -> {
            s.pattern = 18; s.bezel = 0; s.depth = false; s.vignette = false;
            s.ticks = 3; s.tickColor = 0xFFFFFFFF; s.tickSize = 1.3f;
            s.hourHand = 0; s.minuteHand = 0; s.handColor = 0xFFFFFFFF; s.handShadow = true;
            s.secondHand = 1; s.secondColor = 0xFFFF4A3D; s.cap = 2;
        }));
        l.add(make("Topo Explorer", s -> {
            s.shape = 3; s.pattern = 23; s.faceA = 0xFF2E3B2C; s.faceB = 0xFFB9C49A; s.seed = 3;
            s.bezel = 12; s.bezelColor = 0xFFEDE6C8; s.bezelAccent = 0xFF1E261C;
            s.ticks = 13; s.tickColor = 0xFFFFE8A3;
            s.numerals = 2; s.numeralFont = "Teko"; s.numeralColor = 0xFFFFE8A3; s.numeralSize = 1.2f;
            s.hourHand = 16; s.minuteHand = 16; s.handFinish = 3; s.handColor = 0xFFEDE6C8; s.handAccent = 0xFFFFE8A3;
            s.secondHand = 2; s.secondColor = 0xFFE76F51; s.cap = 4;
        }));
        l.add(make("Plasma", s -> {
            s.shape = 10; s.pattern = 24; s.faceA = 0xFF240046; s.faceB = 0xFFFF9E00; s.accent = 0xFFFF006E; s.detail = 0.4f; s.seed = 9;
            s.bezel = 0; s.ticks = 15; s.tickColor = WHITE;
            s.hourHand = 24; s.minuteHand = 24; s.handFinish = 4; s.handColor = WHITE; s.handWidth = 1.2f;
            s.secondHand = 9; s.secondColor = WHITE; s.cap = 6;
        }));
        l.add(make("Clous de Paris", s -> {
            s.pattern = 6; s.faceA = 0xFFE7E9EC; s.detail = 0.6f; s.vignette = false;
            s.bezel = 4; s.bezelColor = GOLD;
            s.ticks = 4; s.tickColor = 0xFF3A2A10;
            s.numerals = 3; s.numeralFont = "Bodoni Moda"; s.numeralColor = 0xFF1A1A1A;
            s.hourHand = 1; s.minuteHand = 1; s.handFinish = 7; s.secondHand = 8; s.secondColor = GOLD; s.cap = 1;
            s.brand = "AION"; s.brandFont = "Bodoni Moda"; s.textColor = 0xFF1A1A1A; s.subtitle = "GENÈVE"; s.subFont = "Bodoni Moda";
        }));
        l.add(make("Pepsi GMT", s -> {
            s.pattern = 0; s.faceA = 0xFF0B0B0B;
            s.bezel = 8; s.bezelColor = 0xFF1B3E91; s.bezelAccent = 0xFFB3121D;
            s.ticks = 6; s.tickColor = WHITE; s.marker12 = 1;
            s.hourHand = 10; s.minuteHand = 3; s.handFinish = 3; s.handColor = SILVER; s.handAccent = 0xFFEFFFEF;
            s.secondHand = 1; s.secondColor = SILVER; s.cap = 1;
            s.brand = "AION"; s.subtitle = "GMT-MASTER"; s.textColor = WHITE; s.comp3 = 1; s.compBg = WHITE; s.compColor = BLACK;
        }));
        l.add(make("Gothic", s -> {
            s.shape = 7; s.pattern = 13; s.faceA = 0xFF1A1A1A; s.detail = 0.5f;
            s.bezel = 5; s.bezelColor = 0xFF6F6F6F;
            s.ticks = 19; s.tickColor = 0xFFC9C2B0;
            s.numerals = 3; s.numeralFont = "Unifraktur Maguntia"; s.numeralColor = 0xFFC9C2B0; s.numeralOrient = 2;
            s.hourHand = 7; s.minuteHand = 7; s.handFinish = 8; s.secondHand = 11; s.secondColor = 0xFF8B0000; s.cap = 6;
            s.brand = "Aion"; s.brandFont = "Unifraktur Maguntia"; s.textColor = 0xFFC9C2B0; s.brandSize = 1.2f;
        }));
        l.add(make("Retro Arcade", s -> {
            s.shape = 4; s.pattern = 25; s.faceA = 0xFF111122; s.faceB = 0xFF1C1C3A; s.detail = 0.3f;
            s.bezel = 11; s.accent = 0xFF39FF14;
            s.ticks = 13; s.tickColor = 0xFF39FF14;
            s.numerals = 1; s.numeralFont = "Press Start 2P"; s.numeralColor = 0xFF39FF14; s.numeralSize = 0.45f;
            s.hourHand = 14; s.minuteHand = 14; s.handColor = 0xFF39FF14; s.handFinish = 4;
            s.secondHand = 4; s.secondColor = 0xFFFF00A8; s.cap = 2; s.followShape = true;
        }));
        l.add(make("Desert Sand", s -> {
            s.shape = 11; s.pattern = 7; s.faceA = 0xFFD8C3A5; s.faceB = 0xFFF2E4CC; s.detail = 0.5f;
            s.bezel = 4; s.bezelColor = 0xFF8A6A4A;
            s.ticks = 2; s.tickColor = 0xFF2E2416;
            s.numerals = 8; s.numeralFont = "Serif"; s.numeralColor = 0xFF2E2416; s.numeralSize = 0.8f;
            s.hourHand = 22; s.minuteHand = 22; s.handColor = 0xFF2E2416; s.handFinish = 2; s.secondHand = 0; s.secondColor = 0xFFB5442E; s.cap = 1;
        }));
        l.add(make("Hypnotic", s -> {
            s.pattern = 19; s.faceA = WHITE; s.faceB = BLACK; s.detail = 0.3f; s.vignette = false;
            s.bezel = 2; s.bezelColor = 0xFF111111;
            s.ticks = 0;
            s.hourHand = 13; s.minuteHand = 13; s.handFinish = 0; s.handColor = 0xFFE63946; s.handShadow = true;
            s.secondHand = 6; s.secondColor = 0xFFFFD60A; s.cap = 5;
        }));
        l.add(make("Triple Calendar", s -> {
            s.pattern = 2; s.faceA = 0xFFF3F1EA; s.faceB = 0xFFD9D4C5; s.vignette = false;
            s.bezel = 4; s.bezelColor = SILVER;
            s.ticks = 4; s.tickColor = 0xFF2B2B2B;
            s.numerals = 1; s.numeralFont = "Marcellus"; s.numeralColor = 0xFF2B2B2B; s.numeralSize = 0.8f;
            s.hourHand = 20; s.minuteHand = 20; s.handFinish = 1; s.handColor = 0xFF3A3A3A; s.secondHand = 0; s.secondColor = 0xFF1E3A8A;
            s.comp9 = 7; s.comp3 = 9; s.comp6 = 8; s.compColor = 0xFF2B2B2B; s.compBg = 0xFFE8E4D8; s.cap = 4;
            s.brand = "AION"; s.brandFont = "Marcellus"; s.textColor = 0xFF2B2B2B; s.brandPos = 0.35f;
        }));
        l.add(make("Honeycomb", s -> {
            s.shape = 6; s.pattern = 12; s.faceA = 0xFFF2B134; s.faceB = 0xFF8A5A00; s.detail = 0.5f;
            s.bezel = 2; s.bezelColor = 0xFF2D1E0F;
            s.ticks = 3; s.tickColor = 0xFF2D1E0F;
            s.hourHand = 17; s.minuteHand = 17; s.handColor = 0xFF2D1E0F; s.secondHand = 11; s.secondColor = 0xFFB3261E; s.cap = 6;
            s.logo = 0; s.brand = "BUZZ"; s.brandFont = "Bungee"; s.textColor = 0xFF2D1E0F; s.brandSize = 0.7f;
        }));
        l.add(make("Aurora", s -> {
            s.pattern = 15; s.faceA = 0xFF00C2A8; s.faceB = 0xFF5B2EFF; s.accent = 0xFF00FF87; s.seed = 4;
            s.bezel = 0; s.vignette = true; s.gloss = true;
            s.ticks = 13; s.tickColor = 0xFFFFFFFF; s.tickSize = 0.8f;
            s.numerals = 2; s.numeralFont = "Comfortaa"; s.numeralColor = 0xFFFFFFFF; s.numeralSize = 0.9f;
            s.hourHand = 18; s.minuteHand = 18; s.handColor = 0xFFFFFFFF; s.secondHand = 10; s.secondColor = 0xFFFFFFFF; s.cap = 1;
        }));
        l.add(make("Zodiac", s -> {
            s.pattern = 9; s.faceA = 0xFF1B1464; s.faceB = 0xFF05031A; s.accent = 0xFFE84855; s.detail = 0.4f; s.seed = 11;
            s.bezel = 14; s.bezelColor = 0xFFF5D76E; s.bezelAccent = 0xFF0B0833;
            s.ticks = 0;
            s.numerals = 16; s.numeralColor = 0xFFF5D76E; s.numeralSize = 0.8f; s.numeralEffect = 4;
            s.hourHand = 26; s.minuteHand = 26; s.handFinish = 7; s.secondHand = 9; s.secondColor = 0xFFF5D76E; s.cap = 3;
            s.comp6 = 10; s.compBg = 0xFF05031A; s.compColor = 0xFFF5D76E;
        }));
        return l;
    }
}
