#include "LinkEditor.h"
#include "Common/Dsp.h"

LinkEditor::LinkEditor (LinkProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&look);
    name.setText (p.customName(), juce::dontSendNotification);
    name.setTextToShowWhenEmpty ("Name in Toolkit (blank = track name)", tk::col::text3);
    name.onReturnKey = [this] { proc.setCustomName (name.getText()); unfocusAllComponents(); };
    name.onFocusLost = [this] { proc.setCustomName (name.getText()); };
    addAndMakeVisible (name);
    addAndMakeVisible (meter);
    addAndMakeVisible (tuner);
    buf.resize (4096);
    setSize (460, 280);
    startTimerHz (30);
}

LinkEditor::~LinkEditor() { setLookAndFeel (nullptr); }

void LinkEditor::paint (juce::Graphics& g)
{
    g.fillAll (tk::col::bg);
    auto r = getLocalBounds().toFloat().reduced (8);
    tk::drawSlab (g, r, 12);
    g.setColour (tk::col::white); g.setFont (tk::font (20, 2));
    g.drawText ("BC Link", 24, 18, 220, 26, juce::Justification::left);
    g.setColour (tk::col::text3); g.setFont (tk::font (11, 1));
    g.drawText ("BC TOOLKIT", 24, 44, 220, 14, juce::Justification::left);
    // status lamp
    auto lamp = juce::Rectangle<float> (getWidth() - 186.0f, 24, 14, 14);
    tk::drawKey (g, lamp, linked, false, false, 7);
    g.setColour (linked ? tk::col::white : tk::col::text2); g.setFont (tk::font (13, 1));
    g.drawText (linked ? "Linked to Toolkit" : "Waiting for Toolkit", getWidth() - 166, 20, 150, 22, juce::Justification::left);
    g.setColour (tk::col::text3); g.setFont (tk::font (11.5f));
    g.drawText (linked ? "Showing as: " + shownName : "Open the Toolkit app; this connects by itself.", 24, getHeight() - 36, getWidth() - 48, 16, juce::Justification::left);
}

void LinkEditor::resized()
{
    name.setBounds (24, 70, getWidth() - 48, 30);
    meter.setBounds (24, 112, 30, getHeight() - 160);
    tuner.setBounds (66, 112, getWidth() - 90, getHeight() - 160);
}

void LinkEditor::timerCallback()
{
    bool l = proc.isLinked();
    auto n = proc.displayName();
    if (l != linked || n != shownName) { linked = l; shownName = n; repaint(); }
    float pk = proc.peak.load();
    float db = juce::Decibels::gainToDecibels (pk, -90.0f);
    meter.setLevels (db, db - 6);
    proc.copyRecent (buf.data(), 4096);
    double hz = pk > 0.01f ? tk::yinPitch (buf.data(), 4096, proc.sampleRate()) : -1;
    tuner.setPitch (hz, 440.0);
}
