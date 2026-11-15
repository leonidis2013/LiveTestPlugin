// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

// Chorus (SQ: Chorus) - stereo modulated-delay chorus.
// Three stereo-field switches that combine freely (all off = no stereo enhancement):
//   LFO SPLIT  - separate LFOs (90 degrees apart) for left and right
//   WIDE       - pushes the voices apart (can sound phasey)
//   MULTIVOICE - three voices per side (thicker, can sound a little muffled)
// RATE, DEPTH, Sine/Rectified LFO, and an AUTOPAN for the wet signal.

#include "Fx.h"

using namespace ltp;

class Chorus : public FxBase
{
public:
    Chorus() : FxBase (createLayout()) {}

    static Layout createLayout()
    {
        using U = param::Unit;
        Layout l;
        addCommon (l);
        param::addBool (l, "split", "LFO Split", false);
        param::addBool (l, "wide", "Wide", false);
        param::addBool (l, "multi", "MultiVoice", false);
        param::addFloat (l, "rate", "Rate", 0.05f, 5.0f, 0.8f, U::Hz, 0.8f);
        param::addFloat (l, "depth", "Depth", 0.0f, 100.0f, 40.0f, U::Percent);
        param::addChoice (l, "shape", "LFO", { "Sine", "Rectified" }, 0);
        param::addBool (l, "autopan", "Autopan", false);
        param::addFloat (l, "panDepth", "Pan Depth", 0.0f, 100.0f, 50.0f, U::Percent);
        param::addFloat (l, "panSpeed", "Pan Speed", 0.05f, 5.0f, 0.5f, U::Hz, 0.5f);
        addFxCommon (l);
        return l;
    }

    Info getInfo() const override
    {
        Info i; LTP_BASE_INFO (i)
        i.dynamics = false;
        i.editorWidth = 800;
        return i;
    }

    std::vector<Section> getSections() const override
    {
        return {
            { "CHORUS", 1, { knob ("rate", "RATE"), knob ("depth", "DEPTH"), segment ("shape", "LFO") } },
            { "STEREO FIELD", 3, { toggle ("split", "LFO SPLIT", "Separate LFOs for left and right"),
                                   toggle ("wide", "WIDE", "Pushes the voices apart - can sound phasey"),
                                   toggle ("multi", "MULTIVOICE", "Three voices per side - thicker, can sound muffled") } },
            { "AUTOPAN", 5, { toggle ("autopan", "AUTOPAN"), knob ("panDepth", "DEPTH"), knob ("panSpeed", "SPEED") }, {}, true },
            outputSection()
        };
    }

    Faceplate getFaceplate() const override
    {
        Faceplate f;
        f.title = "STEREO CHORUS"; f.subtitle = "SQ model: Chorus";
        f.panel = 0xff1f4c74; f.ink = 0xffeef5ff; f.knobs = Faceplate::Silver; f.led = 0xff7fe3ff;
        f.rows = { { fpKnob ("rate", "RATE", 2), fpKnob ("depth", "DEPTH", 2), fpSwitch ("shape", "SINE", "RECTIFIED"),
                     fpPush ("split", "LFO SPLIT"), fpPush ("wide", "WIDE"), fpPush ("multi", "MULTI") },
                   { fpPush ("autopan", "AUTOPAN"), fpKnob ("panDepth", "PAN DEPTH", 0), fpKnob ("panSpeed", "PAN SPEED", 0) } };
        return f;
    }

protected:
    void prepareFx (double sampleRate, int) override
    {
        for (auto& d : line) d.prepare ((int) (sampleRate * 0.05));
        depthS.prepare (sampleRate, 30.0f);
        depthS.reset (pv ("depth") * 0.01f);
    }

    void resetFx() override { for (auto& d : line) d.reset(); }

    void beginBlock() override
    {
        lfo.setRate (sr, pv ("rate"));
        depthS.setTarget (pv ("depth") * 0.01f);
        split = pb ("split"); wide = pb ("wide"); multi = pb ("multi");
        shape = pi ("shape") == 1 ? Lfo::Rectified : Lfo::Sine;
        autopan = pb ("autopan");
        panDepth = pv ("panDepth") * 0.01f;
        pan.setRate (sr, pv ("panSpeed"));
    }

    void processFrame (float inL, float inR, float& wetL, float& wetR) override
    {
        line[0].push (inL); line[1].push (inR);
        lfo.advance();
        const float depth = depthS.next();
        const float msToS = 0.001f * (float) sr;
        static constexpr float baseMs[3] = { 9.0f, 12.5f, 7.0f };
        const int voices = multi ? 3 : 1;
        float v[2] = { 0.0f, 0.0f };
        for (int ch = 0; ch < 2; ++ch)
        {
            const float chPhase = ch == 0 ? 0.0f : (wide ? 0.5f : split ? 0.25f : 0.0f);
            for (int k = 0; k < voices; ++k)
            {
                const float ph = chPhase + (float) k / 3.0f;
                const float sweep = 0.5f + 0.5f * lfo.value (shape, ph);
                const float d = (baseMs[k] + depth * 6.0f * sweep) * msToS;
                v[ch] += line[ch].read (d);
            }
            v[ch] /= std::sqrt ((float) voices);
        }
        wetL = v[0]; wetR = v[1];
        if (wide) { const float a = wetL, b = wetR; wetL = a - 0.35f * b; wetR = b - 0.35f * a; }
        if (autopan)
        {
            pan.advance();
            const float p = pan.value (Lfo::Sine) * panDepth;
            wetL *= std::sqrt (0.5f * (1.0f - p)) * 1.41421356f;
            wetR *= std::sqrt (0.5f * (1.0f + p)) * 1.41421356f;
        }
    }

private:
    DelayLine line[2];
    Lfo lfo, pan;
    Smoother depthS;
    Lfo::Shape shape = Lfo::Sine;
    float panDepth = 0.5f;
    bool split = false, wide = false, multi = false, autopan = false;
};

LTP_REGISTER (Chorus)
