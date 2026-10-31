#pragma once

#include <JuceHeader.h>
#include "LiveSet.h"
#include "Theme.h"

// Side panel listing every sample loaded into the set, in folders: each sample under the
// folder it's in on disk, or one of the panel's own it's been moved to (the set keeps those;
// files are never moved). Click a folder to open or close it; right-click for its menu; drop
// samples on it to move them there. Files or folders can be dropped on the panel (or added
// with the buttons); samples are dragged from it onto channels and keys.
class SamplePool : public juce::Component,
                   public juce::FileDragAndDropTarget,
                   public juce::DragAndDropTarget,
                   private juce::ListBoxModel
{
public:
    static constexpr const char* dragPrefix = "samples:";

    explicit SamplePool (juce::AudioFormatManager& formats) : formatManager (formats)
    {
        addAndMakeVisible (addFilesButton);
        addAndMakeVisible (addFolderButton);
        addAndMakeVisible (list);
        addAndMakeVisible (emptyHint);

        addFilesButton.onClick = [this] { browse (false); };
        addFolderButton.onClick = [this] { browse (true); };

        list.setModel (this);
        list.setMultipleSelectionEnabled (true);
        list.setRowHeight (30);
        list.setColour (juce::ListBox::backgroundColourId, Theme::panel);

        emptyHint.setText ("Drop audio files or folders here", juce::dontSendNotification);
        emptyHint.setJustificationType (juce::Justification::centred);
        emptyHint.setColour (juce::Label::textColourId, Theme::textDim);
        emptyHint.setInterceptsMouseClicks (false, false);

        addAndMakeVisible (collapseButton);
        collapseButton.setTooltip ("Fold the samples list away (click the strip to bring it back)");
        collapseButton.onClick = [this] { if (onToggleCollapse) onToggleCollapse(); };
    }

    static constexpr int collapsedWidth = 36;
    std::function<void()> onToggleCollapse;

    // Folded: just a thin strip with the title, which still accepts dropped files.
    void setCollapsed (bool shouldCollapse)
    {
        collapsed = shouldCollapse;
        for (auto* c : std::initializer_list<juce::Component*> { &addFilesButton, &addFolderButton, &list })
            c->setVisible (! collapsed);
        emptyHint.setVisible (! collapsed && files.isEmpty());
        collapseButton.setButtonText (collapsed ? ">" : "<");
        resized();
        repaint();
    }

    bool isCollapsed() const   { return collapsed; }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (collapsed && e.mouseWasClicked() && onToggleCollapse)
            onToggleCollapse();
    }

    std::function<void (const juce::Array<juce::File>&)> onFilesAdded;
    std::function<void (const juce::Array<juce::File>&)> onRightClick;
    std::function<juce::String (const juce::File&)> describeUsage;   // e.g. "C 1, Nature"
    std::function<bool (const juce::File&)> isPlaying;               // on a channel that's playing
    std::function<void()> onFoldersChanged;                          // the folders changed: save the set

    // The set's folders, which the panel shows and changes (it must outlive the panel's use of it).
    void setFolders (LiveSet::PoolFolders* newFolders)   { folders = newFolders; rebuild(); }

    void setFiles (const juce::Array<juce::File>& newFiles)
    {
        files = newFiles;
        rebuild();
        emptyHint.setVisible (files.isEmpty() && ! collapsed);
        repaint();
    }

    void refreshUsage()     { list.repaint(); }

    juce::Array<juce::File> getSelectedFiles() const
    {
        juce::Array<juce::File> selected;
        for (int r = 0; r < (int) rows.size(); ++r)
            if (list.isRowSelected (r) && rows[(size_t) r].file >= 0)
                selected.add (files[rows[(size_t) r].file]);
        return selected;
    }

    // The folder a sample is shown in.
    juce::String folderOf (const juce::File& f) const
    {
        if (folders != nullptr)
            if (const auto it = folders->moved.find (f.getFullPathName()); it != folders->moved.end())
                return it->second;
        return f.getParentDirectory().getFileName();
    }

    juce::StringArray folderNames() const
    {
        juce::StringArray names;
        for (const auto& r : rows)
            if (r.file < 0)
                names.add (r.folder);
        return names;
    }

    void moveToFolder (const juce::Array<juce::File>& toMove, const juce::String& folder)
    {
        if (folders == nullptr || folder.trim().isEmpty())
            return;
        for (const auto& f : toMove)
        {
            if (f.getParentDirectory().getFileName() == folder && ! folders->made.contains (folder))
                folders->moved.erase (f.getFullPathName());   // back where it is on disk
            else
                folders->moved[f.getFullPathName()] = folder;
        }
        folders->closed.removeString (folder);   // show where they went
        changed();
    }

    void makeFolder (const juce::String& name)
    {
        if (folders == nullptr || name.trim().isEmpty())
            return;
        folders->made.addIfNotAlreadyThere (name.trim());
        changed();
    }

    // Audio files among the paths, searching dropped folders recursively.
    juce::Array<juce::File> collectAudio (const juce::StringArray& paths) const
    {
        const auto wildcard = formatManager.getWildcardForAllFormats();
        juce::Array<juce::File> found;

        for (auto& p : paths)
        {
            const juce::File f (p);
            if (f.isDirectory())
                found.addArray (f.findChildFiles (juce::File::findFiles, true, wildcard));
            else if (formatManager.findFormatForFileExtension (f.getFileExtension()) != nullptr)
                found.add (f);
        }

        return found;
    }

    static juce::Array<juce::File> filesFromDragDescription (const juce::var& description)
    {
        juce::Array<juce::File> result;
        const auto text = description.toString();

        if (text.startsWith (dragPrefix))
            for (auto& p : juce::StringArray::fromLines (text.fromFirstOccurrenceOf (dragPrefix, false, false)))
                if (p.isNotEmpty())
                    result.add (juce::File (p));

        return result;
    }

    bool isInterestedInFileDrag (const juce::StringArray&) override   { return true; }
    void fileDragEnter (const juce::StringArray&, int, int) override  { setDragOver (true); }
    void fileDragExit (const juce::StringArray&) override             { setDragOver (false); }

    void filesDropped (const juce::StringArray& paths, int, int) override
    {
        setDragOver (false);
        if (auto found = collectAudio (paths); ! found.isEmpty() && onFilesAdded)
            onFilesAdded (found);
    }

    // Samples dragged from the panel onto a folder move into it.
    bool isInterestedInDragSource (const SourceDetails& d) override
    {
        return d.description.toString().startsWith (dragPrefix);
    }

    void itemDragMove (const SourceDetails& d) override
    {
        const int row = headerRowAt (d.localPosition);
        if (row != dropFolderRow) { dropFolderRow = row; list.repaint(); }
    }

    void itemDragExit (const SourceDetails&) override
    {
        dropFolderRow = -1;
        list.repaint();
    }

    void itemDropped (const SourceDetails& d) override
    {
        const int row = headerRowAt (d.localPosition);
        dropFolderRow = -1;
        list.repaint();
        if (row >= 0)
            moveToFolder (filesFromDragDescription (d.description), rows[(size_t) row].folder);
    }

    void paint (juce::Graphics& g) override
    {
        Theme::drawSurface (g, getLocalBounds().toFloat(), Theme::panel, 8.0f);

        if (collapsed)
        {
            // Title running up the strip.
            juce::Graphics::ScopedSaveState state (g);
            g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi)
                                .translated (0.0f, (float) getHeight()));
            g.setColour (Theme::textDim);
            g.setFont (Theme::heading (13.0f, 0.14f));
            g.drawText ("SAMPLES  (" + juce::String (files.size()) + ")", juce::Rectangle<int> (0, 0, getHeight() - 50, getWidth()),
                        juce::Justification::centred);
            return;
        }

        g.setColour (Theme::text);
        g.setFont (Theme::heading (14.0f, 0.12f));
        g.drawText ("SAMPLES", titleArea, juce::Justification::centredLeft);

        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (14.0f));
        g.drawText (juce::String (files.size()), titleArea, juce::Justification::centredRight);
        g.drawFittedText ("Drag onto a channel, key or folder. Right-click for more.", hintArea, juce::Justification::centred, 2);

        if (dragOver)
        {
            g.setColour (Theme::accent);
            g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 6.0f, 2.0f);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (collapsed ? 4 : 8);

        if (collapsed)
        {
            collapseButton.setBounds (area.removeFromTop (28));
            return;
        }

        titleArea = area.removeFromTop (24);
        collapseButton.setBounds (titleArea.removeFromRight (26).reduced (0, 1));
        titleArea.removeFromRight (6);
        area.removeFromTop (6);

        auto buttons = area.removeFromTop (30);
        addFilesButton.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2).withTrimmedRight (3));
        addFolderButton.setBounds (buttons.withTrimmedLeft (3));
        area.removeFromTop (8);

        hintArea = area.removeFromBottom (34);
        list.setBounds (area);
        emptyHint.setBounds (area);
    }

