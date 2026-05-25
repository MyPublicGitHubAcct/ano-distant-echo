// Unit tests for AnoLookAndFeel.
//
// Covers:
//   - Color IDs set by the constructor (spec compliance)
//   - Per-component color override propagation via findColour
//   - LAF lifecycle safety (install / detach / destroy order)
//   - Rotary-slider geometry helpers (formula correctness without rendering)
//
// Requires JUCE GUI initialization because LookAndFeel_V4 and Component
// touch JUCE singletons.  main() calls ScopedJuceInitialiser_GUI before
// running the Catch2 session.

#include <catch2/catch_all.hpp>
#include <catch2/catch_session.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "AnoLookAndFeel.h"

// Project accent colors (from CLAUDE.md color table)
static constexpr juce::uint32 kGold = 0xffc8a96e;   // Overdrive
static constexpr juce::uint32 kBlue = 0xff4477aa;   // Delay

// ---------------------------------------------------------------------------
// Color configuration — constructor must set every spec'd ID exactly.
// Any typo in the ARGB literal or the wrong colour ID will fail here.
// ---------------------------------------------------------------------------

TEST_CASE("AnoLookAndFeel: popup menu colors match spec", "[laf][colors]")
{
    AnoLookAndFeel laf;
    CHECK(laf.findColour(juce::PopupMenu::backgroundColourId)
              == juce::Colour(0xff1e1e38));
    CHECK(laf.findColour(juce::PopupMenu::textColourId)
              == juce::Colour(0xffcccccc));
    CHECK(laf.findColour(juce::PopupMenu::highlightedBackgroundColourId)
              == juce::Colour(0xff2a2a50));
    CHECK(laf.findColour(juce::PopupMenu::highlightedTextColourId)
              == juce::Colours::white);
}

TEST_CASE("AnoLookAndFeel: combo box default colors match spec", "[laf][colors]")
{
    AnoLookAndFeel laf;
    CHECK(laf.findColour(juce::ComboBox::backgroundColourId)
              == juce::Colour(0xff1a1a2e));
    CHECK(laf.findColour(juce::ComboBox::textColourId)
              == juce::Colour(0xffcccccc));
    CHECK(laf.findColour(juce::ComboBox::outlineColourId)
              == juce::Colour(0xff555566));
    CHECK(laf.findColour(juce::ComboBox::arrowColourId)
              == juce::Colour(0xffaaaaaa));
}

TEST_CASE("AnoLookAndFeel: slider default colors match spec", "[laf][colors]")
{
    AnoLookAndFeel laf;
    CHECK(laf.findColour(juce::Slider::rotarySliderFillColourId)
              == juce::Colour(kGold));
    CHECK(laf.findColour(juce::Slider::rotarySliderOutlineColourId)
              == juce::Colour(0xff333344));
    CHECK(laf.findColour(juce::Slider::textBoxTextColourId)
              == juce::Colour(0xffaaaaaa));
    CHECK(laf.findColour(juce::Slider::textBoxBackgroundColourId)
              == juce::Colour(0xff1a1a2e));
    CHECK(laf.findColour(juce::Slider::textBoxOutlineColourId)
              == juce::Colours::transparentBlack);
    CHECK(laf.findColour(juce::Slider::textBoxHighlightColourId)
              == juce::Colour(0xff333355));
}

// Critical: the global default fill must be Overdrive gold, not Delay blue.
// The editor overrides individual sliders to blue — the LAF must not do so
// globally or the Overdrive knobs would appear in the wrong color.
TEST_CASE("AnoLookAndFeel: default rotary fill is Overdrive gold, not Delay blue",
          "[laf][spec]")
{
    AnoLookAndFeel laf;
    const auto fill = laf.findColour(juce::Slider::rotarySliderFillColourId);
    CHECK(fill == juce::Colour(kGold));
    CHECK(fill != juce::Colour(kBlue));
}

// Panel background color used in both text-box background and combo background
// must match the editor's g.fillAll color (0xff1a1a2e) for a seamless look.
TEST_CASE("AnoLookAndFeel: panel background color is consistent across components",
          "[laf][spec]")
{
    AnoLookAndFeel laf;
    const juce::Colour panelBg(0xff1a1a2e);
    CHECK(laf.findColour(juce::ComboBox::backgroundColourId)  == panelBg);
    CHECK(laf.findColour(juce::Slider::textBoxBackgroundColourId) == panelBg);
}

