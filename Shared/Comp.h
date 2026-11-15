// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once

// Base for the compressor models: sidechain filter (Off/HPF/LPF/BPF, like the SQ's
// dynamics sidechain), parallel (wet/dry) mix, stereo-linked processing and metering.
// A model only implements the per-frame gain computer + colour.

#include "Base.h"

namespace ltp
{
    class CompBase : public Processor
    {
    public:
        using Processor::Processor;

        static void addCompCommon (Layout& l)
        {
            param::addChoice (l, "scType", "SC Filter", { "Off", "HPF", "LPF", "BPF" }, 0);
            param::addFloat (l, "scFreq", "SC Freq", 20.0f, 20000.0f, 120.0f, param::Unit::Hz, 1000.0f);
            param::addFloat (l, "mix", "Parallel Mix", 0.0f, 100.0f, 100.0f, param::Unit::Percent);
        }

        static Section sidechainSection (int colour = 6)
        {
            return { "SIDECHAIN & PARALLEL", colour,
                     { choice ("scType", "SC FILTER", "Filters only what the detector hears (the audio is not filtered)"),
                       knob ("scFreq", "SC FREQ"),
                       knob ("mix", "PARALLEL", "Wet/dry blend: 100% = fully compressed, lower blends in the dry signal (parallel compression)") },
                     {}, false, Section::Extra };
        }

        float transferCurveDb (float inDbfs) const override { return inDbfs + staticGainDb (inDbfs); }

        void prepareDsp (double sampleRate, int) override
        {
            lastScType = -1;
            mixSmooth.prepare (sampleRate, 20.0f);
            mixSmooth.reset (pv ("mix") * 0.01f);
            prepareModel (sampleRate);
        }

        void resetDsp() override
        {
            for (auto& f : sc) f.reset();
            resetModel();
        }

        void processDsp (juce::AudioBuffer<float>& b) override
        {
            beginBlock();
            updateSidechain();
            mixSmooth.setTarget (pv ("mix") * 0.01f);
            const int n = b.getNumSamples();
            auto* l = b.getWritePointer (0);
            auto* r = b.getWritePointer (1);
            float maxGr = 0.0f, maxSc = 0.0f;
            for (int i = 0; i < n; ++i)
            {
                const float dl = l[i], dr = r[i];
                float scL = dl, scR = dr;
                if (scActive) { scL = sc[0].process (dl); scR = sc[1].process (dr); }
                maxSc = std::max (maxSc, std::max (std::abs (scL), std::abs (scR)));
                float wl = dl, wr = dr;
                const float gr = processFrame (wl, wr, scL, scR);
                maxGr = std::max (maxGr, gr);
                const float m = mixSmooth.next();
                l[i] = dl + m * (wl - dl);
                r[i] = dr + m * (wr - dr);
            }
            reportGr (maxGr);
            reportDetector (gainToDb (maxSc));
        }

    protected:
        virtual void prepareModel (double sampleRate) = 0;
        virtual void resetModel() {}
        virtual void beginBlock() {}
        // process one stereo frame in place; returns gain reduction (positive dB)
        virtual float processFrame (float& l, float& r, float scL, float scR) = 0;
        // steady-state gain change (dB, includes make-up/output) for a constant input level, for the graph
        virtual float staticGainDb (float inDbfs) const = 0;

        // linked detector input
        static float linkPeak (float a, float b) { return std::max (std::abs (a), std::abs (b)); }

    private:
        void updateSidechain()
        {
            const int type = pi ("scType");
            const float f = pv ("scFreq");
            if (type == lastScType && std::abs (f - lastScFreq) < 0.01f) return;
            lastScType = type; lastScFreq = f;
            scActive = type != 0;
            const auto t = type == 1 ? Biquad::HighPass : type == 2 ? Biquad::LowPass : Biquad::BandPass;
            for (auto& s : sc) s.set (t, sr, f, type == 3 ? 1.4f : 0.707f);
        }

        Biquad sc[2];
        bool scActive = false;
        int lastScType = -1;
        float lastScFreq = 0.0f;
        Smoother mixSmooth;
    };
}
