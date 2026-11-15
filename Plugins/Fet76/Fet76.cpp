// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

// FET 76 (SQ: Peak Limiter 76) - modeled on the late-1960s FET limiting amplifier.
//  - Fixed threshold: you drive INPUT into it and trim with OUTPUT, like the hardware.
//  - ATTACK / RELEASE knobs run 1..7 with 7 the fastest (20 us .. 800 us attack,
//    50 ms .. 1.1 s release), programme-dependent release.
//  - Ratio buttons 4/8/12/20 and ALL ("all buttons in": very hard, slightly negative slope,
//    odd attack lag and lots of distortion).
//  - UNIT: Silver (modern, cleaner) or Black (vintage, more harmonic colour).
//  - GAIN LINK ("rubber band"): Input and Output move inversely so level stays matched.

#include "Comp.h"

using namespace ltp;

class Fet76 : public CompBase, private juce::AudioProcessorValueTreeState::Listener, private juce::AsyncUpdater
{
public:
    Fet76() : CompBase (createLayout())
    {
        apvts.addParameterListener ("input", this);
        apvts.addParameterListener ("output", this);
        lastIn = pv ("input"); lastOut = pv ("output");
    }

    ~Fet76() override
    {
        cancelPendingUpdate();
        apvts.removeParameterListener ("input", this);
        apvts.removeParameterListener ("output", this);
    }

    static Layout createLayout()
    {
        using U = param::Unit;
        Layout l;
        addCommon (l);
        param::addFloat (l, "input", "Input", -12.0f, 36.0f, 0.0f, U::Db, -1.0f, 0.1f);
        param::addFloat (l, "output", "Output", -24.0f, 24.0f, 0.0f, U::Db, -1.0f, 0.1f);
        param::addFloat (l, "attack", "Attack", 1.0f, 7.0f, 3.0f, U::Plain, -1.0f, 0.01f, 1);
        param::addFloat (l, "release", "Release", 1.0f, 7.0f, 4.0f, U::Plain, -1.0f, 0.01f, 1);
        param::addChoice (l, "ratio", "Ratio", { "4", "8", "12", "20", "ALL" }, 0);
        param::addChoice (l, "unit", "Unit", { "Silver", "Black" }, 0);
        param::addBool (l, "link", "Gain Link", false);
        addCompCommon (l);
        return l;
    }

    Info getInfo() const override
    {
        Info i; LTP_BASE_INFO (i)
        i.graph = Graph::Compressor;
        i.editorWidth = 800;
        i.maxGrDb = 30.0f;
        return i;
    }

    std::vector<Section> getSections() const override
    {
        return {
            { "LIMITING AMPLIFIER", 0, { knob ("input", "INPUT", "Drives the signal into the fixed threshold"),
                                         knob ("output", "OUTPUT"),
                                         knob ("attack", "ATTACK", "1 = slowest (800 us), 7 = fastest (20 us)"),
                                         knob ("release", "RELEASE", "1 = slowest (1.1 s), 7 = fastest (50 ms)"),
                                         segment ("ratio", "RATIO") } },
            { "UNIT", 2, { segment ("unit", "MODEL", "Silver: modern, cleaner unit. Black: vintage unit with more harmonic colour"),
                           toggle ("link", "GAIN LINK", "Rubber-band mode: raising Input lowers Output by the same amount (and vice versa)") } },
            sidechainSection()
        };
    }

    // ------------------------------------------------------------ static model helpers
    struct RatioModel { float ratio, threshDb, kneeDb; };
    static RatioModel ratioModel (int idx)
    {
        switch (idx)
        {
            case 0: return { 4.0f, -24.0f, 7.0f };
            case 1: return { 8.0f, -22.0f, 5.0f };
            case 2: return { 12.0f, -20.5f, 4.0f };
            case 3: return { 20.0f, -19.0f, 3.0f };
            default: return { 100.0f, -23.0f, 2.0f };   // ALL: steeper than limiting (see below)
        }
    }

    static float curveGrDb (float drivenDb, int ratioIdx)
    {
        const auto m = ratioModel (ratioIdx);
        float gr = -compressorGainDb (drivenDb, m.threshDb, m.ratio, m.kneeDb);
        if (ratioIdx == 4) gr += 0.12f * std::max (0.0f, drivenDb - m.threshDb);   // output falls slightly as input rises
        return gr;
    }

protected:
    void prepareModel (double sampleRate) override
    {
        dc[0].prepare (sampleRate); dc[1].prepare (sampleRate);
        sustainCoef = msCoef (1500.0f, sampleRate);
        tone[0].set (Biquad::HighShelf, sampleRate, 6000.0f, 0.7f, 0.0f);
        tone[1] = tone[0];
    }

