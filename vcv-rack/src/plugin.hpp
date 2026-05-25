#pragma once
#include <rack.hpp>

using namespace rack;

extern Plugin* pluginInstance;

extern Model* modelOverdrive;
extern Model* modelDelay;

// Panel text label rendered with nanoVG — needed because nanosvg (used by VCV
// Rack to draw panel SVGs) does not support SVG <text> elements.
struct PanelLabel : widget::Widget {
    std::string text;
    NVGcolor color = nvgRGB(0xaa, 0xaa, 0xaa);
    float fontSize = 9.f;

    void draw(const DrawArgs& args) override {
        if (text.empty()) return;
        auto font = APP->window->loadFont(asset::system("res/fonts/DejaVuSans.ttf"));
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        nvgFontSize(args.vg, fontSize);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(args.vg, color);
        nvgText(args.vg, 0, 0, text.c_str(), nullptr);
    }
};

// Factory: place label centred at centerPx (use mm2px to convert from mm).
inline PanelLabel* panelLabel(math::Vec centerPx, const std::string& text,
                               NVGcolor color = nvgRGB(0xaa, 0xaa, 0xaa),
                               float fontSize = 9.f) {
    auto* lbl = new PanelLabel;
    lbl->box.pos  = centerPx;
    lbl->box.size = {0.f, 0.f};
    lbl->text     = text;
    lbl->color    = color;
    lbl->fontSize = fontSize;
    return lbl;
}
