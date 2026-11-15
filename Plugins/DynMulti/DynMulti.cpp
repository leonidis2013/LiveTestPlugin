// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

// LiveTest Dyn8 (SQ: Dyn8) - 4-band dynamic EQ followed by a 4-band multiband compressor,
// laid out like the SQ's Dyn8 / DynEQ4 / Multiband units.
//
// Dynamic EQ: per band frequency, width (2.25 to 1/9 octave), RANGE (max cut or boost,
// centre = flat), threshold (dBu), ABOVE/BELOW and STD/FAST timing. Band 1 can be a
// low shelf, band 4 a high shelf. Sidechains run in PARALLEL (each band hears the input)
// or CASCADE (each band hears the previous band's output).
//
// Multiband: three crossovers with 6, 18 or 24 dB/oct slopes (all phase-compensated so
// the bands sum flat), dynamics modes Peak/RMS manual and four auto modes, hard/soft knee,
// per-band threshold, ratio, gain and IN.

#include "Base.h"

using namespace ltp;

namespace
{
    float octavesToQ (float n) { const float p = std::pow (2.0f, n); return std::sqrt (p) / (p - 1.0f); }

    // one crossover point, with LP/HP outputs and a matching allpass for the other branch
    struct Crossover
    {
        int slope = 2;          // 0 = 6, 1 = 18, 2 = 24 dB/oct
        OnePole op;             // 1st order part (6 and 18)
        Biquad lp1, lp2, hp1, hp2, ap;

        void set (double sr, float f, int s)
        {
            slope = s;
            op.set (sr, f);
            if (s == 1)
            {
                lp1.set (Biquad::LowPass, sr, f, 1.0f);
                hp1.set (Biquad::HighPass, sr, f, 1.0f);
                ap.set (Biquad::AllPass, sr, f, 1.0f);
            }
            else if (s == 2)
            {
                for (auto* b : { &lp1, &lp2 }) b->set (Biquad::LowPass, sr, f, 0.70710678f);
                for (auto* b : { &hp1, &hp2 }) b->set (Biquad::HighPass, sr, f, 0.70710678f);
                ap.set (Biquad::AllPass, sr, f, 0.70710678f);
            }
        }
        void reset() { op.reset(); lp1.reset(); lp2.reset(); hp1.reset(); hp2.reset(); ap.reset(); }

        void split (float x, float& lo, float& hi)
        {
            if (slope == 0) { lo = op.lp (x); hi = x - lo; return; }
            if (slope == 1)
            {
                const float l1 = op.lp (x), h1 = x - l1;
                lo = lp1.process (l1); hi = hp1.process (h1);
                return;
            }
            lo = lp2.process (lp1.process (x));
            hi = hp2.process (hp1.process (x));
        }
        // allpass with the same phase as lo+hi (identity for 6 dB)
        float allpass (float x) { return slope == 0 ? x : ap.process (x); }
    };

    struct BandDyn
    {
        RmsDetector rms;
        float peakEnv = 0.0f, gr = 0.0f, slow = 0.0f;
    };
}

class DynMulti : public Processor
{
public:
    DynMulti() : Processor (createLayout())
    {
        // cache raw parameter pointers so the audio thread never builds strings
        for (int b = 0; b < 4; ++b)
        {
            const auto d = "d" + juce::String (b + 1), m = "m" + juce::String (b + 1);
            auto get = [this] (const juce::String& id) { return apvts.getRawParameterValue (id); };
            dp[b] = { get (d + "On"), get (d + "Below"), get (d + "Range"), get (d + "Thresh"), get (d + "Fast"), get (d + "Freq"), get (d + "Width") };
            mp[b] = { get (m + "On"), get (m + "Thresh"), get (m + "Ratio"), get (m + "Gain") };
        }
    }

    static juce::String octText (float v) { return v >= 1.0f ? juce::String (v, 2) + " oct" : "1/" + juce::String (1.0f / v, 1) + " oct"; }

