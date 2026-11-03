#include "Pages.h"
#include "Common/Wavetable.h"
#include "Common/Dsp.h"

namespace tk
{
namespace
{
using wt::Frame;

/** One single cycle you can draw on. Drag to paint, shift-drag for a straight line. */
class CycleEditor : public Screen
{
public:
    Frame* frame = nullptr;
    const Frame* ghost = nullptr;
    std::function<void()> onEdit, onEditEnd;

    void paintScreen (juce::Graphics& g, juce::Rectangle<float> r) override
    {
        g.setColour (col::grid);
        g.drawHorizontalLine ((int) r.getCentreY(), r.getX(), r.getRight());
        for (int k = 1; k < 4; ++k) g.drawVerticalLine ((int) (r.getX() + r.getWidth() * k / 4), r.getY(), r.getBottom());
        auto path = [&] (const Frame& f) {
            juce::Path p;
            const int pts = juce::jmax (64, (int) r.getWidth());
            for (int i = 0; i <= pts; ++i)
            {
                float v = f[(size_t) juce::jmin (wt::N - 1, i * wt::N / pts)];
                float x = r.getX() + r.getWidth() * i / pts, y = r.getCentreY() - v * r.getHeight() * 0.46f;
                if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
            }
            return p;
        };
        if (ghost != nullptr) { g.setColour (juce::Colours::white.withAlpha (0.14f)); g.strokePath (path (*ghost), juce::PathStrokeType (1.0f)); }
        if (frame == nullptr) return;
        auto p = path (*frame);
        juce::Path fill (p);
        fill.lineTo (r.getRight(), r.getCentreY()); fill.lineTo (r.getX(), r.getCentreY()); fill.closeSubPath();
        g.setColour (juce::Colours::white.withAlpha (0.10f)); g.fillPath (fill);
        g.setColour (col::white); g.strokePath (p, juce::PathStrokeType (2.0f));
        g.setFont (font (11)); g.setColour (col::text3);
        g.drawText ("drag to draw  /  shift-drag for a straight line", r.toNearestInt().removeFromBottom (14), juce::Justification::right);
    }

    juce::Point<float> toWave (juce::Point<float> p) const
    {
        auto r = screenArea().reduced (6);
        return { juce::jlimit (0.0f, 1.0f, (p.x - r.getX()) / r.getWidth()), juce::jlimit (-1.0f, 1.0f, (r.getCentreY() - p.y) / (r.getHeight() * 0.46f)) };
    }

    void drawLine (juce::Point<float> a, juce::Point<float> b)
    {
        int i0 = (int) (a.x * (wt::N - 1)), i1 = (int) (b.x * (wt::N - 1));
        if (i0 == i1) { (*frame)[(size_t) i0] = b.y; return; }
        int s = i0 < i1 ? 1 : -1;
        for (int i = i0; i != i1 + s; i += s)
        {
            float t = (float) (i - i0) / (float) (i1 - i0);
            (*frame)[(size_t) i] = a.y + (b.y - a.y) * t;
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (frame == nullptr) return;
        start = last = toWave (e.position);
        before = *frame;
        drawLine (last, last);
        changed();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (frame == nullptr) return;
        auto p = toWave (e.position);
        if (e.mods.isShiftDown()) { *frame = before; drawLine (start, p); }
        else drawLine (last, p);
        last = p;
        changed();
    }

    void mouseUp (const juce::MouseEvent&) override { if (onEditEnd) onEditEnd(); }

private:
    void changed() { repaint(); if (onEdit) onEdit(); }
    juce::Point<float> start, last;
    Frame before;
};

/** 32 overtone bars, sketch across them to set the spectrum. */
class HarmonicEditor : public Screen
{
public:
    std::array<float, 32> amp {};
    std::function<void()> onEdit, onEditEnd;

