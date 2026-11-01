#include "StemReader.h"
#include "NiStemReader.h"

juce::AudioFormatReader* createStemReader (const StemSet& set, int slot, juce::AudioFormatManager& formats)
{
	if (slot < 0 || slot >= StemSet::numStems || ! set.hasStem (slot))
		return nullptr;
	const auto& file = set.files[(size_t) slot];
	if (set.niStems)
		return NiStemReader::open (file, slot + 1).release();   // track 0 is the master
	return formats.createReaderFor (file);
}
