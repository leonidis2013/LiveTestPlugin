// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once

// The SQ's standard (non-DEEP) compressors: Manual Peak and Manual RMS.
//  - Peak: peak threshold detector, ultra-fast onset, selectable detector smoothing and an
//    optional auto-hold that stops low-frequency ripple (distortion) on fast release settings.
//  - RMS: fairly fast pre-threshold RMS detector, user attack/release for classic RMS feel.

#include "Comp.h"

namespace ltp
{
    template <bool Rms>
    class ManualComp : public CompBase
    {
    public:
        ManualComp() : CompBase (createLayout()) {}

        static Layout createLayout()
        {
            using U = param::Unit;
            Layout l;
            addCommon (l);
            param::addFloat (l, "thresh", "Threshold", -46.0f, 18.0f, 0.0f, U::Dbu, -1.0f, 0.1f);
            param::addFloat (l, "ratio", "Ratio", 1.0f, 100.0f, 3.0f, U::Ratio, 5.0f, 0.01f);
            param::addFloat (l, "attack", "Attack", 0.3f, 300.0f, Rms ? 10.0f : 3.0f, U::Ms, 10.0f);
            param::addFloat (l, "release", "Release", 20.0f, 2000.0f, 150.0f, U::Ms, 250.0f);
            param::addChoice (l, "knee", "Knee", { "Hard", "Soft" }, 1);
            param::addFloat (l, "gain", "Gain", 0.0f, 18.0f, 0.0f, U::Db, -1.0f, 0.1f);
            if (! Rms)
            {
                param::addChoice (l, "smooth", "Smoothing", { "Off", "Low", "High" }, 0);
                param::addBool (l, "hold", "Auto Hold", false);
            }
            addCompCommon (l);
            return l;
        }

        Info getInfo() const override
        {
            Info i;
            i.graph = Graph::Compressor;
            i.editorWidth = Rms ? 700 : 760;
            return i;
        }

        std::vector<Section> getSections() const override
        {
            Section main { Rms ? "RMS COMPRESSOR" : "PEAK COMPRESSOR", 0,
                           { knob ("thresh", "THRESHOLD", "Threshold in dBu, as on the SQ (0 dBu = -18 dBFS)"),
                             knob ("ratio", "RATIO"),
                             knob ("attack", "ATTACK"),
                             knob ("release", "RELEASE"),
                             segment ("knee", "KNEE"),
                             knob ("gain", "GAIN", "Make-up gain") } };
            std::vector<Section> s { main };
            if (! Rms)
                s.push_back ({ "DETECTOR", 3,
                               { segment ("smooth", "SMOOTHING", "Smooths the peak detector to reduce distortion on low frequencies"),
                                 toggle ("hold", "AUTO HOLD", "Holds gain reduction briefly before releasing, so fast releases don't distort bass") } });
            s.push_back (sidechainSection());
            return s;
        }

        Faceplate getFaceplate() const override
        {
            Faceplate f;
            f.title = Rms ? "RMS COMPRESSOR" : "PEAK COMPRESSOR";
            f.subtitle = Rms ? "SQ model: Manual RMS" : "SQ model: Manual Peak";
            f.panel = 0xff2a2f36; f.ink = 0xffd9dee5; f.finish = Faceplate::Brushed; f.knobs = Faceplate::Silver; f.led = 0xff35d0ff;
            f.rows = { { fpDisplay ("thresh", "THRESHOLD"), fpKnob ("thresh", "THRESHOLD", 1), fpKnob ("ratio", "RATIO", 1), fpKnob ("attack", "ATTACK", 1),
                         fpKnob ("release", "RELEASE", 1), fpKnob ("gain", "GAIN", 1), fpLeds ("GR") } };
            if (Rms) f.rows.push_back ({ fpButtons ("knee", "KNEE") });
            else f.rows.push_back ({ fpButtons ("knee", "KNEE"), fpButtons ("smooth", "SMOOTHING"), fpPush ("hold", "AUTO HOLD") });
            return f;
        }

    protected:
        void prepareModel (double sampleRate) override
        {
            rms.set (sampleRate, 5.0f);
            holdSamples = (int) (0.012 * sampleRate);
        }

        void resetModel() override { bal.reset(); rms.reset(); peak.env = 0.0f; holdCount = 0; }

        void beginBlock() override
        {
            thr = dbuToDbfs (pv ("thresh"));
            ratio = pv ("ratio");
            knee = pi ("knee") == 1 ? 8.0f : 0.0f;
            makeup = dbToGain (pv ("gain"));
            bal.set (sr, pv ("attack"), pv ("release"));
            if constexpr (! Rms)
            {
                static constexpr float smoothMs[] = { 0.0f, 4.0f, 15.0f };
                smoothing = pi ("smooth");
                peak.set (sr, smoothMs[juce::jlimit (0, 2, smoothing)]);
                hold = pb ("hold");
            }
        }

        float processFrame (float& l, float& r, float scL, float scR) override
        {
            float level;
            if constexpr (Rms) level = rms.process (linkPeak (scL, scR)) * 1.41421356f;   // sine-peak equivalent
            else level = smoothing == 0 ? linkPeak (scL, scR) : peak.process (linkPeak (scL, scR));

            float target = -compressorGainDb (gainToDb (level), thr, ratio, knee);
            if constexpr (! Rms)
            {
                if (hold)
                {
                    if (target >= bal.gr) holdCount = holdSamples;
                    else if (holdCount > 0) { --holdCount; target = bal.gr; }
                }
            }
            const float gr = bal.process (target);
            const float g = dbToGain (-gr) * makeup;
            l *= g; r *= g;
            return gr;
        }

        float staticGainDb (float in) const override
        {
            return compressorGainDb (in, dbuToDbfs (pv ("thresh")), pv ("ratio"), pi ("knee") == 1 ? 8.0f : 0.0f) + pv ("gain");
        }

    private:
        GrBallistics bal;
        RmsDetector rms;
        PeakDetector peak;
        float thr = 0, ratio = 3, knee = 8, makeup = 1;
        int smoothing = 0, holdSamples = 500, holdCount = 0;
        bool hold = false;
    };
}