    void paintScreen (juce::Graphics& g, juce::Rectangle<float> r) override
    {
        float bw = r.getWidth() / 32;
        for (int h = 0; h < 32; ++h)
        {
            auto b = juce::Rectangle<float> (r.getX() + h * bw + 2, r.getY(), bw - 4, r.getHeight() - 16);
            g.setColour (col::grid); g.drawRect (b, 0.5f);
            auto f = b.withTop (b.getBottom() - amp[(size_t) h] * b.getHeight());
            if (f.getHeight() > 0.5f)
            {
                g.setGradientFill (juce::ColourGradient (col::white, f.getX(), f.getY(), juce::Colour (0xff8a8a8a), f.getX(), f.getBottom(), false));
                g.fillRect (f);
                g.setColour (juce::Colour (0xffd8d8d8)); g.fillRect (f.withHeight (2.0f));
            }
            if (h % 4 == 0 || h < 4)
            {
                g.setFont (font (10)); g.setColour (col::text3);
                g.drawText (juce::String (h + 1), (int) b.getX() - 4, (int) b.getBottom() + 2, (int) bw + 8, 12, juce::Justification::centred);
            }
        }
    }

    void set (juce::Point<float> p)
    {
        auto r = screenArea().reduced (6);
        int h = juce::jlimit (0, 31, (int) ((p.x - r.getX()) / (r.getWidth() / 32)));
        amp[(size_t) h] = juce::jlimit (0.0f, 1.0f, (r.getBottom() - 16 - p.y) / (r.getHeight() - 16));
        repaint();
        if (onEdit) onEdit();
    }
    void mouseDown (const juce::MouseEvent& e) override { set (e.position); }
    void mouseDrag (const juce::MouseEvent& e) override { set (e.position); }
    void mouseUp (const juce::MouseEvent&) override { if (onEditEnd) onEditEnd(); }
};

/** Row of keyframe thumbnails; click to select. */
class KeyStrip : public juce::Component
{
public:
    const std::vector<Frame>* keys = nullptr;
    int selected = 0;
    std::function<void (int)> onSelect;

