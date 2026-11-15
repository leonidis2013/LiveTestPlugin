// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

// ADT (SQ: ADT Doubler) - modeled on tape-based artificial double tracking: a second tape
// machine replays the signal slightly late with varispeed wobble.
//  - SEPARATION: min = chorusing, mid = classic doubling, 3/4 = echo-chorus,
//    max = tape-loop slapback.
//  - THICKNESS: amount of tape flutter. DOUBLE or QUAD tracking. WIDE spreads the left and
//    right delays further apart. AUTOPAN sweeps the effect (wet only) across the image.

#include "Fx.h"

using namespace ltp;

class Adt : public FxBase
{
public:
    Adt() : FxBase (createLayout()) {}

    static Layout createLayout()
    {
        using U = param::Unit;
        Layout l;
        addCommon (l);
        param::addFloat (l, "separation", "Delay Separation", 0.0f, 100.0f, 45.0f, U::Percent);
        param::addFloat (l, "thickness", "Thickness", 0.0f, 100.0f, 35.0f, U::Percent);
        param::addChoice (l, "tracks", "Tracking", { "Double", "Quad" }, 0);
        param::addBool (l, "wide", "Wide", false);
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
        i.editorWidth = 760;
        return i;
    }

    std::vector<Section> getSections() const override
    {
        return {
            { "DOUBLER", 4, { knob ("separation", "SEPARATION", "Min: chorusing. Mid: classic doubling. 3/4: echo-chorus. Max: slapback"),
                              knob ("thickness", "THICKNESS", "Amount of tape flutter"),
                              segment ("tracks", "TRACKING"),
                              toggle ("wide", "WIDE", "Pushes the left and right delays further apart") } },
            { "AUTOPAN", 5, { toggle ("autopan", "AUTOPAN", "Sweeps the effect (wet only) across the stereo image"),
                              knob ("panDepth", "DEPTH"), knob ("panSpeed", "SPEED") } },
            outputSection()
        };
    }

    static float delayMs (float sep) { return 2.0f * std::pow (75.0f, sep / 100.0f); }   // 2 ms .. 150 ms

protected:
    void prepareFx (double sampleRate, int) override
    {
        for (auto& d : line) d.prepare ((int) (sampleRate * 0.35));
        for (int v = 0; v < 6; ++v)
        {
            wow[v].setRate (sampleRate, 0.6f + 0.17f * (float) v);
            wow[v].reset (0.13f * (float) v);
            flut[v].setRate (sampleRate, 5.1f + 0.6f * (float) v);
            flut[v].reset (0.29f * (float) v);
        }
        dS.prepare (sampleRate, 100.0f);
        dS.reset (delayMs (pv ("separation")) * 0.001f * (float) sampleRate);
    }

    void resetFx() override { for (auto& d : line) d.reset(); }

    void beginBlock() override
    {
        dS.setTarget (delayMs (pv ("separation")) * 0.001f * (float) sr);
        thick = pv ("thickness") * 0.01f;
        quad = pi ("tracks") == 1;
        wide = pb ("wide");
        autopan = pb ("autopan");
        panDepth = pv ("panDepth") * 0.01f;
        pan.setRate (sr, pv ("panSpeed"));
    }

    void processFrame (float inL, float inR, float& wetL, float& wetR) override
    {
        line[0].push (inL); line[1].push (inR);
        const float d = dS.next();
        static constexpr float quadMul[3] = { 0.62f, 1.0f, 1.45f };
        const int voices = quad ? 3 : 1;
        float out[2] = { 0.0f, 0.0f };
        for (int ch = 0; ch < 2; ++ch)
        {
            const float chMul = ch == 0 ? 1.0f : (wide ? 1.38f : 1.07f);
            for (int v = 0; v < voices; ++v)
            {
                const int k = ch * 3 + v;
                wow[k].advance(); flut[k].advance();
                const float base = d * chMul * (quad ? quadMul[v] : 1.0f);
                const float modMs = thick * (0.4f + 0.012f * base / (float) sr * 1000.0f);
                const float mod = (modMs * 0.001f * (float) sr) * (0.8f * wow[k].value (Lfo::Sine) + 0.2f * flut[k].value (Lfo::Sine));
                out[ch] += line[ch].read (std::max (1.0f, base + mod));
            }
            out[ch] /= std::sqrt ((float) voices);
        }
        wetL = out[0]; wetR = out[1];
        if (autopan)
        {
            pan.advance();
            const float p = pan.value (Lfo::Sine) * panDepth;   // -1..1
            wetL *= std::sqrt (0.5f * (1.0f - p)) * 1.41421356f;
            wetR *= std::sqrt (0.5f * (1.0f + p)) * 1.41421356f;
        }
    }

private:
    DelayLine line[2];
    Lfo wow[6], flut[6], pan;
    Smoother dS;
    float thick = 0.35f, panDepth = 0.5f;
    bool quad = false, wide = false, autopan = false;
};

LTP_REGISTER (Adt)