    void resetModel() override { gr = 0.0f; sustain = 0.0f; lag = 0.0f; for (auto& d : dc) d.reset(); for (auto& t : tone) t.reset(); }

    void beginBlock() override
    {
        inGain = dbToGain (pv ("input"));
        outGain = dbToGain (pv ("output"));
        ratioIdx = pi ("ratio");
        black = pi ("unit") == 1;
        const float a = pv ("attack"), rl = pv ("release");
        float attackMs = 0.8f * std::pow (0.02f / 0.8f, (a - 1.0f) / 6.0f);
        float releaseMs = 1100.0f * std::pow (50.0f / 1100.0f, (rl - 1.0f) / 6.0f);
        if (ratioIdx == 4) { attackMs *= 1.6f; releaseMs *= 0.7f; }
        attCoef = msCoef (attackMs, sr);
        relMs = releaseMs;
        const float toneDb = black ? 0.8f : 0.0f;
        if (std::abs (toneDb - lastTone) > 0.01f)
        {
            lastTone = toneDb;
            for (auto& t : tone) t.set (Biquad::HighShelf, sr, 6000.0f, 0.7f, toneDb);
        }
    }

    float processFrame (float& l, float& r, float scL, float scR) override
    {
        l *= inGain; r *= inGain;
        const float drivenDb = gainToDb (linkPeak (scL, scR) * inGain);
        const float target = curveGrDb (drivenDb, ratioIdx);

        if (target > gr)
        {
            // ALL mode: attack lags a little before biting
            if (ratioIdx == 4) lag = std::min (1.0f, lag + 1.0f / (0.0003f * (float) sr));
            const float c = ratioIdx == 4 ? lerp (0.999f, attCoef, lag) : attCoef;
            gr = target + c * (gr - target);
        }
        else
        {
            lag = 0.0f;
            // programme dependent: longer, heavier compression releases more slowly
            const float ms = relMs * (1.0f + 1.5f * sustain);
            gr = target + std::exp (-1.0f / (ms * 0.001f * (float) sr)) * (gr - target);
        }
        const float s = clampf (gr / 12.0f, 0.0f, 1.0f);
        sustain = s + sustainCoef * (sustain - s);

        const float g = dbToGain (-gr);
        l *= g; r *= g;

        // FET + output stage colour, rising with gain reduction
        float drive = (black ? 0.10f : 0.04f) + gr * (black ? 0.010f : 0.006f);
        if (ratioIdx == 4) drive += 0.12f + gr * 0.01f;
        drive = std::min (drive, 0.8f);
        l = dc[0].process (tubeStage (l, drive * 0.6f));
        r = dc[1].process (tubeStage (r, drive * 0.6f));
        l = softSat (tone[0].process (l), drive * 0.3f) * outGain;
        r = softSat (tone[1].process (r), drive * 0.3f) * outGain;
        return gr;
    }

    float staticGainDb (float in) const override
    {
        const float inDb = pv ("input");
        return inDb - curveGrDb (in + inDb, pi ("ratio")) + pv ("output");
    }

private:
    // rubber band: whichever of Input/Output the user moved, move the other the opposite way
    void parameterChanged (const juce::String&, float) override { triggerAsyncUpdate(); }
    void handleAsyncUpdate() override
    {
        const float in = pv ("input"), out = pv ("output");
        const float dIn = in - lastIn, dOut = out - lastOut;
        if (pb ("link") && ! adjusting)
        {
            adjusting = true;
            auto set = [this] (const char* id, float v)
            {
                auto* p = apvts.getParameter (id);
                p->beginChangeGesture();
                p->setValueNotifyingHost (p->convertTo0to1 (v));
                p->endChangeGesture();
            };
            if (std::abs (dIn) > 1.0e-4f) set ("output", out - dIn);
            else if (std::abs (dOut) > 1.0e-4f) set ("input", in - dOut);
            adjusting = false;
        }
        lastIn = pv ("input"); lastOut = pv ("output");
    }

    DcBlocker dc[2];
    Biquad tone[2];
    float inGain = 1, outGain = 1, attCoef = 0, relMs = 300, gr = 0, sustain = 0, sustainCoef = 0, lag = 0, lastTone = -1;
    int ratioIdx = 0;
    bool black = false;
    float lastIn = 0, lastOut = 0;
    bool adjusting = false;
};

LTP_REGISTER (Fet76)
