#pragma once

#include <JuceHeader.h>
#include "StemDeckPlayer.h"
#include "StemThumbnails.h"
#include "StemEvents.h"

// Accepts stem sets dragged from the library (see StemLibrary).
struct StemSetDropTarget : public juce::DragAndDropTarget
{
	static constexpr const char* prefix = "stemset:";

	bool isInterestedInDragSource (const SourceDetails& details) override
	{
		return details.description.toString().startsWith (prefix);
	}

	void itemDropped (const SourceDetails& details) override
	{
		if (onSetDropped)
			onSetDropped (details.description.toString().fromFirstOccurrenceOf (prefix, false, false));
	}

	std::function<void (const juce::String& setId)> onSetDropped;
};

//==============================================================================
// Whole track, four stem lanes. Click to jump, drag to set a loop.
class OverviewWaveform : public juce::Component,
						 private juce::ChangeListener
{
public:
	OverviewWaveform (StemDeckPlayer& player, StemThumbnails& thumbnails);
	~OverviewWaveform() override;

	// Called by the deck's timer.
	void refresh();

	// The stems' names, each at the start of its lane, in its colour.
	void setLoadedSlots (const std::array<bool, StemSet::numStems>& loaded) { loadedSlots = loaded; repaint(); }

	// The deck's planned events, one marker each in its stem's lane; `selected`
	// is outlined, -1 for none. The list is not owned and may be null.
	void setEvents (const stemevents::EventList* newEvents, int selected) { events = newEvents; selectedEvent = selected; repaint(); }

	// A tap on a marker selects it; it neither jumps nor starts a loop.
	std::function<void (int index)> onMarkerTapped;

	void paint (juce::Graphics& g) override;
	void resized() override;
	void mouseDown (const juce::MouseEvent& e) override;
	void mouseDrag (const juce::MouseEvent& e) override;
	void mouseUp (const juce::MouseEvent& e) override;

private:
	void changeListenerCallback (juce::ChangeBroadcaster*) override { renderImage(); }
	std::vector<double> eventSeconds() const;
	int markerAt (juce::Point<float> where) const;
	void drawMarkers (juce::Graphics& g) const;
	void renderImage();
	int muteMask() const;
	double xToSeconds (float x) const;
	float secondsToX (double seconds) const;

	StemDeckPlayer& player;
	StemThumbnails& thumbnails;
	juce::Image image;
	int renderedMuteMask = 0;

	bool dragging = false;
	float dragStartX = 0.0f, dragEndX = 0.0f;
	const stemevents::EventList* events = nullptr;
	int selectedEvent = -1;
	int pressedMarker = -1;   // the marker a press began on, until it is released
	std::array<bool, StemSet::numStems> loadedSlots { true, true, true, true };
};

//==============================================================================
// Zoomed view that scrolls past a fixed centre playhead.
// Drag to move through the track, mouse wheel to zoom.
class ScrollingWaveform : public juce::Component,
						  public StemSetDropTarget
{
public:
	ScrollingWaveform (StemDeckPlayer& player, StemThumbnails& thumbnails, int deckIndex);

	void setTitle (const juce::String& newTitle) { title = newTitle; repaint(); }

	void paint (juce::Graphics& g) override;
	void mouseDown (const juce::MouseEvent& e) override;
	void mouseDrag (const juce::MouseEvent& e) override;
	void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

private:
	double trackSpan() const;

	StemDeckPlayer& player;
	StemThumbnails& thumbnails;
	const int deckIndex;
	juce::String title;

	double visibleSeconds = 8.0;   // wall-clock seconds; see WaveformScale.h
	double dragStartPosition = 0.0;
};
