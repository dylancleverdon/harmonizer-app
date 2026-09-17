#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

/**
 * Logic Pro's own plugin-window language: flat neutral-graphite panels,
 * hairline dividers instead of shadows, thin-line knobs and sliders, and a
 * single Logic-blue accent for anything active or selected -- so the plugin
 * reads as a control surface that belongs in the chain, not a skin dropped
 * into it. Colours and control geometry confirmed against a screenshot of a
 * stock Logic plugin (Limiter) and Logic's own documented palette.
 */
namespace harmonizer::look {

inline const juce::Colour background{0xff1a1a1a};
inline const juce::Colour surface{0xff242424};
inline const juce::Colour surfaceVariant{0xff373737};
inline const juce::Colour accent{0xff00a3e0};
inline const juce::Colour onAccent{0xff1a1a1a};
inline const juce::Colour text{0xffd1d1d1};
inline const juce::Colour muted{0xff555555};
inline const juce::Colour warn{0xffffb300};
inline const juce::Colour error{0xffe60000};

inline constexpr int cardPadding = 16;
inline constexpr int rowGap = 10;
inline constexpr int cardGap = 14;

/** Thin-line rotary knob, flat linear slider, and flat buttons/toggles --
 *  Logic's own control shapes rather than JUCE's stock rounded defaults. */
class KnobLookAndFeel final : public juce::LookAndFeel_V4 {
public:
    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float pos, float startAngle, float endAngle,
                          juce::Slider&) override {
        const auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat().reduced(6.0f);
        const float stroke = 1.5f;
        const auto arcBounds = bounds.reduced(stroke * 0.5f);
        const auto centre = arcBounds.getCentre();
        const float radius = juce::jmin(arcBounds.getWidth(), arcBounds.getHeight()) * 0.5f;
        const float angle = startAngle + pos * (endAngle - startAngle);

        juce::Path track;
        track.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, startAngle, endAngle, true);
        g.setColour(surfaceVariant);
        g.strokePath(track, juce::PathStrokeType(stroke, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::butt));

