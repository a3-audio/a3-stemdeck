#include "StemLibrary.h"
#include "Theme.h"
#include "Waveforms.h"
#include "StemJob.h"
#include "StemNames.h"
#include "FolderStep.h"
#include "LibraryFolders.h"

#include <map>

namespace
{
	double nowMs() { return juce::Time::getMillisecondCounterHiRes(); }
}

//==============================================================================
// The table's header for a finger (2026-10-08). JUCE's lets columns be
// dragged and resized, counts a press that moved 4 px as a drag (and then
// does not sort), and keeps 3 px either side of every edge for resizing; on
// the rig the finger's twin (touch and emulated mouse) sorted twice, i.e.
// not at all. Here the whole cell is the key: a tap sorts, a second one
// reverses, wherever the finger lifts within the same column.
class StemLibrary::TapHeader : public juce::TableHeaderComponent
{
public:
	TapHeader() { setPopupMenuActive (false); }

	void mouseDown (const juce::MouseEvent& e) override { pressedColumn = getColumnIdAtX (e.x); }
	void mouseDrag (const juce::MouseEvent&) override {}

	void mouseUp (const juce::MouseEvent& e) override
	{
		const auto column = getColumnIdAtX (e.x);
		if (column == 0 || column != pressedColumn || ! taps.accept (nowMs()))
			return;
		const auto next = sortAfterTap ({ getSortColumnId(), isSortedForwards() }, column);
		setSortColumnId (next.column, next.forwards);
	}

private:
	int pressedColumn = 0;
	TapFilter taps;
};

//==============================================================================
// A folder in the tree: its name and how many sets lie in and below it. A
// tap chooses it and opens or closes it; a drag scrolls the tree. The tap is
// taken on release, from a transparent component over the row, so a drag
// that began on a row neither chooses nor opens it. JUCE's own handling acts
// on the press (and its triangles toggle there, so a touch's twin closed what
// the touch had opened).
class StemLibrary::FolderItem : public juce::TreeViewItem
{
public:
	FolderItem (StemLibrary& library, const juce::String& relativePath, const juce::String& name, int numSets)
		: path (relativePath), owner (library), label (name + "  " + juce::String (numSets))
	{
	}

	std::unique_ptr<juce::Component> createItemComponent() override { return std::make_unique<TapArea> (*this); }

	bool mightContainSubItems() override { return getNumSubItems() > 0; }
	juce::String getUniqueName() const override { return path.isEmpty() ? juce::String ("/") : path; }
	int getItemHeight() const override { return owner.table.getRowHeight() * 6 / 5; }
	void itemDoubleClicked (const juce::MouseEvent&) override {}

	void paintItem (juce::Graphics& g, int width, int height) override
	{
		const auto chosen = owner.chosenFolder == path;
		auto area = juce::Rectangle<int> (width, height);

		if (chosen)
		{
			g.setColour (Theme::loop.withAlpha (0.3f));
			g.fillRoundedRectangle (area.toFloat().reduced (1.0f), 4.0f);
		}

		auto marker = area.removeFromLeft (height * 2 / 3);
		g.setFont (juce::FontOptions ((float) height * 0.55f));
		g.setColour (Theme::textDim);
		if (mightContainSubItems())
			g.drawText (juce::String::fromUTF8 (isOpen() ? "\xe2\x96\xbe" : "\xe2\x96\xb8"), marker, juce::Justification::centred);

		g.setColour (chosen ? Theme::text : Theme::textDim);
		g.drawText (label, area, juce::Justification::centredLeft, true);
	}

	const juce::String path;

private:
	struct TapArea : public juce::Component
	{
		explicit TapArea (FolderItem& i) : item (i) {}
		// On the screen, not in the row: a drag scrolls the row along with the
		// finger, and in its own coordinates the finger has not moved at all.
		void mouseUp (const juce::MouseEvent& e) override
		{
			const auto moved = e.getScreenPosition().getDistanceFrom (e.getMouseDownScreenPosition());
			if (touchlist::isTap ((float) moved, getHeight()))
				item.owner.folderTapped (item);
		}
		FolderItem& item;
	};

