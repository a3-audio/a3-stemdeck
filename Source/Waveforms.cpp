#include "Waveforms.h"
#include "Theme.h"
#include "WaveformColumns.h"
#include "WaveformScale.h"

namespace
{
	// Draws the four stems between startTime and endTime into `area`, one lane
	// each. Times outside the track stay empty, so the scrolling view can show
	// the space before the start and after the end.
	void drawStemLanes (juce::Graphics& g, StemThumbnails& thumbnails, const StemDeckPlayer& player,
						juce::Rectangle<float> area, double startTime, double endTime)
	{
		const auto length = player.getLength();
		const auto laneHeight = area.getHeight() / (float) StemSet::numStems;
		const auto pixelsPerSecond = (double) area.getWidth() / (endTime - startTime);

		const auto visibleStart = juce::jmax (0.0, startTime);
		const auto visibleEnd = juce::jmin (length, endTime);

		if (length <= 0.0 || visibleEnd <= visibleStart)
			return;

		const auto x0 = area.getX() + (float) ((visibleStart - startTime) * pixelsPerSecond);
		const auto x1 = area.getX() + (float) ((visibleEnd - startTime) * pixelsPerSecond);

		for (int s = 0; s < StemSet::numStems; ++s)
		{
			auto& thumbnail = thumbnails[s];
			const auto lane = juce::Rectangle<float> (x0, area.getY() + laneHeight * (float) s, x1 - x0, laneHeight);

			g.setColour (player.isStemMuted (s) ? Theme::textDim.withAlpha (0.35f) : Theme::stem (s));

			if (thumbnail.getTotalLength() > 0.0)
				thumbnail.drawChannel (g, lane.reduced (0.0f, 1.0f).toNearestInt(), visibleStart, visibleEnd, 0, thumbnails.getDisplayGain (s));
		}
	}

	// The scrolling view's stems, one rectangle per column of the track-anchored
	// grid (see WaveformColumns.h). Columns before the start and after the end
	// of the track stay empty.
	void drawStemColumns (juce::Graphics& g, StemThumbnails& thumbnails, const StemDeckPlayer& player,
						  juce::Rectangle<int> area, const waveformColumns::Grid& grid)
	{
		const auto length = player.getLength();
		const auto laneHeight = (float) area.getHeight() / (float) StemSet::numStems;

		for (int s = 0; s < StemSet::numStems; ++s)
		{
			auto& thumbnail = thumbnails[s];
			if (thumbnail.getTotalLength() <= 0.0)
				continue;

			const auto lane = juce::Rectangle<float> ((float) area.getX(), (float) area.getY() + laneHeight * (float) s,
													  (float) area.getWidth(), laneHeight).reduced (0.0f, 1.0f).toNearestInt().toFloat();
			const auto midY = lane.getCentreY();
			const auto halfHeight = thumbnails.getDisplayGain (s) * lane.getHeight() * 0.5f;

			juce::RectangleList<float> waveform;
			waveform.ensureStorageAllocated (area.getWidth());

			for (int x = 0; x < area.getWidth(); ++x)
			{
				const auto k = grid.originColumn + x;
				const auto start = waveformColumns::columnStart (k, grid);
				if (start < 0.0 || start >= length)
					continue;

				float low = 0.0f, high = 0.0f;
				thumbnail.getApproximateMinMax (start, waveformColumns::columnEnd (k, grid), 0, low, high);
				if (high <= low)
					continue;

				// The same 0.3 px of extra ink as AudioThumbnail::drawChannel.
				const auto top = juce::jmax (midY - high * halfHeight - 0.3f, lane.getY());
				const auto bottom = juce::jmin (midY - low * halfHeight + 0.3f, lane.getBottom());
				waveform.addWithoutMerging ({ (float) (area.getX() + x), top, 1.0f, bottom - top });
			}

			g.setColour (player.isStemMuted (s) ? Theme::textDim.withAlpha (0.35f) : Theme::stem (s));
			g.fillRectList (waveform);
		}
	}

	void drawCueMarker (juce::Graphics& g, float x, float height)
	{
		juce::Path triangle;
		triangle.addTriangle (x - 5.0f, 0.0f, x + 5.0f, 0.0f, x, 7.0f);
		g.setColour (Theme::cue);
		g.fillPath (triangle);
		g.drawVerticalLine (juce::roundToInt (x), 0.0f, height);
	}
}

//==============================================================================
OverviewWaveform::OverviewWaveform (StemDeckPlayer& p, StemThumbnails& t) : player (p), thumbnails (t)
{
	thumbnails.addChangeListener (this);
}

OverviewWaveform::~OverviewWaveform()
{
	thumbnails.removeChangeListener (this);
}

int OverviewWaveform::muteMask() const
{
	int mask = 0;

	for (int s = 0; s < StemSet::numStems; ++s)
		if (player.isStemMuted (s))
			mask |= 1 << s;

	return mask;
}

void OverviewWaveform::refresh()
{
	if (muteMask() != renderedMuteMask)
		renderImage();
	else
		repaint();
}

void OverviewWaveform::resized()
{
	renderImage();
}