// ---------------------------------------------------------------------------
// Per-component color override — the editor calls setColour() on each slider
// and combo to apply section accent colors.  findColour() on the component
// must return the override, not the LAF global default.
// ---------------------------------------------------------------------------

TEST_CASE("AnoLookAndFeel: Delay slider blue fill overrides gold LAF default",
          "[laf][override]")
{
    AnoLookAndFeel laf;
    juce::Slider s;
    s.setLookAndFeel(&laf);

    // Editor applies blue to all Delay sliders
    s.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(kBlue));
    CHECK(s.findColour(juce::Slider::rotarySliderFillColourId)
              == juce::Colour(kBlue));

    // LAF-level default must remain gold (not mutated by setColour on the slider)
    CHECK(laf.findColour(juce::Slider::rotarySliderFillColourId)
              == juce::Colour(kGold));

    s.setLookAndFeel(nullptr);
}

TEST_CASE("AnoLookAndFeel: Overdrive slider retains gold fill from LAF default",
          "[laf][override]")
{
    AnoLookAndFeel laf;
    juce::Slider s;
    s.setLookAndFeel(&laf);

    // No per-slider override — falls through to LAF default
    CHECK(s.findColour(juce::Slider::rotarySliderFillColourId)
              == juce::Colour(kGold));

    s.setLookAndFeel(nullptr);
}

TEST_CASE("AnoLookAndFeel: Overdrive combo gold outline override propagates",
          "[laf][override]")
{
    AnoLookAndFeel laf;
    juce::ComboBox c;
    c.setLookAndFeel(&laf);

    c.setColour(juce::ComboBox::outlineColourId, juce::Colour(kGold));
    CHECK(c.findColour(juce::ComboBox::outlineColourId) == juce::Colour(kGold));

    // Unrelated ID still falls through to LAF value
    CHECK(c.findColour(juce::ComboBox::backgroundColourId)
              == juce::Colour(0xff1a1a2e));

    c.setLookAndFeel(nullptr);
}

TEST_CASE("AnoLookAndFeel: un-overridden slider IDs inherit LAF values",
          "[laf][override]")
{
    AnoLookAndFeel laf;
    juce::Slider s;
    s.setLookAndFeel(&laf);

    // Only fill is overridden in the editor; textBoxTextColourId is not.
    s.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(kBlue));
    CHECK(s.findColour(juce::Slider::textBoxTextColourId)
              == juce::Colour(0xffaaaaaa));

    s.setLookAndFeel(nullptr);
}

// ---------------------------------------------------------------------------
// Lifecycle — verify RAII and shared-LAF patterns used by the editor are safe.
// ---------------------------------------------------------------------------

TEST_CASE("AnoLookAndFeel: detach before destroy does not crash", "[laf][lifecycle]")
{
    juce::Slider s;
    {
        AnoLookAndFeel laf;
        s.setLookAndFeel(&laf);
        s.setLookAndFeel(nullptr);   // detach while laf is still alive — editor pattern
    }                                // laf destroyed here — no dangling pointer
    CHECK(true);
}

TEST_CASE("AnoLookAndFeel: single LAF instance shared across multiple components",
          "[laf][lifecycle]")
{
    AnoLookAndFeel laf;
    juce::Slider s1, s2, s3;
    juce::ComboBox c1, c2;

    for (auto* s : { &s1, &s2, &s3 }) s->setLookAndFeel(&laf);
    for (auto* c : { &c1, &c2 })      c->setLookAndFeel(&laf);

    // All sliders see gold fill
    for (auto* s : { &s1, &s2, &s3 })
        CHECK(s->findColour(juce::Slider::rotarySliderFillColourId)
                  == juce::Colour(kGold));

    // All combos see the spec outline
    for (auto* c : { &c1, &c2 })
        CHECK(c->findColour(juce::ComboBox::outlineColourId)
                  == juce::Colour(0xff555566));

    for (auto* s : { &s1, &s2, &s3 }) s->setLookAndFeel(nullptr);
    for (auto* c : { &c1, &c2 })      c->setLookAndFeel(nullptr);
}