	StemLibrary& owner;
	const juce::String label;
};

//==============================================================================

StemLibrary::StemLibrary (juce::AudioFormatManager& fm) : formatManager (fm)
{
	rescanButton.onClick = [this] { setFolder (folder); };
	loadAButton.onClick = [this] { loadSelected (0); };
	loadBButton.onClick = [this] { loadSelected (1); };
	createButton.onClick = [this] { chooseFilesForStems(); };
	cancelCreateButton.onClick = [this] { if (onCancelCreation) onCancelCreation(); };
	loadAButton.setColour (juce::TextButton::textColourOffId, Theme::deck (0));
	loadBButton.setColour (juce::TextButton::textColourOffId, Theme::deck (1));

	for (auto* b : { &rescanButton, &loadAButton, &loadBButton, &createButton, &cancelCreateButton })
	{
		b->setMouseClickGrabsKeyboardFocus (false);
		addAndMakeVisible (b);
	}

	folderLabel.setColour (juce::Label::textColourId, Theme::textDim);
	folderLabel.setMinimumHorizontalScale (0.6f);
	addAndMakeVisible (folderLabel);

	creatorLabel.setColour (juce::Label::textColourId, Theme::textDim);
	creatorLabel.setMinimumHorizontalScale (0.7f);
	addChildComponent (creatorLabel);
	cancelCreateButton.setVisible (false);

	searchBox.setTextToShowWhenEmpty ("Search...", Theme::textDim);
	searchBox.setColour (juce::TextEditor::backgroundColourId, Theme::background);
	searchBox.setColour (juce::TextEditor::outlineColourId, Theme::outline);
	searchBox.onTextChange = [this] { applyFilter(); };
	searchBox.onReturnKey = [this] { loadSelected (-1); };
	searchBox.onEscapeKey = [this] { searchBox.clear(); applyFilter(); };
	addAndMakeVisible (searchBox);

	table.setHeader (std::make_unique<TapHeader>());
	auto& header = table.getHeader();
	// stems/Artist/Album/<sets>: the folders are the first two columns, and
	// the table is sorted artist, album, set to begin with. Not movable, not
	// resizable: a finger on the header sorts, nothing else.
	constexpr auto columnFlags = juce::TableHeaderComponent::visible | juce::TableHeaderComponent::sortable;
	header.addColumn ("Artist", artistColumn, 180, 80, -1, columnFlags);
	header.addColumn ("Album", albumColumn, 180, 80, -1, columnFlags);
	header.addColumn ("Set", nameColumn, 280, 120, -1, columnFlags);
	header.addColumn ("BPM", bpmColumn, 70, 50, -1, columnFlags);
	header.addColumn ("Stems", stemsColumn, 200, 80, -1, columnFlags);
	header.addColumn (juce::String ("Length"), lengthColumn, 70, 50, -1, columnFlags);
	// Into the window's width rather than past its right edge (768 px on the rig).
	header.setStretchToFitActive (true);
	header.setSortColumnId (artistColumn, true);
	header.setColour (juce::TableHeaderComponent::backgroundColourId, Theme::panelRaised);
	header.setColour (juce::TableHeaderComponent::textColourId, Theme::textDim);

	table.setMultipleSelectionEnabled (false);
	// Touch lists: a finger's drag scrolls (a held press drags a set, see
	// getDragSourceDescription); a tap selects, on release.
	table.getViewport()->setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::all);
	table.addMouseListener (this, true);
	table.setColour (juce::ListBox::backgroundColourId, Theme::background);
	addAndMakeVisible (table);

	folderTree.setOpenCloseButtonsVisible (false);
	folderTree.getViewport()->setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::all);
	folderTree.setRootItemVisible (true);
	folderTree.setColour (juce::TreeView::backgroundColourId, Theme::background);
	folderTree.setColour (juce::TreeView::selectedItemBackgroundColourId, juce::Colours::transparentBlack);
	addAndMakeVisible (folderTree);

	foldersButton.setClickingTogglesState (true);
	foldersButton.setToggleState (true, juce::dontSendNotification);
	foldersButton.setColour (juce::TextButton::buttonOnColourId, Theme::panelRaised.brighter (0.3f));
	foldersButton.setTooltip ("Show or hide the folder tree");
	foldersButton.setMouseClickGrabsKeyboardFocus (false);
	foldersButton.onClick = [this] { showFolders (foldersButton.getToggleState()); };
	addAndMakeVisible (foldersButton);
}

