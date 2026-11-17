package com.binaryimage.mneme;

import android.content.Context;

import java.io.File;
import java.io.FileOutputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;

/** Only used by the build machine's emulator test: fake clips and idea lines so every screen has something on it. */
final class Ci {
    private Ci() {}

    static void seed(Context c) {
        Store.Song q = Store.load(c, "demo");
        if (q == null) {
            q = new Store.Song();
            q.id = "demo";
            q.title = "Demo Song";
            q.created = q.updated = System.currentTimeMillis();
            q.text = DEMO;
        }
        if (q.clips.isEmpty()) {
            q.clips.add(clip(c, q.id, "Verse 1 idea 1", "Verse 1", 2400, 330));
            q.clips.add(clip(c, q.id, "Chorus melody", "Chorus", 3600, 392));
            q.clips.add(clip(c, q.id, "Bridge idea 3", "Bridge", 1800, 294));
            Store.save(c, q);
        }
        Store.Song i = Store.load(c, Store.IDEAS);
        if (i.text.isEmpty()) {
            i.text = "Static where the singing was\nA house that hums at night\n[Chorus]\nMaybe a song about the radio left on\n";
            i.clips.add(clip(c, i.id, "Idea 1", "", 2000, 262));
            Store.save(c, i);
        }
    }

    static final String DEMO =
            "[Verse 1]\n" +
            "Grey light leaking through the blinds\n" +
            "and the kettle starts to sing\n" +
            "every clock is out of time\n" +
            "and the phone won't ring\n" +
            "\n" +
            "[Chorus]\n" +
            "Hold the line, hold the line\n" +
            "till the static clears\n" +
            "Every signal fades in time\n" +
            "still I'm waiting here\n" +
            "\n" +
            "[Bridge]\n" +
            "Rust on the wire\n" +
            "salt in the wound\n";

    private static Store.Clip clip(Context c, String song, String name, String section, int ms, double hz) {
        File f = new File(Store.clipsDir(c), song + "-ci" + System.nanoTime() + ".wav");
        writeTone(f, ms, hz);
        Store.Clip k = new Store.Clip();
        k.file = f.getName();
        k.name = name;
        k.section = section;
        k.created = System.currentTimeMillis();
        k.durMs = ms;
        return k;
    }

    private static void writeTone(File f, int ms, double hz) {
        int rate = 22050, n = rate * ms / 1000;
        ByteBuffer b = ByteBuffer.allocate(44 + n * 2).order(ByteOrder.LITTLE_ENDIAN);
        b.put("RIFF".getBytes()).putInt(36 + n * 2).put("WAVE".getBytes());
        b.put("fmt ".getBytes()).putInt(16).putShort((short) 1).putShort((short) 1).putInt(rate).putInt(rate * 2)
                .putShort((short) 2).putShort((short) 16);
        b.put("data".getBytes()).putInt(n * 2);
        for (int i = 0; i < n; i++) {
            double env = Math.min(1, Math.min(i, n - i) / (rate * 0.05));
            b.putShort((short) (Math.sin(2 * Math.PI * hz * i / rate) * 6000 * env));
        }
        try (FileOutputStream o = new FileOutputStream(f)) {
            o.write(b.array());
        } catch (Exception ignored) {
        }
    }
}
