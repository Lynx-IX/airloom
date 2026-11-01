#pragma once

#include <JuceHeader.h>
#include "Theme.h"

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#endif

// Windows draws Airloom's title bars; on Windows 11 they can take the theme's colours (a shade
// darker than the window), and on 10 they at least go dark. Include last: it brings in windows.h.
namespace TitleBar
{
    inline void tint (juce::Component& window)
    {
       #if JUCE_WINDOWS
        auto* hwnd = (HWND) window.getWindowHandle();
        if (hwnd == nullptr)
            return;

        using SetAttribute = HRESULT (WINAPI*) (HWND, DWORD, LPCVOID, DWORD);
        static auto* setAttribute = []() -> SetAttribute
        {
            if (auto* dwm = LoadLibraryW (L"dwmapi.dll"))
                return (SetAttribute) GetProcAddress (dwm, "DwmSetWindowAttribute");
            return nullptr;
        }();
        if (setAttribute == nullptr)
            return;

        auto colorRef = [] (juce::Colour c) { return (DWORD) RGB (c.getRed(), c.getGreen(), c.getBlue()); };
        const auto caption = Theme::background.interpolatedWith (juce::Colours::black, 0.35f);
        const BOOL dark = TRUE;
        const DWORD captionColour = colorRef (caption), textColour = colorRef (Theme::text), borderColour = colorRef (Theme::panelEdge);

        setAttribute (hwnd, 20, &dark, sizeof (dark));                         // DWMWA_USE_IMMERSIVE_DARK_MODE
        setAttribute (hwnd, 35, &captionColour, sizeof (captionColour));       // DWMWA_CAPTION_COLOR
        setAttribute (hwnd, 36, &textColour, sizeof (textColour));             // DWMWA_TEXT_COLOR
        setAttribute (hwnd, 34, &borderColour, sizeof (borderColour));         // DWMWA_BORDER_COLOR
       #else
        juce::ignoreUnused (window);
       #endif
    }

    // Every Airloom window open now (after a theme change).
    inline void tintAll()
    {
        auto& desktop = juce::Desktop::getInstance();
        for (int i = 0; i < desktop.getNumComponents(); ++i)
            if (auto* w = dynamic_cast<juce::ResizableWindow*> (desktop.getComponent (i)))
                tint (*w);
    }
}
