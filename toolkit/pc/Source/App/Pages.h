#pragma once
#include "Page.h"

namespace tk
{
    std::unique_ptr<Page> makeMetersPage (Engine&);
    std::unique_ptr<Page> makeRecordPage (Engine&);
    std::unique_ptr<Page> makeSamplerPage (Engine&);
    std::unique_ptr<Page> makeBackupPage (Engine&);
    std::unique_ptr<Page> makeGearCheckPage (Engine&);
    std::unique_ptr<Page> makeChopPage (Engine&);
    std::unique_ptr<Page> makeLibraryPage (Engine&);
    std::unique_ptr<Page> makeWavesPage (Engine&);
    std::unique_ptr<Page> makeManglePage (Engine&);
    std::unique_ptr<Page> makeRiffsPage (Engine&);
    std::unique_ptr<Page> makeBooksPage (Engine&);
    std::unique_ptr<Page> makeHelpPage (Engine&);
    std::unique_ptr<Page> makeSettingsPage (Engine&);
}