    void paint (juce::Graphics& g) override
    {
        if (keys == nullptr) return;
        for (int i = 0; i < (int) keys->size(); ++i)
        {
            auto b = cell (i).toFloat().reduced (3);
            drawKey (g, b, i == selected, false, false, 6);
            auto w = b.reduced (8, 10);
            juce::Path p;
            const auto& f = (*keys)[(size_t) i];
            for (int k = 0; k <= 48; ++k)
            {
                float x = w.getX() + w.getWidth() * k / 48, y = w.getCentreY() - f[(size_t) juce::jmin (wt::N - 1, k * wt::N / 48)] * w.getHeight() * 0.45f;
                if (k == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
            }
            g.setColour (i == selected ? juce::Colours::black : col::text);
            g.strokePath (p, juce::PathStrokeType (1.4f));
            g.setFont (font (9.5f));
            g.drawText (juce::String (i + 1), b.toNearestInt().reduced (5, 2), juce::Justification::topLeft);
        }
    }
    juce::Rectangle<int> cell (int i) const { int w = juce::jmin (84, getWidth() / juce::jmax (8, (int) keys->size())); return { i * w, 0, w, getHeight() }; }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (keys == nullptr) return;
        for (int i = 0; i < (int) keys->size(); ++i)
            if (cell (i).contains (e.getPosition())) { selected = i; repaint(); if (onSelect) onSelect (i); }
    }
};

class WavesPage : public Page, public juce::FileDragAndDropTarget, private juce::KeyListener
{
public:
    WavesPage (Engine& e) : Page (e, "waves", "Wavetables",
                                  "Draw single cycles or pull them from a sound, morph them into a table, play it, and export for the MiniFreak, Pigments, Serum or Vital.")
    {
        for (auto* s : { &edit, &table, &out }) addAndMakeVisible (s);
        edit.setTitle ("Keyframe"); table.setTitle ("Table"); out.setTitle ("Play + export");

        keys.push_back (wt::shape (2));
        keys.push_back (wt::shape (3));
        drawBtn = makeButton ("Draw", [this] { setMode (0); }, true);
        harmBtn = makeButton ("Harmonics", [this] { setMode (1); }, true);
        drawBtn->setRadioGroupId (11); harmBtn->setRadioGroupId (11);
        drawBtn->setToggleState (true, juce::dontSendNotification);
        undoBtn = makeButton ("Undo", [this] { undo(); });
        for (auto* b : { drawBtn.get(), harmBtn.get(), undoBtn.get() }) edit.addAndMakeVisible (b);
        edit.addAndMakeVisible (cyc);
        edit.addChildComponent (harm);
        cyc.onEdit = [this] { rebuildSoon(); };
        cyc.onEditEnd = [this] { rebuild(); };
        harm.onEdit = [this] { fromHarmonics(); };
        harm.onEditEnd = [this] { rebuild(); };

        startLbl = makeLabel ("Start from", 12, col::text3); edit.addAndMakeVisible (*startLbl);
        for (int s = 0; s < 6; ++s)
        {
            auto b = makeButton (wt::kShapeNames[s], [this, s] { pushUndo(); cur() = wt::shape (s); afterEdit(); });
            edit.addAndMakeVisible (*b); shapeBtns.push_back (std::move (b));
        }
        toolLbl = makeLabel ("Shape it", 12, col::text3); edit.addAndMakeVisible (*toolLbl);
        const char* tools[] = { "Smooth", "Normalize", "Mirror", "Fold", "Crush", "Invert", "Reverse", "Clear" };
        for (int t = 0; t < 8; ++t)
        {
            auto b = makeButton (tools[t], [this, t] { tool (t); });
            edit.addAndMakeVisible (*b); toolBtns.push_back (std::move (b));
        }

        strip.keys = &keys;
        strip.onSelect = [this] (int i) { select (i); };
        table.addAndMakeVisible (strip);
        addKey = makeButton ("+ Copy", [this] { if (keys.size() < 64) { keys.insert (keys.begin() + sel + 1, cur()); select (sel + 1); rebuild(); } });
        addSine = makeButton ("+ Sine", [this] { if (keys.size() < 64) { keys.insert (keys.begin() + sel + 1, wt::shape (0)); select (sel + 1); rebuild(); } });
        delKey = makeButton ("Delete", [this] { if (keys.size() > 1) { keys.erase (keys.begin() + sel); select (juce::jmin (sel, (int) keys.size() - 1)); rebuild(); } });
        leftKey = makeButton ("<", [this] { if (sel > 0) { std::swap (keys[(size_t) sel], keys[(size_t) sel - 1]); select (sel - 1); rebuild(); } });
        rightKey = makeButton (">", [this] { if (sel + 1 < (int) keys.size()) { std::swap (keys[(size_t) sel], keys[(size_t) sel + 1]); select (sel + 1); rebuild(); } });
        importBtn = makeButton ("Import WAV...", [this] { open(); });
        grabBtn = makeButton ("Grab from input", [this] { grab(); });
        for (auto* b : { addKey.get(), addSine.get(), delKey.get(), leftKey.get(), rightKey.get(), importBtn.get(), grabBtn.get() }) table.addAndMakeVisible (b);
        grabSrc = std::make_unique<SourcePicker> (engine, "wavesSource");
        styleCombo (*grabSrc); table.addAndMakeVisible (*grabSrc);
        total.addItemList ({ "1 frame", "8 frames", "16 frames", "32 frames", "64 frames", "256 frames" }, 1);
        total.setSelectedId (3, juce::dontSendNotification);
        total.onChange = [this] { rebuild(); };
        styleCombo (total); table.addAndMakeVisible (total);
        table.addAndMakeVisible (stack);
        stack.onScrub = [this] (float p) { position.setValue (p, juce::sendNotificationSync); };

        droneBtn = makeButton ("Drone", [this] { gate = droneBtn->getToggleState(); }, true);
        scanBtn = makeButton ("Scan", [this] { scan = scanBtn->getToggleState(); }, true);
        for (auto* b : { droneBtn.get(), scanBtn.get() }) out.addAndMakeVisible (b);
        fillNotes (note, 24, 84); note.setSelectedId (48 + 1, juce::dontSendNotification);
        note.onChange = [this] { midiNote = note.getSelectedId() - 1; };
        styleCombo (note); out.addAndMakeVisible (note);
        position.setRange (0, 1, 0.001); position.setSliderStyle (juce::Slider::LinearHorizontal); position.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        position.onValueChange = [this] { pos = (float) position.getValue(); stack.setPosition (pos); };
        out.addAndMakeVisible (position);
        posLbl = makeLabel ("Position", 12, col::text3); out.addAndMakeVisible (*posLbl);
        keysLbl = makeLabel ("Play it from the computer keyboard: A W S E D F T G Y H U J K (Z / X shift octave).", 12, col::text3);
        out.addAndMakeVisible (*keysLbl);
        name.setTextToShowWhenEmpty ("Name, e.g. Rust Fold", col::text3);
        out.addAndMakeVisible (name);
        format.addItemList ({ "MiniFreak (512 points, 24-bit)", "Pigments / Serum / Vital (2048)", "Single cycles (2048, 16-bit, one WAV per key)" }, 1);
        format.setSelectedId (1, juce::dontSendNotification);
        styleCombo (format); out.addAndMakeVisible (format);
        exportBtn = makeButton ("Export", [this] { exportTable(); });
        folderBtn = makeButton ("Open folder", [] { folder().createDirectory(); folder().startAsProcess(); });
        for (auto* b : { exportBtn.get(), folderBtn.get() }) out.addAndMakeVisible (b);
        how = makeLabel ("MiniFreak: open MiniFreak V (5.0 or newer), drag the file into the user wavetable import and sync to the synth. "
                         "512 points is Arturia's spec. If a file won't load, try single cycles. The 2048 table drops straight into Pigments, Serum and Vital.",
                         12, col::text3);
        how->setJustificationType (juce::Justification::topLeft);
        out.addAndMakeVisible (*how);
        status = makeLabel ("", 12.5f, col::text2); out.addAndMakeVisible (*status);

        addKeyListener (this);
        setWantsKeyboardFocus (true);
        select (0);
        rebuild();
    }

