// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once

// Common processor for every LiveTest plugin. A plugin supplies its parameters, a
// description of its control panels and a stereo DSP callback; the base handles buses,
// bypass crossfade, metering, state save/load and the shared editor.

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <functional>
#include "Dsp.h"

namespace ltp
{
    using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;

    // ---------------------------------------------------------------- UI description
    struct Ctl
    {
        enum Type { Knob, Toggle, Choice, Segment, Tap };
        Type type = Knob;
        juce::String paramId;   // empty for Tap
        juce::String label;     // shown above / on the control
        juce::String tooltip;
    };

    inline Ctl knob (const juce::String& id, const juce::String& label, const juce::String& tip = {}) { return { Ctl::Knob, id, label, tip }; }
    inline Ctl toggle (const juce::String& id, const juce::String& label, const juce::String& tip = {}) { return { Ctl::Toggle, id, label, tip }; }
    inline Ctl choice (const juce::String& id, const juce::String& label, const juce::String& tip = {}) { return { Ctl::Choice, id, label, tip }; }
    inline Ctl segment (const juce::String& id, const juce::String& label, const juce::String& tip = {}) { return { Ctl::Segment, id, label, tip }; }
    inline Ctl tapButton (const juce::String& tip = {}) { return { Ctl::Tap, {}, "TAP", tip }; }

    struct Section
    {
        // Extra = not part of the modeled unit (sidechain, parallel, FX output): in UNIT view these
        // stay as normal panels under the faceplate; in the SQ compressor page they become the
        // side-chain / parallel columns.
        enum Role { Normal, Extra };

        Section() = default;
        Section (juce::String t, int c, std::vector<Ctl> ctls, juce::String on = {}, bool row = false, Role r = Normal)
            : title (std::move (t)), colour (c), controls (std::move (ctls)), onParam (std::move (on)), newRow (row), role (r) {}
        juce::String title;
        int colour = 0;                 // index into the accent list (see Ui.cpp)
        std::vector<Ctl> controls;
        juce::String onParam;           // optional bool parameter shown as the panel's IN button
        bool newRow = false;            // start this panel on a new row
        Role role = Normal;
    };

    // ---------------------------------------------------------------- hardware (UNIT view) faceplates
    // A faceplate is a few rows of hardware-style controls, laid out automatically.
    struct FpItem
    {
        enum Kind { Knob, Stepped, Buttons, Push, Switch, Vu, Leds, Display, Tap, Gap };
        Kind kind = Knob;
        juce::String param, label;
        int size = 1;                   // knobs: 0 small, 1 medium, 2 large. Vu: 1 normal, 2 big
        juce::StringArray scale;        // knob scale labels (empty = 0..10)
        juce::String alt;               // Switch: label for the upper (on / second) position
    };

    inline FpItem fpKnob (const juce::String& id, const juce::String& label, int size = 1, juce::StringArray scale = {})
    { FpItem i; i.kind = FpItem::Knob; i.param = id; i.label = label; i.size = size; i.scale = std::move (scale); return i; }
    inline FpItem fpStepped (const juce::String& id, const juce::String& label, int size = 1)
    { FpItem i; i.kind = FpItem::Stepped; i.param = id; i.label = label; i.size = size; return i; }
    inline FpItem fpButtons (const juce::String& id, const juce::String& label)
    { FpItem i; i.kind = FpItem::Buttons; i.param = id; i.label = label; return i; }
    inline FpItem fpPush (const juce::String& id, const juce::String& label)
    { FpItem i; i.kind = FpItem::Push; i.param = id; i.label = label; return i; }
    inline FpItem fpSwitch (const juce::String& id, const juce::String& down, const juce::String& up)
    { FpItem i; i.kind = FpItem::Switch; i.param = id; i.label = down; i.alt = up; return i; }
    inline FpItem fpVu (const juce::String& label, int size = 1)
    { FpItem i; i.kind = FpItem::Vu; i.label = label; i.size = size; return i; }
    inline FpItem fpLeds (const juce::String& label)
    { FpItem i; i.kind = FpItem::Leds; i.label = label; return i; }
    inline FpItem fpDisplay (const juce::String& id, const juce::String& label)
    { FpItem i; i.kind = FpItem::Display; i.param = id; i.label = label; return i; }
    inline FpItem fpTap() { FpItem i; i.kind = FpItem::Tap; i.label = "TAP"; return i; }
    inline FpItem fpGap() { FpItem i; i.kind = FpItem::Gap; return i; }

