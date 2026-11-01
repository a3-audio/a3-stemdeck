#pragma once

#include <JuceHeader.h>
#include "StemSet.h"

// A reader for one slot of a set: an NI Stems set's slot n is track n + 1 of
// its .stem.mp4 (NiStemReader.h), any other slot its own file through the
// format manager. Null if the slot cannot be read. The caller owns it.
juce::AudioFormatReader* createStemReader (const StemSet& set, int slot, juce::AudioFormatManager& formats);
