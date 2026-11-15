// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

// Tape Echo (SQ: Echo) - modeled on the classic three-head tape echo. Stereo in, mono out.
//  - MODE 1-7 picks the playback heads: 1-3 single heads (1 shortest, 3 longest),
//    4-7 head combinations (more repeats, easier to run away).
//  - REPEAT RATE is the tape speed: faster tape = shorter delay and brighter repeats;
//    moving it glides the pitch like the real thing.
//  - INTENSITY is the feedback; above ~75% it can self-oscillate (tape saturation keeps it
//    musical). BASS / TREBLE are in the feedback path (centre = flat).
//  - Wow & flutter, tape saturation, head losses, and optional tape NOISE.

#include "Fx.h"

using namespace ltp;

class TapeEcho : public FxBase
{
public:
    TapeEcho() : FxBase (createLayout()) {}

    static Layout createLayout()
    {
        using U = param::Unit;
        Layout l;
        addCommon (l);
        param::addFloat (l, "inGain", "Input Gain", -15.0f, 15.0f, 0.0f, U::Db, -1.0f, 0.1f);
        param::addBool (l, "noise", "Noise", true);
        param::addChoice (l, "mode", "Mode", { "1", "2", "3", "4", "5", "6", "7" }, 3);
        param::addFloat (l, "bass", "Bass", -12.0f, 12.0f, 0.0f, U::Db, -1.0f, 0.1f);
        param::addFloat (l, "treble", "Treble", -12.0f, 12.0f, 0.0f, U::Db, -1.0f, 0.1f);
        param::addFloat (l, "rate", "Repeat Rate", 0.0f, 100.0f, 50.0f, U::Percent);
        param::addFloat (l, "intensity", "Intensity", 0.0f, 100.0f, 40.0f, U::Percent);
        param::addFloat (l, "echoVol", "Echo Volume", -15.0f, 15.0f, 0.0f, U::Db, -1.0f, 0.1f);
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
            { "TAPE ECHO", 2, { knob ("inGain", "INPUT"),
                                segment ("mode", "MODE (HEADS)", "1-3: single heads, short to long. 4: 1+2, 5: 2+3, 6: 1+3, 7: all three"),
                                knob ("rate", "REPEAT RATE", "Tape speed: faster = shorter delay"),
                                knob ("intensity", "INTENSITY", "Feedback. Above about 75% it can self-oscillate"),
                                knob ("echoVol", "ECHO VOLUME") } },
            { "TONE", 0, { knob ("bass", "BASS"), knob ("treble", "TREBLE"), toggle ("noise", "NOISE", "Tape hiss on/off") } },
            outputSection()
        };
    }

    double tailSeconds() const override { return 15.0; }

    static constexpr float headRatio[3] = { 1.0f, 1.97f, 2.95f };
    static float head1Ms (float rate) { return 190.0f * std::pow (62.0f / 190.0f, rate / 100.0f); }

protected:
    void prepareFx (double sampleRate, int) override
    {
        tape.prepare ((int) (sampleRate * 0.65));
        speed.prepare (sampleRate, 220.0f);
        speed.reset (head1Ms (pv ("rate")) * 0.001f * (float) sampleRate);
        wow.setRate (sampleRate, 0.55f);
        flutter.setRate (sampleRate, 6.8f);
        drift.setRate (sampleRate, 0.13f);
        noise.seed (4242);
        hissLp.set (sampleRate, 7000.0f);
        hissHp.set (sampleRate, 600.0f);
        dc.prepare (sampleRate);
        lastBass = lastTreble = -100.0f; lastRate = -1.0f;
    }

    void resetFx() override { tape.reset(); for (auto& h : headLp) h.reset(); bassF.reset(); trebleF.reset(); dc.reset(); fbState = 0.0f; }

    void beginBlock() override
    {
        static const int heads[7] = { 0b001, 0b010, 0b100, 0b011, 0b110, 0b101, 0b111 };
        mask = heads[juce::jlimit (0, 6, pi ("mode"))];
        nHeads = 0; for (int h = 0; h < 3; ++h) if (mask & (1 << h)) ++nHeads;
        inGain = dbToGain (pv ("inGain"));
        echoVol = dbToGain (pv ("echoVol"));
        const float inten = pv ("intensity") * 0.01f;
        fbGain = (inten / 0.75f) * 0.97f / std::sqrt ((float) nHeads);
        useNoise = pb ("noise");
        const float rate = pv ("rate");
        speed.setTarget (head1Ms (rate) * 0.001f * (float) sr);
        if (std::abs (rate - lastRate) > 0.05f)
        {
            lastRate = rate;
            const float bright = 5500.0f + 50.0f * rate;   // faster tape keeps more top end
            for (auto& h : headLp) h.set (sr, bright);
        }
        const float b = pv ("bass"), t = pv ("treble");
        if (std::abs (b - lastBass) > 0.01f) { lastBass = b; bassF.set (Biquad::LowShelf, sr, 150.0f, 0.7f, b); }
        if (std::abs (t - lastTreble) > 0.01f) { lastTreble = t; trebleF.set (Biquad::HighShelf, sr, 3000.0f, 0.7f, t); }
        bassOn = std::abs (b) > 0.05f; trebleOn = std::abs (t) > 0.05f;
    }

    void processFrame (float inL, float inR, float& wetL, float& wetR) override
    {
        wow.advance(); flutter.advance(); drift.advance();
        const float mod = 1.0f + 0.0025f * wow.value (Lfo::Sine) + 0.0006f * flutter.value (Lfo::Sine) + 0.001f * drift.value (Lfo::Triangle);
        const float base = speed.next() * mod;

        float sum = 0.0f;
        for (int h = 0; h < 3; ++h)
            if (mask & (1 << h))
                sum += headLp[h].lp (tape.read (base * headRatio[h]));
        const float echo = sum / std::sqrt ((float) nHeads);

        // feedback path: tone controls, then the record head (saturating tape)
        float fbSig = echo * fbGain;
        if (bassOn) fbSig = bassF.process (fbSig); else bassF.process (fbSig);
        if (trebleOn) fbSig = trebleF.process (fbSig); else trebleF.process (fbSig);
        float rec = 0.5f * (inL + inR) * inGain + fbSig;
        rec = fastTanh (rec * 1.4f) / 1.4f;
        if (useNoise) rec += hissHp.hp (hissLp.lp (noise.next())) * 0.0009f;
        tape.push (dc.process (rec));

        wetL = wetR = echo * echoVol;
    }

private:
    DelayLine tape;
    Smoother speed;
    Lfo wow, flutter, drift;
    OnePole headLp[3], hissLp, hissHp;
    Biquad bassF, trebleF;
    DcBlocker dc;
    Noise noise;
    int mask = 0b011, nHeads = 2;
    float inGain = 1, echoVol = 1, fbGain = 0.5f, fbState = 0, lastBass = -100, lastTreble = -100, lastRate = -1;
    bool useNoise = true, bassOn = false, trebleOn = false;
};

LTP_REGISTER (TapeEcho)