    struct Faceplate
    {
        enum Finish { Paint, Brushed, Hammertone };
        enum KnobStyle { BlackSkirt, Silver, ChickenHead, Cream, Pointer };
        juce::String title, subtitle;
        juce::uint32 panel = 0xff2a2d31, ink = 0xffe8e4da, led = 0xffff3b2f;
        Finish finish = Paint;
        KnobStyle knobs = BlackSkirt;
        // optional second look selected by a choice parameter (e.g. FET 76 silver / black unit)
        juce::String altParam;
        juce::uint32 altPanel = 0, altInk = 0;
        Finish altFinish = Paint;
        KnobStyle altKnobs = BlackSkirt;
        std::vector<std::vector<FpItem>> rows;
        bool empty() const { return rows.empty(); }
    };

    enum class Graph { None, Compressor, DynEq };

    struct Info
    {
        juce::String name, sqName, modeledOn, version;
        bool dynamics = true;
        Graph graph = Graph::None;
        int editorWidth = 860;          // panels wrap at this width (meters column added on the right)
        float maxGrDb = 24.0f;
        bool hasUnitView = true;        // false when there is no hardware original (Dyn8)
    };

    // ---------------------------------------------------------------- parameter helpers
    namespace param
    {
        enum class Unit { None, Db, Dbu, Ms, Hz, Percent, Ratio, Seconds, Semitones, Plain };

        juce::String format (float v, Unit u, int decimals = -1);

        void addFloat (Layout& l, const juce::String& id, const juce::String& name, float min, float max, float def,
                       Unit unit, float skewCentre = -1.0f, float step = 0.0f, int decimals = -1);
        // custom value text
        void addFloatText (Layout& l, const juce::String& id, const juce::String& name, float min, float max, float def,
                           std::function<juce::String (float)> toText, float skewCentre = -1.0f, float step = 0.0f);
        void addChoice (Layout& l, const juce::String& id, const juce::String& name, const juce::StringArray& items, int def);
        void addBool (Layout& l, const juce::String& id, const juce::String& name, bool def);
    }

    // ---------------------------------------------------------------- processor base
    class Processor : public juce::AudioProcessor
    {
    public:
        explicit Processor (Layout layout);
        ~Processor() override = default;

        // ------------------------------------------- implemented by each plugin
        virtual Info getInfo() const = 0;
        virtual std::vector<Section> getSections() const = 0;
        virtual void prepareDsp (double sampleRate, int maxBlock) = 0;
        // always a 2-channel buffer (mono hosts are up/down-mixed by the base)
        virtual void processDsp (juce::AudioBuffer<float>& stereo) = 0;
        virtual void resetDsp() {}
        virtual double tailSeconds() const { return 0.0; }

        // graphs (called from the message thread; read params/atomics only)
        virtual float transferCurveDb (float inDbfs) const { return inDbfs; }        // compressor
        virtual float responseDb (float hz, bool withDynamics) const { (void) hz; (void) withDynamics; return 0.0f; }
        virtual void graphMarkers (std::vector<std::pair<float, float>>& freqGain) const { (void) freqGain; }
        virtual void crossoverFreqs (std::vector<float>& f) const { (void) f; }

        // UNIT view front panel. Empty = build a rack-style panel from the sections.
        virtual Faceplate getFaceplate() const { return {}; }

        // tap tempo (message thread) - sets the plugin's "bpm" parameter or its "timeL" ms parameter
        void tap();