    void shown() override { engine.setGenerator ([this] (float* L, float* R, int n, double sr) { render (L, R, n, sr); }); }

    ~WavesPage() override { engine.setGenerator (nullptr); }

    static juce::File folder() { return musicFolder().getChildFile ("Toolkit Wavetables"); }

    Frame& cur() { return keys[(size_t) sel]; }

    void setMode (int m)
    {
        mode = m;
        cyc.setVisible (m == 0); harm.setVisible (m == 1);
        if (m == 1) toHarmonics();
    }

    void select (int i)
    {
        sel = juce::jlimit (0, (int) keys.size() - 1, i);
        strip.selected = sel; strip.repaint();
        cyc.frame = &cur();
        cyc.ghost = sel > 0 ? &keys[(size_t) sel - 1] : nullptr;
        cyc.repaint();
        if (mode == 1) toHarmonics();
        edit.setTitle ("Keyframe " + juce::String (sel + 1) + " of " + juce::String ((int) keys.size()));
    }

    void pushUndo() { undoStack.push_back ({ sel, cur() }); if (undoStack.size() > 40) undoStack.erase (undoStack.begin()); }
    void undo()
    {
        if (undoStack.empty()) return;
        auto [i, f] = undoStack.back(); undoStack.pop_back();
        if (i < (int) keys.size()) { keys[(size_t) i] = f; select (i); rebuild(); }
    }

    void tool (int t)
    {
        pushUndo();
        auto& w = cur();
        switch (t)
        {
            case 0: wt::smooth (w); break;
            case 1: wt::removeDc (w); wt::normalize (w); break;
            case 2: wt::mirror (w); break;
            case 3: wt::fold (w); break;
            case 4: wt::crush (w); break;
            case 5: for (auto& v : w) v = -v; break;
            case 6: std::reverse (w.begin(), w.end()); break;
            case 7: std::fill (w.begin(), w.end(), 0.0f); break;
        }
        afterEdit();
    }

    void afterEdit() { cyc.repaint(); if (mode == 1) toHarmonics(); rebuild(); }

    void toHarmonics()
    {
        auto& w = cur();
        float mx = 0;
        for (int h = 1; h <= 32; ++h)
        {
            double re = 0, im = 0;
            for (int i = 0; i < wt::N; i += 2) { double a = 2 * juce::MathConstants<double>::pi * h * i / wt::N; re += w[(size_t) i] * std::cos (a); im += w[(size_t) i] * std::sin (a); }
            harm.amp[(size_t) h - 1] = (float) std::sqrt (re * re + im * im);
            mx = juce::jmax (mx, harm.amp[(size_t) h - 1]);
        }
        for (auto& a : harm.amp) a = mx > 0 ? a / mx : 0;
        harm.repaint();
    }

    void fromHarmonics()
    {
        if (! harmTouched) { pushUndo(); harmTouched = true; }
        auto& w = cur();
        for (int i = 0; i < wt::N; ++i)
        {
            double v = 0;
            for (int h = 1; h <= 32; ++h) v += harm.amp[(size_t) h - 1] * std::sin (2 * juce::MathConstants<double>::pi * h * i / wt::N) / 1.0;
            w[(size_t) i] = (float) v;
        }
        wt::normalize (w);
        cyc.repaint();
        rebuildSoon();
    }