private:
    // A line of the list: a folder's header (file -1) or one of its samples.
    struct Line { juce::String folder; int file = -1; int count = 0; bool playing = false; };

    void changed()
    {
        rebuild();
        if (onFoldersChanged)
            onFoldersChanged();
    }

    void rebuild()
    {
        std::map<juce::String, std::vector<int>> byFolder;   // sorted by folder name
        for (int i = 0; i < files.size(); ++i)
            byFolder[folderOf (files[i])].push_back (i);
        if (folders != nullptr)
            for (const auto& name : folders->made)
                byFolder[name];   // an empty folder of the panel's own still shows

        rows.clear();
        for (const auto& [folder, members] : byFolder)
        {
            rows.push_back ({ folder, -1, (int) members.size() });
            if (folders == nullptr || ! folders->closed.contains (folder))
                for (int i : members)
                    rows.push_back ({ folder, i });
        }
        list.updateContent();
        list.repaint();
    }

    bool folderPlaying (const juce::String& folder) const
    {
        if (! isPlaying)
            return false;
        for (const auto& f : files)
            if (folderOf (f) == folder && isPlaying (f))
                return true;
        return false;
    }

    int headerRowAt (juce::Point<int> posInPanel) const
    {
        const auto p = list.getLocalPoint (this, posInPanel);
        const int row = list.getRowContainingPosition (p.x, p.y);
        return juce::isPositiveAndBelow (row, (int) rows.size()) && rows[(size_t) row].file < 0 ? row : -1;
    }

    void showFolderMenu (const juce::String& folder)
    {
        if (folders == nullptr)
            return;
        const bool own = folders->made.contains (folder);
        const bool anyMoved = std::any_of (folders->moved.begin(), folders->moved.end(), [&] (const auto& m) { return m.second == folder; });

        juce::PopupMenu m;
        m.addSectionHeader (folder);
        m.addItem (1, "Rename...");
        m.addItem (2, "Remove folder (its samples go back to their own folders)", own || anyMoved);
        m.addSeparator();
        m.addItem (3, "New folder...");
        m.showMenuAsync (juce::PopupMenu::Options(), [this, folder] (int r)
        {
            if (r == 1 || r == 3)
            {
                auto* w = new juce::AlertWindow (r == 1 ? "Rename folder" : "New folder", {}, juce::MessageBoxIconType::NoIcon);
                w->addTextEditor ("name", r == 1 ? folder : juce::String());
                w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
                w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
                juce::Component::SafePointer<SamplePool> safe (this);
                w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w, r, folder] (int result)
                {
                    const auto name = w->getTextEditorContents ("name").trim();
                    if (auto* self = safe.getComponent(); self != nullptr && result == 1 && name.isNotEmpty())
                    {
                        if (r == 3)
                            self->makeFolder (name);
                        else
                            self->renameFolder (folder, name);
                    }
                }), true);
            }
            if (r == 2)
                removeFolder (folder);
        });
    }

    void renameFolder (const juce::String& from, const juce::String& to)
    {
        if (folders == nullptr || from == to)
            return;
        for (const auto& f : files)   // a folder from disk is renamed by moving its samples into one named so
            if (folderOf (f) == from)
                folders->moved[f.getFullPathName()] = to;
        if (folders->made.contains (from))
            folders->made.set (folders->made.indexOf (from), to);
        else
            folders->made.addIfNotAlreadyThere (to);
        if (folders->closed.contains (from))
            folders->closed.set (folders->closed.indexOf (from), to);
        changed();
    }

    void removeFolder (const juce::String& folder)
    {
        if (folders == nullptr)
            return;
        for (auto it = folders->moved.begin(); it != folders->moved.end();)
            it = it->second == folder ? folders->moved.erase (it) : std::next (it);
        folders->made.removeString (folder);
        folders->closed.removeString (folder);
        changed();
    }

    int getNumRows() override   { return (int) rows.size(); }

    void paintListBoxItem (int line, juce::Graphics& g, int width, int height, bool selected) override
    {
        if (! juce::isPositiveAndBelow (line, (int) rows.size()))
            return;
        const auto& r = rows[(size_t) line];

        if (r.file < 0)
        {
            // A folder: open/closed arrow, name, a dot while something in it plays, how many.
            const bool open = folders == nullptr || ! folders->closed.contains (r.folder);
            auto area = juce::Rectangle<int> (0, 0, width, height).reduced (4, 3);
            if (line == dropFolderRow)
            {
                g.setColour (Theme::accent.withAlpha (0.22f));
                g.fillRoundedRectangle (area.toFloat(), 4.0f);
            }
            const auto c = area.removeFromLeft (16).toFloat().getCentre();
            juce::Path arrow;
            if (open) arrow.addTriangle (c.x - 4.0f, c.y - 2.5f, c.x + 4.0f, c.y - 2.5f, c.x, c.y + 3.0f);
            else      arrow.addTriangle (c.x - 2.5f, c.y - 4.0f, c.x - 2.5f, c.y + 4.0f, c.x + 3.0f, c.y);
            g.setColour (Theme::textDim);
            g.fillPath (arrow);

            g.setFont (juce::FontOptions (13.0f));
            g.drawText (juce::String (r.count), area.removeFromRight (30), juce::Justification::centredRight);
            if (folderPlaying (r.folder))
            {
                g.setColour (Theme::accent);
                g.fillEllipse (area.removeFromRight (14).toFloat().withSizeKeepingCentre (6.0f, 6.0f));
            }
            g.setColour (Theme::text.withAlpha (0.9f));
            g.setFont (Theme::heading (12.5f, 0.08f));
            g.drawText (r.folder.toUpperCase(), area.withTrimmedLeft (4), juce::Justification::centredLeft, true);
            g.setColour (Theme::panelEdge);
            g.fillRect (4, height - 1, width - 8, 1);
            return;
        }

        const int row = r.file;
        if (selected)
        {
            g.setColour (Theme::accent.withAlpha (0.14f));
            g.fillRoundedRectangle (juce::Rectangle<float> ((float) width, (float) height).reduced (2.0f, 1.0f), 4.0f);
        }

        const auto usage = describeUsage ? describeUsage (files[row]) : juce::String();
        auto area = juce::Rectangle<int> (0, 0, width, height).reduced (6, 0);

        if (usage.isNotEmpty())
        {
            g.setColour (Theme::accent);
            g.setFont (juce::FontOptions (12.0f));
            g.drawText (usage, area.removeFromRight (juce::jmin (100, width / 3)), juce::Justification::centredRight, true);
        }

        g.setColour (usage.isNotEmpty() ? Theme::text : Theme::text.withAlpha (0.75f));
        g.setFont (juce::FontOptions (14.5f));
        g.drawText (files[row].getFileNameWithoutExtension(), area.withTrimmedLeft (14), juce::Justification::centredLeft, true);
    }

    void listBoxItemClicked (int line, const juce::MouseEvent& e) override
    {
        if (! juce::isPositiveAndBelow (line, (int) rows.size()))
            return;

        if (rows[(size_t) line].file < 0)
        {
            // A folder: a click opens or closes it, a right-click is its menu.
            const auto folder = rows[(size_t) line].folder;
            list.deselectRow (line);
            if (e.mods.isPopupMenu())
            {
                showFolderMenu (folder);
            }
            else if (folders != nullptr)
            {
                if (folders->closed.contains (folder)) folders->closed.removeString (folder);
                else                                   folders->closed.add (folder);
                changed();
            }
            return;
        }

        if (! e.mods.isPopupMenu())
            return;

        if (! list.isRowSelected (line))
            list.selectRow (line);

        if (onRightClick)
            onRightClick (getSelectedFiles());
    }

    juce::var getDragSourceDescription (const juce::SparseSet<int>& selectedRows) override
    {
        juce::StringArray paths;
        for (int i = 0; i < selectedRows.size(); ++i)
            if (juce::isPositiveAndBelow (selectedRows[i], (int) rows.size()) && rows[(size_t) selectedRows[i]].file >= 0)
                paths.add (files[rows[(size_t) selectedRows[i]].file].getFullPathName());

        if (paths.isEmpty())
            return {};
        return dragPrefix + paths.joinIntoString ("\n");
    }

    void browse (bool folder)
    {
        chooser = std::make_unique<juce::FileChooser> (folder ? "Add a folder of samples" : "Add samples",
                                                       juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                       formatManager.getWildcardForAllFormats());

        auto chooserFlags = juce::FileBrowserComponent::openMode
                          | (folder ? juce::FileBrowserComponent::canSelectDirectories
                                    : juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems);

        chooser->launchAsync (chooserFlags,[this] (const juce::FileChooser& fc)
        {
            juce::StringArray paths;
            for (auto& f : fc.getResults())
                paths.add (f.getFullPathName());

            if (auto found = collectAudio (paths); ! found.isEmpty() && onFilesAdded)
                onFilesAdded (found);
        });
    }

    void setDragOver (bool over)
    {
        if (dragOver != over) { dragOver = over; repaint(); }
    }

    juce::AudioFormatManager& formatManager;
    juce::Array<juce::File> files;
    std::vector<Line> rows;
    LiveSet::PoolFolders* folders = nullptr;
    int dropFolderRow = -1;

    juce::TextButton addFilesButton { "Add files" }, addFolderButton { "Add folder" };
    juce::ListBox list;
    juce::Label emptyHint;
    juce::Rectangle<int> titleArea, hintArea;
    std::unique_ptr<juce::FileChooser> chooser;
    bool dragOver = false, collapsed = false;
    juce::TextButton collapseButton { "<" };
};