StemLibrary::~StemLibrary()
{
	folderTree.setRootItem (nullptr);   // the tree does not own its items
}

void StemLibrary::setFolder (const juce::File& newFolder)
{
	folder = newFolder;
	allSets = folder.isDirectory() ? StemSet::scanFolder (folder, formatManager) : std::vector<StemSet>();
	rebuildFolderTree();
	applyFilter();

	if (onFolderChanged)
		onFolderChanged (folder);
}

const StemSet* StemLibrary::findSet (const juce::String& setId) const
{
	for (const auto& set : allSets)
		if (idFor (set) == setId)
			return &set;

	return nullptr;
}

// One sort for all sets, in the table's order; the table shows those the
// search matches, and a deck's Next / Prev walks them all (FolderStep.h).
void StemLibrary::applyFilter()
{
	orderedSets.clear();
	for (const auto& set : allSets)
		orderedSets.push_back (&set);
	std::stable_sort (orderedSets.begin(), orderedSets.end(), [this] (const StemSet* a, const StemSet* b) { return listsBefore (*a, *b); });

	const auto words = juce::StringArray::fromTokens (searchBox.getText(), true);
	visibleSets.clear();

	const auto chosen = chosenFolder.toStdString();

	for (const auto* set : orderedSets)
		if (libraryfolders::isWithin (set->folder.toStdString(), chosen)
			&& std::all_of (words.begin(), words.end(), [set] (const juce::String& w)
			{
				return set->name.containsIgnoreCase (w) || set->artist.containsIgnoreCase (w) || set->album.containsIgnoreCase (w);
			}))
			visibleSets.push_back (set);

	table.updateContent();
	table.selectRow (0);
	table.repaint();
	updateFolderLabel();

	if (onOrderChanged)
		onOrderChanged();
}

void StemLibrary::rebuildFolderTree()
{
	std::vector<std::string> setFolders;
	for (const auto& set : allSets)
		setFolders.push_back (set.folder.toStdString());
	const auto folders = libraryfolders::folderTree (setFolders);
	chosenFolder = libraryfolders::validChoice (chosenFolder.toStdString(), folders);

	// Open as they were, across a rescan.
	const auto openness = folderTree.getOpennessState (false);
	folderTree.setRootItem (nullptr);

	auto root = std::make_unique<FolderItem> (*this, juce::String(), folder.getFileName(), (int) allSets.size());
	std::map<std::string, FolderItem*> items { { std::string(), root.get() } };
	for (const auto& path : folders)   // parents first
	{
		auto* item = new FolderItem (*this, path, libraryfolders::nameOf (path),
									 (int) libraryfolders::countWithin (setFolders, path));
		items.at (libraryfolders::parentOf (path))->addSubItem (item);
		items[path] = item;
	}

	rootFolderItem = std::move (root);
	folderTree.setRootItem (rootFolderItem.get());
	if (openness != nullptr)
		folderTree.restoreOpennessState (*openness, false);
	rootFolderItem->setOpen (true);
	for (auto f = chosenFolder.toStdString(); ! f.empty(); f = libraryfolders::parentOf (f))
		items.at (f)->setOpen (true);
}