    static Layout createLayout()
    {
        using U = param::Unit;
        Layout l;
        addCommon (l);
        static const float defF[] = { 100.0f, 500.0f, 2500.0f, 8000.0f };
        param::addBool (l, "deqIn", "Dyn EQ In", true);
        param::addChoice (l, "deqSc", "Dyn EQ Sidechain", { "Parallel", "Cascade" }, 0);
        for (int b = 1; b <= 4; ++b)
        {
            const auto p = "d" + juce::String (b);
            param::addBool (l, p + "On", "DEQ " + juce::String (b) + " In", true);
            if (b == 1) param::addChoice (l, p + "Type", "DEQ 1 Type", { "Bell", "Shelf" }, 1);
            if (b == 4) param::addChoice (l, p + "Type", "DEQ 4 Type", { "Bell", "Shelf" }, 1);
            param::addFloat (l, p + "Freq", "DEQ " + juce::String (b) + " Freq", 40.0f, 18000.0f, defF[b - 1], U::Hz, 1000.0f);
            param::addFloatText (l, p + "Width", "DEQ " + juce::String (b) + " Width", 1.0f / 9.0f, 2.25f, 1.0f, octText, 0.8f);
            param::addFloat (l, p + "Range", "DEQ " + juce::String (b) + " Range", -18.0f, 18.0f, -6.0f, U::Db, -1.0f, 0.1f);
            param::addFloat (l, p + "Thresh", "DEQ " + juce::String (b) + " Threshold", -50.0f, 18.0f, 0.0f, U::Dbu, -1.0f, 0.1f);
            param::addBool (l, p + "Below", "DEQ " + juce::String (b) + " Below", false);
            param::addBool (l, p + "Fast", "DEQ " + juce::String (b) + " Fast", false);
        }

        param::addBool (l, "mbIn", "Multiband In", true);
        param::addChoice (l, "mbSlope", "Slope", { "6", "18", "24" }, 2);
        param::addChoice (l, "mbMode", "Dynamics", { "Peak Manual", "RMS Manual", "Auto Punch", "Auto Opto", "Slow AutoF", "Fast AutoF" }, 1);
        param::addChoice (l, "mbKnee", "Knee", { "Hard", "Soft" }, 1);
        param::addFloat (l, "mbAttack", "MB Attack", 0.3f, 300.0f, 10.0f, U::Ms, 10.0f);
        param::addFloat (l, "mbRelease", "MB Release", 20.0f, 2000.0f, 150.0f, U::Ms, 250.0f);
        param::addFloat (l, "xo1", "Crossover 1", 40.0f, 1000.0f, 150.0f, U::Hz, 200.0f);
        param::addFloat (l, "xo2", "Crossover 2", 200.0f, 6000.0f, 1200.0f, U::Hz, 1200.0f);
        param::addFloat (l, "xo3", "Crossover 3", 1000.0f, 18000.0f, 6000.0f, U::Hz, 5000.0f);
        for (int b = 1; b <= 4; ++b)
        {
            const auto p = "m" + juce::String (b);
            param::addBool (l, p + "On", "MB " + juce::String (b) + " In", true);
            param::addFloat (l, p + "Thresh", "MB " + juce::String (b) + " Threshold", -50.0f, 18.0f, 0.0f, U::Dbu, -1.0f, 0.1f);
            param::addFloat (l, p + "Ratio", "MB " + juce::String (b) + " Ratio", 1.0f, 20.0f, 2.0f, U::Ratio, 4.0f, 0.01f);
            param::addFloat (l, p + "Gain", "MB " + juce::String (b) + " Gain", -6.0f, 18.0f, 0.0f, U::Db, -1.0f, 0.1f);
        }
        param::addFloat (l, "trim", "Master Trim", -12.0f, 12.0f, 0.0f, U::Db, -1.0f, 0.1f);
        return l;
    }

    Info getInfo() const override
    {
        Info i; LTP_BASE_INFO (i)
        i.graph = Graph::DynEq;
        i.editorWidth = 1130;
        i.maxGrDb = 18.0f;
        return i;
    }

