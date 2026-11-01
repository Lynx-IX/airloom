#pragma once

#include <JuceHeader.h>
#include "LiveSet.h"
#include "Theme.h"

// The Settings window: Audio (the device), Look (theme, zoom), Layout (how the page behaves).
class SettingsPanel : public juce::Component
{
public:
    std::function<void (int)> onTheme;
    std::function<void (float)> onZoom;
    std::function<void (bool)> onRearrange, onDockFolded, onSamplesFolded;

    SettingsPanel (juce::AudioDeviceManager& devices, int tab, float zoom, bool rearrange, bool dockFolded, bool samplesFolded)
    {
        addAndMakeVisible (tabs);
        tabs.setTabBarDepth (38);
        tabs.setOutline (0);
        tabs.setIndent (0);

        auto* audio = new juce::AudioDeviceSelectorComponent (devices, 0, LiveSet::maxDeviceInputs, 0, 2, false, false, true, false);
        tabs.addTab ("Audio", Theme::panel, audio, true);

        auto* look = new juce::Component();
        look->addAndMakeVisible (themeLabel);
        look->addAndMakeVisible (themeBox);
        look->addAndMakeVisible (zoomLabel);
        look->addAndMakeVisible (zoomSlider);
        themeLabel.setText ("Theme", juce::dontSendNotification);
        for (int i = 0; i < Theme::numPalettes; ++i)
            themeBox.addItem (Theme::palettes[i].name, i + 1);
        themeBox.setSelectedId (Theme::currentPalette + 1, juce::dontSendNotification);
        themeBox.onChange = [this] { if (onTheme) onTheme (themeBox.getSelectedId() - 1); };
        zoomLabel.setText ("Zoom", juce::dontSendNotification);
        zoomSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        zoomSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 24);
        zoomSlider.setRange (60.0, 160.0, 10.0);
        zoomSlider.setValue (juce::roundToInt (zoom * 100.0f), juce::dontSendNotification);
        zoomSlider.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v)) + "%"; };
        zoomSlider.updateText();
        zoomSlider.onDragEnd = [this] { if (onZoom) onZoom ((float) zoomSlider.getValue() / 100.0f); };
        zoomSlider.onValueChange = [this] { if (! zoomSlider.isMouseButtonDown() && onZoom) onZoom ((float) zoomSlider.getValue() / 100.0f); };
        look->setComponentID ("look");
        tabs.addTab ("Look", Theme::panel, look, true);

        auto* layout = new juce::Component();
        for (auto* t : { &rearrangeToggle, &dockToggle, &samplesToggle })
            layout->addAndMakeVisible (t);
        rearrangeToggle.setButtonText ("Rearrange groups to fit the window (off: keep the layout, scroll sideways)");
        dockToggle.setButtonText ("Fold the LIVE dock away");
        samplesToggle.setButtonText ("Fold the samples list away");
        rearrangeToggle.setToggleState (rearrange, juce::dontSendNotification);
        dockToggle.setToggleState (dockFolded, juce::dontSendNotification);
        samplesToggle.setToggleState (samplesFolded, juce::dontSendNotification);
        rearrangeToggle.onClick = [this] { if (onRearrange) onRearrange (rearrangeToggle.getToggleState()); };
        dockToggle.onClick = [this] { if (onDockFolded) onDockFolded (dockToggle.getToggleState()); };
        samplesToggle.onClick = [this] { if (onSamplesFolded) onSamplesFolded (samplesToggle.getToggleState()); };
        layout->setComponentID ("layout");
        tabs.addTab ("Layout", Theme::panel, layout, true);

        tabs.setCurrentTabIndex (juce::jlimit (0, 2, tab), false);
        setSize (560, 460);
    }

    void paint (juce::Graphics& g) override   { g.fillAll (Theme::panel); }

    void resized() override
    {
        tabs.setBounds (getLocalBounds());

        // Each page sits under the tab bar: its controls go from its own top-left.
        const auto page = juce::Rectangle<int> (0, 0, getWidth(), getHeight() - tabs.getTabBarDepth()).reduced (28, 24);
        auto look = page;
        auto row = look.removeFromTop (30);
        themeLabel.setBounds (row.removeFromLeft (80));
        themeBox.setBounds (row.removeFromLeft (220));
        look.removeFromTop (14);
        row = look.removeFromTop (30);
        zoomLabel.setBounds (row.removeFromLeft (80));
        zoomSlider.setBounds (row.removeFromLeft (320));

        auto layout = page;
        for (auto* t : { &rearrangeToggle, &dockToggle, &samplesToggle })
        {
            t->setBounds (layout.removeFromTop (30));
            layout.removeFromTop (8);
        }
    }

private:
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
    juce::Label themeLabel, zoomLabel;
    juce::ComboBox themeBox;
    juce::Slider zoomSlider;
    juce::ToggleButton rearrangeToggle, dockToggle, samplesToggle;
};