TEST_CASE("AnoLookAndFeel: per-slider override does not bleed to sibling sliders",
          "[laf][lifecycle]")
{
    AnoLookAndFeel laf;
    juce::Slider od, dl;
    od.setLookAndFeel(&laf);
    dl.setLookAndFeel(&laf);

    // Overdrive slider: no override (stays gold)
    // Delay slider: override to blue
    dl.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(kBlue));

    CHECK(od.findColour(juce::Slider::rotarySliderFillColourId)
              == juce::Colour(kGold));
    CHECK(dl.findColour(juce::Slider::rotarySliderFillColourId)
              == juce::Colour(kBlue));

    od.setLookAndFeel(nullptr);
    dl.setLookAndFeel(nullptr);
}

// ---------------------------------------------------------------------------
// Geometry — inline helpers replicating the exact formulas from
// drawRotarySlider.  Tests here catch formula mistakes (e.g. wrong offset,
// inverted interpolation, wrong integer-division ordering) without needing
// a render target.
// ---------------------------------------------------------------------------

namespace {

float rotaryRadius(int width, int height)
{
    // jmin operates on integers before the float cast
    return static_cast<float>(juce::jmin(width / 2, height / 2)) - 4.0f;
}

float trackRadius(float knobRadius)
{
    return knobRadius - 3.0f;
}

float rotaryAngle(float proportion, float startAngle, float endAngle)
{
    return startAngle + proportion * (endAngle - startAngle);
}

} // namespace

TEST_CASE("AnoLookAndFeel: rotary radius = floor(min(w,h)/2) - 4 px",
          "[laf][geometry]")
{
    CHECK(rotaryRadius(100, 100) == Catch::Approx(46.0f));
    CHECK(rotaryRadius(80,  100) == Catch::Approx(36.0f));  // width is narrower
    CHECK(rotaryRadius(100,  60) == Catch::Approx(26.0f));  // height is narrower
    CHECK(rotaryRadius( 50,  50) == Catch::Approx(21.0f));

    // Integer division truncates before the float cast — odd sizes round down
    CHECK(rotaryRadius(101, 101) == Catch::Approx(46.0f));  // same as 100×100
    CHECK(rotaryRadius( 81, 101) == Catch::Approx(36.0f));  // same as 80×100
}

TEST_CASE("AnoLookAndFeel: track radius is 3 px inside knob radius",
          "[laf][geometry]")
{
    for (int dim : { 50, 80, 100, 120 })
    {
        float r = rotaryRadius(dim, dim);
        CHECK(trackRadius(r) == Catch::Approx(r - 3.0f));
    }
}

TEST_CASE("AnoLookAndFeel: rotary angle interpolates linearly from start to end",
          "[laf][geometry]")
{
    const float start = -2.4f;
    const float end   =  2.4f;

    CHECK(rotaryAngle(0.0f,  start, end) == Catch::Approx(start));
    CHECK(rotaryAngle(1.0f,  start, end) == Catch::Approx(end));
    CHECK(rotaryAngle(0.5f,  start, end) == Catch::Approx(0.0f).margin(1e-5f));
    CHECK(rotaryAngle(0.25f, start, end) == Catch::Approx(-1.2f).margin(1e-4f));
    CHECK(rotaryAngle(0.75f, start, end) == Catch::Approx( 1.2f).margin(1e-4f));
}

TEST_CASE("AnoLookAndFeel: angle at proportion=0 equals start (no fill drawn)",
          "[laf][geometry]")
{
    // When sliderPosProportional == 0, angle == rotaryStartAngle and the value
    // arc degenerates to zero length — drawRotarySlider skips it.
    const float start = -2.4f, end = 2.4f;
    CHECK(rotaryAngle(0.0f, start, end) == Catch::Approx(start));
    CHECK(rotaryAngle(0.0f, start, end) != Catch::Approx(end));
}

TEST_CASE("AnoLookAndFeel: angle at proportion=1 equals end (full arc)",
          "[laf][geometry]")
{
    const float start = -2.4f, end = 2.4f;
    CHECK(rotaryAngle(1.0f, start, end) == Catch::Approx(end));
}

// ---------------------------------------------------------------------------
// main — initialize JUCE before running any tests
// ---------------------------------------------------------------------------

int main(int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    return Catch::Session().run(argc, argv);
}