    std::vector<Section> getSections() const override
    {
        std::vector<Section> s;
        for (int b = 1; b <= 4; ++b)
        {
            const auto p = "d" + juce::String (b);
            std::vector<Ctl> c;
            if (b == 1 || b == 4) c.push_back (segment (p + "Type", "TYPE", b == 1 ? "Bell or low shelf" : "Bell or high shelf"));
            c.push_back (knob (p + "Freq", "FREQ"));
            c.push_back (knob (p + "Width", "WIDTH"));
            c.push_back (knob (p + "Range", "RANGE", "Maximum cut (left) or boost (right) this band can apply. Centre = no change"));
            c.push_back (knob (p + "Thresh", "THRESHOLD", "dBu, as on the SQ"));
            c.push_back (toggle (p + "Below", "BELOW", "Off: acts when the band is ABOVE the threshold. On: acts when it is BELOW"));
            c.push_back (toggle (p + "Fast", "FAST", "Off: standard (smooth) timing. On: fast attack and release"));
            s.push_back ({ "DYN EQ BAND " + juce::String (b), b - 1, c, p + "On", b == 3 });
        }
        s.push_back ({ "DYN EQ", 1, { segment ("deqSc", "SIDECHAINS", "Parallel: every band listens to the input. Cascade: each band listens to the previous band's output") }, "deqIn", true });
        s.push_back ({ "MULTIBAND", 5, { segment ("mbSlope", "SLOPE dB/oct"),
                                         choice ("mbMode", "DYNAMICS"),
                                         segment ("mbKnee", "KNEE"),
                                         knob ("mbAttack", "ATTACK", "Used in the Peak/RMS Manual modes"),
                                         knob ("mbRelease", "RELEASE", "Used in the Peak/RMS Manual modes"),
                                         knob ("xo1", "XOVER 1"), knob ("xo2", "XOVER 2"), knob ("xo3", "XOVER 3") }, "mbIn" });
        for (int b = 1; b <= 4; ++b)
        {
            const auto p = "m" + juce::String (b);
            s.push_back ({ "MB BAND " + juce::String (b), 4 + b % 4, { knob (p + "Thresh", "THRESHOLD", "dBu, as on the SQ"),
                                                                     knob (p + "Ratio", "RATIO"),
                                                                     knob (p + "Gain", "GAIN") }, p + "On", b == 1 });
        }
        s.push_back ({ "OUTPUT", 6, { knob ("trim", "TRIM") } });
        return s;
    }

    // ------------------------------------------------------------------ DSP
    void prepareDsp (double sampleRate, int) override
    {
        for (auto& b : deq) { b.coefCounter = 0; b.curGain = 0.0f; }
        modeLast = -1;
        juce::ignoreUnused (sampleRate);
    }

    void resetDsp() override
    {
        for (auto& b : deq) { for (auto& f : b.audio) f.reset(); for (auto& f : b.det) f.reset(); b.env = 0.0f; b.curGain = 0.0f; b.coefCounter = 0; }
        for (int ch = 0; ch < 2; ++ch) { for (auto& x : xo[ch]) x.reset(); compLow[ch].reset(); compHigh[ch].reset(); }
        for (auto& d : mbDyn) { d.rms.reset(); d.peakEnv = d.gr = d.slow = 0.0f; }
    }

