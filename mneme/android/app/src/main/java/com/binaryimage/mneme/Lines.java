package com.binaryimage.mneme;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/** Per-line checks: syllable count and words used twice in the same line. */
final class Lines {
    static final Pattern WORD = Pattern.compile("[A-Za-z][A-Za-z'’]*");

    /** Little words that repeat in every song and don't count as a repeat. */
    private static final Set<String> SMALL = new HashSet<>(Arrays.asList(
            "a", "an", "the", "and", "or", "but", "nor", "so", "if", "to", "of", "in", "on", "at", "by", "for", "with",
            "from", "up", "out", "off", "as", "is", "am", "are", "was", "were", "be", "been", "it", "its", "it's",
            "i", "i'm", "i'll", "i'd", "i've", "me", "my", "you", "you're", "your", "you'll", "we", "our", "us",
            "they", "them", "their", "he", "she", "his", "her", "him", "this", "that", "there", "then", "than",
            "not", "no", "do", "did", "does", "don't", "can", "can't", "just", "oh", "yeah", "what", "when",
            "where", "who", "how", "all", "too", "into", "onto", "la", "na", "ooh", "ah"));

    private Lines() {}

    static final class Info {
        int syllables;
        final List<String> repeats = new ArrayList<>();
    }

    static Info check(String line) {
        Info out = new Info();
        Map<String, Integer> count = new LinkedHashMap<>();
        Matcher m = WORD.matcher(line);
        while (m.find()) {
            String w = m.group();
            out.syllables += Dict.syllables(w);
            String k = w.toLowerCase(Locale.US).replace('’', '\'');
            if (SMALL.contains(k)) continue;
            count.merge(k, 1, Integer::sum);
        }
        for (Map.Entry<String, Integer> e : count.entrySet()) if (e.getValue() > 1) out.repeats.add(e.getKey());
        return out;
    }
}
