// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once

// UNIT view: draws a hardware-style front panel from a Faceplate spec - knobs with printed
// scales, stepped selectors, lit push-button rows, bat switches, needle VU meters, LED
// ladders and value displays. Original artwork in the spirit of each unit type.

#include <juce_gui_basics/juce_gui_basics.h>
#include "Base.h"

namespace ltp
{
    class HwLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        HwLookAndFeel();
        void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
        juce::Font getSliderPopupFont (juce::Slider&) override;
        int getSliderPopupPlacement (juce::Slider&) override;
    };

    class FaceplateView : public juce::Component,
                          private juce::AudioProcessorValueTreeState::Listener,
                          private juce::AsyncUpdater
    {
    public:
        FaceplateView (Processor& p, Faceplate spec);
        ~FaceplateView() override;

        int preferredWidth() const { return prefW; }
        int preferredHeight() const { return prefH; }
        void setMinWidth (int w) { prefW = std::max (prefW, w); }

        void update (float grDb);   // called by the editor's timer
        void paint (juce::Graphics&) override;
        void resized() override;

        // builds a rack-style faceplate from the plugin's sections (used when a plugin has none)
        static Faceplate fromSections (const Processor& p);

    private:
        struct Placed
        {
            FpItem item;
            juce::Rectangle<int> area;           // whole item incl. label
            juce::Component* main = nullptr;     // slider / button / meter
            std::vector<juce::Button*> group;    // Buttons kind
            juce::StringArray choices;
        };

        void parameterChanged (const juce::String&, float) override { triggerAsyncUpdate(); }
        void handleAsyncUpdate() override;
        void applyLook();
        void renderBackground();
        void layout();
        static juce::Point<int> naturalSize (const FpItem&, const juce::StringArray& choices);

        Processor& proc;
        Faceplate spec;
        HwLookAndFeel hwLnf;
        std::vector<Placed> placed;
        juce::OwnedArray<juce::Component> owned;
        std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> sliderAtt;
        std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> buttonAtt;
        juce::StringArray listened;
        juce::Image bg;
        juce::Colour panel, ink;
        Faceplate::Finish finish = Faceplate::Paint;
        Faceplate::KnobStyle knobs = Faceplate::BlackSkirt;
        int prefW = 600, prefH = 300;
        bool altActive = false;
        static constexpr int ear = 30, titleH = 46, rowGap = 10, itemGap = 18;
    };
}
