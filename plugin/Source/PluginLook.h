#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

/**
 * The Android app's visual language, reused here so the plugin and the phone
 * look like one product: dark ground, rounded surface cards with small
 * upper-case titles, a teal accent, pill-shaped selectors and thin bar meters.
 */
namespace harmonizer::look {

inline const juce::Colour background{0xff0e1113};
inline const juce::Colour surface{0xff171b1e};
inline const juce::Colour surfaceVariant{0xff232a2e};
inline const juce::Colour accent{0xff4dd0c0};
inline const juce::Colour onAccent{0xff00201c};
inline const juce::Colour text{0xffe3e6e8};
inline const juce::Colour muted{0xffa8b4b8};
inline const juce::Colour warn{0xffe8a33d};
inline const juce::Colour error{0xffe05c5c};

inline constexpr int cardPadding = 16;
inline constexpr int rowGap = 10;
inline constexpr int cardGap = 14;

/** Matches the app's knob: 270 degrees of travel, thick track, accent fill. */
class KnobLookAndFeel final : public juce::LookAndFeel_V4 {
public:
    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float pos, float startAngle, float endAngle,
                          juce::Slider&) override {
        const auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat().reduced(6.0f);
        const float stroke = juce::jmin(14.0f, bounds.getWidth() * 0.11f);
        const auto arcBounds = bounds.reduced(stroke * 0.5f);
        const auto centre = arcBounds.getCentre();
        const float radius = juce::jmin(arcBounds.getWidth(), arcBounds.getHeight()) * 0.5f;
        const float angle = startAngle + pos * (endAngle - startAngle);

        juce::Path track;
        track.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, startAngle, endAngle, true);
        g.setColour(surfaceVariant);
        g.strokePath(track, juce::PathStrokeType(stroke, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));

        if (pos > 0.001f) {
            juce::Path value;
            value.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, startAngle, angle, true);
            g.setColour(accent);
            g.strokePath(value, juce::PathStrokeType(stroke, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));
        }

        g.setColour(surface);
        g.fillEllipse(juce::Rectangle<float>(radius * 2.0f - stroke * 2.0f,
                                             radius * 2.0f - stroke * 2.0f)
                          .withCentre(centre));

        const juce::Point<float> inner{centre.x + radius * 0.34f * std::sin(angle),
                                       centre.y - radius * 0.34f * std::cos(angle)};
        const juce::Point<float> outer{centre.x + (radius - stroke * 1.1f) * std::sin(angle),
                                       centre.y - (radius - stroke * 1.1f) * std::cos(angle)};
        g.setColour(accent);
        g.drawLine({inner, outer}, 4.0f);
    }
};

/** Pill selector, the desktop equivalent of the app's filter chips. */
class Chip final : public juce::Button {
public:
    explicit Chip(const juce::String& label) : juce::Button(label) { setClickingTogglesState(false); }

    void paintButton(juce::Graphics& g, bool hover, bool) override {
        const auto r = getLocalBounds().toFloat().reduced(1.0f);
        const bool on = getToggleState();
        g.setColour(on ? accent : surfaceVariant);
        g.fillRoundedRectangle(r, r.getHeight() * 0.5f);
        if (!on && hover) {
            g.setColour(accent.withAlpha(0.35f));
            g.drawRoundedRectangle(r, r.getHeight() * 0.5f, 1.0f);
        }
        g.setColour(on ? harmonizer::look::onAccent : harmonizer::look::text);
        g.setFont(juce::FontOptions(13.0f, on ? juce::Font::bold : juce::Font::plain));
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

/** Thin rounded bar, amber past warnAbove and red past dangerAbove. */
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
        g.fillRoundedRectangle(r, r.getHeight() * 0.5f);
        if (level_ <= 0.001f) return;
        const auto colour = level_ >= 0.9f ? error : (level_ >= 0.7f ? warn : accent);
        g.setColour(colour);
        g.fillRoundedRectangle(r.withWidth(r.getWidth() * level_), r.getHeight() * 0.5f);
    }

private:
    float level_ = 0.0f;
};

/** Ten dots, lit for each sounding voice. */
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
            g.fillRoundedRectangle(static_cast<float>(i) * 16.0f, 0.0f, 10.0f,
                                   static_cast<float>(getHeight()), 5.0f);
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

/** Rounded surface with a small upper-case title, stacking its rows vertically. */
class Card final : public juce::Component {
public:
    explicit Card(juce::String title) : title_(std::move(title).toUpperCase()) {}

    void addRow(juce::Component& c, int height) {
        rows_.push_back({&c, height});
        addAndMakeVisible(c);
    }

    int preferredHeight() const {
        int h = cardPadding + 18 + rowGap;
        for (const auto& r : rows_) h += r.height + rowGap;
        return h + cardPadding - rowGap;
    }

    void paint(juce::Graphics& g) override {
        g.setColour(surface);
        g.fillRoundedRectangle(getLocalBounds().toFloat(), 12.0f);
        g.setColour(muted);
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
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
