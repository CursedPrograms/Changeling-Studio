// Changeling Studio - Cosmeta in C++: see cosmeta.h.
#include "cosmeta.h"

#include <algorithm>
#include <cmath>

namespace Cosmeta {

const char* parserModel() { return "bisenet_resnet_34.onnx"; }

// BiSeNet's classes (CelebAMask-HQ)
enum { C_BG, C_SKIN, C_LBROW, C_RBROW, C_LEYE, C_REYE, C_GLASSES, C_LEAR, C_REAR, C_EARRING, C_NOSE, C_MOUTH, C_ULIP, C_LLIP, C_NECK, C_NECKLACE, C_CLOTH, C_HAIR, C_HAT, C_N };

static const int N = 512;   // the parser's size
static int S = 512;         // the crop's: the picture's own resolution round the head (so nothing gets softer)

static float luma(const float* c) { return 0.299f * c[0] + 0.587f * c[1] + 0.114f * c[2]; }
// Photoshop's Colour blend: the colour's hue and saturation, the pixel's lightness
static void colourBlend(const float* px, const float* col, float* out) {
    const float d = luma(px) - luma(col);
    for (int k = 0; k < 3; ++k) out[k] = col[k] + d;
    // back into range without changing the lightness
    const float l = luma(px), lo = std::min({out[0], out[1], out[2]}), hi = std::max({out[0], out[1], out[2]});
    if (lo < 0) for (int k = 0; k < 3; ++k) out[k] = l + (out[k] - l) * l / std::max(1e-5f, l - lo);
    if (hi > 1) for (int k = 0; k < 3; ++k) out[k] = l + (out[k] - l) * (1 - l) / std::max(1e-5f, hi - l);
}
static void blurRGB(std::vector<float>& px, int w, int h, float sigma) {
    std::vector<float> ch((size_t)w * h);
    for (int k = 0; k < 3; ++k) {
        for (size_t i = 0; i < ch.size(); ++i) ch[i] = px[i * 3 + k];
        gaussianBlur(ch, w, h, sigma);
        for (size_t i = 0; i < ch.size(); ++i) px[i * 3 + k] = ch[i];
    }
}

static bool parse(const Crop& crop, std::vector<int>& cls, std::string& err) {
    Crop small(N, N);
    for (int y = 0; y < N; ++y) for (int x = 0; x < N; ++x) sampleCrop(crop, (x + 0.5) * crop.w / N - 0.5, (y + 0.5) * crop.h / N - 0.5, &small.px[((size_t)y * N + x) * 3]);
    OrtSession* s = Ort::open(parserModel(), err);
    if (!s) return false;
    const float mean[3] = {0.485f, 0.456f, 0.406f}, sd[3] = {0.229f, 0.224f, 0.225f};
    Ort::Tensor in; in.name = Ort::inputNames(s)[0]; in.shape = {1, 3, N, N}; in.f.resize((size_t)3 * N * N);
    for (int i = 0; i < N * N; ++i) for (int k = 0; k < 3; ++k) in.f[(size_t)k * N * N + i] = (small.px[(size_t)i * 3 + k] / 255.0f - mean[k]) / sd[k];
    std::vector<Ort::Tensor> ins = {in}, outs;
    if (!Ort::run(s, ins, outs, err) || outs.empty()) return false;
    const auto& o = outs[0].f;
    const int C = (int)(o.size() / ((size_t)N * N));
    std::vector<int> c512((size_t)N * N, 0);
    for (int i = 0; i < N * N; ++i) {
        int best = 0; float bv = o[i];
        for (int c = 1; c < C; ++c) if (o[(size_t)c * N * N + i] > bv) { bv = o[(size_t)c * N * N + i]; best = c; }
        c512[i] = best;
    }
    cls.assign((size_t)S * S, 0);
    for (int y = 0; y < S; ++y) for (int x = 0; x < S; ++x) cls[(size_t)y * S + x] = c512[(size_t)std::min(N - 1, y * N / S) * N + std::min(N - 1, x * N / S)];
    return true;
}
// a soft mask of some classes
static std::vector<float> maskOf(const std::vector<int>& cls, std::initializer_list<int> which, float blur) {
    std::vector<float> m((size_t)S * S, 0.0f);
    for (size_t i = 0; i < m.size(); ++i) for (int c : which) if (cls[i] == c) { m[i] = 1; break; }
    gaussianBlur(m, S, S, blur);
    return m;
}
// a soft round blob (centre, radii in crop pixels)
static void blob(std::vector<float>& m, float cx, float cy, float rx, float ry) {
    for (int y = std::max(0, (int)(cy - ry * 2)); y < std::min(S, (int)(cy + ry * 2)); ++y)
        for (int x = std::max(0, (int)(cx - rx * 2)); x < std::min(S, (int)(cx + rx * 2)); ++x) {
            const float dx = (x - cx) / rx, dy = (y - cy) / ry;
            m[(size_t)y * S + x] = std::max(m[(size_t)y * S + x], std::exp(-(dx * dx + dy * dy) * 1.2f));
        }
}

bool apply(Img& im, const std::vector<Face>& faces, const Look& L, std::string& err) {
    if (!L.any()) return true;
    for (const Face& f : faces) {
        // the face aligned, then zoomed out (x0.55 about the middle) so the hair is in the crop too; the crop as big as
        // the head is in the picture (512..2048), so the looks are laid at full detail
        Affine M = alignFace(f.kps, T_FFHQ_512, N);
        const double z = 0.55;
        for (int k : {0, 1, 3, 4}) M.m[k] *= z;
        M.m[2] = M.m[2] * z + N / 2.0 * (1 - z); M.m[5] = M.m[5] * z + N / 2.0 * (1 - z);
        S = std::clamp((int)std::lround(N / std::max(1e-6, M.scale()) / 16) * 16, N, 2048);
        for (double& v : M.m) v *= (double)S / N;
        Crop c = warpCrop(im, M, S, S);
        std::vector<int> cls;
        if (!parse(c, cls, err)) return false;
        // the five points in the crop: eyes, nose, mouth corners
        double k[10];
        for (int i = 0; i < 5; ++i) M.apply(f.kps[i * 2], f.kps[i * 2 + 1], k[i * 2], k[i * 2 + 1]);
        const float eyeGap = (float)std::hypot(k[2] - k[0], k[3] - k[1]);
        const float fb = 1.0f + L.feather * eyeGap * 0.06f;   // edges this soft (pixels of blur)
        Crop out = c;
        std::vector<float> touched((size_t)S * S, 0.0f);
        auto lay = [&](const std::vector<float>& mask, float amount, auto&& op) {   // op(px 0..1, out 0..1) where mask * amount
            if (amount <= 0) return;
            for (size_t i = 0; i < mask.size(); ++i) {
                const float a = std::clamp(mask[i] * amount, 0.0f, 1.0f);
                if (a <= 0.002f) continue;
                float p[3], r[3];
                for (int q = 0; q < 3; ++q) p[q] = out.px[i * 3 + q] / 255.0f;
                op(p, r, i);
                for (int q = 0; q < 3; ++q) out.px[i * 3 + q] = std::clamp(p[q] + (r[q] - p[q]) * a, 0.0f, 1.0f) * 255.0f;
                touched[i] = std::max(touched[i], a);
            }
        };
        const auto skin = maskOf(cls, {C_SKIN}, fb);
        // the skin first: smoothed (a blur mixed back with some of its texture), then a glow
        if (L.smooth > 0) {
            std::vector<float> soft = out.px; blurRGB(soft, S, S, eyeGap * 0.035f);
            lay(skin, L.smooth * 0.85f, [&](const float* p, float* r, size_t i) { for (int q = 0; q < 3; ++q) { const float b = soft[i * 3 + q] / 255.0f; r[q] = b + (p[q] - b) * 0.35f; } });
        }
        if (L.glow > 0) lay(skin, L.glow * 0.5f, [&](const float* p, float* r, size_t) { for (int q = 0; q < 3; ++q) r[q] = 1 - (1 - p[q]) * (1 - p[q] * 0.6f); });   // screen
        // blush: soft on the cheeks, under the eyes and out from the nose
        if (L.blush > 0) {
            std::vector<float> m((size_t)S * S, 0.0f);
            for (int s = 0; s < 2; ++s) {
                const float ex = (float)k[s * 2], ey = (float)k[s * 2 + 1], mx = (float)k[6 + s * 2], my = (float)k[7 + s * 2];
                blob(m, ex * 0.55f + mx * 0.45f + (s == 0 ? -1 : 1) * eyeGap * 0.18f, ey * 0.4f + my * 0.6f - eyeGap * 0.12f, eyeGap * 0.38f, eyeGap * 0.26f);
            }
            for (size_t i = 0; i < m.size(); ++i) m[i] *= skin[i];
            lay(m, L.blush * 0.45f, [&](const float* p, float* r, size_t) { for (int q = 0; q < 3; ++q) r[q] = p[q] * (0.45f + 0.55f * L.blushColour[q]) + L.blushColour[q] * 0.06f; });
        }
        // eye shadow: on the lids, between the eye and the brow (skin only), and liner round the eye
        if (L.shadow > 0 || L.liner > 0) {
            const auto eyes = maskOf(cls, {C_LEYE, C_REYE}, 0);
            std::vector<float> lid((size_t)S * S, 0.0f), line((size_t)S * S, 0.0f);
            for (int s = 0; s < 2; ++s) blob(lid, (float)k[s * 2], (float)k[s * 2 + 1] - eyeGap * 0.14f, eyeGap * 0.26f, eyeGap * 0.13f);
            std::vector<float> ring = eyes; gaussianBlur(ring, S, S, eyeGap * 0.025f + 0.5f);
            for (size_t i = 0; i < lid.size(); ++i) { lid[i] *= skin[i] * (1 - eyes[i]); line[i] = std::clamp((ring[i] - eyes[i]) * 3.0f, 0.0f, 1.0f); }
            gaussianBlur(lid, S, S, fb);
            lay(lid, L.shadow * 0.8f, [&](const float* p, float* r, size_t) { for (int q = 0; q < 3; ++q) r[q] = p[q] * (0.25f + 0.75f * L.shadowColour[q]); });
            lay(line, L.liner, [&](const float* p, float* r, size_t) { for (int q = 0; q < 3; ++q) r[q] = p[q] * 0.18f + 0.02f; });
        }
        // brows: filled in (darker) and tinted
        if (L.brow > 0) {
            const auto m = maskOf(cls, {C_LBROW, C_RBROW}, fb * 0.8f);
            lay(m, L.brow, [&](const float* p, float* r, size_t) { float t[3]; colourBlend(p, L.browColour, t); for (int q = 0; q < 3; ++q) r[q] = t[q] * (0.55f + 0.45f * luma(p)); });
        }
        // lips: the colour (its lightness from the lips, so their shape and lines stay), then the gloss
        if (L.lip > 0 || L.gloss > 0) {
            const auto m = maskOf(cls, {C_ULIP, C_LLIP}, fb * 0.7f);
            lay(m, L.lip, [&](const float* p, float* r, size_t) { float t[3]; colourBlend(p, L.lipColour, t); for (int q = 0; q < 3; ++q) r[q] = t[q] * 0.85f + p[q] * L.lipColour[q] * 0.15f; });
            lay(m, L.gloss, [&](const float* p, float* r, size_t) { const float h = std::pow(std::clamp(luma(p) * 1.25f, 0.0f, 1.0f), 4.0f); for (int q = 0; q < 3; ++q) r[q] = p[q] + (1 - p[q]) * h; });
        }
        // hair: its colour, keeping its light and strands
        if (L.hair > 0) {
            const auto m = maskOf(cls, {C_HAIR}, fb * 0.6f);
            lay(m, L.hair, [&](const float* p, float* r, size_t) { colourBlend(p, L.hairColour, r); });
        }
        // put back wherever anything changed (the looks are blended in already: the mask is just where)
        for (auto& v : touched) v = std::min(1.0f, v * 25.0f);
        gaussianBlur(touched, S, S, 1.0f);
        pasteBack(im, out, touched, M);
    }
    return true;
}

}
