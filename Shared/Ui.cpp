// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#include "Ui.h"

namespace ltp
{
    namespace
    {
        const juce::Colour reductionColour { 0xfff472b6 };   // pink: gain reduction, as in LiveVoice
        constexpr int knobW = 66, toggleW = 74, comboW = 112, tapW = 60, ctlGap = 4;
    }

    juce::Colour accentColour (int index)
    {
        switch (((index % 8) + 8) % 8)
        {
            case 0: return theme::comp;       // amber
            case 1: return theme::eq;         // teal
            case 2: return theme::feedback;   // orange
            case 3: return theme::deess;      // violet
            case 4: return theme::pse;        // green
            case 5: return theme::output;     // blue
            case 6: return theme::hpf;        // steel
            default: return reductionColour;  // pink
        }
    }

    // ================================================================ SQ skin
    void SqLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider& s)
    {
        auto r = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (2.0f, 1.0f);
        const auto accent = s.findColour (juce::Slider::rotarySliderFillColourId);
        const bool hot = s.isMouseOverOrDragging();
        g.setColour (theme::panelHi);
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (hot ? accent : theme::border);
        g.drawRoundedRectangle (r, 4.0f, hot ? 1.5f : 1.0f);

        // value, large, like the SQ's touch tiles
        g.setColour (s.isEnabled() ? theme::text : theme::textFaint);
        g.setFont (theme::font (std::min (15.0f, r.getHeight() * 0.32f), true));
        g.drawFittedText (s.getTextFromValue (s.getValue()), r.withTrimmedBottom (10.0f).toNearestInt(), juce::Justification::centred, 1, 0.75f);

        // position bar along the bottom (bipolar parameters fill from the centre)
        auto bar = r.removeFromBottom (9.0f).reduced (7.0f, 2.5f);
        g.setColour (theme::bg);
        g.fillRoundedRectangle (bar, 2.0f);
        const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0 && std::abs (s.getMinimum() + s.getMaximum()) < 1.0e-3;
        const float from = bipolar ? bar.getCentreX() : bar.getX();
        const float to = bar.getX() + bar.getWidth() * pos;
        g.setColour (s.isEnabled() ? accent : accent.withSaturation (0.0f));
        g.fillRoundedRectangle (juce::Rectangle<float> (std::min (from, to), bar.getY(), std::max (2.0f, std::abs (to - from)), bar.getHeight()), 2.0f);
    }

    void SqLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const bool on = b.getToggleState();
        const auto accent = b.findColour (juce::TextButton::buttonOnColourId);
        g.setColour (on ? accent.withAlpha (0.22f) : theme::panelHi);
        g.fillRoundedRectangle (r, 3.0f);
        if (down || over) { g.setColour (theme::text.withAlpha (down ? 0.10f : 0.05f)); g.fillRoundedRectangle (r, 3.0f); }
        g.setColour (on ? accent : theme::border);
        g.drawRoundedRectangle (r, 3.0f, 1.0f);
        if (on)   // lit strip along the top edge
        {
            g.setColour (accent);
            g.fillRoundedRectangle (r.withHeight (3.0f).reduced (3.0f, 0.0f).translated (0.0f, 1.5f), 1.5f);
        }
    }

    void SqLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
    {
        g.setColour (b.getToggleState() ? theme::text : theme::textDim);
        g.setFont (getTextButtonFont (b, b.getHeight()));
        g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (3, 0), juce::Justification::centred, 1, 0.7f);
    }

    // ================================================================ controls
    Knob::Knob (const juce::String& name, juce::Colour accent)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 16);
        slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
        slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        slider.setMouseDragSensitivity (220);
        addAndMakeVisible (slider);

        label.setText (name, juce::dontSendNotification);
        label.setFont (theme::font (11.5f));
        label.setColour (juce::Label::textColourId, theme::textDim);
        label.setJustificationType (juce::Justification::centred);
        label.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (label);
    }

    void Knob::attach (juce::AudioProcessorValueTreeState& state, const juce::String& id)
    {
        attachment = std::make_unique<SliderAttachment> (state, id, slider);
        if (auto* p = state.getParameter (id))
            slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
    }

    void Knob::setSq (bool sq)
    {
        sqStyle = sq;
        slider.setTextBoxStyle (sq ? juce::Slider::NoTextBox : juce::Slider::TextBoxBelow, false, 80, 16);
        resized();
    }

    void Knob::resized()
    {
        auto r = getLocalBounds();
        label.setBounds (r.removeFromTop (14));
        slider.setBounds (sqStyle ? r.withSizeKeepingCentre (r.getWidth(), std::min (r.getHeight(), 60)) : r);
    }

    LabelledToggle::LabelledToggle (const juce::String& name, juce::Colour accent)
    {
        button.setButtonText (name);
        button.setClickingTogglesState (true);
        button.setColour (juce::TextButton::buttonOnColourId, accent);
        button.setColour (juce::TextButton::textColourOnId, theme::onAccent);
        addAndMakeVisible (button);
        label.setFont (theme::font (11.5f));
        label.setColour (juce::Label::textColourId, theme::textDim);
        label.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (label);
    }

    void LabelledToggle::attach (juce::AudioProcessorValueTreeState& state, const juce::String& id)
    {
        attachment = std::make_unique<ButtonAttachment> (state, id, button);
    }

    void LabelledToggle::resized()
    {
        auto r = getLocalBounds();
        label.setBounds (r.removeFromTop (14));
        button.setBounds (r.withSizeKeepingCentre (r.getWidth() - 4, 26));
    }

    LabelledCombo::LabelledCombo (const juce::String& name)
    {
        label.setText (name, juce::dontSendNotification);
        label.setFont (theme::font (11.5f));
        label.setColour (juce::Label::textColourId, theme::textDim);
        label.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (label);
        addAndMakeVisible (box);
    }

    void LabelledCombo::attach (juce::AudioProcessorValueTreeState& state, const juce::String& id)
    {
        box.clear (juce::dontSendNotification);
        if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (id)))
            box.addItemList (c->choices, 1);
        attachment = std::make_unique<ComboAttachment> (state, id, box);
    }

    void LabelledCombo::resized()
    {
        auto r = getLocalBounds();
        label.setBounds (r.removeFromTop (14));
        box.setBounds (r.withSizeKeepingCentre (r.getWidth() - 4, 26));
    }

    Segment::Segment (const juce::String& name, juce::Colour accent, juce::AudioProcessorValueTreeState& s, const juce::String& paramID)
        : state (s), id (paramID)
    {
        label.setText (name, juce::dontSendNotification);
        label.setFont (theme::font (11.5f));
        label.setColour (juce::Label::textColourId, theme::textDim);
        label.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (label);

        if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (id)))
        {
            for (int i = 0; i < c->choices.size(); ++i)
            {
                auto* b = buttons.add (new juce::TextButton (c->choices[i]));
                b->setColour (juce::TextButton::buttonOnColourId, accent);
                b->setColour (juce::TextButton::textColourOnId, theme::onAccent);
                b->onClick = [this, i]
                {
                    if (auto* p = state.getParameter (id))
                    {
                        p->beginChangeGesture();
                        p->setValueNotifyingHost (p->convertTo0to1 ((float) i));
                        p->endChangeGesture();
                    }
                };
                addAndMakeVisible (b);
            }
        }
        state.addParameterListener (id, this);
        handleAsyncUpdate();
    }

    Segment::~Segment() { state.removeParameterListener (id, this); }

    int Segment::preferredWidth() const
    {
        int w = 0;
        for (auto* b : buttons)
            w += std::max (30, juce::GlyphArrangement::getStringWidthInt (theme::font (12.0f, true), b->getButtonText()) + 14);
        return std::max (w + 4, 60);
    }

    void Segment::resized()
    {
        auto r = getLocalBounds();
        label.setBounds (r.removeFromTop (14));
        auto row = r.withSizeKeepingCentre (r.getWidth() - 4, 26);
        const int total = std::max (1, preferredWidth() - 4);
        int x = row.getX();
        for (auto* b : buttons)
        {
            const int bw = std::max (30, juce::GlyphArrangement::getStringWidthInt (theme::font (12.0f, true), b->getButtonText()) + 14);
            const int w = row.getWidth() * bw / total;
            b->setBounds (x, row.getY(), w - 2, row.getHeight());
            x += w;
        }
    }

    void Segment::handleAsyncUpdate()
    {
        const int idx = (int) std::lround (state.getRawParameterValue (id)->load());
        for (int i = 0; i < buttons.size(); ++i)
            buttons[i]->setToggleState (i == idx, juce::dontSendNotification);
    }

    // ================================================================ panel
    Panel::Panel (const Section& s, Processor& p, bool sq, bool vert) : sqStyle (sq), vertical (vert), title (s.title), accentIndex (s.colour)
    {
        const auto accent = accentColour (accentIndex);
        const int kw = sq ? 80 : knobW;
        for (const auto& c : s.controls)
        {
            switch (c.type)
            {
                case Ctl::Knob:
                {
                    auto* k = new Knob (c.label, accent);
                    k->attach (p.apvts, c.paramId);
                    k->setSq (sq);
                    if (c.tooltip.isNotEmpty()) k->slider.setTooltip (c.tooltip);
                    controls.add (k); widths.push_back (kw);
                    break;
                }
                case Ctl::Toggle:
                {
                    auto* t = new LabelledToggle (c.label, accent);
                    t->attach (p.apvts, c.paramId);
                    if (c.tooltip.isNotEmpty()) t->button.setTooltip (c.tooltip);
                    controls.add (t); widths.push_back (toggleW);
                    break;
                }
                case Ctl::Choice:
                {
                    auto* cb = new LabelledCombo (c.label);
                    cb->attach (p.apvts, c.paramId);
                    if (c.tooltip.isNotEmpty()) cb->box.setTooltip (c.tooltip);
                    controls.add (cb); widths.push_back (comboW);
                    break;
                }
                case Ctl::Segment:
                {
                    auto* sg = new Segment (c.label, accent, p.apvts, c.paramId);
                    if (c.tooltip.isNotEmpty()) sg->setTooltip (c.tooltip);
                    controls.add (sg); widths.push_back (sg->preferredWidth());
                    break;
                }
                case Ctl::Tap:
                {
                    auto* holder = new LabelledToggle ("TAP", accent);
                    holder->button.setClickingTogglesState (false);
                    holder->button.setTooltip (c.tooltip.isNotEmpty() ? c.tooltip : juce::String ("Tap at least twice to set the tempo / time"));
                    holder->button.onClick = [&p] { p.tap(); };
                    controls.add (holder); widths.push_back (tapW);
                    break;
                }
            }
            addAndMakeVisible (controls.getLast());
        }

        if (s.onParam.isNotEmpty())
        {
            power = std::make_unique<juce::TextButton> ("IN");
            power->setClickingTogglesState (true);
            power->setColour (juce::TextButton::buttonOnColourId, accent);
            power->setColour (juce::TextButton::textColourOnId, theme::onAccent);
            power->setTooltip ("Switch this section in or out");
            powerAttachment = std::make_unique<ButtonAttachment> (p.apvts, s.onParam, *power);
            addAndMakeVisible (*power);
        }
    }

    int Panel::preferredHeight() const
    {
        if (! vertical) return height;
        return 32 + 82 * (int) controls.size() + 6;
    }

    int Panel::preferredWidth() const
    {
        if (vertical)
        {
            int w = 0;
            for (auto cw : widths) w = std::max (w, cw);
            return w + 20;
        }
        int w = 20;
        for (auto cw : widths) w += cw + ctlGap;
        const int titleW = juce::GlyphArrangement::getStringWidthInt (theme::font (13.0f, true), title) + 40 + (power ? 50 : 0);
        return std::max (w, titleW);
    }

    void Panel::resized()
    {
        if (power) power->setBounds (getWidth() - 48, 7, 38, 20);
        auto content = getLocalBounds().reduced (10, 0).withTrimmedTop (32).withTrimmedBottom (8);
        if (vertical)
        {
            int y = content.getY();
            for (auto* c : controls) { c->setBounds (content.getX(), y, content.getWidth(), 78); y += 82; }
            return;
        }
        int total = 0;
        for (auto cw : widths) total += cw + ctlGap;
        total -= ctlGap;
        int x = content.getX() + std::max (0, (content.getWidth() - total) / 2);
        for (int i = 0; i < controls.size(); ++i)
        {
            controls[i]->setBounds (x, content.getY(), widths[(size_t) i], content.getHeight());
            x += widths[(size_t) i] + ctlGap;
        }
    }

    void Panel::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (theme::panel);
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (theme::border);
        g.drawRoundedRectangle (r, 8.0f, 1.0f);
        g.setColour (accentColour (accentIndex));
        g.fillRoundedRectangle (12.0f, 12.0f, 3.0f, 12.0f, 1.5f);
        g.setColour (theme::text);
        g.setFont (theme::font (13.0f, true));
        g.drawText (title, juce::Rectangle<int> (22, 6, getWidth() - (power ? 80 : 30), 24), juce::Justification::centredLeft);
    }

    // ================================================================ I/O meters
    void IoMeters::ballistics (Channel& c, float v)
    {
        c.level = v > c.level ? v : std::max (v, c.level - 1.5f);
        if (v >= c.peak) { c.peak = v; c.hold = 60; }
        else if (--c.hold < 0) c.peak = std::max (-100.0f, c.peak - 0.75f);
    }

    void IoMeters::update()
    {
        ballistics (in, proc.inPeakDb.exchange (-100.0f));
        ballistics (out, proc.outPeakDb.exchange (-100.0f));
        repaint();
    }

    void IoMeters::mouseDown (const juce::MouseEvent&) { in.peak = in.level; out.peak = out.level; repaint(); }

    void IoMeters::paint (juce::Graphics& g)
    {
        constexpr float minDb = -60.0f;
        auto r = getLocalBounds().toFloat();
        g.setColour (theme::panel);
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (theme::border);
        g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);

        auto inner = r.reduced (6.0f);
        auto head = inner.removeFromTop (30.0f);
        inner.removeFromBottom (6.0f);
        auto scale = inner.removeFromLeft (22.0f);
        const float barW = (inner.getWidth() - 6.0f) / 2.0f;
        auto inBar = inner.removeFromLeft (barW);
        inner.removeFromLeft (6.0f);
        auto outBar = inner.removeFromLeft (barW);
        auto yFor = [&] (float db) { return juce::jmap (juce::jlimit (minDb, 0.0f, db), 0.0f, minDb, inBar.getY(), inBar.getBottom()); };
        auto zone = [] (float db)
        {
            if (db < -30.0f) return theme::input;
            if (db < -18.0f) return theme::output;
            if (db < -6.0f)  return theme::good;
            if (db < -1.0f)  return theme::warn;
            return theme::danger;
        };

        for (auto [bar, ch, name] : { std::tuple { inBar, &in, "IN" }, std::tuple { outBar, &out, "OUT" } })
        {
            g.setColour (theme::bg);
            g.fillRoundedRectangle (bar, 2.0f);
            const float y = yFor (ch->level);
            juce::ColourGradient grad (theme::output, 0.0f, bar.getBottom(), theme::danger, 0.0f, bar.getY(), false);
            auto pos = [&] (float db) { return (double) juce::jlimit (0.0f, 1.0f, (db - minDb) / -minDb); };
            grad.addColour (pos (-18.5f), theme::output);
            grad.addColour (pos (-17.5f), theme::good);
            grad.addColour (pos (-6.5f), theme::good);
            grad.addColour (pos (-5.5f), theme::warn);
            grad.addColour (pos (-1.3f), theme::warn);
            g.setGradientFill (grad);
            g.fillRoundedRectangle (bar.withTop (y), 2.0f);
            if (ch->peak > minDb)
            {
                g.setColour (zone (ch->peak));
                g.fillRect (bar.getX(), yFor (ch->peak) - 1.0f, bar.getWidth(), 2.0f);
            }
            auto h = head.withX (bar.getX() - 4.0f).withWidth (bar.getWidth() + 8.0f);
            g.setColour (theme::textDim);
            g.setFont (theme::font (10.5f, true));
            g.drawText (name, h.removeFromTop (14.0f), juce::Justification::centred);
            g.setColour (ch->peak > minDb ? zone (ch->peak) : theme::textFaint);
            g.setFont (theme::font (11.0f, true));
            g.drawText (ch->peak > -99.0f ? juce::String (ch->peak, 1) : "-inf", h, juce::Justification::centred);
        }

        g.setFont (theme::font (9.5f));
        for (float db : { 0.0f, -6.0f, -12.0f, -18.0f, -24.0f, -30.0f, -40.0f, -50.0f, -60.0f })
        {
            g.setColour (std::abs (db + 18.0f) < 0.1f ? theme::good : theme::textFaint);
            g.drawText (juce::String ((int) db), juce::Rectangle<float> (scale.getX(), yFor (db) - 6.0f, scale.getWidth() - 3.0f, 12.0f),
                        juce::Justification::centredRight);
        }
    }

    void GrMeter::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (theme::panel);
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (theme::border);
        g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
        auto inner = r.reduced (5.0f);
        auto head = inner.removeFromTop (30.0f);
        inner.removeFromBottom (6.0f);
        auto bar = inner.removeFromLeft (10.0f);
        g.setColour (theme::bg);
        g.fillRoundedRectangle (bar, 2.0f);
        const float frac = juce::jlimit (0.0f, 1.0f, value / maxReduction);
        g.setColour (reductionColour);
        g.fillRoundedRectangle (bar.withHeight (bar.getHeight() * frac), 2.0f);
        g.setColour (theme::textDim);
        g.setFont (theme::font (10.5f, true));
        g.drawText ("GR", head.removeFromTop (14.0f), juce::Justification::centred);
        g.setColour (value > 0.05f ? reductionColour : theme::textFaint);
        g.setFont (theme::font (11.0f, true));
        g.drawText (juce::String (-value, 1), head, juce::Justification::centred);
        g.setFont (theme::font (9.5f));
        const float step = maxReduction > 18.0f ? 6.0f : 3.0f;
        for (float db = 0.0f; db <= maxReduction + 0.01f; db += step)
        {
            const float y = bar.getY() + bar.getHeight() * db / maxReduction;
            g.setColour (theme::textFaint);
            g.drawText (db < 0.01f ? "0" : "-" + juce::String ((int) db),
                        juce::Rectangle<float> (bar.getRight() + 2.0f, y - 6.0f, inner.getWidth() - 2.0f, 12.0f),
                        juce::Justification::centredLeft);
        }
    }

    // ================================================================ compressor graph
    void CompGraph::push (float inDb, float gr)
    {
        history[(size_t) writePos] = { inDb, gr };
        writePos = (writePos + 1) % (int) history.size();
        dotIn = inDb;
    }

    void CompGraph::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (theme::panel);
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (theme::border);
        g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
        auto inner = r.reduced (12.0f, 10.0f);
        auto titleRow = inner.removeFromTop (16.0f);
        g.setColour (theme::textDim);
        g.setFont (theme::font (11.0f, true));
        if (showCurve) g.drawText ("TRANSFER CURVE (dBu)", titleRow.withWidth (inner.getHeight() + 30.0f), juce::Justification::centredLeft);
        if (showHistory)
            g.drawText (showCurve ? "DETECTOR LEVEL (grey) & GAIN REDUCTION (pink, dB)" : "HISTOGRAM",
                        showCurve ? titleRow.withTrimmedLeft (inner.getHeight() + 50.0f) : titleRow, juce::Justification::centredLeft);
        inner.removeFromTop (4.0f);

        juce::Rectangle<float> square, hist;
        if (showCurve && showHistory)
        {
            square = inner.removeFromLeft (inner.getHeight()).withTrimmedLeft (22.0f).withTrimmedBottom (12.0f);
            inner.removeFromLeft (28.0f);
            hist = inner;
        }
        else if (showCurve)
        {
            const float side = std::min (inner.getHeight(), inner.getWidth());
            square = inner.withSizeKeepingCentre (side, side).withTrimmedLeft (22.0f).withTrimmedBottom (12.0f);
        }
        else hist = inner.withTrimmedLeft (26.0f);

        const float lo = -60.0f, hi = 0.0f;
        if (showCurve) {
        auto xFor = [&] (float db) { return juce::jmap (juce::jlimit (lo, hi, db), lo, hi, square.getX(), square.getRight()); };
        auto yFor = [&] (float db) { return juce::jmap (juce::jlimit (lo, hi, db), lo, hi, square.getBottom(), square.getY()); };

        g.setColour (theme::bg);
        g.fillRect (square);
        g.setFont (theme::font (9.0f));
        for (float db = -54.0f; db <= 0.0f; db += 6.0f)
        {
            g.setColour (theme::grid);
            g.drawVerticalLine ((int) xFor (db), square.getY(), square.getBottom());
            g.drawHorizontalLine ((int) yFor (db), square.getX(), square.getRight());
            if (std::fmod (db + 60.0f, 12.0f) < 0.1f)
            {
                g.setColour (theme::textFaint);
                const juce::String t = juce::String ((int) dbfsToDbu (db));
                g.drawText (t, juce::Rectangle<float> (square.getX() - 24.0f, yFor (db) - 6.0f, 20.0f, 12.0f), juce::Justification::centredRight);
                g.drawText (t, juce::Rectangle<float> (xFor (db) - 12.0f, square.getBottom() + 1.0f, 24.0f, 11.0f), juce::Justification::centred);
            }
        }
        g.setColour (theme::textFaint.withAlpha (0.6f));
        g.drawLine (xFor (lo), yFor (lo), xFor (hi), yFor (hi), 1.0f);

        juce::Path curve;
        for (int i = 0; i <= 120; ++i)
        {
            const float in = lo + (hi - lo) * (float) i / 120.0f;
            const float out = proc.transferCurveDb (in);
            if (i == 0) curve.startNewSubPath (xFor (in), yFor (out));
            else curve.lineTo (xFor (in), yFor (out));
        }
        g.saveState();
        g.reduceClipRegion (square.toNearestInt());
        g.setColour (theme::comp);
        g.strokePath (curve, juce::PathStrokeType (2.0f));
        if (dotIn > lo)
        {
            const float out = proc.transferCurveDb (dotIn);
            g.setColour (theme::text);
            g.fillEllipse (xFor (dotIn) - 4.0f, yFor (out) - 4.0f, 8.0f, 8.0f);
        }
        g.restoreState();
        }
        if (! showHistory) return;

        // history strip
        g.setColour (theme::bg);
        g.fillRect (hist);
        const float maxGr = proc.getInfo().maxGrDb;
        const int n = (int) history.size();
        const float colW = hist.getWidth() / (float) n;
        juce::Path level, grPath;
        level.startNewSubPath (hist.getX(), hist.getBottom());
        for (int i = 0; i < n; ++i)
        {
            const auto& h = history[(size_t) ((writePos + i) % n)];
            const float x = hist.getX() + colW * (float) i;
            const float ly = juce::jmap (juce::jlimit (-60.0f, 0.0f, h.first), -60.0f, 0.0f, hist.getBottom(), hist.getY());
            level.lineTo (x, ly);
            const float gy = hist.getY() + hist.getHeight() * juce::jlimit (0.0f, 1.0f, h.second / maxGr);
            if (i == 0) grPath.startNewSubPath (x, gy); else grPath.lineTo (x, gy);
        }
        level.lineTo (hist.getRight(), hist.getBottom());
        level.closeSubPath();
        g.setColour (theme::input.withAlpha (0.45f));
        g.fillPath (level);
        g.setColour (reductionColour);
        g.strokePath (grPath, juce::PathStrokeType (1.6f));
        g.setColour (theme::textFaint);
        g.setFont (theme::font (9.0f));
        for (float db = 0.0f; db <= maxGr; db += maxGr / 4.0f)
            g.drawText ("-" + juce::String ((int) db), juce::Rectangle<float> (hist.getX() - 26.0f, hist.getY() + hist.getHeight() * db / maxGr - 6.0f, 22.0f, 12.0f),
                        juce::Justification::centredRight);
    }

    // ================================================================ dynamic EQ graph
    void DynEqGraph::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (theme::panel);
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (theme::border);
        g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
        auto area = r.reduced (34.0f, 12.0f).withTrimmedBottom (10.0f);
        g.setColour (theme::bg);
        g.fillRect (area);

        const float range = 18.0f;
        auto xFor = [&] (float hz) { return area.getX() + area.getWidth() * std::log (hz / 20.0f) / std::log (1000.0f); };
        auto yFor = [&] (float db) { return juce::jmap (juce::jlimit (-range, range, db), -range, range, area.getBottom(), area.getY()); };

        g.setFont (theme::font (9.0f));
        for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
        {
            g.setColour (theme::grid);
            g.drawVerticalLine ((int) xFor (f), area.getY(), area.getBottom());
            g.setColour (theme::textFaint);
            g.drawText (f >= 1000.0f ? juce::String ((int) (f / 1000.0f)) + "k" : juce::String ((int) f),
                        juce::Rectangle<float> (xFor (f) - 14.0f, area.getBottom() + 1.0f, 28.0f, 10.0f), juce::Justification::centred);
        }
        for (float db = -18.0f; db <= 18.0f; db += 6.0f)
        {
            g.setColour (std::abs (db) < 0.1f ? theme::border : theme::grid);
            g.drawHorizontalLine ((int) yFor (db), area.getX(), area.getRight());
            g.setColour (theme::textFaint);
            g.drawText (juce::String ((int) db), juce::Rectangle<float> (area.getX() - 28.0f, yFor (db) - 6.0f, 24.0f, 12.0f), juce::Justification::centredRight);
        }

        // multiband regions with their live gain change
        std::vector<float> xo;
        proc.crossoverFreqs (xo);
        if (! xo.empty())
        {
            std::vector<float> edges { 20.0f };
            for (auto f : xo) edges.push_back (f);
            edges.push_back (20000.0f);
            for (size_t b = 0; b + 1 < edges.size() && b < 4; ++b)
            {
                const float x0 = xFor (edges[b]), x1 = xFor (edges[b + 1]);
                const float gdb = proc.bandGrDb[4 + b].load();
                const float y0 = yFor (0.0f), y1 = yFor (gdb);
                g.setColour (accentColour (5 + (int) b % 3).withAlpha (0.22f));
                g.fillRect (juce::Rectangle<float> (x0 + 1.0f, std::min (y0, y1), x1 - x0 - 2.0f, std::abs (y1 - y0)));
                if (b > 0)
                {
                    g.setColour (theme::textDim.withAlpha (0.6f));
                    g.drawVerticalLine ((int) x0, area.getY(), area.getBottom());
                }
            }
        }

        auto drawResponse = [&] (bool dyn, juce::Colour c, float thickness)
        {
            juce::Path p;
            for (int i = 0; i <= 200; ++i)
            {
                const float hz = 20.0f * std::pow (1000.0f, (float) i / 200.0f);
                const float y = yFor (proc.responseDb (hz, dyn));
                if (i == 0) p.startNewSubPath (area.getX(), y); else p.lineTo (xFor (hz), y);
            }
            g.setColour (c);
            g.strokePath (p, juce::PathStrokeType (thickness));
        };
        g.saveState();
        g.reduceClipRegion (area.toNearestInt());
        drawResponse (false, theme::textDim.withAlpha (0.6f), 1.2f);
        drawResponse (true, theme::eq, 2.2f);

        std::vector<std::pair<float, float>> markers;
        proc.graphMarkers (markers);
        for (size_t i = 0; i < markers.size(); ++i)
        {
            const float x = xFor (markers[i].first), y = yFor (markers[i].second);
            g.setColour (theme::bandColours[i % 6]);
            g.fillEllipse (x - 7.0f, y - 7.0f, 14.0f, 14.0f);
            g.setColour (theme::onAccent);
            g.setFont (theme::font (9.5f, true));
            g.drawText (juce::String ((int) i + 1), juce::Rectangle<float> (x - 7.0f, y - 7.0f, 14.0f, 14.0f), juce::Justification::centred);
        }
        g.restoreState();
    }

    // ================================================================ header
    Header::Header (Processor& p, std::function<void()> onTheme, std::function<void()> onView) : info (p.getInfo())
    {
        viewBtn.setTooltip ("Switch between the SQ-style page and the hardware-style UNIT front panel (saved with this plugin instance)");
        viewBtn.onClick = [onView] { onView(); };
        addAndMakeVisible (viewBtn);
        bypass.setClickingTogglesState (true);
        bypass.setColour (juce::TextButton::buttonOnColourId, theme::danger);
        bypass.setColour (juce::TextButton::textColourOnId, theme::onAccent);
        bypass.setTooltip ("Bypass this plugin (smooth crossfade)");
        bypassAttachment = std::make_unique<ButtonAttachment> (p.apvts, "bypass", bypass);
        addAndMakeVisible (bypass);

        themeBtn.setButtonText (theme::lightMode ? "DARK" : "LIGHT");
        themeBtn.setTooltip ("Switch between dark and light mode (remembered for every LiveTest plugin)");
        themeBtn.onClick = [this, onTheme] { onTheme(); themeBtn.setButtonText (theme::lightMode ? "DARK" : "LIGHT"); };
        addAndMakeVisible (themeBtn);
    }

    void Header::paint (juce::Graphics& g)
    {
        g.fillAll (theme::bg);
        g.setColour (theme::text);
        g.setFont (theme::font (17.0f, true));
        g.drawText ("LIVETEST", juce::Rectangle<int> (14, 6, 100, 24), juce::Justification::centredLeft);
        auto shortName = info.name.fromFirstOccurrenceOf ("LiveTest ", false, false);
        g.setColour (info.dynamics ? theme::comp : theme::eq);
        g.drawText (shortName.toUpperCase(), juce::Rectangle<int> (104, 6, 320, 24), juce::Justification::centredLeft);
        g.setColour (theme::textFaint);
        g.setFont (theme::font (10.5f));
        g.drawText ("v" + info.version + "  -  SQ model: " + info.sqName + "  -  modeled on: " + info.modeledOn,
                    juce::Rectangle<int> (15, 28, getWidth() - 300, 14), juce::Justification::centredLeft);
        g.setColour (theme::border);
        g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
    }

    void Header::resized()
    {
        auto r = getLocalBounds().reduced (10, 0);
        const int cy = getHeight() / 2;
        bypass.setBounds (r.removeFromRight (80).withHeight (26).withY (cy - 13));
        r.removeFromRight (8);
        themeBtn.setBounds (r.removeFromRight (64).withHeight (26).withY (cy - 13));
        r.removeFromRight (8);
        viewBtn.setBounds (r.removeFromRight (96).withHeight (26).withY (cy - 13));
    }

    void Header::setViewIsUnit (bool unit)
    {
        viewBtn.setButtonText (unit ? "SQ VIEW" : "UNIT VIEW");
        viewBtn.setVisible (info.hasUnitView);
    }

    // ================================================================ editor
    bool Editor::applyStoredTheme()
    {
        theme::setLightMode (theme::loadLightModePreference());
        return true;
    }

    Editor::Editor (Processor& p)
        : juce::AudioProcessorEditor (p), proc (p), info (p.getInfo()),
          header (p, [this] { toggleTheme(); }, [this] { toggleView(); }), io (p)
    {
        juce::ignoreUnused (themeReady);
        lnf.applyColours();
        sqLnf.applyColours();
        setLookAndFeel (&lnf);
        addAndMakeVisible (header);
        addAndMakeVisible (io);
        io.setTooltip ("Input and output peak levels (dBFS). 0 dB on the SQ's meters is -18 dBFS here.");
        if (info.dynamics)
        {
            gr = std::make_unique<GrMeter> (info.maxGrDb);
            addAndMakeVisible (*gr);
        }
        unitView = info.hasUnitView && proc.apvts.state.getProperty ("view", "sq").toString() == "unit";
        buildView();
        startTimerHz (30);
    }

    Editor::~Editor()
    {
        stopTimer();
        panels.clear();
        scPanel.reset(); parPanel.reset();
        faceplate.reset();
        setLookAndFeel (nullptr);
    }

    void Editor::paint (juce::Graphics& g) { g.fillAll (theme::bg); }

    void Editor::toggleView()
    {
        unitView = ! unitView;
        proc.apvts.state.setProperty ("view", unitView ? "unit" : "sq", nullptr);
        buildView();
    }

    void Editor::buildView()
    {
        panels.clear();
        panelNewRow.clear();
        compGraph.reset(); histogram.reset(); dynGraph.reset(); faceplate.reset();
        scPanel.reset(); parPanel.reset();
        header.setViewIsUnit (unitView);

        const auto sections = proc.getSections();
        const bool sq = ! unitView;
        auto addPanel = [&] (const Section& s)
        {
            auto* panel = panels.add (new Panel (s, proc, sq));
            if (sq) panel->setLookAndFeel (&sqLnf);
            panelNewRow.push_back (s.newRow);
            addAndMakeVisible (panel);
        };

        int bodyH = 0;
        if (isSqCompPage())
        {
            // SQ compressor page: side chain | graph + model parameters | parallel + histogram
            compGraph = std::make_unique<CompGraph> (proc, true, false);
            histogram = std::make_unique<CompGraph> (proc, false, true);
            addAndMakeVisible (*compGraph);
            addAndMakeVisible (*histogram);
            for (const auto& s : sections)
            {
                if (s.role != Section::Extra) { addPanel (s); continue; }
                Section side ("SIDE CHAIN", s.colour, {}), par ("PARALLEL", s.colour, {});
                for (const auto& c : s.controls) (c.paramId == "mix" ? par : side).controls.push_back (c);
                if (! side.controls.empty()) { scPanel = std::make_unique<Panel> (side, proc, true, true); scPanel->setLookAndFeel (&sqLnf); addAndMakeVisible (*scPanel); }
                if (! par.controls.empty()) { parPanel = std::make_unique<Panel> (par, proc, true, true); parPanel->setLookAndFeel (&sqLnf); addAndMakeVisible (*parPanel); }
            }
            int centre = 470;
            for (auto* pnl : panels) centre = std::max (centre, pnl->preferredWidth());
            bodyWidth = (scPanel ? scColW + gap : 0) + centre + gap + parColW;
            const int centreH = graphH + gap + layoutPanels ({ 0, 0, centre, 10000 }, false);
            const int sideH = std::max (scPanel ? scPanel->preferredHeight() : 0, (parPanel ? parPanel->preferredHeight() + gap : 0) + 150);
            bodyH = std::max (centreH, sideH);
        }
        else if (unitView)
        {
            auto spec = proc.getFaceplate();
            if (spec.empty()) spec = FaceplateView::fromSections (proc);
            faceplate = std::make_unique<FaceplateView> (proc, spec);
            addAndMakeVisible (*faceplate);
            if (info.graph == Graph::DynEq) { dynGraph = std::make_unique<DynEqGraph> (proc); addAndMakeVisible (*dynGraph); }
            for (const auto& s : sections) if (s.role == Section::Extra) addPanel (s);
            int extrasW = 0;
            for (auto* pnl : panels) extrasW += pnl->preferredWidth() + gap;
            bodyWidth = std::max ({ faceplate->preferredWidth(), extrasW - gap, 560 });
            faceplate->setMinWidth (bodyWidth);
            bodyH = faceplate->preferredHeight() + (panels.isEmpty() ? 0 : gap + layoutPanels ({ 0, 0, bodyWidth, 10000 }, false))
                    + (dynGraph ? graphH + gap : 0);
        }
        else
        {
            if (info.graph == Graph::Compressor) { compGraph = std::make_unique<CompGraph> (proc); addAndMakeVisible (*compGraph); }
            if (info.graph == Graph::DynEq) { dynGraph = std::make_unique<DynEqGraph> (proc); addAndMakeVisible (*dynGraph); }
            for (const auto& s : sections) addPanel (s);
            bodyWidth = info.editorWidth;
            bodyH = layoutPanels ({ 0, 0, bodyWidth, 10000 }, false) + ((compGraph || dynGraph) ? graphH + gap : 0);
        }

        const int meters = info.dynamics ? meterW : fxMeterW;
        const int w = gap + bodyWidth + gap + meters + gap;
        const int h = std::max (headerH + gap + bodyH + gap, 360);
        sendLookAndFeelChange();
        if (getWidth() == w && getHeight() == h) resized();
        else setSize (w, h);
    }

    int Editor::layoutPanels (juce::Rectangle<int> area, bool apply)
    {
        int y = area.getY();
        size_t i = 0;
        while (i < (size_t) panels.size())
        {
            std::vector<size_t> row;
            int used = 0;
            while (i < (size_t) panels.size())
            {
                const int w = panels[(int) i]->preferredWidth();
                if (! row.empty() && (panelNewRow[i] || used + gap + w > area.getWidth())) break;
                used += (row.empty() ? 0 : gap) + w;
                row.push_back (i++);
            }
            if (apply)
            {
                const int extra = std::max (0, area.getWidth() - used);
                int x = area.getX();
                for (size_t k = 0; k < row.size(); ++k)
                {
                    const int w = panels[(int) row[k]]->preferredWidth() + extra / (int) row.size();
                    panels[(int) row[k]]->setBounds (x, y, std::min (w, area.getRight() - x), Panel::height);
                    x += w + gap;
                }
            }
            y += Panel::height + gap;
        }
        return y - area.getY() - gap;
    }

    void Editor::resized()
    {
        auto r = getLocalBounds();
        header.setBounds (r.removeFromTop (headerH));
        r.reduce (gap, gap);
        const int meters = info.dynamics ? meterW : fxMeterW;
        auto right = r.removeFromRight (meters);
        r.removeFromRight (gap);
        if (gr)
        {
            gr->setBounds (right.removeFromRight (52));
            right.removeFromRight (6);
        }
        io.setBounds (right);

        if (isSqCompPage())
        {
            if (scPanel) { scPanel->setBounds (r.removeFromLeft (scColW).withHeight (scPanel->preferredHeight())); r.removeFromLeft (gap); }
            auto par = r.removeFromRight (parColW);
            r.removeFromRight (gap);
            if (parPanel) { parPanel->setBounds (par.removeFromTop (parPanel->preferredHeight())); par.removeFromTop (gap); }
            if (histogram) histogram->setBounds (par);
            if (compGraph) { compGraph->setBounds (r.removeFromTop (graphH)); r.removeFromTop (gap); }
            layoutPanels (r, true);
            return;
        }
        if (unitView)
        {
            if (dynGraph) { dynGraph->setBounds (r.removeFromTop (graphH)); r.removeFromTop (gap); }
            if (faceplate) { faceplate->setBounds (r.removeFromTop (faceplate->preferredHeight())); r.removeFromTop (gap); }
            layoutPanels (r, true);
            return;
        }
        if (compGraph) { compGraph->setBounds (r.removeFromTop (graphH)); r.removeFromTop (gap); }
        if (dynGraph) { dynGraph->setBounds (r.removeFromTop (graphH)); r.removeFromTop (gap); }
        layoutPanels (r, true);
    }

    void Editor::timerCallback()
    {
        io.update();
        const float g = proc.grDb.exchange (0.0f);
        if (gr) gr->setValue (g);
        const float det = proc.detectorDb.exchange (-100.0f);
        if (compGraph) { compGraph->push (det, g); compGraph->repaint(); }
        if (histogram) { histogram->push (det, g); histogram->repaint(); }
        if (faceplate) faceplate->update (g);
        if (dynGraph && (++frame % 2) == 0) dynGraph->repaint();
    }

    void Editor::toggleTheme()
    {
        const auto before = theme::currentPalette();
        theme::setLightMode (! theme::lightMode);
        theme::saveLightModePreference (theme::lightMode);
        lnf.applyColours();
        sqLnf.applyColours();
        theme::remapColours (*this, before, theme::currentPalette());
        sendLookAndFeelChange();
        repaint();
    }
}
