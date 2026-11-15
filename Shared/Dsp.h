// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once

// Small, allocation-free DSP building blocks shared by every LiveTest plugin.
// Everything here is header-only and runs per sample on the audio thread.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace ltp
{
    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kTwoPi = 2.0f * kPi;

    // The SQ aligns +18 dBu to 0 dBFS (so 0 dBu = -18 dBFS). Thresholds in the plugins are
    // shown in dBu like the desk, and converted with this offset.
    constexpr float kDbuOffset = 18.0f;
    inline float dbuToDbfs (float dbu) { return dbu - kDbuOffset; }
    inline float dbfsToDbu (float dbfs) { return dbfs + kDbuOffset; }

    inline float dbToGain (float db) { return std::pow (10.0f, db * 0.05f); }
    inline float gainToDb (float g) { return 20.0f * std::log10 (std::max (g, 1.0e-6f)); }

    // one-pole coefficient for a time constant in ms: y = x + c (y - x)
    inline float msCoef (float ms, double sr)
    {
        if (ms <= 0.0f) return 0.0f;
        return std::exp (-1.0f / (ms * 0.001f * (float) sr));
    }

    inline float clampf (float v, float lo, float hi) { return std::min (hi, std::max (lo, v)); }
    inline float lerp (float a, float b, float t) { return a + (b - a) * t; }

    // keeps denormals out of recursive filters
    inline float flushDenormal (float v) { return std::abs (v) < 1.0e-15f ? 0.0f : v; }

    // fast tanh (Pade), accurate to ~0.1% in [-3,3], clamps beyond
    inline float fastTanh (float x)
    {
        x = clampf (x, -4.5f, 4.5f);
        const float x2 = x * x;
        return x * (27.0f + x2) / (27.0f + 9.0f * x2);
    }

    // ------------------------------------------------------------------ parameter smoothing
    struct Smoother
    {
        float current = 0.0f, target = 0.0f, coef = 0.0f;
        void prepare (double sr, float ms = 20.0f) { coef = msCoef (ms, sr); }
        void setTarget (float t) { target = t; }
        void reset (float v) { current = target = v; }
        float next() { current = target + coef * (current - target); return current; }
        bool settled() const { return std::abs (current - target) < 1.0e-5f; }
    };

    // ------------------------------------------------------------------ white noise
    struct Noise
    {
        uint32_t state = 0x12345678u;
        void seed (uint32_t s) { state = s ? s : 1u; }
        uint32_t nextInt() { state ^= state << 13; state ^= state >> 17; state ^= state << 5; return state; }
        float next() { return (float) (int32_t) nextInt() * (1.0f / 2147483648.0f); }   // -1..1
        float uniform() { return (float) (nextInt() >> 8) * (1.0f / 16777216.0f); }      // 0..1
    };

    // ------------------------------------------------------------------ biquad (RBJ cookbook, TDF-II)
    struct Biquad
    {
        enum Type { LowPass, HighPass, BandPass, Peak, LowShelf, HighShelf, AllPass, Notch };

        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        float z1 = 0, z2 = 0;

        void reset() { z1 = z2 = 0.0f; }

        void set (Type t, double sr, float freq, float q, float gainDb = 0.0f)
        {
            freq = clampf (freq, 5.0f, (float) sr * 0.49f);
            q = std::max (q, 0.025f);
            const float w0 = kTwoPi * freq / (float) sr;
            const float cw = std::cos (w0), sw = std::sin (w0);
            const float alpha = sw / (2.0f * q);
            const float A = std::pow (10.0f, gainDb / 40.0f);
            float B0 = 1, B1 = 0, B2 = 0, A0 = 1, A1 = 0, A2 = 0;

            switch (t)
            {
                case LowPass:  B0 = (1 - cw) * 0.5f; B1 = 1 - cw; B2 = B0; A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
                case HighPass: B0 = (1 + cw) * 0.5f; B1 = -(1 + cw); B2 = B0; A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
                case BandPass: B0 = alpha; B1 = 0; B2 = -alpha; A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
                case Notch:    B0 = 1; B1 = -2 * cw; B2 = 1; A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
                case AllPass:  B0 = 1 - alpha; B1 = -2 * cw; B2 = 1 + alpha; A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
                case Peak:
                    B0 = 1 + alpha * A; B1 = -2 * cw; B2 = 1 - alpha * A;
                    A0 = 1 + alpha / A; A1 = -2 * cw; A2 = 1 - alpha / A; break;
                case LowShelf:
                {
                    const float sq = 2.0f * std::sqrt (A) * alpha;
                    B0 = A * ((A + 1) - (A - 1) * cw + sq);
                    B1 = 2 * A * ((A - 1) - (A + 1) * cw);
                    B2 = A * ((A + 1) - (A - 1) * cw - sq);
                    A0 = (A + 1) + (A - 1) * cw + sq;
                    A1 = -2 * ((A - 1) + (A + 1) * cw);
                    A2 = (A + 1) + (A - 1) * cw - sq;
                    break;
                }
                case HighShelf:
                {
                    const float sq = 2.0f * std::sqrt (A) * alpha;
                    B0 = A * ((A + 1) + (A - 1) * cw + sq);
                    B1 = -2 * A * ((A - 1) + (A + 1) * cw);
                    B2 = A * ((A + 1) + (A - 1) * cw - sq);
                    A0 = (A + 1) - (A - 1) * cw + sq;
                    A1 = 2 * ((A - 1) - (A + 1) * cw);
                    A2 = (A + 1) - (A - 1) * cw - sq;
                    break;
                }
            }
            b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0;
        }

        inline float process (float x)
        {
            const float y = b0 * x + z1;
            z1 = flushDenormal (b1 * x - a1 * y + z2);
            z2 = flushDenormal (b2 * x - a2 * y);
            return y;
        }

        // magnitude response in dB (for drawing)
        float magnitudeDb (double sr, float hz) const
        {
            const double w = 2.0 * 3.14159265358979323846 * hz / sr;
            const double c1 = std::cos (w), s1 = std::sin (w), c2 = std::cos (2 * w), s2 = std::sin (2 * w);
            const double nr = b0 + b1 * c1 + b2 * c2, ni = -(b1 * s1 + b2 * s2);
            const double dr = 1 + a1 * c1 + a2 * c2, di = -(a1 * s1 + a2 * s2);
            const double mag2 = (nr * nr + ni * ni) / std::max (1.0e-20, dr * dr + di * di);
            return (float) (10.0 * std::log10 (std::max (1.0e-20, mag2)));
        }
    };

    // Butterworth Q values for cascades (2nd-order sections)
    inline constexpr float kButterQ4[2] = { 0.5411961f, 1.3065630f };   // 4th order = 24 dB/oct

    // ------------------------------------------------------------------ 24 dB/oct Butterworth HP / LP
    struct Filter24
    {
        Biquad s[2];
        bool enabled = true;
        void set (Biquad::Type t, double sr, float f) { for (int i = 0; i < 2; ++i) s[i].set (t, sr, f, kButterQ4[i]); }
        void reset() { for (auto& b : s) b.reset(); }
        float process (float x) { if (! enabled) return x; return s[1].process (s[0].process (x)); }
    };

    // ------------------------------------------------------------------ TPT one-pole (LP + HP outputs)
    struct OnePole
    {
        float G = 0.0f, s = 0.0f;
        void set (double sr, float freq)
        {
            const float g = std::tan (kPi * clampf (freq, 1.0f, (float) sr * 0.49f) / (float) sr);
            G = g / (1.0f + g);
        }
        void reset() { s = 0.0f; }
        inline float lp (float x)
        {
            const float v = (x - s) * G;
            const float y = v + s;
            s = flushDenormal (y + v);
            return y;
        }
        inline float hp (float x) { return x - lp (x); }
    };

    // first-order allpass (phaser stage): y = -a x + x1 + a y1  with a in (-1,1)
    struct AllPass1
    {
        float x1 = 0.0f, y1 = 0.0f;
        void reset() { x1 = y1 = 0.0f; }
        static float coefFor (double sr, float freq)
        {
            const float t = std::tan (kPi * clampf (freq, 5.0f, (float) sr * 0.45f) / (float) sr);
            return (t - 1.0f) / (t + 1.0f);
        }
        inline float process (float x, float a)
        {
            const float y = a * x + x1 - a * y1;
            x1 = x; y1 = flushDenormal (y);
            return y;
        }
    };

    // ------------------------------------------------------------------ fractional delay line
    struct DelayLine
    {
        std::vector<float> buf;
        int mask = 0, w = 0;

        void prepare (int maxDelaySamples)
        {
            int size = 1;
            while (size < maxDelaySamples + 8) size <<= 1;
            buf.assign ((size_t) size, 0.0f);
            mask = size - 1;
            w = 0;
        }
        void reset() { std::fill (buf.begin(), buf.end(), 0.0f); w = 0; }
        int maxDelay() const { return mask - 4; }

        inline void push (float x) { buf[(size_t) w] = x; w = (w + 1) & mask; }

        // sample written `d` pushes ago (d = 0 is the most recent sample)
        inline float tap (int d) const { return buf[(size_t) ((w - 1 - d) & mask)]; }

        // fractional read with cubic (Hermite) interpolation, d >= 0
        inline float read (float d) const
        {
            d = clampf (d, 0.0f, (float) maxDelay());
            const int i = (int) d;
            const float f = d - (float) i;
            const float xm1 = tap (i - 1 < 0 ? 0 : i - 1), x0 = tap (i), x1 = tap (i + 1), x2 = tap (i + 2);
            const float c1 = 0.5f * (x1 - xm1);
            const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
            const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
            return ((c3 * f + c2) * f + c1) * f + x0;
        }

        inline float readLinear (float d) const
        {
            d = clampf (d, 0.0f, (float) maxDelay());
            const int i = (int) d;
            const float f = d - (float) i;
            return lerp (tap (i), tap (i + 1), f);
        }
    };

    // Schroeder allpass diffuser with its own delay memory
    struct AllpassDiffuser
    {
        DelayLine d;
        float delay = 100.0f, g = 0.5f;
        void prepare (int maxSamples) { d.prepare (maxSamples); }
        void reset() { d.reset(); }
        inline float process (float x, float modOffset = 0.0f)
        {
            const float vd = d.readLinear (std::max (1.0f, delay + modOffset) - 1.0f);
            const float v = x + g * vd;
            d.push (flushDenormal (v));
            return vd - g * v;
        }
    };

    // ------------------------------------------------------------------ LFO
    struct Lfo
    {
        enum Shape { Sine, Triangle, Rectified, InvRectified };
        float phase = 0.0f, inc = 0.0f;
        void setRate (double sr, float hz) { inc = hz / (float) sr; }
        void reset (float p = 0.0f) { phase = p - std::floor (p); }
        inline void advance() { phase += inc; if (phase >= 1.0f) phase -= 1.0f; }

        // bipolar -1..1 at a phase offset (0..1)
        static float shape (Shape s, float p)
        {
            p -= std::floor (p);
            switch (s)
            {
                case Sine:         return std::sin (kTwoPi * p);
                case Triangle:     return p < 0.5f ? 4.0f * p - 1.0f : 3.0f - 4.0f * p;
                case Rectified:    return 2.0f * std::abs (std::sin (kPi * p * 2.0f)) - 1.0f;      // twice the speed
                case InvRectified: return 1.0f - 2.0f * std::abs (std::sin (kPi * p * 2.0f));
            }
            return 0.0f;
        }
        float value (Shape s, float offset = 0.0f) const { return shape (s, phase + offset); }
    };

    // ------------------------------------------------------------------ dynamics helpers
    // static gain computer (dB domain), returns gain change <= 0
    inline float compressorGainDb (float inDb, float threshDb, float ratio, float kneeDb)
    {
        const float slope = ratio >= 100.0f ? -1.0f : (1.0f / ratio - 1.0f);
        const float over = inDb - threshDb;
        if (kneeDb > 0.0f && 2.0f * std::abs (over) <= kneeDb)
        {
            const float t = over + kneeDb * 0.5f;
            return slope * t * t / (2.0f * kneeDb);
        }
        return over > 0.0f ? slope * over : 0.0f;
    }

    // attack/release ballistics on a gain-reduction amount (positive dB)
    struct GrBallistics
    {
        float gr = 0.0f, att = 0.0f, rel = 0.0f;
        void set (double sr, float attackMs, float releaseMs) { att = msCoef (attackMs, sr); rel = msCoef (releaseMs, sr); }
        void reset() { gr = 0.0f; }
        inline float process (float target)
        {
            const float c = target > gr ? att : rel;
            gr = target + c * (gr - target);
            return gr;
        }
    };

    // peak follower with instant attack
    struct PeakDetector
    {
        float env = 0.0f, rel = 0.0f;
        void set (double sr, float releaseMs) { rel = msCoef (releaseMs, sr); }
        inline float process (float x)
        {
            const float a = std::abs (x);
            env = a > env ? a : a + rel * (env - a);
            return env;
        }
    };

    // RMS detector (one-pole mean square)
    struct RmsDetector
    {
        float ms = 0.0f, coef = 0.0f;
        void set (double sr, float windowMs) { coef = msCoef (windowMs, sr); }
        void reset() { ms = 0.0f; }
        inline float process (float x)
        {
            ms = x * x + coef * (ms - x * x);
            return std::sqrt (std::max (ms, 0.0f));
        }
    };

    // ------------------------------------------------------------------ colour
    // asymmetric "tube" stage: adds mostly 2nd harmonic, unity gain for small signals
    inline float tubeStage (float x, float drive)
    {
        if (drive <= 0.0001f) return x;
        const float bias = 0.18f;
        const float k = 1.0f + drive * 3.0f;
        const float y = fastTanh (k * (x + bias)) - fastTanh (k * bias);
        const float norm = 1.0f - fastTanh (k * bias) * fastTanh (k * bias);   // derivative at 0 / k
        return y / (k * std::max (norm, 0.05f));
    }

    // symmetric saturation (odd harmonics), unity small-signal gain
    inline float softSat (float x, float drive)
    {
        if (drive <= 0.0001f) return x;
        const float k = 1.0f + drive * 4.0f;
        return fastTanh (k * x) / k;
    }

    // tracks DC from asymmetric saturation so it never reaches the output
    struct DcBlocker
    {
        float x1 = 0.0f, y1 = 0.0f, r = 0.995f;
        void prepare (double sr) { r = 1.0f - (kTwoPi * 10.0f / (float) sr); }
        void reset() { x1 = y1 = 0.0f; }
        inline float process (float x) { const float y = x - x1 + r * y1; x1 = x; y1 = flushDenormal (y); return y; }
    };
}