void StemLibrary::folderTapped (FolderItem& item)
{
	if (! folderTaps.accept (nowMs()))
		return;

	if (item.mightContainSubItems())
		item.setOpen (! item.isOpen());
	if (item.path != chosenFolder)
		chooseFolder (item.path);
	else
		folderTree.repaint();
}

void StemLibrary::chooseFolder (const juce::String& relativeFolder)
{
	chosenFolder = relativeFolder;
	applyFilter();
	folderTree.repaint();
}

void StemLibrary::showFolders (bool show)
{
	foldersButton.setToggleState (show, juce::dontSendNotification);
	folderTree.setVisible (show);
	resized();
}

void StemLibrary::updateFolderLabel()
{
	const auto where = chosenFolder.isEmpty() ? folder.getFullPathName() : folder.getFileName() + "/" + chosenFolder;
	const auto count = chosenFolder.isEmpty() ? juce::String ((int) allSets.size())
											  : juce::String ((int) visibleSets.size()) + " of " + juce::String ((int) allSets.size());
	folderLabel.setText (where + "  -  " + count + " sets", juce::dontSendNotification);
}

bool StemLibrary::listsBefore (const StemSet& a, const StemSet& b) const
{
	int order = 0;

	// Artist and album sort down through the levels below them, so an
	// artist's albums and an album's sets stay together and in order.
	switch (sortColumn)
	{
		case lengthColumn: order = a.lengthSeconds < b.lengthSeconds ? -1 : (a.lengthSeconds > b.lengthSeconds ? 1 : 0); break;
		case bpmColumn:    order = bpmOf (a) < bpmOf (b) ? -1 : (bpmOf (a) > bpmOf (b) ? 1 : 0); break;
		case artistColumn: order = a.artist.compareNatural (b.artist);
						   if (order == 0) order = a.album.compareNatural (b.album);
						   if (order == 0) order = a.name.compareNatural (b.name);
						   break;
		case albumColumn:  order = a.album.compareNatural (b.album);
						   if (order == 0) order = a.name.compareNatural (b.name);
						   break;
		default:           order = a.name.compareNatural (b.name); break;
	}

	return sortForwards ? order < 0 : order > 0;
}

const StemSet* StemLibrary::neighbourInFolder (const juce::String& setId, int direction) const
{
	std::vector<std::string> folders;
	folders.reserve (orderedSets.size());
	std::optional<size_t> current;

	for (const auto* set : orderedSets)
	{
		if (idFor (*set) == setId)
			current = folders.size();
		folders.push_back (set->firstFile().getParentDirectory().getFullPathName().toStdString());
	}

	if (! current)
		return nullptr;
	const auto found = folderstep::neighbour (folders, *current, direction);
	return found ? orderedSets[*found] : nullptr;
}

double StemLibrary::bpmOf (const StemSet& set) const
{
	if (lookUpBeatGrid)
		if (const auto grid = lookUpBeatGrid (set))
			return grid->bpm;

	return 0.0;
}

void StemLibrary::selectRelative (int delta)
{
	if (visibleSets.empty())
		return;
	const auto row = juce::jlimit (0, (int) visibleSets.size() - 1, juce::jmax (0, table.getSelectedRow()) + delta);
	table.selectRow (row);
	table.scrollToEnsureRowIsOnscreen (row);
}

const StemSet* StemLibrary::selectedSet() const
{
	const auto row = table.getSelectedRow();
	return row >= 0 && row < (int) visibleSets.size() ? visibleSets[(size_t) row] : nullptr;
}

std::vector<std::string> StemLibrary::shownIds() const
{
	std::vector<std::string> ids;
	ids.reserve (visibleSets.size());
	for (const auto* set : visibleSets)
		ids.push_back (idFor (*set).toStdString());
	return ids;
}

const StemSet* StemLibrary::randomVisibleSet (const std::set<juce::String>& played) const
{
	std::vector<const StemSet*> fresh;
	for (const auto* set : visibleSets)
		if (played.count (idFor (*set)) == 0)
			fresh.push_back (set);

	const auto& from = fresh.empty() ? visibleSets : fresh;
	if (from.empty())
		return nullptr;
	return from[(size_t) juce::Random::getSystemRandom().nextInt ((int) from.size())];
}

