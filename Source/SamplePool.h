#pragma once

#include <JuceHeader.h>
#include "Theme.h"

// Side panel listing every sample loaded into the set. Files or folders can be
// dropped on it (or added with the buttons); samples are dragged from it onto
// channels and keys.
class SamplePool : public juce::Component,
                   public juce::FileDragAndDropTarget,
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

    void setFiles (const juce::Array<juce::File>& newFiles)
    {
        files = newFiles;
        list.updateContent();
        list.repaint();
        emptyHint.setVisible (files.isEmpty() && ! collapsed);
        repaint();
    }

    void refreshUsage()     { list.repaint(); }

    juce::Array<juce::File> getSelectedFiles() const
    {
        juce::Array<juce::File> selected;
        for (int i = 0; i < files.size(); ++i)
            if (list.isRowSelected (i))
                selected.add (files[i]);
        return selected;
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
        g.drawFittedText ("Drag onto a channel or key. Right-click for more.", hintArea, juce::Justification::centred, 2);

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
    int getNumRows() override   { return files.size(); }

    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        if (! juce::isPositiveAndBelow (row, files.size()))
            return;

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
        g.drawText (files[row].getFileNameWithoutExtension(), area, juce::Justification::centredLeft, true);
    }

    void listBoxItemClicked (int row, const juce::MouseEvent& e) override
    {
        if (! e.mods.isPopupMenu())
            return;

        if (! list.isRowSelected (row))
            list.selectRow (row);

        if (onRightClick)
            onRightClick (getSelectedFiles());
    }

    juce::var getDragSourceDescription (const juce::SparseSet<int>& rows) override
    {
        juce::StringArray paths;
        for (int i = 0; i < rows.size(); ++i)
            if (juce::isPositiveAndBelow (rows[i], files.size()))
                paths.add (files[rows[i]].getFullPathName());

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

    juce::TextButton addFilesButton { "Add files" }, addFolderButton { "Add folder" };
    juce::ListBox list;
    juce::Label emptyHint;
    juce::Rectangle<int> titleArea, hintArea;
    std::unique_ptr<juce::FileChooser> chooser;
    bool dragOver = false, collapsed = false;
    juce::TextButton collapseButton { "<" };
};
