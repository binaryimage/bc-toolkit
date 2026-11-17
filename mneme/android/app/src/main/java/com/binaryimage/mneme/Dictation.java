package com.binaryimage.mneme;

import android.content.Context;
import android.content.Intent;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.speech.RecognitionListener;
import android.speech.RecognizerIntent;
import android.speech.SpeechRecognizer;

import java.util.ArrayList;

/**
 * Speech-to-text that keeps listening until you stop it. Every phrase you say comes back as one line.
 * Uses the phone's own voice typing (offline if the language pack is downloaded).
 */
final class Dictation implements RecognitionListener {
    interface Sink {
        void onPartial(String text);
        void onLine(String text);
        void onState(boolean listening, String message);
    }

    private static final int ERROR_LANGUAGE_NOT_SUPPORTED = 12;
    private static final int ERROR_LANGUAGE_UNAVAILABLE = 13;

    private final Context ctx;
    private final Sink sink;
    private final Handler h = new Handler(Looper.getMainLooper());
    private SpeechRecognizer sr;
    private boolean on, preferOffline = true;
    private int errorsInARow;

    Dictation(Context c, Sink sink) {
        this.ctx = c;
        this.sink = sink;
    }

    static boolean available(Context c) {
        return SpeechRecognizer.isRecognitionAvailable(c);
    }

    boolean isOn() { return on; }

    void start() {
        if (on) return;
        on = true;
        errorsInARow = 0;
        if (sr == null) {
            sr = SpeechRecognizer.createSpeechRecognizer(ctx);
            sr.setRecognitionListener(this);
        }
        sink.onState(true, "Starting…");
        listen();
    }

    void stop() {
        on = false;
        h.removeCallbacksAndMessages(null);
        if (sr != null) sr.cancel();
        sink.onState(false, null);
    }

    void destroy() {
        on = false;
        h.removeCallbacksAndMessages(null);
        if (sr != null) {
            sr.destroy();
            sr = null;
        }
    }

    private void listen() {
        if (!on || sr == null) return;
        Intent i = new Intent(RecognizerIntent.ACTION_RECOGNIZE_SPEECH);
        i.putExtra(RecognizerIntent.EXTRA_LANGUAGE_MODEL, RecognizerIntent.LANGUAGE_MODEL_FREE_FORM);
        i.putExtra(RecognizerIntent.EXTRA_PARTIAL_RESULTS, true);
        i.putExtra(RecognizerIntent.EXTRA_MAX_RESULTS, 1);
        i.putExtra(RecognizerIntent.EXTRA_CALLING_PACKAGE, ctx.getPackageName());
        if (preferOffline) i.putExtra(RecognizerIntent.EXTRA_PREFER_OFFLINE, true);
        try {
            sr.startListening(i);
        } catch (Exception e) {
            on = false;
            sink.onState(false, "Voice typing couldn't start");
        }
    }

    private void again(long delay) {
        h.postDelayed(this::listen, delay);
    }

    @Override public void onReadyForSpeech(Bundle b) { sink.onState(true, "Listening…"); }
    @Override public void onBeginningOfSpeech() {}
    @Override public void onRmsChanged(float v) {}
    @Override public void onBufferReceived(byte[] b) {}
    @Override public void onEndOfSpeech() {}
    @Override public void onEvent(int t, Bundle b) {}

    @Override public void onPartialResults(Bundle b) {
        ArrayList<String> r = b.getStringArrayList(SpeechRecognizer.RESULTS_RECOGNITION);
        if (r != null && !r.isEmpty()) sink.onPartial(r.get(0));
    }

    @Override public void onResults(Bundle b) {
        errorsInARow = 0;
        ArrayList<String> r = b.getStringArrayList(SpeechRecognizer.RESULTS_RECOGNITION);
        if (r != null && !r.isEmpty() && !r.get(0).trim().isEmpty()) sink.onLine(r.get(0).trim());
        if (on) again(150);
    }

    @Override public void onError(int error) {
        if (!on) return;
        switch (error) {
            case SpeechRecognizer.ERROR_NO_MATCH:
            case SpeechRecognizer.ERROR_SPEECH_TIMEOUT:
                again(100);                       // silence: just keep listening
                return;
            case SpeechRecognizer.ERROR_INSUFFICIENT_PERMISSIONS:
                on = false;
                sink.onState(false, "Mic permission is off");
                return;
            case ERROR_LANGUAGE_NOT_SUPPORTED:
            case ERROR_LANGUAGE_UNAVAILABLE:
                if (preferOffline) {
                    preferOffline = false;    // no offline pack: fall back to online voice typing
                    again(200);
                    return;
                }
                on = false;
                sink.onState(false, "Voice typing language isn't installed");
                return;
            case SpeechRecognizer.ERROR_NETWORK:
            case SpeechRecognizer.ERROR_NETWORK_TIMEOUT:
                on = false;
                sink.onState(false, "No signal - download the offline English voice pack");
                return;
            default:
                if (++errorsInARow > 4) {
                    on = false;
                    sink.onState(false, "Voice typing stopped (error " + error + ")");
                    return;
                }
                if (sr != null) sr.cancel();
                again(400);
        }
    }

    /**
     * Spoken section names become tags: say "chorus" or "new verse" on its own and you get [Chorus] / [Verse].
     * Returns the tag text, or null if the phrase is a normal lyric line.
     */
    static String spokenTag(String said) {
        String s = said.trim().toLowerCase(java.util.Locale.US).replaceAll("[.!?,]", "");
        if (s.startsWith("new ")) s = s.substring(4).trim();
        switch (s) {
            case "intro": return "Intro";
            case "verse": return "Verse";
            case "verse 1": case "verse one": return "Verse 1";
            case "verse 2": case "verse two": return "Verse 2";
            case "verse 3": case "verse three": return "Verse 3";
            case "pre chorus": case "pre-chorus": case "prechorus": return "Pre-Chorus";
            case "chorus": case "hook": return "Chorus";
            case "bridge": return "Bridge";
            case "breakdown": case "break down": return "Breakdown";
            case "solo": return "Solo";
            case "outro": return "Outro";
            case "end": case "ending": return "End";
            default: return null;
        }
    }

    static String tidy(String line) {
        String t = line.trim();
        if (t.isEmpty()) return t;
        return Character.toUpperCase(t.charAt(0)) + t.substring(1);
    }
}