void OverviewWaveform::renderImage()
{
	if (getWidth() <= 0 || getHeight() <= 0)
		return;

	const auto scale = juce::Component::getApproximateScaleFactorForComponent (this);
	image = juce::Image (juce::Image::ARGB, juce::roundToInt ((float) getWidth() * scale),
						 juce::roundToInt ((float) getHeight() * scale), true);

	juce::Graphics g (image);
	g.addTransform (juce::AffineTransform::scale (scale));
	drawStemLanes (g, thumbnails, player, getLocalBounds().toFloat(), 0.0, player.getLength());

	renderedMuteMask = muteMask();
	repaint();
}

double OverviewWaveform::xToSeconds (float x) const
{
	return juce::jlimit (0.0, 1.0, (double) x / (double) juce::jmax (1, getWidth())) * player.getLength();
}

float OverviewWaveform::secondsToX (double seconds) const
{
	const auto length = player.getLength();
	return length > 0.0 ? (float) (seconds / length) * (float) getWidth() : 0.0f;
}

void OverviewWaveform::paint (juce::Graphics& g)
{
	const auto bounds = getLocalBounds().toFloat();
	g.setColour (Theme::background);
	g.fillRect (bounds);

	if (image.isValid())
		g.drawImage (image, bounds);

	if (! player.isLoaded())
		return;

	const auto playX = secondsToX (player.getPosition());

	// Already played part is darker, as in Mixxx.
	g.setColour (juce::Colours::black.withAlpha (0.45f));
	g.fillRect (0.0f, 0.0f, playX, bounds.getHeight());

	juce::Range<float> loopX;

	if (dragging)
		loopX = { juce::jmin (dragStartX, dragEndX), juce::jmax (dragStartX, dragEndX) };
	else if (player.hasLoop())
		loopX = { secondsToX (player.getLoop().getStart()), secondsToX (player.getLoop().getEnd()) };

	if (! loopX.isEmpty())
	{
		g.setColour (Theme::loop.withAlpha (0.2f));
		g.fillRect (loopX.getStart(), 0.0f, loopX.getLength(), bounds.getHeight());
	}

	drawCueMarker (g, secondsToX (player.getCuePoint()), bounds.getHeight());

	g.setColour (juce::Colours::white);
	g.fillRect (playX - 1.0f, 0.0f, 2.0f, bounds.getHeight());

	// The slots' numbers at the start of their lanes (names until
	// 2026-10-29: anything can be in a track): small, in the slot's colour,
	// on a dark backing so they read over the waveform; an empty slot's dim.
	const auto laneHeight = bounds.getHeight() / (float) StemSet::numStems;
	const juce::Font font (juce::FontOptions (laneHeight * 0.3f, juce::Font::bold));
	g.setFont (font);
	for (int s = 0; s < StemSet::numStems; ++s)
	{
		const auto number = juce::String (s + 1);
		const auto textWidth = juce::GlyphArrangement::getStringWidth (font, number) + font.getHeight() * 0.6f;
		const auto label = juce::Rectangle<float> (0.0f, laneHeight * (float) s, textWidth, font.getHeight() * 1.2f);
		g.setColour (juce::Colours::black.withAlpha (0.6f));
		g.fillRect (label);
		g.setColour (Theme::stem (s).withMultipliedAlpha (loadedSlots[(size_t) s] ? 1.0f : Theme::emptySlotAlpha));
		g.drawText (number, label, juce::Justification::centred, false);
	}
}

void OverviewWaveform::mouseDown (const juce::MouseEvent& e)
{
	dragging = false;
	dragStartX = dragEndX = e.position.x;
}

void OverviewWaveform::mouseDrag (const juce::MouseEvent& e)
{
	dragEndX = juce::jlimit (0.0f, (float) getWidth(), e.position.x);
	dragging = std::abs (dragEndX - dragStartX) > 4.0f;
	repaint();
}

void OverviewWaveform::mouseUp (const juce::MouseEvent& e)
{
	if (player.isLoaded())
	{
		if (dragging)
			player.setLoop (xToSeconds (juce::jmin (dragStartX, dragEndX)), xToSeconds (juce::jmax (dragStartX, dragEndX)));
		else
			player.jumpTo (xToSeconds (e.position.x));
	}

	dragging = false;
	repaint();
}

//==============================================================================
ScrollingWaveform::ScrollingWaveform (StemDeckPlayer& p, StemThumbnails& t, int index)
	: player (p), thumbnails (t), deckIndex (index)
{
	setOpaque (true);
}

