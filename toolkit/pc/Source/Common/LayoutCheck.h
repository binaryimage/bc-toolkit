#pragma once
// Finds controls you couldn't use: squashed to nothing, cut off by their panel or the window, off-screen,
// sitting on top of each other, or with their text crushed. The builds run it on every page, every tab and
// several window sizes, and fail if it finds anything, so a missing button never ships again.
#include "Common/JuceHeader.h"

namespace tk
{
    /** One line per problem, e.g. "button \"Save .mid\": cut off (62% visible)". Empty = all good. */
    juce::StringArray findLayoutProblems (juce::Component& root);

    /** Every control the check looked at, for the build log ("button \"Random\"", "dropdown \"Scale\"" ...). */
    juce::StringArray listControls (juce::Component& root);
}