juce::String StemLibrary::storedId (const juce::String& setId) const
{
	const juce::File file (setId);
	return setId.isNotEmpty() && file.isAChildOf (folder) ? file.getRelativePathFrom (folder) : setId;
}

juce::String StemLibrary::idFromStored (const juce::String& stored) const
{
	return stored.isEmpty() || juce::File::isAbsolutePath (stored) ? stored : folder.getChildFile (stored).getFullPathName();
}

void StemLibrary::saveState (LibrarySession& state) const
{
	state.sortColumn = sortColumn;
	state.sortForwards = sortForwards;
	state.search = searchBox.getText();
	const auto row = table.getSelectedRow();
	state.selectedSetId = row >= 0 && row < (int) visibleSets.size() ? storedId (idFor (*visibleSets[(size_t) row])) : juce::String();
	state.folder = chosenFolder;
	state.showFolders = foldersButton.getToggleState();
}

void StemLibrary::restoreState (const LibrarySession& state)
{
	showFolders (state.showFolders);
	chosenFolder = state.folder;
	rebuildFolderTree();                                                             // drops a folder that is gone
	applyFilter();
	if (state.sortColumn > 0)
		table.getHeader().setSortColumnId (state.sortColumn, state.sortForwards);   // sorts through sortOrderChanged
	searchBox.setText (state.search, true);                                          // filters through onTextChange

	for (int row = 0; row < (int) visibleSets.size(); ++row)
		if (idFor (*visibleSets[(size_t) row]) == idFromStored (state.selectedSetId))
		{
			table.selectRow (row);
			table.scrollToEnsureRowIsOnscreen (row);
			break;
		}
}

void StemLibrary::sortOrderChanged (int columnId, bool forwards)
{
	sortColumn = columnId;
	sortForwards = forwards;
	applyFilter();
}

void StemLibrary::setOneDeck (juce::Colour deckColour)
{
	loadAButton.setButtonText ("Load");
	loadAButton.setColour (juce::TextButton::textColourOffId, deckColour);
	loadBButton.setVisible (false);
	resized();
}

void StemLibrary::loadSelected (int deckIndex)
{
	const auto row = table.getSelectedRow();

	if (juce::isPositiveAndBelow (row, (int) visibleSets.size()) && onLoadSet)
		onLoadSet (*visibleSets[(size_t) row], deckIndex);
}

//==============================================================================
int StemLibrary::getNumRows()
{
	return (int) visibleSets.size();
}

void StemLibrary::paintRowBackground (juce::Graphics& g, int row, int, int, bool selected)
{
	if (selected)
		g.fillAll (juce::Colour (0xff34506b));
	else if (row % 2 == 1)
		g.fillAll (Theme::panel);
}

void StemLibrary::paintCell (juce::Graphics& g, int row, int columnId, int width, int height, bool)
{
	if (! juce::isPositiveAndBelow (row, (int) visibleSets.size()))
		return;

	const auto& set = *visibleSets[(size_t) row];
	juce::String text;

	switch (columnId)
	{
		case nameColumn:   text = set.name; break;
		case stemsColumn:  text = juce::String (loadedSlotsText (set.loadedSlots())); break;
		case bpmColumn:    { const auto bpm = bpmOf (set); text = bpm > 0.0 ? juce::String (bpm, 2) : juce::String(); break; }
		case lengthColumn: text = Theme::formatTime (set.lengthSeconds).upToLastOccurrenceOf (".", false, false); break;
		case artistColumn: text = set.artist; break;
		case albumColumn:  text = set.album; break;
		default: break;
	}

	g.setColour (columnId == nameColumn ? Theme::text : Theme::textDim);
	g.setFont (juce::FontOptions ((float) height * 0.6f));
	g.drawText (text, 6, 0, width - 12, height, juce::Justification::centredLeft, true);
}