void ScrollingWaveform::paint (juce::Graphics& g)
{
	const auto bounds = getLocalBounds().toFloat();
	g.fillAll (Theme::background);

	// Track-anchored columns that move by whole pixels: a peak keeps its
	// column while it scrolls, so it cannot sparkle (WaveformColumns.h).
	const auto columns = waveformColumns::gridFor (player.getPosition(), trackSpan(), getWidth(), bounds.getCentreX());
	const auto startTime = waveformColumns::columnStart (columns.originColumn, columns);
	const auto endTime = waveformColumns::columnStart (columns.originColumn + getWidth(), columns);
	const auto pixelsPerSecond = 1.0 / columns.secondsPerColumn;
	const auto timeToX = [&] (double t) { return (float) juce::roundToInt (waveformColumns::timeToX (t, columns)); };

	// Lane separators
	g.setColour (Theme::panel);
	for (int s = 1; s < StemSet::numStems; ++s)
		g.drawHorizontalLine (juce::roundToInt (bounds.getHeight() * (float) s / (float) StemSet::numStems), 0.0f, bounds.getWidth());

	if (player.isLoaded())
	{
		// Beat grid behind the waveform; every 4th beat (bar start) brighter.
		const auto grid = player.getBeatGrid();

		if (grid.isValid() && grid.beatLength() * pixelsPerSecond > 4.0)
		{
			const auto firstIndex = (int) std::ceil (grid.beatsAt (juce::jmax (0.0, startTime)));
			const auto lastIndex = (int) std::floor (grid.beatsAt (juce::jmin (player.getLength(), endTime)));

			for (int beat = firstIndex; beat <= lastIndex; ++beat)
			{
				const auto x = timeToX (grid.firstBeat + beat * grid.beatLength());
				const bool barStart = ((beat % 4) + 4) % 4 == 0;
				g.setColour (juce::Colours::white.withAlpha (barStart ? 0.35f : 0.12f));
				g.fillRect (x - (barStart ? 1.0f : 0.0f), 0.0f, barStart ? 2.0f : 1.0f, bounds.getHeight());
			}
		}

		if (player.hasLoop())
		{
			const auto loop = player.getLoop();
			const auto x0 = timeToX (loop.getStart()), x1 = timeToX (loop.getEnd());
			g.setColour (Theme::loop.withAlpha (0.18f));
			g.fillRect (x0, 0.0f, x1 - x0, bounds.getHeight());
			g.setColour (Theme::loop);
			g.drawVerticalLine (juce::roundToInt (x0), 0.0f, bounds.getHeight());
			g.drawVerticalLine (juce::roundToInt (x1), 0.0f, bounds.getHeight());
		}

		drawStemColumns (g, thumbnails, player, getLocalBounds(), columns);

		const auto cueX = timeToX (player.getCuePoint());
		if (cueX >= -6.0f && cueX <= bounds.getWidth() + 6.0f)
			drawCueMarker (g, cueX, bounds.getHeight());
	}

	// Centre playhead
	g.setColour (juce::Colours::white);
	g.fillRect (bounds.getCentreX() - 1.0f, 0.0f, 2.0f, bounds.getHeight());

	// Deck badge and title, shares of the waveform's height.
	const auto unit = bounds.getHeight() / 19.0f;   // 6 px on the rig's 113 px waveform
	const auto badge = juce::Rectangle<float> (unit, unit, unit * 3.7f, unit * 3.0f);
	g.setColour (Theme::deck (deckIndex));
	g.fillRoundedRectangle (badge, unit / 2.0f);
	g.setColour (juce::Colours::black);
	g.setFont (juce::FontOptions (badge.getHeight() * 0.72f, juce::Font::bold));
	g.drawText (Theme::deckName (deckIndex), badge, juce::Justification::centred);

	if (title.isNotEmpty())
	{
		const juce::FontOptions font (badge.getHeight() * 0.72f);
		const auto titleX = badge.getRight() + unit * 0.7f;
		const auto titleWidth = juce::jmin ((float) getWidth() / 2.0f - titleX - unit,
											juce::GlyphArrangement::getStringWidth (juce::Font (font), title) + unit * 2.0f);
		const auto titleArea = juce::Rectangle<float> (titleX, unit, titleWidth, badge.getHeight());
		g.setColour (Theme::background.withAlpha (0.8f));
		g.fillRoundedRectangle (titleArea, unit / 2.0f);
		g.setColour (Theme::text);
		g.setFont (font);
		g.drawText (title, titleArea.reduced (unit, 0.0f), juce::Justification::centredLeft, true);
	}
}

// The deck's tempo, without the sync nudge and the jog bend: those change
// every frame by a hair, and the view would rescale -- and resample every
// column -- with them. Synced decks have the same speed-adjusted tempo, so
// their beats still lie equally far apart.
double ScrollingWaveform::trackSpan() const
{
	return waveformScale::trackSpan (visibleSeconds, player.getSpeed());
}

void ScrollingWaveform::mouseDown (const juce::MouseEvent&)
{
	dragStartPosition = player.getPosition();
}

void ScrollingWaveform::mouseDrag (const juce::MouseEvent& e)
{
	if (! player.isLoaded())
		return;

	const auto secondsPerPixel = trackSpan() / juce::jmax (1, getWidth());
	player.setPosition (dragStartPosition - e.getDistanceFromDragStartX() * secondsPerPixel);
	repaint();
}

void ScrollingWaveform::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
	visibleSeconds = juce::jlimit (2.0, 60.0, visibleSeconds * std::pow (0.8, (double) wheel.deltaY * 4.0));
	repaint();
}
