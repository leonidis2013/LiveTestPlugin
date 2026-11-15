// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

// Tube Opto (SQ: OptTronik) - modeled on the tube electro-optical leveling amplifier.
//  - Feedback design: the light cell "sees" the output, so the ratio emerges from the loop
//    (about 3:1 in COMPRESS, much harder in LIMIT).
//  - PEAK REDUCTION is the sidechain gain (amount of compression), GAIN restores level.
//  - Optical cell with a ~10 ms attack and the famous two-stage release: about half of the
//    reduction recovers in ~60 ms, the rest over one to several seconds (memory).
//  - EMPHASIS shapes the sidechain's high-frequency sensitivity.
//  - UNIT A / B: two modeled hardware units - B has a longer cell memory and more tube colour.

#include "Comp.h"

using namespace ltp;

class TubeOpto : public CompBase
{
public:
    TubeOpto() : CompBase (createLayout()) {}

    static juce::String gainText (float v) { return juce::String ((int) std::lround (v)) + " (" + param::format (gainDbFor (v), param::Unit::Db) + ")"; }
    static float gainDbFor (float knob) { return knob * 0.4f - 10.0f; }      // 0..100 -> -10..+30 dB
    static float scGainFor (float pr) { return pr * 0.45f; }                  // 0..100 -> 0..45 dB of sidechain gain

    static Layout createLayout()
    {
        Layout l;
        addCommon (l);
        param::addChoice (l, "mode", "Mode", { "Compress", "Limit" }, 0);
        param::addFloat (l, "peakRed", "Peak Reduction", 0.0f, 100.0f, 40.0f, param::Unit::Plain, -1.0f, 0.1f, 0);
        param::addFloatText (l, "gain", "Gain", 0.0f, 100.0f, 40.0f, gainText, -1.0f, 0.1f);
        param::addFloat (l, "emphasis", "Emphasis", 0.0f, 100.0f, 0.0f, param::Unit::Plain, -1.0f, 0.1f, 0);
        param::addChoice (l, "unit", "Unit", { "A", "B" }, 0);
        addCompCommon (l);
        return l;
    }

    Info getInfo() const override
    {
        Info i; LTP_BASE_INFO (i)
        i.graph = Graph::Compressor;
        i.editorWidth = 760;
        i.maxGrDb = 20.0f;
        return i;
    }

    std::vector<Section> getSections() const override
    {
        return {
            { "LEVELING AMPLIFIER", 0, { segment ("mode", "MODE", "Compress (about 3:1) or Limit (much harder)"),
                                         knob ("peakRed", "PEAK REDUCTION", "Amount of compression"),
                                         knob ("gain", "GAIN", "Restores the level lost to compression") } },
            { "SIDECHAIN EMPHASIS & UNIT", 2, { knob ("emphasis", "EMPHASIS", "Makes the detector more sensitive to high frequencies"),
                                                segment ("unit", "UNIT", "Two modeled hardware units: B has a longer release memory and more tube colour") } },
            { "PARALLEL", 6, { knob ("mix", "PARALLEL", "Wet/dry blend: 100% = fully compressed") }, {}, false, Section::Extra }
        };
    }

    Faceplate getFaceplate() const override
    {
        Faceplate f;
        f.title = "TUBE LEVELING AMPLIFIER"; f.subtitle = "Electro-optical  -  SQ model: OptTronik";
        f.panel = 0xffa2a7ab; f.ink = 0xff141414; f.finish = Faceplate::Hammertone; f.knobs = Faceplate::Pointer; f.led = 0xffff4a3a;
        f.rows = { { fpKnob ("gain", "GAIN", 2, { "0", "20", "40", "60", "80", "100" }), fpVu ("GAIN REDUCTION", 2),
                     fpKnob ("peakRed", "PEAK REDUCTION", 2, { "0", "20", "40", "60", "80", "100" }) },
                   { fpSwitch ("mode", "COMPRESS", "LIMIT"), fpKnob ("emphasis", "EMPHASIS", 0, { "0", "25", "50", "75", "100" }),
                     fpSwitch ("unit", "UNIT A", "UNIT B") } };
        return f;
    }

    static constexpr float fixedThreshold = 0.0f;   // dBFS, before the sidechain gain
    static float ratioFor (int mode) { return mode == 0 ? 3.0f : 12.0f; }

protected:
    void prepareModel (double sampleRate) override
    {
        rms.set (sampleRate, 2.0f);
        for (auto& d : dc) d.prepare (sampleRate);
        lastEmph = -1.0f;
    }

    void resetModel() override { rms.reset(); fastGr = memory = 0.0f; yl = yr = 0.0f; emph.reset(); for (auto& d : dc) d.reset(); }

    void beginBlock() override
    {
        mode = pi ("mode");
        scGain = dbToGain (scGainFor (pv ("peakRed")));
        gain = dbToGain (gainDbFor (pv ("gain")));
        unitB = pi ("unit") == 1;
        attCoef = msCoef (10.0f, sr);
        fastRelCoef = msCoef (60.0f, sr);
        memChargeCoef = msCoef (unitB ? 1400.0f : 800.0f, sr);
        memReleaseCoef = msCoef (unitB ? 4000.0f : 1600.0f, sr);
        const float e = pv ("emphasis");
        if (std::abs (e - lastEmph) > 0.01f)
        {
            lastEmph = e;
            emph.set (Biquad::HighShelf, sr, 2500.0f, 0.7f, e * 0.15f);   // up to +15 dB HF sensitivity
        }
    }

    float processFrame (float& l, float& r, float scL, float scR) override
    {
        // feedback detection: the cell sees the previous output sample (so the input
        // sidechain filter doesn't apply to this model - EMPHASIS shapes the detector instead)
        juce::ignoreUnused (scL, scR);
        const float fb = 0.5f * (yl + yr);
        const float det = rms.process (emph.process (fb)) * 1.41421356f * scGain;
        const float over = gainToDb (det) - fixedThreshold;
        const float k = ratioFor (mode) - 1.0f;
        // soft knee on the feedback law
        const float target = k * (over > 4.0f ? over : over > -4.0f ? (over + 4.0f) * (over + 4.0f) / 16.0f : 0.0f);

        fastGr = target + (target > fastGr ? attCoef : fastRelCoef) * (fastGr - target);
        memory = fastGr + (fastGr > memory ? memChargeCoef : memReleaseCoef) * (memory - fastGr);
        const float gr = clampf (0.5f * (fastGr + memory), 0.0f, 40.0f);

        const float g = dbToGain (-gr);
        yl = l * g; yr = r * g;   // feeds the detector next sample
        const float drive = unitB ? 0.22f : 0.12f;
        l = dc[0].process (tubeStage (yl * gain, drive));
        r = dc[1].process (tubeStage (yr * gain, drive));
        return gr;
    }

    float staticGainDb (float in) const override
    {
        // steady state of the feedback loop: gr = (R-1)/R * over
        const float R = ratioFor (pi ("mode"));
        const float over = in + scGainFor (pv ("peakRed")) - fixedThreshold;
        return -std::max (0.0f, over) * (R - 1.0f) / R + gainDbFor (pv ("gain"));
    }

private:
    RmsDetector rms;
    Biquad emph;
    DcBlocker dc[2];
    float scGain = 1, gain = 1, attCoef = 0, fastRelCoef = 0, memChargeCoef = 0, memReleaseCoef = 0;
    float fastGr = 0, memory = 0, yl = 0, yr = 0, lastEmph = -1;
    int mode = 0;
    bool unitB = false;
};

LTP_REGISTER (TubeOpto)