        if (pos > 0.001f) {
            juce::Path value;
            value.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, startAngle, angle, true);
            g.setColour(accent);
            g.strokePath(value, juce::PathStrokeType(stroke, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::butt));
        }

        const float faceRadius = radius * 0.72f;
        const auto faceBounds = juce::Rectangle<float>(faceRadius * 2.0f, faceRadius * 2.0f)
                                     .withCentre(centre);
        g.setColour(surface);
        g.fillEllipse(faceBounds);
        g.setColour(surfaceVariant);
        g.drawEllipse(faceBounds, 1.0f);

        const juce::Point<float> inner{centre.x + faceRadius * 0.3f * std::sin(angle),
                                       centre.y - faceRadius * 0.3f * std::cos(angle)};
        const juce::Point<float> outer{centre.x + faceRadius * 0.92f * std::sin(angle),
                                       centre.y - faceRadius * 0.92f * std::cos(angle)};
        g.setColour(accent);
        g.drawLine({inner, outer}, 2.0f);
    }

    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float minSliderPos, float maxSliderPos,
                          const juce::Slider::SliderStyle style, juce::Slider& slider) override {
        if (style != juce::Slider::LinearHorizontal) {
            LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, sliderPos, minSliderPos,
                                             maxSliderPos, style, slider);
            return;
        }

        const float trackH = 3.0f;
        const float cy = static_cast<float>(y) + static_cast<float>(height) * 0.5f;
        g.setColour(slider.findColour(juce::Slider::backgroundColourId));
        g.fillRect(juce::Rectangle<float>(static_cast<float>(x), cy - trackH * 0.5f,
                                          static_cast<float>(width), trackH));
        g.setColour(slider.findColour(juce::Slider::trackColourId));
        g.fillRect(juce::Rectangle<float>(static_cast<float>(x), cy - trackH * 0.5f,
                                          sliderPos - static_cast<float>(x), trackH));

        const float thumbW = 4.0f, thumbH = 12.0f;
        g.setColour(slider.findColour(juce::Slider::thumbColourId));
        g.fillRect(juce::Rectangle<float>(sliderPos - thumbW * 0.5f, cy - thumbH * 0.5f,
                                          thumbW, thumbH));
    }

    void drawButtonBackground(juce::Graphics& g, juce::Button& button,
                              const juce::Colour& backgroundColour,
                              bool shouldDrawButtonAsHighlighted,
                              bool shouldDrawButtonAsDown) override {
        const auto bounds = button.getLocalBounds().toFloat();
        auto fill = backgroundColour;
        if (shouldDrawButtonAsDown) fill = fill.darker(0.15f);
        else if (shouldDrawButtonAsHighlighted) fill = fill.brighter(0.08f);

        if (button.getToggleState()) {
            g.setColour(fill);
            g.fillRect(bounds);
        } else {
            // Nearly flat -- Logic's own buttons vary only a few percent
            // brightness top to bottom, enough to read as a surface without
            // looking skeuomorphic.
            juce::ColourGradient grad(fill.brighter(0.03f), bounds.getX(), bounds.getY(),
                                      fill, bounds.getX(), bounds.getBottom(), false);
            g.setGradientFill(grad);
            g.fillRect(bounds);
        }
        g.setColour(surfaceVariant);
        g.drawRect(bounds, 1.0f);
    }

    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                          bool /*shouldDrawButtonAsHighlighted*/,
                          bool /*shouldDrawButtonAsDown*/) override {
        const float boxSize = 14.0f;
        const auto bounds = button.getLocalBounds().toFloat();
        const juce::Rectangle<float> box(bounds.getX(), bounds.getCentreY() - boxSize * 0.5f,
                                         boxSize, boxSize);

        g.setColour(button.findColour(juce::ToggleButton::tickDisabledColourId));
        g.drawRect(box, 1.0f);
        if (button.getToggleState()) {
            g.setColour(button.findColour(juce::ToggleButton::tickColourId));
            g.fillRect(box.reduced(3.0f));
        }

        g.setColour(button.findColour(juce::ToggleButton::textColourId));
        g.setFont(juce::FontOptions(13.0f));
        const int textX = static_cast<int>(box.getRight()) + 8;
        g.drawFittedText(button.getButtonText(),
                         juce::Rectangle<int>(textX, 0, button.getWidth() - textX,
                                               button.getHeight()),
                         juce::Justification::centredLeft, 1);
    }
};

/** Flat rectangular tab, the desktop equivalent of the app's filter chips. */
class Chip final : public juce::Button {
public:
    explicit Chip(const juce::String& label) : juce::Button(label) { setClickingTogglesState(false); }

    void paintButton(juce::Graphics& g, bool hover, bool) override {
        const auto r = getLocalBounds().toFloat();
        const bool on = getToggleState();
        g.setColour(on ? accent : surfaceVariant);
        g.fillRect(r);
        if (!on) {
            g.setColour(hover ? accent.withAlpha(0.5f) : surfaceVariant.brighter(0.15f));
            g.drawRect(r, 1.0f);
        }
        g.setColour(on ? harmonizer::look::onAccent : harmonizer::look::text);
        g.setFont(juce::FontOptions(13.0f));
        g.drawText(getButtonText(), getLocalBounds(), juce::Justification::centred);
    }
};

/** Label on the left, value right-aligned in a monospaced face, as in the app. */
class StatRow final : public juce::Component {
public:
    StatRow(juce::String label, bool emphasis = false)
        : label_(std::move(label)), emphasis_(emphasis) {}

    void setValue(const juce::String& v) {
        if (v == value_) return;
        value_ = v;
        repaint();
    }
    void setLabel(const juce::String& l) { label_ = l; repaint(); }

    void paint(juce::Graphics& g) override {
        g.setFont(juce::FontOptions(13.0f));
        g.setColour(muted);
        g.drawText(label_, 0, 0, getWidth() / 2, getHeight(), juce::Justification::centredLeft);
        g.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(),
                                    emphasis_ ? 15.0f : 13.0f, juce::Font::plain));
        g.setColour(emphasis_ ? accent : text);
        g.drawText(value_, getWidth() / 2, 0, getWidth() / 2, getHeight(),
                   juce::Justification::centredRight);
    }