    void processDsp (juce::AudioBuffer<float>& buf) override
    {
        const int n = buf.getNumSamples();
        auto* L = buf.getWritePointer (0);
        auto* R = buf.getWritePointer (1);
        updateParams();
        float maxGr = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            float l = L[i], r = R[i];

            // ---------------- dynamic EQ
            if (deqOn)
            {
                const float inL = l, inR = r;
                for (int b = 0; b < 4; ++b)
                {
                    auto& band = deq[b];
                    const float sl = cascade ? l : inL, srr = cascade ? r : inR;
                    const float dl = band.det[0].process (sl), dr = band.det[1].process (srr);
                    const float a = std::max (std::abs (dl), std::abs (dr));
                    band.env = a + (a > band.env ? band.attCoef : band.relCoef) * (band.env - a);
                    float gain = 0.0f;
                    if (band.on)
                    {
                        const float lvl = gainToDb (band.env * 1.41421356f);
                        const float over = band.below ? band.thresh - lvl : lvl - band.thresh;
                        if (over > 0.0f) gain = (band.range < 0.0f ? -1.0f : 1.0f) * std::min (std::abs (band.range), over * 0.6f);
                    }
                    band.gainSmooth = gain + band.gainCoef * (band.gainSmooth - gain);
                    if (--band.coefCounter <= 0 || std::abs (band.gainSmooth - band.curGain) > 0.25f)
                    {
                        band.coefCounter = 16;
                        if (std::abs (band.gainSmooth - band.curGain) > 0.01f || band.dirty)
                        {
                            band.curGain = band.gainSmooth;
                            band.dirty = false;
                            setDeqFilter (band, band.curGain);
                        }
                    }
                    if (band.on) { l = band.audio[0].process (l); r = band.audio[1].process (r); }
                    maxGr = std::max (maxGr, -band.curGain);
                }
            }

            // ---------------- multiband
            if (mbOn)
            {
                float outL = 0.0f, outR = 0.0f;
                float bandsL[4], bandsR[4];
                for (int ch = 0; ch < 2; ++ch)
                {
                    const float x = ch == 0 ? l : r;
                    float lo, hi, b1, b2, b3, b4;
                    xo[ch][1].split (x, lo, hi);
                    lo = compLow[ch].allpass (lo);    // phase of crossover 3
                    hi = compHigh[ch].allpass (hi);   // phase of crossover 1
                    xo[ch][0].split (lo, b1, b2);
                    xo[ch][2].split (hi, b3, b4);
                    float* dst = ch == 0 ? bandsL : bandsR;
                    dst[0] = b1; dst[1] = b2; dst[2] = b3; dst[3] = b4;
                }
                for (int b = 0; b < 4; ++b)
                {
                    float g = 1.0f;
                    if (mbBand[b].on)
                    {
                        const float gr = mbDynamics (b, std::max (std::abs (bandsL[b]), std::abs (bandsR[b])));
                        maxGr = std::max (maxGr, gr);
                        g = dbToGain (mbBand[b].gain - gr);
                        mbGain[b] = mbBand[b].gain - gr;
                    }
                    else mbGain[b] = 0.0f;
                    outL += bandsL[b] * g;
                    outR += bandsR[b] * g;
                }
                l = outL; r = outR;
            }

            L[i] = l * trim; R[i] = r * trim;
        }

        for (int b = 0; b < 4; ++b) bandGrDb[(size_t) b].store (deq[b].curGain);
        for (int b = 0; b < 4; ++b) bandGrDb[(size_t) (4 + b)].store (mbOn ? mbGain[b] : 0.0f);
        reportGr (maxGr);
    }

    // ------------------------------------------------------------------ graph
    float responseDb (float hz, bool withDynamics) const override
    {
        if (! pb ("deqIn")) return 0.0f;
        float total = 0.0f;
        const double rate = getSampleRateSafe();
        for (int b = 0; b < 4; ++b)
        {
            const auto p = "d" + juce::String (b + 1);
            if (! pb ((p + "On").toRawUTF8())) continue;
            const float gain = withDynamics ? bandGrDb[(size_t) b].load() : pv ((p + "Range").toRawUTF8());
            Biquad f;
            configureFilter (f, rate, b, pv ((p + "Freq").toRawUTF8()), pv ((p + "Width").toRawUTF8()), isShelf (b), gain);
            total += f.magnitudeDb (rate, hz);
        }
        return total;
    }

    void graphMarkers (std::vector<std::pair<float, float>>& m) const override
    {
        if (! pb ("deqIn")) return;
        for (int b = 0; b < 4; ++b)
        {
            const auto p = "d" + juce::String (b + 1);
            const float f = pv ((p + "Freq").toRawUTF8());
            m.push_back ({ f, responseDb (f, true) });
        }
    }

    void crossoverFreqs (std::vector<float>& f) const override
    {
        if (! pb ("mbIn")) return;
        float a = pv ("xo1"), b = pv ("xo2"), c = pv ("xo3");
        orderCrossovers (a, b, c);
        f = { a, b, c };
    }