void StemLibrary::cellDoubleClicked (int row, int, const juce::MouseEvent&)
{
	table.selectRow (row);
	loadSelected (-1);
}

void StemLibrary::returnKeyPressed (int)
{
	loadSelected (-1);
}

// A row is dragged onto a deck only after a press held still (touchlist):
// a quick swipe scrolls the table instead. While a set is dragged the table
// stays put; the next release frees it again (mouseUp).
juce::var StemLibrary::getDragSourceDescription (const juce::SparseSet<int>& rows)
{
	if (rows.size() != 1 || ! juce::isPositiveAndBelow (rows[0], (int) visibleSets.size()) || ! pressHeldStill())
		return {};

	if (auto* content = table.getViewport()->getViewedComponent())
		content->setViewportIgnoreDragFlag (true);
	return juce::String (StemSetDropTarget::prefix) + idFor (*visibleSets[(size_t) rows[0]]);
}

bool StemLibrary::pressHeldStill() const
{
	auto& desktop = juce::Desktop::getInstance();
	for (int i = 0; i < desktop.getNumDraggingMouseSources(); ++i)
		if (auto* source = desktop.getDraggingMouseSource (i))
			if (touchlist::isLongPress ((juce::Time::getCurrentTime() - source->getLastMouseDownTime()).inMilliseconds()))
				return true;
	return false;
}

void StemLibrary::mouseUp (const juce::MouseEvent&)
{
	if (auto* content = table.getViewport()->getViewedComponent())
		content->setViewportIgnoreDragFlag (false);
}

//==============================================================================
void StemLibrary::paint (juce::Graphics& g)
{
	g.setColour (Theme::panel);
	g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (2.0f), 6.0f);

	if (dropHighlight)
	{
		g.setColour (Theme::deck (0));
		g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (3.0f), 6.0f, 2.0f);
	}
}

void StemLibrary::resized()
{
	// Every size a share of the library's width or height (sized for the
	// rig's 756 x 222).
	const auto wide = [this] (int perMille) { return getWidth() * perMille / 1000; };
	const auto high = [this] (int perMille) { return getHeight() * perMille / 1000; };
	const auto gap = wide (8);
	auto area = getLocalBounds().reduced (wide (13));

	// Two rows where one does not hold them all (a portrait screen): the
	// search and the load keys, then where the library is, creating and
	// rescanning. The folder itself is chosen in Settings (2026-09-30).
	const auto twoRows = getWidth() < getHeight() * 4;
	const auto keyWidth = wide (132);

	auto bar = area.removeFromTop (high (135));
	auto folderBar = twoRows ? (area.removeFromTop (gap), area.removeFromTop (high (135))) : bar;

	const auto loadKeys = loadBButton.isVisible() ? 2 : 1;
	searchBox.setBounds (bar.removeFromLeft (twoRows ? bar.getWidth() - loadKeys * (keyWidth + gap) : wide (370)));
	bar.removeFromLeft (gap);
	loadAButton.setBounds (bar.removeFromLeft (keyWidth));
	bar.removeFromLeft (gap);
	if (loadBButton.isVisible())
		loadBButton.setBounds (bar.removeFromLeft (keyWidth));
	bar.removeFromLeft (2 * gap);
	if (! twoRows)
		folderBar = bar;

	foldersButton.setBounds (folderBar.removeFromLeft (keyWidth));
	folderBar.removeFromLeft (gap);
	rescanButton.setBounds (folderBar.removeFromRight (wide (145)));
	folderBar.removeFromRight (gap);
	createButton.setBounds (folderBar.removeFromRight (wide (172)));
	folderBar.removeFromRight (gap);
	folderLabel.setBounds (folderBar);

	if (creatorLabel.isVisible())
	{
		area.removeFromTop (gap);
		auto strip = area.removeFromTop (high (108));
		if (cancelCreateButton.isVisible())
			cancelCreateButton.setBounds (strip.removeFromRight (keyWidth));
		creatorLabel.setBounds (strip);
	}

	area.removeFromTop (gap);

	// The tree a third of the width, when shown.
	if (folderTree.isVisible())
	{
		folderTree.setBounds (area.removeFromLeft (area.getWidth() * 3 / 10));
		area.removeFromLeft (gap);
	}
	// Rows a share of the library's height, so more fit; the header a little
	// higher, as a row of keys.
	table.setRowHeight (juce::jmax (1, getHeight() / 11));
	table.setHeaderHeight (table.getRowHeight() * 3 / 2);
	table.setBounds (area);
}

