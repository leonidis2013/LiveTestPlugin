// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once

// Timing controls shared by the stereo delays (SQ: Stereo Tap Delay, Bucket Brigade):
// BPM mode (note values incl. dotted/triplet) or MS mode (direct times), L/R LINK,
// a TAP button, LOCAL/GLOBAL tempo (global = follow the host's tempo) and the 2.7 s limit.

#include "Fx.h"

namespace ltp::delaytiming
{
    constexpr float maxMs = 2700.0f;

    inline juce::String bpmText (float v) { return juce::String (v, 1) + " BPM"; }

    // input filters that read "Off" at their end stops
    inline void addInputFilters (Layout& l)
    {
        param::addFloatText (l, "inHp", "Input HP", 20.0f, 2000.0f, 20.0f,
                             [] (float v) { return v <= 20.5f ? juce::String ("Off") : param::format (v, param::Unit::Hz); }, 200.0f);
        param::addFloatText (l, "inLp", "Input LP", 1000.0f, 20000.0f, 20000.0f,
                             [] (float v) { return v >= 19900.0f ? juce::String ("Off") : param::format (v, param::Unit::Hz); }, 6000.0f);
    }

    inline void addParams (Layout& l, float defL = 375.0f, float defR = 500.0f)
    {
        param::addChoice (l, "timeMode", "Time Mode", { "BPM", "MS" }, 0);
        param::addFloatText (l, "bpm", "Tempo", 40.0f, 300.0f, 120.0f, bpmText, -1.0f, 0.1f);
        param::addChoice (l, "tempoSrc", "Tempo Source", { "Local", "Global" }, 0);
        param::addChoice (l, "noteL", "Note L", FxBase::noteNamesPublic(), 6);
        param::addChoice (l, "noteR", "Note R", FxBase::noteNamesPublic(), 4);
        param::addFloat (l, "timeL", "Time L", 1.0f, maxMs, defL, param::Unit::Ms, 400.0f, 0.1f);
        param::addFloat (l, "timeR", "Time R", 1.0f, maxMs, defR, param::Unit::Ms, 400.0f, 0.1f);
        param::addBool (l, "link", "Link", false);
    }

    inline Section section (int colour)
    {
        return { "TIME", colour,
                 { segment ("timeMode", "MODE", "BPM: note values from the tempo. MS: times in milliseconds"),
                   knob ("bpm", "TEMPO"),
                   segment ("tempoSrc", "TEMPO SOURCE", "Local: this plugin's tempo / TAP. Global: follow the host's tempo"),
                   tapButton ("Tap at least twice: sets the tempo in BPM mode, or Time L in MS mode"),
                   choice ("noteL", "NOTE L"), choice ("noteR", "NOTE R"),
                   knob ("timeL", "TIME L"), knob ("timeR", "TIME R"),
                   toggle ("link", "LINK", "Right side follows the left side") } };
    }

    // delay times in ms for the current settings (clamped to the 2.7 s maximum)
    inline std::pair<float, float> times (const juce::AudioProcessorValueTreeState& s, double hostBpm)
    {
        auto v = [&s] (const char* id) { return s.getRawParameterValue (id)->load(); };
        const bool link = v ("link") >= 0.5f;
        float l, r;
        if ((int) std::lround (v ("timeMode")) == 0)
        {
            const float bpm = (int) std::lround (v ("tempoSrc")) == 1 ? (float) hostBpm : v ("bpm");
            const float quarter = 60000.0f / std::max (20.0f, bpm);
            l = quarter * FxBase::noteBeatsPublic ((int) std::lround (v ("noteL")));
            r = link ? l : quarter * FxBase::noteBeatsPublic ((int) std::lround (v ("noteR")));
        }
        else
        {
            l = v ("timeL");
            r = link ? l : v ("timeR");
        }
        return { clampf (l, 1.0f, maxMs), clampf (r, 1.0f, maxMs) };
    }
}