private:
    struct DeqBand
    {
        Biquad audio[2], det[2];
        bool on = true, below = false, dirty = true;
        float freq = 1000, width = 1, range = -6, thresh = -18, env = 0, gainSmooth = 0, curGain = 0;
        float attCoef = 0, relCoef = 0, gainCoef = 0;
        int coefCounter = 0, type = 0;
    };
    struct MbBand { bool on = true; float thresh = -18, ratio = 2, gain = 0; };

    bool isShelf (int b) const
    {
        if (b == 0) return pi ("d1Type") == 1;
        if (b == 3) return pi ("d4Type") == 1;
        return false;
    }

    static void configureFilter (Biquad& f, double rate, int b, float freq, float width, bool shelf, float gain)
    {
        if (shelf) f.set (b == 0 ? Biquad::LowShelf : Biquad::HighShelf, rate, freq, 0.7f, gain);
        else f.set (Biquad::Peak, rate, freq, octavesToQ (width), gain);
    }

    void setDeqFilter (DeqBand& band, float gain)
    {
        const int b = (int) (&band - deq);
        for (auto& f : band.audio)
        {
            const float z1 = f.z1, z2 = f.z2;   // keep state while coefficients glide
            configureFilter (f, sr, b, band.freq, band.width, band.type == 1, gain);
            f.z1 = z1; f.z2 = z2;
        }
    }

    static void orderCrossovers (float& a, float& b, float& c)
    {
        b = std::max (b, a * 1.25f);
        c = std::max (c, b * 1.25f);
    }

    void updateParams()
    {
        deqOn = pb ("deqIn");
        cascade = pi ("deqSc") == 1;
        for (int b = 0; b < 4; ++b)
        {
            auto& band = deq[b];
            const auto& P = dp[b];
            band.on = P.on->load() >= 0.5f;
            band.below = P.below->load() >= 0.5f;
            band.range = P.range->load();
            band.thresh = dbuToDbfs (P.thresh->load());
            const bool fast = P.fast->load() >= 0.5f;
            band.attCoef = msCoef (fast ? 2.0f : 10.0f, sr);
            band.relCoef = msCoef (fast ? 40.0f : 150.0f, sr);
            band.gainCoef = msCoef (fast ? 1.0f : 4.0f, sr);
            const float f = P.freq->load(), w = P.width->load();
            const int type = isShelf (b) ? 1 : 0;
            if (std::abs (f - band.freq) > 0.01f || std::abs (w - band.width) > 1.0e-4f || type != band.type || band.dirty)
            {
                band.freq = f; band.width = w; band.type = type; band.dirty = true;
                // detector: band-pass at the band, or LP / HP for the shelves
                for (auto& d : band.det)
                {
                    if (type == 1) d.set (b == 0 ? Biquad::LowPass : Biquad::HighPass, sr, f, 0.7f);
                    else d.set (Biquad::BandPass, sr, f, octavesToQ (w));
                }
                setDeqFilter (band, band.curGain);
            }
        }

        mbOn = pb ("mbIn");
        const int slope = pi ("mbSlope");
        float a = pv ("xo1"), b = pv ("xo2"), c = pv ("xo3");
        orderCrossovers (a, b, c);
        if (slope != lastSlope || std::abs (a - lastXo[0]) > 0.01f || std::abs (b - lastXo[1]) > 0.01f || std::abs (c - lastXo[2]) > 0.01f)
        {
            if (slope != lastSlope) for (int ch = 0; ch < 2; ++ch) { for (auto& x : xo[ch]) x.reset(); compLow[ch].reset(); compHigh[ch].reset(); }
            lastSlope = slope; lastXo[0] = a; lastXo[1] = b; lastXo[2] = c;
            for (int ch = 0; ch < 2; ++ch)
            {
                xo[ch][0].set (sr, a, slope); xo[ch][1].set (sr, b, slope); xo[ch][2].set (sr, c, slope);
                compLow[ch].set (sr, c, slope);    // low branch gets crossover 3's phase
                compHigh[ch].set (sr, a, slope);   // high branch gets crossover 1's phase
            }
        }

        const int mode = pi ("mbMode");
        knee = pi ("mbKnee") == 1 ? 8.0f : 0.0f;
        struct M { float rmsMs, att, rel; bool peak, autoRel; };
        static const M modes[] = {
            { 0.0f, -1.0f, -1.0f, true, false },     // Peak Manual
            { 5.0f, -1.0f, -1.0f, false, false },    // RMS Manual
            { 5.0f, 5.0f, 100.0f, false, true },     // Auto Punch
            { 10.0f, 10.0f, 300.0f, false, true },   // Auto Opto
            { 20.0f, 25.0f, 800.0f, false, true },   // Slow AutoF
            { 3.0f, 3.0f, 150.0f, false, true } };   // Fast AutoF
        const auto& m = modes[juce::jlimit (0, 5, mode)];
        if (mode != modeLast)
        {
            modeLast = mode;
            for (auto& d : mbDyn) d.rms.set (sr, std::max (0.5f, m.rmsMs));
        }
        mbPeak = m.peak;
        mbAuto = m.autoRel;
        mbAtt = msCoef (m.att > 0.0f ? m.att : pv ("mbAttack"), sr);
        mbRel = msCoef (m.rel > 0.0f ? m.rel : pv ("mbRelease"), sr);
        mbSlowAtt = msCoef (300.0f, sr);
        mbSlowRel = msCoef (2000.0f, sr);
        for (int b2 = 0; b2 < 4; ++b2)
        {
            const auto& P = mp[b2];
            mbBand[b2].on = P.on->load() >= 0.5f;
            mbBand[b2].thresh = dbuToDbfs (P.thresh->load());
            mbBand[b2].ratio = P.ratio->load();
            mbBand[b2].gain = P.gain->load();
        }
        trim = dbToGain (pv ("trim"));
    }

    float mbDynamics (int b, float x)
    {
        auto& d = mbDyn[b];
        const float level = mbPeak ? x : d.rms.process (x) * 1.41421356f;
        const float target = -compressorGainDb (gainToDb (level), mbBand[b].thresh, mbBand[b].ratio, knee);
        d.gr = target + (target > d.gr ? mbAtt : mbRel) * (d.gr - target);
        if (! mbAuto) return d.gr;
        const float st = target * 0.5f;
        d.slow = st + (st > d.slow ? mbSlowAtt : mbSlowRel) * (d.slow - st);
        return std::max (d.gr, d.slow);
    }

    struct DeqPtrs { std::atomic<float> *on, *below, *range, *thresh, *fast, *freq, *width; };
    struct MbPtrs { std::atomic<float> *on, *thresh, *ratio, *gain; };
    DeqPtrs dp[4] {};
    MbPtrs mp[4] {};
    DeqBand deq[4];
    MbBand mbBand[4];
    BandDyn mbDyn[4];
    Crossover xo[2][3], compLow[2], compHigh[2];
    float mbGain[4] {};
    float lastXo[3] { -1, -1, -1 };
    int lastSlope = -1, modeLast = -1;
    bool deqOn = true, cascade = false, mbOn = true, mbPeak = false, mbAuto = false;
    float knee = 8, mbAtt = 0, mbRel = 0, mbSlowAtt = 0, mbSlowRel = 0, trim = 1;
};

LTP_REGISTER (DynMulti)