    void rebuildSoon() { dirty = true; }

    void rebuild()
    {
        harmTouched = false;
        dirty = false;
        static const int totals[] = { 1, 8, 16, 32, 64, 256 };
        int t = totals[juce::jlimit (0, 5, total.getSelectedId() - 1)];
        auto frames = std::make_shared<std::vector<Frame>> (wt::render (keys, t));
        stack.setFrames (*frames);
        strip.repaint();
        const juce::SpinLock::ScopedLockType l (tableLock);
        playTable = frames;
    }

    // ------------------------------------------------------------ preview synth (audio thread)
    void render (float* L, float* R, int n, double sr)
    {
        std::shared_ptr<std::vector<Frame>> t;
        {
            const juce::SpinLock::ScopedTryLockType l (tableLock);
            if (l.isLocked()) cached = playTable;
            t = cached;
        }
        if (t == nullptr || t->empty()) return;
        const double hz = 440.0 * std::pow (2.0, (midiNote.load() - 69) / 12.0);
        const double inc = hz / sr;
        const float target = gate.load() || keyDown.load() ? 0.25f : 0.0f;
        const int F = (int) t->size();
        for (int i = 0; i < n; ++i)
        {
            env += (target - env) * (target > env ? 0.004f : 0.0008f);
            if (env < 1e-5f && target == 0) { phase = 0; continue; }
            float p = pos.load();
            if (scan.load()) { scanPh += 0.15 / sr; p = (float) (0.5 - 0.5 * std::cos (2 * juce::MathConstants<double>::pi * scanPh)); }
            float fp = p * (F - 1);
            int f0 = (int) fp, f1 = juce::jmin (F - 1, f0 + 1);
            float ft = fp - f0;
            double x = phase * wt::N;
            int i0 = (int) x, i1 = (i0 + 1) % wt::N;
            float xt = (float) (x - i0);
            auto& a = (*t)[(size_t) f0]; auto& b = (*t)[(size_t) f1];
            float va = a[(size_t) i0] + (a[(size_t) i1] - a[(size_t) i0]) * xt;
            float vb = b[(size_t) i0] + (b[(size_t) i1] - b[(size_t) i0]) * xt;
            float v = (va + (vb - va) * ft) * env;
            L[i] += v; if (R) R[i] += v;
            phase += inc; if (phase >= 1) phase -= 1;
        }
        shownPos = scan.load() ? (float) (0.5 - 0.5 * std::cos (2 * juce::MathConstants<double>::pi * scanPh)) : pos.load();
    }

    bool keyPressed (const juce::KeyPress& k, juce::Component*) override
    {
        const juce::String row = "awsedftgyhujk";
        auto c = juce::CharacterFunctions::toLowerCase (k.getTextCharacter());
        if (c == 'z') { octave = juce::jmax (1, octave - 1); return true; }
        if (c == 'x') { octave = juce::jmin (6, octave + 1); return true; }
        int i = row.indexOfChar (c);
        if (i < 0) return false;
        midiNote = 12 * (octave + 1) + i;
        keyDown = true; heldKey = k.getKeyCode();
        return true;
    }

    bool keyStateChanged (bool isDown, juce::Component*) override
    {
        if (! isDown && keyDown && ! juce::KeyPress::isKeyCurrentlyDown (heldKey)) keyDown = false;
        return false;
    }

    void tick() override
    {
        if (dirty) rebuild();
        if (scan.load() && (gate.load() || keyDown.load())) stack.setPosition (shownPos.load());
    }

    // ------------------------------------------------------------ in/out
    bool isInterestedInFileDrag (const juce::StringArray& f) override { return f.size() == 1; }
    void filesDropped (const juce::StringArray& f, int, int) override { importFile (juce::File (f[0])); }

    void open()
    {
        chooser = std::make_unique<juce::FileChooser> ("Import a wavetable, single cycle or any sound", musicFolder(), "*.wav;*.aif;*.aiff;*.flac");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc) {
            if (fc.getResult().existsAsFile()) importFile (fc.getResult());
        });
    }