private:
    juce::String label_, value_;
    bool emphasis_;
};

/** Thin flat bar, amber past warnAbove and red past dangerAbove. */
class Meter final : public juce::Component {
public:
    void setLevel(float v) {
        const float clamped = juce::jlimit(0.0f, 1.0f, v);
        if (std::abs(clamped - level_) < 0.005f) return;
        level_ = clamped;
        repaint();
    }

    void paint(juce::Graphics& g) override {
        const auto r = getLocalBounds().toFloat();
        g.setColour(surfaceVariant);
        g.fillRect(r);
        if (level_ <= 0.001f) return;
        const auto colour = level_ >= 0.9f ? error : (level_ >= 0.7f ? warn : accent);
        g.setColour(colour);
        g.fillRect(r.withWidth(r.getWidth() * level_));
    }

private:
    float level_ = 0.0f;
};

/** Ten flat squares, lit for each sounding voice. */
class VoiceDots final : public juce::Component {
public:
    void setActive(int n) {
        if (n == active_) return;
        active_ = n;
        repaint();
    }
    void paint(juce::Graphics& g) override {
        for (int i = 0; i < 10; ++i) {
            g.setColour(i < active_ ? accent : surfaceVariant);
            g.fillRect(static_cast<float>(i) * 16.0f, 0.0f, 10.0f,
                      static_cast<float>(getHeight()));
        }
    }

private:
    int active_ = 0;
};

/** Small wrapped paragraph in the muted body style the app uses under controls. */
class Note final : public juce::Component {
public:
    void setText(const juce::String& t, juce::Colour c = muted) {
        if (t == text_ && c == colour_) return;
        text_ = t;
        colour_ = c;
        repaint();
    }
    void paint(juce::Graphics& g) override {
        g.setColour(colour_);
        g.setFont(juce::FontOptions(12.0f));
        g.drawFittedText(text_, getLocalBounds(), juce::Justification::topLeft, 4);
    }

private:
    juce::String text_;
    juce::Colour colour_{muted};
};

/** Flat panel with a thin top divider and a small dense section title,
 *  stacking its rows vertically -- Logic's own sub-panel treatment rather
 *  than a padded, shadowed card. */
class Card final : public juce::Component {
public:
    explicit Card(juce::String title) : title_(std::move(title).toUpperCase()) {}

    void addRow(juce::Component& c, int height) {
        rows_.push_back({&c, height});
        addAndMakeVisible(c);
    }

    /** Detaches every row added so far, without destroying the components
     *  themselves -- for a card whose content is rebuilt (a filtered list,
     *  say) rather than fixed for the page's lifetime. The caller owns
     *  actually destroying or reusing whatever it had added. */
    void clearRows() {
        for (auto& r : rows_) removeChildComponent(r.component);
        rows_.clear();
    }

    int preferredHeight() const {
        int h = cardPadding + 18 + rowGap;
        for (const auto& r : rows_) h += r.height + rowGap;
        return h + cardPadding - rowGap;
    }

    void paint(juce::Graphics& g) override {
        g.setColour(surface);
        g.fillRect(getLocalBounds());
        g.setColour(surfaceVariant);
        g.fillRect(0, 0, getWidth(), 1);
        g.setColour(muted);
        g.setFont(juce::FontOptions(11.0f));
        g.drawText(title_, cardPadding, cardPadding - 4, getWidth(), 16,
                   juce::Justification::centredLeft);
    }

    void resized() override {
        int y = cardPadding + 18 + rowGap;
        for (auto& r : rows_) {
            r.component->setBounds(cardPadding, y, getWidth() - cardPadding * 2, r.height);
            y += r.height + rowGap;
        }
    }

private:
    struct Row { juce::Component* component; int height; };
    juce::String title_;
    std::vector<Row> rows_;
};

}  // namespace harmonizer::look
