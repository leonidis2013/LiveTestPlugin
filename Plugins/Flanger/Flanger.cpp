// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

// Flanger (SQ: Flanger) - three flavours:
//   AMBIENT - gentle, wide sweep through longer delays, softer feedback
//   VINTAGE - tape flanging: very short delays sweeping close to zero, negative feedback
//   WILD    - big sweep range with strong regeneration
// STEREO SPLIT runs the right side's LFO the opposite way, STEREO SPREAD offsets the two
// sides. LFO shapes: Sine, Rectified Sine, Inverted Rectified Sine, Triangle. MANUAL
// overrides the LFO so you can sweep by hand. The flanger output already contains the
// dry/delayed comb, so 100% MIX still flanges (as an SQ FX return does).

#include "Fx.h"

using namespace ltp;

class Flanger : public FxBase
{
public:
    Flanger() : FxBase (createLayout()) {}

    static Layout createLayout()
    {
        using U = param::Unit;
        Layout l;
        addCommon (l);
        param::addChoice (l, "model", "Model", { "Ambient", "Vintage", "Wild" }, 1);
        param::addBool (l, "split", "Stereo Split", false);
        param::addBool (l, "spread", "Stereo Spread", false);
        param::addChoice (l, "lfoType", "LFO", { "Sine", "Rect Sine", "Inv Rect", "Triangle" }, 3);
        param::addBool (l, "manual", "Manual", false);
        param::addFloat (l, "manualPos", "Manual Position", 0.0f, 100.0f, 50.0f, U::Percent);
        param::addFloat (l, "speed", "Speed", 0.0f, 5.0f, 0.3f, U::Hz, 0.5f);
        param::addFloat (l, "depth", "Depth", 0.0f, 100.0f, 70.0f, U::Percent);
        param::addFloat (l, "regen", "Regenerate", 0.0f, 100.0f, 45.0f, U::Percent);
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
            { "FLANGER", 2, { segment ("model", "MODEL"),
                              knob ("speed", "SPEED", "0 = LFO stopped"),
                              knob ("depth", "DEPTH", "Min: tight flange zone. Max: sweeps right through it"),
                              knob ("regen", "REGENERATE", "Feedback: from barely there to aggressive"),
                              choice ("lfoType", "LFO") } },
            { "STEREO & MANUAL", 3, { toggle ("split", "SPLIT", "Separate LFOs sweeping in opposite directions"),
                                      toggle ("spread", "SPREAD", "Offsets the left and right voices"),
                                      toggle ("manual", "MANUAL", "Overrides the LFO: sweep with POSITION"),
                                      knob ("manualPos", "POSITION") }, {}, true },
            outputSection()
        };
    }

    struct Model { float minMs, maxMs, fbSign, fbMax, sat; };
    static Model modelFor (int i)
    {
        static const Model m[] = { { 1.2f, 12.0f, 1.0f, 0.7f, 0.0f },     // Ambient
                                   { 0.08f, 6.0f, -1.0f, 0.88f, 0.15f },  // Vintage (tape)
                                   { 0.05f, 11.0f, 1.0f, 0.95f, 0.05f } }; // Wild
        return m[juce::jlimit (0, 2, i)];
    }

protected:
    void prepareFx (double sampleRate, int) override
    {
        for (auto& d : line) d.prepare ((int) (sampleRate * 0.03));
        manualS.prepare (sampleRate, 60.0f);
        manualS.reset (pv ("manualPos") * 0.01f);
    }

    void resetFx() override { for (auto& d : line) d.reset(); fbState[0] = fbState[1] = 0.0f; for (auto& t : tone) t.reset(); }

    void beginBlock() override
    {
        model = modelFor (pi ("model"));
        split = pb ("split"); spread = pb ("spread"); manual = pb ("manual");
        manualS.setTarget (pv ("manualPos") * 0.01f);
        static const Lfo::Shape shapes[] = { Lfo::Sine, Lfo::Rectified, Lfo::InvRectified, Lfo::Triangle };
        shape = shapes[juce::jlimit (0, 3, pi ("lfoType"))];
        lfo.setRate (sr, pv ("speed"));
        depth = pv ("depth") * 0.01f;
        fb = model.fbSign * model.fbMax * pv ("regen") * 0.01f;
        if (pi ("model") == 0) for (auto& t : tone) t.set (sr, 9000.0f); else for (auto& t : tone) t.set (sr, 18000.0f);
    }

    void processFrame (float inL, float inR, float& wetL, float& wetR) override
    {
        lfo.advance();
        const float man = manualS.next();
        const float in[2] = { inL, inR };
        float out[2];
        for (int ch = 0; ch < 2; ++ch)
        {
            float pos;
            if (manual) pos = man;
            else
            {
                float v = lfo.value (shape);
                if (ch == 1 && split) v = -v;
                pos = 0.5f + 0.5f * depth * v;
            }
            if (ch == 1 && spread) pos = clampf (pos + 0.12f, 0.0f, 1.0f);
            const float ms = model.minMs * std::pow (model.maxMs / model.minMs, clampf (pos, 0.0f, 1.0f));
            const float delayed = line[ch].read (ms * 0.001f * (float) sr);
            float w = in[ch] + fb * fbState[ch];
            if (model.sat > 0.0f) w = softSat (w, model.sat);
            line[ch].push (w);
            fbState[ch] = delayed;
            out[ch] = tone[ch].lp (0.5f * (in[ch] + delayed));
        }
        wetL = out[0]; wetR = out[1];
    }

private:
    DelayLine line[2];
    OnePole tone[2];
    Lfo lfo;
    Smoother manualS;
    Model model = modelFor (1);
    Lfo::Shape shape = Lfo::Triangle;
    float depth = 0.7f, fb = 0.0f, fbState[2] {};
    bool split = false, spread = false, manual = false;
};

LTP_REGISTER (Flanger)