    void importFile (const juce::File& f)
    {
        juce::AudioBuffer<float> b; double sr;
        if (! readAudio (f, b, sr, 60)) { status->setText ("Couldn't read that file.", juce::dontSendNotification); return; }
        name.setText (f.getFileNameWithoutExtension(), juce::dontSendNotification);
        // short files are treated as wavetables / single cycles, long ones as recordings
        if (b.getNumSamples() <= 256 * 2048 && b.getNumSamples() % 256 == 0)
        {
            std::vector<float> s ((size_t) b.getNumSamples());
            for (int i = 0; i < b.getNumSamples(); ++i) s[(size_t) i] = b.getSample (0, i);
            auto fr = wt::framesFromSamples (s);
            if (! fr.empty()) { setKeys (fr); status->setText ("Loaded " + juce::String ((int) fr.size()) + " frames.", juce::dontSendNotification); return; }
        }
        receiveAudio (b, sr, f.getFileNameWithoutExtension());
    }

    void receiveAudio (const juce::AudioBuffer<float>& b, double sr, const juce::String& n) override
    {
        double hz = 0;
        auto fr = wt::framesFromRecording (b, sr, 8, &hz);
        if (fr.empty()) { status->setText ("Couldn't find a steady pitch in that. A held note works best.", juce::dontSendNotification); return; }
        name.setText (n, juce::dontSendNotification);
        setKeys (fr);
        status->setText ("Pulled 8 cycles across the sound (" + juce::String (hz, 1) + " Hz, " + noteName ((int) std::lround (midiFromHz (hz))) + ").", juce::dontSendNotification);
    }

    void setKeys (std::vector<Frame> fr)
    {
        if (fr.size() > 64) { std::vector<Frame> k; for (int i = 0; i < 64; ++i) k.push_back (fr[(size_t) i * fr.size() / 64]); fr = k; }
        keys = std::move (fr);
        undoStack.clear();
        if (keys.size() > 1) total.setSelectedId (keys.size() > 32 ? 5 : keys.size() > 16 ? 4 : 3, juce::dontSendNotification);
        select (0);
        rebuild();
    }

    void grab()
    {
        auto* s = grabSrc->selected();
        if (s == nullptr) return;
        juce::AudioBuffer<float> b (2, 16000);
        s->copyLatest (b.getWritePointer (0), b.getWritePointer (1), b.getNumSamples());
        receiveAudio (b, s->sr.load(), s->name);
    }

    void exportTable()
    {
        auto nm = safeName (name.getText().isEmpty() ? "Toolkit table" : name.getText());
        folder().createDirectory();
        static const int totals[] = { 1, 8, 16, 32, 64, 256 };
        int t = totals[juce::jlimit (0, 5, total.getSelectedId() - 1)];
        auto frames = wt::render (keys, t);
        juce::File f;
        if (format.getSelectedId() == 1)
        {
            f = folder().getChildFile (nm + " (MiniFreak).wav");
            if (frames.size() > 64) { std::vector<Frame> k; for (int i = 0; i < 64; ++i) k.push_back (frames[(size_t) i * frames.size() / 64]); frames = k; }
            f.replaceWithData (wt::wav (frames, 512, 24).getData(), wt::wav (frames, 512, 24).getSize());
        }
        else if (format.getSelectedId() == 2)
        {
            f = folder().getChildFile (nm + " (2048).wav");
            auto m = wt::wav (frames, 2048, 24);
            f.replaceWithData (m.getData(), m.getSize());
        }
        else
        {
            f = folder().getChildFile (nm + " cycles");
            f.createDirectory();
            for (size_t i = 0; i < keys.size(); ++i)
            {
                auto m = wt::wav ({ keys[i] }, 2048, 16);
                f.getChildFile (nm + " " + juce::String ((int) i + 1).paddedLeft ('0', 2) + ".wav").replaceWithData (m.getData(), m.getSize());
            }
        }
        lastExport = f;
        status->setText ("Saved " + f.getFullPathName(), juce::dontSendNotification);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto right = r.removeFromRight (440);
        auto topHalf = r.removeFromTop (r.getHeight() * 52 / 100);
        edit.setBounds (topHalf);
        table.setBounds (r.withTrimmedTop (10));
        out.setBounds (right.withTrimmedLeft (10));

        auto c = edit.content();
        auto side = c.removeFromRight (300);
        auto bar = c.removeFromTop (40);
        rowLayout (bar, { { drawBtn.get(), 100 }, { harmBtn.get(), 120 }, { undoBtn.get(), 90 } });
        c.removeFromTop (6);
        cyc.setBounds (c); harm.setBounds (c);
        side.removeFromLeft (14);
        startLbl->setBounds (side.removeFromTop (20));
        for (int i = 0; i < 6; ++i) shapeBtns[(size_t) i]->setBounds (side.getX() + (i % 3) * (side.getWidth() / 3), side.getY() + (i / 3) * 40, side.getWidth() / 3, 40);
        side.removeFromTop (88);
        toolLbl->setBounds (side.removeFromTop (20));
        for (int i = 0; i < 8; ++i) toolBtns[(size_t) i]->setBounds (side.getX() + (i % 2) * (side.getWidth() / 2), side.getY() + (i / 2) * 40, side.getWidth() / 2, 40);

        auto t = table.content();
        auto tb = t.removeFromTop (40);
        rowLayout (tb, { { addKey.get(), 86 }, { addSine.get(), 86 }, { delKey.get(), 86 }, { leftKey.get(), 44 }, { rightKey.get(), 44 },
                         { &total, 120 }, { importBtn.get(), 130 }, { grabBtn.get(), 150 }, { grabSrc.get(), 170 } });
        t.removeFromTop (6);
        strip.setBounds (t.removeFromTop (58));
        t.removeFromTop (6);
        stack.setBounds (t);

        auto o = out.content();
        rowLayout (o.removeFromTop (40), { { droneBtn.get(), 100 }, { scanBtn.get(), 100 }, { &note, 100 } });
        o.removeFromTop (6);
        auto pr = o.removeFromTop (30);
        posLbl->setBounds (pr.removeFromLeft (70)); position.setBounds (pr);
        keysLbl->setBounds (o.removeFromTop (40));
        o.removeFromTop (16);
        name.setBounds (o.removeFromTop (34)); o.removeFromTop (8);
        format.setBounds (o.removeFromTop (32)); o.removeFromTop (8);
        rowLayout (o.removeFromTop (44), { { exportBtn.get(), 180 }, { folderBtn.get(), 150 } });
        o.removeFromTop (8);
        status->setBounds (o.removeFromTop (40));
        how->setBounds (o);
    }

