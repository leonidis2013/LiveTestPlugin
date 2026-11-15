// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

// Ensemble (SQ: Symphonic Chorus) - multi-voice ensemble chorus in the style of the
// classic string-machine ensembles: three delay voices per side, each moved by a slow
// sweep plus a faster vibrato, 120 degrees apart, for a lush "many players" sound.
// Two controls, like the SQ unit: FREQUENCY (typical setting just left of centre) and
// DEPTH (centre is a typical starting point).

#include "Fx.h"

using namespace ltp;

class Ensemble : public FxBase
{
public:
    Ensemble() : FxBase (createLayout()) {}

    static Layout createLayout()
    {
        using U = param::Unit;
        Layout l;
        addCommon (l);
        param::addFloat (l, "freq", "Frequency", 0.0f, 100.0f, 42.0f, U::Percent);
        param::addFloat (l, "depth", "Depth", 0.0f, 100.0f, 50.0f, U::Percent);
        addFxCommon (l);
        return l;
    }

    Info getInfo() const override
    {
        Info i; LTP_BASE_INFO (i)
        i.dynamics = false;
        i.editorWidth = 560;
        return i;
    }

    std::vector<Section> getSections() const override
    {
        return {
            { "SYMPHONIC", 3, { knob ("freq", "FREQUENCY", "Speed of the modulation; a typical setting is just left of centre"),
                                knob ("depth", "DEPTH", "Centre is a typical starting point") } },
            outputSection()
        };
    }

protected:
    void prepareFx (double sampleRate, int) override
    {
        for (auto& d : line) d.prepare ((int) (sampleRate * 0.04));
        depthS.prepare (sampleRate, 30.0f);
        depthS.reset (pv ("depth") * 0.01f);
    }

    void resetFx() override { for (auto& d : line) d.reset(); }

    void beginBlock() override
    {
        const float rate = 0.12f * std::pow (25.0f, pv ("freq") / 100.0f);   // 0.12 .. 3 Hz
        slow.setRate (sr, rate);
        fast.setRate (sr, rate * 6.3f);
        depthS.setTarget (pv ("depth") * 0.01f);
    }

    void processFrame (float inL, float inR, float& wetL, float& wetR) override
    {
        line[0].push (inL); line[1].push (inR);
        slow.advance(); fast.advance();
        const float depth = depthS.next();
        const float msToS = 0.001f * (float) sr;
        float out[2] = { 0.0f, 0.0f };
        for (int ch = 0; ch < 2; ++ch)
            for (int v = 0; v < 3; ++v)
            {
                const float ph = (float) v / 3.0f + (ch == 1 ? 1.0f / 6.0f : 0.0f);
                const float ms = 10.0f + depth * (4.5f * (0.5f + 0.5f * slow.value (Lfo::Sine, ph))
                                                + 0.55f * (0.5f + 0.5f * fast.value (Lfo::Sine, ph + 0.11f)));
                out[ch] += line[ch].read (ms * msToS);
            }
        wetL = out[0] * 0.5773f;
        wetR = out[1] * 0.5773f;
    }

private:
    DelayLine line[2];
    Lfo slow, fast;
    Smoother depthS;
};

LTP_REGISTER (Ensemble)
