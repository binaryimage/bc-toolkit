#include "Settings.h"
#include "Common/Files.h"

namespace tk
{
Settings& Settings::get() { static Settings s; return s; }

Settings::Settings()
{
    juce::PropertiesFile::Options o;
    o.applicationName = "settings";
    o.filenameSuffix = ".xml";
    o.folderName = "binaryimage Toolkit";
    o.osxLibrarySubFolder = "Application Support";
    o.storageFormat = juce::PropertiesFile::storeAsXML;
    o.millisecondsBeforeSaving = 500;
   #if TOOLKIT_TESTS
    file = std::make_unique<juce::PropertiesFile> (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("toolkit-test-settings.xml"), o);
   #else
    file = std::make_unique<juce::PropertiesFile> (appDataFolder().getChildFile ("settings.xml"), o);
   #endif
}

juce::File Settings::folder (const juce::String& key, const juce::File& def)
{
    auto s = file->getValue (key);
    juce::File f = s.isNotEmpty() ? juce::File (s) : def;
    return f;
}
}