    void demo() override
    {
        keys.clear();
        for (int k = 0; k < 5; ++k)
        {
            Frame f ((size_t) wt::N);
            for (int i = 0; i < wt::N; ++i)
            {
                double ph = (double) i / wt::N, s = std::sin (2 * juce::MathConstants<double>::pi * ph);
                f[(size_t) i] = (float) std::tanh ((1 + k * 1.6) * (s + 0.3 * k * std::sin (6 * juce::MathConstants<double>::pi * ph)));
            }
            keys.push_back (f);
        }
        wt::fold (keys[4]);
        name.setText ("Rust Fold", juce::dontSendNotification);
        total.setSelectedId (4, juce::dontSendNotification);
        select (3);
        rebuild();
        position.setValue (0.62, juce::sendNotificationSync);
        status->setText ("Saved " + folder().getChildFile ("Rust Fold (MiniFreak).wav").getFullPathName(), juce::dontSendNotification);
    }

    Slab edit, table, out;
    CycleEditor cyc; HarmonicEditor harm; KeyStrip strip; StackView stack;
    std::vector<Frame> keys;
    std::vector<std::pair<int, Frame>> undoStack;
    int sel = 0, mode = 0, octave = 3, heldKey = 0;
    bool dirty = false, harmTouched = false;
    std::unique_ptr<juce::TextButton> drawBtn, harmBtn, undoBtn, addKey, addSine, delKey, leftKey, rightKey, importBtn, grabBtn,
        droneBtn, scanBtn, exportBtn, folderBtn;
    std::vector<std::unique_ptr<juce::TextButton>> shapeBtns, toolBtns;
    std::unique_ptr<juce::Label> startLbl, toolLbl, posLbl, keysLbl, how, status;
    std::unique_ptr<SourcePicker> grabSrc;
    juce::ComboBox total, note, format;
    juce::Slider position;
    juce::TextEditor name;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::File lastExport;

    juce::SpinLock tableLock;
    std::shared_ptr<std::vector<Frame>> playTable, cached;
    std::atomic<int> midiNote { 48 };
    std::atomic<bool> gate { false }, keyDown { false }, scan { false };
    std::atomic<float> pos { 0 }, shownPos { 0 };
    double phase = 0, scanPh = 0; float env = 0;
};
}

std::unique_ptr<Page> makeWavesPage (Engine& e) { return std::make_unique<WavesPage> (e); }
}
