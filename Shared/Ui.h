// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once

// The shared LiveTest editor. It builds itself from the plugin's Section list, so every
// plugin in the suite has the same header, panel style, meters and light/dark modes as
// the rest of the Live series.

#include <juce_gui_basics/juce_gui_basics.h>
#include "Base.h"
#include "Theme.h"
#include "Faceplate.h"

namespace ltp
{
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    juce::Colour accentColour (int index);

    // SQ view skin: touch-screen style tiles (value inside, bar underneath) and lit buttons
    class SqLookAndFeel : public theme::LookAndFeel
    {
    public:
        void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
        void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    };

    // ---------------------------------------------------------------- controls
    class Knob : public juce::Component
    {
    public:
        Knob (const juce::String& name, juce::Colour accent);
        void attach (juce::AudioProcessorValueTreeState& state, const juce::String& paramID);
        void setSq (bool sq);
        void resized() override;
        juce::Slider slider;
        juce::Label label;
    private:
        std::unique_ptr<SliderAttachment> attachment;
        bool sqStyle = false;
    };

    class LabelledToggle : public juce::Component
    {
    public:
        LabelledToggle (const juce::String& name, juce::Colour accent);
        void attach (juce::AudioProcessorValueTreeState& state, const juce::String& paramID);
        void resized() override;
        juce::Label label;
        juce::TextButton button;
    private:
        std::unique_ptr<ButtonAttachment> attachment;
    };

    class LabelledCombo : public juce::Component
    {
    public:
        explicit LabelledCombo (const juce::String& name);
        void attach (juce::AudioProcessorValueTreeState& state, const juce::String& paramID);
        void resized() override;
        juce::Label label;
        juce::ComboBox box;
    private:
        std::unique_ptr<ComboAttachment> attachment;
    };

    // a row of buttons for a choice parameter (ratio buttons, attack/release steps...)
    class Segment : public juce::Component, public juce::SettableTooltipClient, private juce::AudioProcessorValueTreeState::Listener, private juce::AsyncUpdater
    {
    public:
        Segment (const juce::String& name, juce::Colour accent, juce::AudioProcessorValueTreeState& state, const juce::String& paramID);
        ~Segment() override;
        void resized() override;
        int preferredWidth() const;
        juce::Label label;
    private:
        void parameterChanged (const juce::String&, float) override { triggerAsyncUpdate(); }
        void handleAsyncUpdate() override;
        juce::AudioProcessorValueTreeState& state;
        juce::String id;
        juce::OwnedArray<juce::TextButton> buttons;
    };

    // ---------------------------------------------------------------- panel
    class Panel : public juce::Component
    {
    public:
        Panel (const Section& s, Processor& p, bool sq = false, bool vertical = false);
        void paint (juce::Graphics&) override;
        void resized() override;
        int preferredWidth() const;
        int preferredHeight() const;
        static constexpr int height = 122;
    private:
        bool sqStyle = false, vertical = false;
        juce::String title;
        int accentIndex = 0;
        juce::OwnedArray<juce::Component> controls;
        std::vector<int> widths;
        std::unique_ptr<juce::TextButton> power;
        std::unique_ptr<ButtonAttachment> powerAttachment;
    };

    // ---------------------------------------------------------------- meters & graphs
    class IoMeters : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        explicit IoMeters (Processor& p) : proc (p) {}
        void update();
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
    private:
        struct Channel { float level = -100.0f, peak = -100.0f; int hold = 0; };
        static void ballistics (Channel&, float);
        Processor& proc;
        Channel in, out;
    };

    class GrMeter : public juce::Component
    {
    public:
        explicit GrMeter (float maxDb) : maxReduction (maxDb) {}
        void setValue (float v) { if (std::abs (v - value) > 0.05f) { value = v; repaint(); } }
        void paint (juce::Graphics&) override;
    private:
        float maxReduction, value = 0.0f;
    };

    class CompGraph : public juce::Component
    {
    public:
        CompGraph (Processor& p, bool curve = true, bool hist = true) : proc (p), showCurve (curve), showHistory (hist) { history.assign (240, { -100.0f, 0.0f }); }
        void push (float inDb, float gr);
        void paint (juce::Graphics&) override;
    private:
        Processor& proc;
        bool showCurve = true, showHistory = true;
        std::vector<std::pair<float, float>> history;   // (input dBFS, GR dB)
        int writePos = 0;
        float dotIn = -100.0f;
    };

    class DynEqGraph : public juce::Component
    {
    public:
        explicit DynEqGraph (Processor& p) : proc (p) {}
        void paint (juce::Graphics&) override;
    private:
        Processor& proc;
    };

    class Header : public juce::Component
    {
    public:
        Header (Processor& p, std::function<void()> onTheme, std::function<void()> onView);
        void paint (juce::Graphics&) override;
        void resized() override;
        void setViewIsUnit (bool unit);
    private:
        Info info;
        juce::TextButton bypass { "BYPASS" }, themeBtn, viewBtn;
        std::unique_ptr<ButtonAttachment> bypassAttachment;
    };

    // ---------------------------------------------------------------- editor
    class Editor : public juce::AudioProcessorEditor, public juce::Timer
    {
    public:
        explicit Editor (Processor&);
        ~Editor() override;
        void paint (juce::Graphics&) override;
        void resized() override;
    private:
        void timerCallback() override;
        void toggleTheme();
        void toggleView();
        void buildView();
        int layoutPanels (juce::Rectangle<int> area, bool apply);
        bool isSqCompPage() const { return ! unitView && info.graph == Graph::Compressor; }

        static bool applyStoredTheme();

        Processor& proc;
        Info info;
        bool themeReady = applyStoredTheme();   // must run before any child bakes colours
        theme::LookAndFeel lnf;
        SqLookAndFeel sqLnf;
        juce::TooltipWindow tooltips { this, 600 };
        Header header;
        IoMeters io;
        std::unique_ptr<GrMeter> gr;
        std::unique_ptr<CompGraph> compGraph, histogram;
        std::unique_ptr<DynEqGraph> dynGraph;
        std::unique_ptr<FaceplateView> faceplate;
        std::unique_ptr<Panel> scPanel, parPanel;
        juce::OwnedArray<Panel> panels;
        std::vector<bool> panelNewRow;
        bool unitView = false;
        int frame = 0, bodyWidth = 600;
        static constexpr int scColW = 150, parColW = 170;
        static constexpr int headerH = 50, graphH = 190, meterW = 150, fxMeterW = 96, gap = 8;
    };
}