void StemLibrary::selectSetWithFile (const juce::File& file)
{
	for (int row = 0; row < (int) visibleSets.size(); ++row)
		for (const auto& f : visibleSets[(size_t) row]->files)
			if (f == file)
			{
				table.selectRow (row);
				table.scrollToEnsureRowIsOnscreen (row);
				return;
			}
}

void StemLibrary::setCreatorStatus (const juce::String& text, bool canCancel, bool isError)
{
	const auto show = text.isNotEmpty();
	creatorLabel.setText (text, juce::dontSendNotification);
	creatorLabel.setColour (juce::Label::textColourId, isError ? Theme::mute : Theme::textDim);
	if (show == creatorLabel.isVisible() && canCancel == cancelCreateButton.isVisible())
		return;
	creatorLabel.setVisible (show);
	cancelCreateButton.setVisible (show && canCancel);
	resized();
}

juce::Array<juce::File> StemLibrary::separableFiles (const juce::StringArray& paths) const
{
	const auto offered = [this] (const juce::File& f)
	{
		return shouldOfferForSeparation (f.getFullPathName().toStdString(), folder.getFullPathName().toStdString());
	};

	juce::Array<juce::File> found;
	for (const auto& path : paths)
	{
		const juce::File f (path);
		if (f.isDirectory())
		{
			for (const auto& entry : juce::RangedDirectoryIterator (f, true, "*", juce::File::findFiles))
				if (offered (entry.getFile()))
					found.add (entry.getFile());
		}
		else if (offered (f))
			found.add (f);
	}
	found.sort();
	return found;
}

bool StemLibrary::isInterestedInFileDrag (const juce::StringArray& files)
{
	for (const auto& path : files)
		if (juce::File (path).isDirectory() || isSeparableAudioFile (juce::File (path).getFileName().toStdString()))
			return true;
	return false;
}

void StemLibrary::filesDropped (const juce::StringArray& files, int, int)
{
	setDropHighlight (false);
	offerForStems (files);
}

void StemLibrary::offerForStems (const juce::StringArray& paths)
{
	if (paths.isEmpty())
		return;

	if (const auto found = separableFiles (paths); ! found.isEmpty())
	{
		if (onCreateStems)
			onCreateStems (found);
		return;
	}

	juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Create stems",
		juce::String ("No stereo file to separate.\n\n"
					  "Files already in the library folder are not separated: put the originals outside\n")
		+ folder.getFullPathName()
		+ "\nand drag them in from there.\n\nFormats: FLAC, WAV, MP3, AIFF, OGG, M4A, Opus.");
}

void StemLibrary::setDropHighlight (bool on)
{
	dropHighlight = on;
	repaint();
}

void StemLibrary::chooseFilesForStems()
{
	chooser = std::make_unique<juce::FileChooser> ("Split stereo files into stems",
		juce::File::getSpecialLocation (juce::File::userMusicDirectory),
		"*.flac;*.wav;*.mp3;*.aiff;*.aif;*.ogg;*.m4a;*.opus");
	chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
							  | juce::FileBrowserComponent::canSelectMultipleItems,
						  [this] (const juce::FileChooser& fc)
						  {
							  juce::StringArray paths;
							  for (const auto& f : fc.getResults())
								  paths.add (f.getFullPathName());
							  offerForStems (paths);
						  });
}
