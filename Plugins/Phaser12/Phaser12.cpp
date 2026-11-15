// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

// Phaser 12 (SQ: MOO 12 Stage Phaser) - modeled on the 12-stage analogue synth phaser.
//  - PHASING STAGES: 6 (clean) or 12 (classic).
//  - RESONANT STAGE: which stage feeds the resonance loop back to the input (12 = classic);
//    MIX RESONANCE sets how much (classic around 5-8, full at 10).
//  - LFO RATE 0.1-10 Hz; Sine, Rectified (runs at twice the speed), Inverse Rectified,
//    Linear (triangle, the classic). MONO = in-phase on both sides, SPLIT = stereo swirl.
//  - DEPTH at minimum stops the LFO so OFFSET sweeps by hand; OFFSET shifts the range.

#include "Fx.h"

using namespace ltp;

class Phaser12 : public FxBase
{
public:
    Phaser12() : FxBase (createLayout()) {}

    static Layout createLayout()
    {
        using U = param::Unit;
        Layout l;
        addCommon (l);
        param::addChoice (l, "stages", "Phasing Stages", { "6", "12" }, 1);
        param::addFloat (l, "resStage", "Resonant Stage", 1.0f, 12.0f, 12.0f, U::Plain, -1.0f, 1.0f, 0);
        param::addFloat (l, "resonance", "Mix Resonance", 0.0f, 10.0f, 6.0f, U::Plain, -1.0f, 0.01f, 1);
        param::addFloat (l, "rate", "LFO Rate", 0.1f, 10.0f, 0.5f, U::Hz, 1.0f);
        param::addChoice (l, "lfoType", "LFO", { "Sine", "Rectified", "Inv Rectified", "Linear" }, 3);
        param::addChoice (l, "stereo", "Stereo", { "Mono", "Split" }, 1);
        param::addFloat (l, "depth", "Depth", 0.0f, 100.0f, 70.0f, U::Percent);
        param::addFloat (l, "offset", "Offset", 0.0f, 100.0f, 35.0f, U::Percent);
        addFxCommon (l);
        return l;
    }

    Info getInfo() const override
    {
        Info i; LTP_BASE_INFO (i)
        i.dynamics = false;
        i.editorWidth = 860;
        return i;
    }

    std::vector<Section> getSections() const override
    {
        return {
            { "PHASER", 0, { segment ("stages", "STAGES", "6 = clean, 12 = classic"),
                             knob ("resStage", "RES STAGE", "Which stage feeds the resonance (12 = classic)"),
                             knob ("resonance", "RESONANCE", "Classic around 5-8, full at 10") } },
            { "LFO", 4, { knob ("rate", "RATE"),
                          choice ("lfoType", "SHAPE"),
                          segment ("stereo", "STEREO", "Mono: in-phase phasing. Split: stereo swirl"),
                          knob ("depth", "DEPTH", "At minimum the LFO stops - sweep by hand with OFFSET"),
                          knob ("offset", "OFFSET", "Shifts the operating frequency range") }, {}, true },
            outputSection()
        };
    }

protected:
    void prepareFx (double sampleRate, int) override { offsetS.prepare (sampleRate, 40.0f); offsetS.reset (pv ("offset") * 0.01f); counter = 0; }

    void resetFx() override { for (auto& c : ap) for (auto& s : c) s.reset(); fbState[0] = fbState[1] = 0.0f; }

    void beginBlock() override
    {
        nStages = pi ("stages") == 1 ? 12 : 6;
        tap = juce::jlimit (1, nStages, (int) std::lround (pv ("resStage")));
        fb = pv ("resonance") * 0.085f;
        lfo.setRate (sr, pv ("rate"));
        static const Lfo::Shape shapes[] = { Lfo::Sine, Lfo::Rectified, Lfo::InvRectified, Lfo::Triangle };
        shape = shapes[juce::jlimit (0, 3, pi ("lfoType"))];
        split = pi ("stereo") == 1;
        depth = pv ("depth") * 0.01f;
        offsetS.setTarget (pv ("offset") * 0.01f);
    }

    void processFrame (float inL, float inR, float& wetL, float& wetR) override
    {
        lfo.advance();
        const float off = offsetS.next();
        if (--counter <= 0)
        {
            counter = 8;   // coefficient update rate
            const float fmin = 60.0f * std::pow (2.0f, off * 5.0f);
            for (int ch = 0; ch < 2; ++ch)
            {
                const float v = lfo.value (shape, ch == 1 && split ? 0.25f : 0.0f);
                const float s = depth <= 0.001f ? 0.0f : 0.5f + 0.5f * v * depth;
                coef[ch] = AllPass1::coefFor (sr, fmin * std::pow (24.0f, clampf (s, 0.0f, 1.0f)));
            }
        }
        const float in[2] = { inL, inR };
        float out[2];
        for (int ch = 0; ch < 2; ++ch)
        {
            float x = in[ch] + fastTanh (fb * fbState[ch]);
            for (int s = 0; s < nStages; ++s)
            {
                x = ap[ch][s].process (x, coef[ch]);
                if (s + 1 == tap) fbState[ch] = x;
            }
            out[ch] = 0.5f * (in[ch] + x);
        }
        wetL = out[0]; wetR = out[1];
    }

private:
    AllPass1 ap[2][12];
    Lfo lfo;
    Smoother offsetS;
    Lfo::Shape shape = Lfo::Triangle;
    float coef[2] {}, fbState[2] {}, fb = 0.5f, depth = 0.7f;
    int nStages = 12, tap = 12, counter = 0;
    bool split = true;
};

LTP_REGISTER (Phaser12)
