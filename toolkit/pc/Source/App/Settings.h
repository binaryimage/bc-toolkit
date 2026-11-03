#pragma once
#include "Common/JuceHeader.h"

/** Persistent settings in %APPDATA%/binaryimage Toolkit/settings.xml */
namespace tk
{
    class Settings
    {
    public:
        static Settings& get();
        juce::PropertiesFile& props() { return *file; }
        juce::String str (const juce::String& k, const juce::String& def = {}) { return file->getValue (k, def); }
        int num (const juce::String& k, int def = 0) { return file->getIntValue (k, def); }
        double dbl (const juce::String& k, double def = 0) { return file->getDoubleValue (k, def); }
        bool flag (const juce::String& k, bool def = false) { return file->getBoolValue (k, def); }
        void set (const juce::String& k, const juce::var& v) { file->setValue (k, v); file->saveIfNeeded(); }
        juce::File folder (const juce::String& key, const juce::File& def);
    private:
        Settings();
        std::unique_ptr<juce::PropertiesFile> file;
    };
}
