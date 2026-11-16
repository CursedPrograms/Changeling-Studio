// Changeling Studio - the splash (after Mimic's): the logo's split face drawn large, a second face's outline lifting off it, the name, the
// version, the credits and what's loading. Solid colours only (Atelier greys + Cursed pink), drawn with ImGui's
// draw list so it's sharp at any size.
#include "splash.h"

#include <imgui.h>

#include <cfloat>
#include <cmath>

#ifndef STUDIO_VERSION
#define STUDIO_VERSION "0.1.0"
#endif

namespace Splash {

static ImFont *titleFont = nullptr, *smallFont = nullptr;

void setFonts(ImFont* title, ImFont* small) { titleFont = title; smallFont = small; }

static const ImU32 BG = IM_COL32(0x1a, 0x1a, 0x1a, 255), LINE = IM_COL32(0x33, 0x33, 0x33, 255),
                   TEXT = IM_COL32(0xd6, 0xd6, 0xd6, 255), DIM = IM_COL32(0x9a, 0x9a, 0x9a, 255),
                   PINK = IM_COL32(0x00, 0x7b, 0xb7, 255), DARK = IM_COL32(0x3a, 0x3a, 0x3a, 255),
                   GHOST = IM_COL32(0xd6, 0xd6, 0xd6, 150);

static void text(ImDrawList* dl, ImFont* f, float size, ImVec2 p, ImU32 col, const char* s) {
    if (!f) f = ImGui::GetFont();
    dl->AddText(f, size, p, col, s);
}
static float textWidth(ImFont* f, float size, const char* s) {
    if (!f) f = ImGui::GetFont();
    return f->CalcTextSizeA(size, FLT_MAX, 0, s).x;
}

// the logo's face: a disc split down the middle, grey (you) and pink (who you become), eyes and a smile each in
// the other half's colour
static void face(ImDrawList* dl, ImVec2 c, float r) {
    dl->AddCircleFilled(c, r, DARK, 96);
    // the right half in pink: a fan of the half circle
    dl->PathClear();
    dl->PathLineTo(ImVec2(c.x, c.y - r));
    dl->PathArcTo(c, r, -1.5707963f, 1.5707963f, 64);
    dl->PathLineTo(ImVec2(c.x, c.y + r));
    dl->PathFillConvex(PINK);
    const float er = r * 0.14f, ey = c.y - r * 0.2f;
    dl->AddCircleFilled(ImVec2(c.x - r * 0.38f, ey), er, PINK, 32);
    dl->AddCircleFilled(ImVec2(c.x + r * 0.38f, ey), er, DARK, 32);
    const float sr = r * 0.45f, sy = c.y - r * 0.12f, th = r * 0.1f;
    dl->PathClear(); dl->PathArcTo(ImVec2(c.x, sy), sr, 1.5707963f, 2.7925268f, 24); dl->PathStroke(PINK, 0, th);
    dl->PathClear(); dl->PathArcTo(ImVec2(c.x, sy), sr, 0.3490659f, 1.5707963f, 24); dl->PathStroke(DARK, 0, th);
}

// a stopped dashed circle: the second face, lifting off
static void dashedCircle(ImDrawList* dl, ImVec2 c, float r, ImU32 col) {
    const int n = 72;
    for (int i = 0; i < n; i += 2) {
        const float a0 = 6.2831853f * i / n, a1 = 6.2831853f * (i + 1) / n;
        dl->AddLine(ImVec2(c.x + std::cos(a0) * r, c.y + std::sin(a0) * r), ImVec2(c.x + std::cos(a1) * r, c.y + std::sin(a1) * r), col, 1.0f);
    }
}

void draw(ImDrawList* dl, float x, float y, const char* status, float progress) {
    const ImVec2 o(x, y), end(x + W, y + H);
    dl->PushClipRect(o, end, true);
    dl->AddRectFilled(o, end, BG);

    // the art: the face large, bleeding off the right edge, a second face's outline lifting off up and right
    const ImVec2 c(x + 520, y + 185);
    face(dl, c, 150);
    dashedCircle(dl, ImVec2(c.x + 26, c.y - 30), 150, GHOST);

    // the name: "Changeling" in the text colour, "Studio" in the Atelier blue
    const float ts = 46, tx = x + 40, ty = y + 92;
    text(dl, titleFont, ts, ImVec2(tx, ty), TEXT, "Changeling");
    text(dl, titleFont, ts, ImVec2(tx + textWidth(titleFont, ts, "Changeling "), ty), PINK, "Studio");
    text(dl, nullptr, 16, ImVec2(tx + 2, ty + 62), DIM, "Version " STUDIO_VERSION);
    text(dl, nullptr, 16, ImVec2(tx + 2, ty + 84), DIM, "Every Changeling in one place, by Cursed Entertainment");

    // the credits, small, like the names on a splash
    const float cs = 13, cy = y + 306;
    text(dl, smallFont, cs, ImVec2(tx, cy), DIM, "Imago after roop and FaceFusion. Models: InsightFace, GFPGAN, CodeFormer, BiSeNet, FairFace.");
    text(dl, smallFont, cs, ImVec2(tx, cy + 17), DIM, "Built with Dear ImGui, SDL2, stb, ONNX Runtime with DirectML, and ffmpeg. Face core from Mimic.");
    text(dl, smallFont, cs, ImVec2(tx, cy + 34), DIM, "Farica\xe2\x98\x86Kimora \xc2\xa9 Cursed Entertainment");

    // what's loading, and how far along
    if (status) text(dl, nullptr, 16, ImVec2(tx, y + 368), TEXT, status);
    if (progress >= 0) {
        const float p = progress > 1 ? 1 : progress;
        dl->AddRectFilled(ImVec2(x, end.y - 3), end, LINE);
        dl->AddRectFilled(ImVec2(x, end.y - 3), ImVec2(x + W * p, end.y), PINK);
    }
    dl->AddRect(o, end, LINE);
    dl->PopClipRect();
}

}  // namespace Splash