        // ------------------------------------------- AudioProcessor
        const juce::String getName() const override { return getInfo().name; }
        bool acceptsMidi() const override { return false; }
        bool producesMidi() const override { return false; }
        bool isMidiEffect() const override { return false; }
        double getTailLengthSeconds() const override { return tailSeconds(); }
        int getNumPrograms() override { return 1; }
        int getCurrentProgram() override { return 0; }
        void setCurrentProgram (int) override {}
        const juce::String getProgramName (int) override { return "Default"; }
        void changeProgramName (int, const juce::String&) override {}

        bool isBusesLayoutSupported (const BusesLayout&) const override;
        void prepareToPlay (double sampleRate, int samplesPerBlock) override;
        void releaseResources() override {}
        void reset() override { resetDsp(); }
        void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
        using juce::AudioProcessor::processBlock;

        bool hasEditor() const override { return true; }
        juce::AudioProcessorEditor* createEditor() override;

        void getStateInformation (juce::MemoryBlock&) override;
        void setStateInformation (const void*, int) override;

        juce::AudioProcessorValueTreeState apvts;

        // ------------------------------------------- metering (audio -> UI)
        std::atomic<float> inPeakDb { -100.0f }, outPeakDb { -100.0f };
        std::atomic<float> grDb { 0.0f };             // max gain reduction since last read (positive dB)
        std::atomic<float> detectorDb { -100.0f };    // sidechain level (dBFS) for the transfer-curve dot
        std::array<std::atomic<float>, 8> bandGrDb {};   // per-band gain change (dB, signed) for Dyn8
        std::atomic<double> hostBpm { 120.0 };

        double getSampleRateSafe() const { return sr > 0.0 ? sr : 48000.0; }

    protected:
        float pv (const char* id) const { return apvts.getRawParameterValue (id)->load(); }
        int pi (const char* id) const { return (int) std::lround (pv (id)); }
        bool pb (const char* id) const { return pv (id) >= 0.5f; }

        void reportGr (float db) { float c = grDb.load(); while (db > c && ! grDb.compare_exchange_weak (c, db)) {} }
        void reportDetector (float db) { float c = detectorDb.load(); while (db > c && ! detectorDb.compare_exchange_weak (c, db)) {} }

        double sr = 0.0;

    private:
        juce::AudioBuffer<float> work, dry;
        Smoother bypassMix;
        double lastTapMs = 0.0;
        std::vector<double> tapIntervals;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Processor)
    };

    // adds the "bypass" parameter every plugin has
    inline void addCommon (Layout& l) { param::addBool (l, "bypass", "Bypass", false); }

    // ---------------------------------------------------------------- registration
    // Each plugin's .cpp ends with LTP_REGISTER(ClassName). In a plugin build it defines the
    // JUCE entry point; in the offline test build it adds the class to a registry instead.
    using Factory = std::function<juce::AudioProcessor*()>;
    std::vector<std::pair<juce::String, Factory>>& registry();
    struct Registrar { Registrar (const char* n, Factory f) { registry().push_back ({ n, std::move (f) }); } };
}

#if LTP_TEST_BUILD
  #define LTP_REGISTER(Cls) static ltp::Registrar ltpRegistrar_##Cls (#Cls, [] { return (juce::AudioProcessor*) new Cls(); });
#else
  #define LTP_REGISTER(Cls) juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new Cls(); }
#endif

// fills an Info with this plugin's identity from plugins.json (set per plugin by CMake)
#ifndef LTP_NAME
  #define LTP_NAME "LiveTest"
  #define LTP_SQ ""
  #define LTP_MODELED ""
#endif
#ifndef LTP_PLUGIN_VERSION
  #define LTP_PLUGIN_VERSION LTP_VERSION
#endif
#define LTP_BASE_INFO(i) (i).name = LTP_NAME; (i).sqName = LTP_SQ; (i).modeledOn = LTP_MODELED; (i).version = LTP_PLUGIN_VERSION;

#ifndef LTP_VERSION
  #define LTP_VERSION "0.0.0"
#endif
#ifndef LTP_SUITE_VERSION
  #define LTP_SUITE_VERSION "0.0.0"
#endif
