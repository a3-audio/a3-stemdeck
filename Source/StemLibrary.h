#pragma once

#include <JuceHeader.h>
#include "StemSet.h"
#include "TempoAnalysis.h"
#include "Session.h"
#include "TapFilter.h"

#include <set>

// Table of the complete stem sets found in a folder. Sets are loaded with the
// deck buttons, by double-click (first deck that is not playing) or by
// dragging a row onto a deck. Beside it, the library's folders as a tree: a
// chosen folder shows its sets and those below it (LibraryFolders.h).
class StemLibrary : public juce::Component,
					public juce::FileDragAndDropTarget,
					private juce::TableListBoxModel
{
public:
	explicit StemLibrary (juce::AudioFormatManager& formatManager);
	~StemLibrary() override;

	void setFolder (const juce::File& folder);
	const juce::File& getFolder() const { return folder; }

	// Selects and shows the set a file belongs to (after the creator made it).
	void selectSetWithFile (const juce::File& file);

	// The stem creator's line under the bar; empty hides it.
	void setCreatorStatus (const juce::String& text, bool canCancel, bool isError = false);

	// Looks up a set by the id carried in drag-and-drop descriptions.
	const StemSet* findSet (const juce::String& setId) const;

	// A set id as the session keeps it: relative to the library folder, so a
	// moved checkout or library still finds its sets; and back.
	juce::String storedId (const juce::String& setId) const;
	juce::String idFromStored (const juce::String& stored) const;

	// For a controller: move the selection up or down, and the set selected.
	void selectRelative (int delta);
	const StemSet* selectedSet() const;

	// Next / Prev on a deck: the set beside `setId` in its own folder, in
	// the order the table is sorted (the search aside); null at the folder's
	// ends or for a set not in the library.
	const StemSet* neighbourInFolder (const juce::String& setId, int direction) const;

	// The sets the table shows, in its order, by id: CONTINUE's list (PlayMode.h).
	std::vector<std::string> shownIds() const;

	// A set picked at random from those the search shows, not one of
	// `played`; when every one was, from all of them again. Null: none shown.
	const StemSet* randomVisibleSet (const std::set<juce::String>& played) const;

	// Sort order, search, selection and the chosen folder, for the session.
	void saveState (LibrarySession& state) const;
	void restoreState (const LibrarySession& state);

	void paint (juce::Graphics& g) override;
	void resized() override;

	std::function<void (const StemSet&, int deckIndex)> onLoadSet; // deckIndex -1: first free deck
	// A -bformat instance has one deck: one Load key, in that deck's colour (deck index 0).
	void setOneDeck (juce::Colour deckColour);
	std::function<void (const juce::File&)> onFolderChanged;
	std::function<std::optional<BeatGrid> (const StemSet&)> lookUpBeatGrid; // for the BPM column
	std::function<void (const juce::Array<juce::File>&)> onCreateStems;       // stereo files, dropped or chosen
	std::function<void()> onCancelCreation;
	std::function<void()> onOrderChanged;   // a rescan, a new sort or search

	bool isInterestedInFileDrag (const juce::StringArray& files) override;
	void fileDragEnter (const juce::StringArray&, int, int) override { setDropHighlight (true); }
	void fileDragExit (const juce::StringArray&) override { setDropHighlight (false); }
	void filesDropped (const juce::StringArray& files, int, int) override;

	void analysisChanged() { table.repaint(); }

private:
	enum Columns { nameColumn = 1, bpmColumn, stemsColumn, lengthColumn, artistColumn, albumColumn };

	class TapHeader;
	class FolderItem;

	double bpmOf (const StemSet& set) const;
	bool listsBefore (const StemSet& a, const StemSet& b) const;   // the table's sort

	int getNumRows() override;
	void paintRowBackground (juce::Graphics&, int row, int width, int height, bool selected) override;
	void paintCell (juce::Graphics&, int row, int columnId, int width, int height, bool selected) override;
	void cellDoubleClicked (int row, int columnId, const juce::MouseEvent&) override;
	void returnKeyPressed (int row) override;
	void sortOrderChanged (int columnId, bool forwards) override;
	juce::var getDragSourceDescription (const juce::SparseSet<int>& rows) override;
	bool pressHeldStill() const;
	void mouseUp (const juce::MouseEvent&) override;   // from the table's rows: a set drag is over

	static juce::String idFor (const StemSet& set) { return set.firstFile().getFullPathName(); }
	void applyFilter();
	void rebuildFolderTree();
	void folderTapped (FolderItem& item);
	void chooseFolder (const juce::String& relativeFolder);
	void showFolders (bool show);
	void updateFolderLabel();
	void loadSelected (int deckIndex);
	void chooseFilesForStems();
	void setDropHighlight (bool on);
	juce::Array<juce::File> separableFiles (const juce::StringArray& paths) const;
	void offerForStems (const juce::StringArray& paths);

	juce::AudioFormatManager& formatManager;
	juce::File folder;
	std::vector<StemSet> allSets;
	std::vector<const StemSet*> orderedSets;   // all of them, sorted
	std::vector<const StemSet*> visibleSets;   // those in the chosen folder the search matches
	juce::String chosenFolder;                 // relative to `folder`; "" all
	int sortColumn = artistColumn;
	bool sortForwards = true;

	juce::TextButton rescanButton { "Rescan" };
	juce::TextButton loadAButton { "Load to A" }, loadBButton { "Load to B" };
	juce::TextButton createButton { juce::String::fromUTF8 ("Create stems\xe2\x80\xa6") }, cancelCreateButton { "Cancel" };
	juce::Label folderLabel, creatorLabel;
	bool dropHighlight = false;
	juce::TextEditor searchBox;
	juce::TableListBox table { "Sets", this };
	juce::TextButton foldersButton { "FOLDERS" };
	juce::TreeView folderTree;
	std::unique_ptr<juce::TreeViewItem> rootFolderItem;
	TapFilter folderTaps;
	std::unique_ptr<juce::FileChooser> chooser;

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StemLibrary)
};
