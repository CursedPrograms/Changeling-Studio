// Mimic - the face editor: FaceFusion's face_editor (LivePortrait) in C++. A smile, eyes open or shut, the gaze, the
// brows, the lips, the mouth's shape and place, and the head's pitch, yaw and roll, each a dial from -1 to 1. Also
// a prompt ("a big smile, eyes closed, looking left") that sets the dials.
//
// LivePortrait (Kuaishou, MIT) as FaceFusion exports it: the feature extractor, the motion extractor (head pose,
// scale, translation, expression and 21 motion points), the eye and lip retargeters, the stitcher and the generator.
#include "mimic.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace Mimic {

const char* editName(int e) {
    static const char* n[] = {"Eyebrows", "Gaze left/right", "Gaze up/down", "Eyes open", "Lips open", "Mouth grim",
                              "Mouth pout", "Mouth purse", "Smile", "Mouth left/right", "Mouth up/down",
                              "Head pitch", "Head yaw", "Head roll"};
    return e >= 0 && e < ED_COUNT ? n[e] : "?";
}
const char* editHint(int e) {
    static const char* h[] = {"-1 frowning, +1 raised", "Where the eyes look, sideways", "Where the eyes look, up or down",
                              "-1 shut, +1 wide open", "-1 pressed shut, +1 wide open", "A grimace (or its opposite)",
                              "A pout (or its opposite)", "Pursed lips (or its opposite)", "-1 a frown, +1 a big smile",
                              "The mouth moved sideways", "The mouth moved up or down",
                              "The head tipped up or down", "The head turned left or right", "The head tilted sideways"};
    return e >= 0 && e < ED_COUNT ? h[e] : "";
}

// ---------- LivePortrait's limits (FaceFusion's live_portrait.py) ----------
static const float EXPR_MIN[21][3] = {
    {-2.88067125e-02f, -8.12731311e-02f, -1.70541159e-03f}, {-4.88598682e-02f, -3.32196616e-02f, -1.67431499e-04f},
    {-6.75425082e-02f, -4.28681746e-02f, -1.98950816e-04f}, {-7.23103955e-02f, -3.28503326e-02f, -7.31324719e-04f},
    {-3.87073644e-02f, -6.01546466e-02f, -5.50269964e-04f}, {-6.38048723e-02f, -2.23840728e-01f, -7.13261834e-04f},
    {-3.02710701e-02f, -3.93195450e-02f, -8.24086510e-06f}, {-2.95799859e-02f, -5.39318882e-02f, -1.74219604e-04f},
    {-2.92359516e-02f, -1.53050944e-02f, -6.30460854e-05f}, {-5.56493877e-03f, -2.34344602e-02f, -1.26858242e-04f},
    {-4.37593013e-02f, -2.77768299e-02f, -2.70503685e-02f}, {-1.76926646e-02f, -1.91676542e-02f, -1.15090821e-04f},
    {-8.34268332e-03f, -3.99775570e-03f, -3.27481248e-05f}, {-3.40162888e-02f, -2.81868968e-02f, -1.96679524e-04f},
    {-2.91855410e-02f, -3.97511162e-02f, -2.81230678e-05f}, {-1.50395725e-02f, -2.49494594e-02f, -9.42573533e-05f},
    {-1.67938769e-02f, -2.00953931e-02f, -4.00750607e-04f}, {-1.86435618e-02f, -2.48535164e-02f, -2.74416432e-02f},
    {-4.61211195e-03f, -1.21660791e-02f, -2.93173041e-04f}, {-4.10017073e-02f, -7.43824020e-02f, -4.42762971e-02f},
    {-1.90370996e-02f, -3.74363363e-02f, -1.34740388e-02f}};
static const float EXPR_MAX[21][3] = {
    {4.46682945e-02f, 7.08772913e-02f, 4.08344204e-04f}, {2.14308221e-02f, 6.15894832e-02f, 4.85319615e-05f},
    {3.02363783e-02f, 4.45043296e-02f, 1.28298725e-05f}, {3.05869691e-02f, 3.79812494e-02f, 6.57040102e-04f},
    {4.45670523e-02f, 3.97259220e-02f, 7.10966764e-04f}, {9.43699256e-02f, 9.85926315e-02f, 2.02551950e-04f},
    {1.61131397e-02f, 2.92906128e-02f, 3.44733417e-06f}, {5.23825921e-02f, 1.07065082e-01f, 6.61510974e-04f},
    {2.85718683e-03f, 8.32320191e-03f, 2.39314613e-04f}, {2.57947259e-02f, 1.60935968e-02f, 2.41853559e-05f},
    {4.90833223e-02f, 3.43903080e-02f, 3.22353356e-02f}, {1.44766076e-02f, 3.39248963e-02f, 1.42291479e-04f},
    {8.75749043e-04f, 6.82212645e-03f, 2.76097053e-05f}, {1.86958015e-02f, 3.84016186e-02f, 7.33085908e-05f},
    {2.01714113e-02f, 4.90544215e-02f, 2.34028921e-05f}, {2.46518422e-02f, 3.29151377e-02f, 3.48571630e-05f},
    {2.22457591e-02f, 1.21796541e-02f, 1.56396593e-04f}, {1.72109623e-02f, 3.01626958e-02f, 1.36556877e-02f},
    {1.83460284e-02f, 1.61141958e-02f, 2.87440169e-04f}, {3.57594155e-02f, 1.80554688e-01f, 2.75554154e-02f},
    {2.17450950e-02f, 8.66811201e-02f, 3.34241726e-02f}};

using Pts = std::vector<float>;   // 21 x 3

// numpy.interp(v, [-1, 1], [-a, a]) == v * a (v in -1..1)
static float lerp(float v, float a) { return std::clamp(v, -1.0f, 1.0f) * a; }

static void editExpression(Pts& e, const float* d) {
    auto E = [&](int p, int c) -> float& { return e[(size_t)p * 3 + c]; };
    // eye gaze (+ looks towards the picture's right)
    const float gh = d[ED_GAZE_H], gv = d[ED_GAZE_V];
    if (gh > 0) { E(11, 0) += lerp(gh, 0.015f); E(15, 0) += lerp(gh, 0.020f); }
    else { E(11, 0) += lerp(gh, 0.020f); E(15, 0) += lerp(gh, 0.015f); }
    E(1, 1) += lerp(gv, 0.0025f); E(2, 1) -= lerp(gv, 0.0025f);
    E(11, 1) -= lerp(gv, 0.010f); E(13, 1) -= lerp(gv, 0.005f); E(15, 1) -= lerp(gv, 0.010f); E(16, 1) -= lerp(gv, 0.005f);
    // mouth grim
    const float g = d[ED_GRIM];
    if (g > 0) { E(17, 2) -= lerp(g, 0.005f); E(19, 2) += lerp(g, 0.01f); E(20, 1) -= lerp(g, 0.06f); E(20, 2) -= lerp(g, 0.03f); }
    else { E(19, 1) -= lerp(g, 0.05f); E(19, 2) -= lerp(g, 0.02f); E(20, 2) -= lerp(g, 0.03f); }
    // mouth position
    const float mh = d[ED_MOUTH_H], mv = d[ED_MOUTH_V];
    E(19, 0) += lerp(mh, 0.05f); E(20, 0) += lerp(mh, 0.04f);
    if (mv > 0) { E(19, 1) -= lerp(mv, 0.04f); E(20, 1) -= lerp(mv, 0.02f); }
    else { E(19, 1) -= lerp(mv, 0.05f); E(20, 1) -= lerp(mv, 0.04f); }
    // mouth pout
    const float po = d[ED_POUT];
    if (po > 0) { E(19, 1) -= lerp(po, 0.022f); E(19, 2) += lerp(po, 0.025f); E(20, 2) -= lerp(po, 0.002f); }
    else { E(19, 1) += lerp(po, 0.022f); E(19, 2) += lerp(po, 0.025f); E(20, 2) -= lerp(po, 0.002f); }
    // mouth purse
    const float pu = d[ED_PURSE];
    if (pu > 0) { E(19, 1) -= lerp(pu, 0.04f); E(19, 2) -= lerp(pu, 0.02f); }
    else { E(14, 1) -= lerp(pu, 0.02f); E(17, 2) += lerp(pu, 0.01f); E(19, 2) -= lerp(pu, 0.015f); E(20, 2) -= lerp(pu, 0.002f); }
    // smile
    const float sm = d[ED_SMILE];
    if (sm > 0) {
        E(20, 1) -= lerp(sm, 0.015f); E(14, 1) -= lerp(sm, 0.025f); E(17, 1) += lerp(sm, 0.01f); E(17, 2) += lerp(sm, 0.004f);
        E(3, 1) -= lerp(sm, 0.0045f); E(7, 1) -= lerp(sm, 0.0045f);
    } else {
        E(14, 1) -= lerp(sm, 0.02f); E(17, 1) += lerp(sm, 0.003f); E(19, 1) += lerp(sm, 0.02f); E(19, 2) -= lerp(sm, 0.005f);
        E(20, 2) += lerp(sm, 0.01f); E(3, 1) += lerp(sm, 0.0045f); E(7, 1) += lerp(sm, 0.0045f);
    }
    // eyebrows
    const float eb = d[ED_EYEBROW];
    if (eb > 0) { E(1, 1) += lerp(eb, 0.015f); E(2, 1) -= lerp(eb, 0.020f); }
    else { E(1, 0) -= lerp(eb, 0.015f); E(2, 0) += lerp(eb, 0.020f); E(1, 1) += lerp(eb, 0.005f); E(2, 1) -= lerp(eb, 0.005f); }
    // LivePortrait's limits
    for (int p = 0; p < 21; ++p) for (int c = 0; c < 3; ++c) E(p, c) = std::clamp(E(p, c), EXPR_MIN[p][c], EXPR_MAX[p][c]);
}

// scipy's Rotation.from_euler('xyz', [pitch, yaw, roll], degrees=True): extrinsic x, y, z = Rz * Ry * Rx
static void rotation(float pitch, float yaw, float roll, float R[3][3]) {
    const double d = 3.14159265358979 / 180.0;
    const double cx = std::cos(pitch * d), sx = std::sin(pitch * d), cy = std::cos(yaw * d), sy = std::sin(yaw * d), cz = std::cos(roll * d), sz = std::sin(roll * d);
    const double Rx[3][3] = {{1, 0, 0}, {0, cx, -sx}, {0, sx, cx}};
    const double Ry[3][3] = {{cy, 0, sy}, {0, 1, 0}, {-sy, 0, cy}};
    const double Rz[3][3] = {{cz, -sz, 0}, {sz, cz, 0}, {0, 0, 1}};
    double t[3][3];
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) { t[i][j] = 0; for (int k = 0; k < 3; ++k) t[i][j] += Rz[i][k] * Ry[k][j]; }
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) { double v = 0; for (int k = 0; k < 3; ++k) v += t[i][k] * Rx[k][j]; R[i][j] = (float)v; }
}

// scale * (points @ R^T + expression) + translation
static Pts transform(const Pts& mp, const float R[3][3], const Pts& expr, float scale, const float* t) {
    Pts o(63);
    for (int p = 0; p < 21; ++p)
        for (int i = 0; i < 3; ++i) {
            float v = 0;
            for (int k = 0; k < 3; ++k) v += mp[(size_t)p * 3 + k] * R[i][k];
            o[(size_t)p * 3 + i] = scale * (v + expr[(size_t)p * 3 + i]) + t[i];
        }
    return o;
}

static float ratio(const float* lm, int top, int bottom, int left, int right) {
    const float vx = lm[top * 2] - lm[bottom * 2], vy = lm[top * 2 + 1] - lm[bottom * 2 + 1];
    const float hx = lm[left * 2] - lm[right * 2], hy = lm[left * 2 + 1] - lm[right * 2 + 1];
    return std::sqrt(vx * vx + vy * vy) / (std::sqrt(hx * hx + hy * hy) + 1e-6f);
}

static bool run1(const char* model, std::vector<Ort::Tensor>& ins, std::vector<Ort::Tensor>& outs, std::string& err) {
    OrtSession* s = Ort::open(model, err);
    return s && Ort::run(s, ins, outs, err) && !outs.empty();
}
static Ort::Tensor tensor(const char* name, std::vector<int64_t> shape, std::vector<float> f) { Ort::Tensor t; t.name = name; t.shape = std::move(shape); t.f = std::move(f); return t; }

bool editOne(Img& frame, const Face& target, const Settings& st, std::string& err) {
    const float* d = st.edits;
    bool any = false;
    for (int i = 0; i < ED_COUNT; ++i) if (std::fabs(d[i]) > 1e-4f) any = true;
    if (!any) return true;
    // the crop: the face's five points spread 1.5x round the nose, on the ffhq template; 512 to paste, 256 for the model
    float k5[10];
    for (int p = 0; p < 5; ++p) { k5[p * 2] = target.kps[4] + (target.kps[p * 2] - target.kps[4]) * 1.5f; k5[p * 2 + 1] = target.kps[5] + (target.kps[p * 2 + 1] - target.kps[5]) * 1.5f; }
    const Affine M = alignFace(k5, T_FFHQ_512, 512), Mh = alignFace(k5, T_FFHQ_512, 256);
    const Crop small = warpCrop(frame, Mh, 256, 256);
    std::vector<float> in((size_t)3 * 256 * 256);
    for (int i = 0; i < 256 * 256; ++i) for (int c = 0; c < 3; ++c) in[(size_t)c * 65536 + i] = small.px[(size_t)i * 3 + c] / 255.0f;

    std::vector<Ort::Tensor> ins = {tensor("input", {1, 3, 256, 256}, in)}, feat, mot;
    if (!run1("live_portrait_feature_extractor.onnx", ins, feat, err)) return false;
    if (!run1("live_portrait_motion_extractor.onnx", ins, mot, err)) return false;
    if (mot.size() < 7) { err = "LivePortrait's motion extractor gave too little"; return false; }
    const float pitch = mot[0].f[0], yaw = mot[1].f[0], roll = mot[2].f[0], scale = mot[3].f[0];
    float t[3] = {0, 0, 0};
    for (int i = 0; i < 3 && i < (int)mot[4].f.size(); ++i) t[i] = mot[4].f[i];
    Pts expr = mot[5].f, mp = mot[6].f;
    if (expr.size() < 63 || mp.size() < 63) { err = "LivePortrait's motion is the wrong size"; return false; }
    expr.resize(63); mp.resize(63);

    float R[3][3];
    rotation(pitch, yaw, roll, R);
    const Pts tgt = transform(mp, R, expr, scale, t);
    Pts e2 = expr;
    editExpression(e2, d);
    // the head: FaceFusion's ranges (pitch +-20, yaw +-60, roll +-15 degrees), kept within LivePortrait's limits
    auto limit = [](float now, float v, float lo, float hi) { if (now < 0) lo = std::min(now, lo); else hi = std::max(now, hi); return std::clamp(v, lo, hi); };
    const float np = limit(pitch, pitch - lerp(d[ED_PITCH], 20.0f), -30, 30);
    const float ny = limit(yaw, yaw - lerp(d[ED_YAW], 60.0f), -60, 60);
    const float nr = limit(roll, roll + lerp(d[ED_ROLL], 15.0f), -20, 20);
    float R2[3][3];
    rotation(np, ny, nr, R2);
    Pts src = transform(mp, R2, e2, scale, t);

    // eyes and lips: the retargeters, scaled by how far the dial is turned
    const float eo = d[ED_EYE_OPEN], lo = d[ED_LIP_OPEN];
    if (std::fabs(eo) > 1e-4f) {
        std::vector<float> v = tgt;
        v.push_back(ratio(target.lm68, 37, 40, 39, 36));
        v.push_back(ratio(target.lm68, 43, 46, 45, 42));
        v.push_back(eo < 0 ? 0.0f : 0.6f);
        std::vector<Ort::Tensor> i2 = {tensor("input", {1, (int64_t)v.size()}, v)}, o2;
        if (!run1("live_portrait_eye_retargeter.onnx", i2, o2, err) || o2[0].f.size() < 63) return false;
        for (int i = 0; i < 63; ++i) src[i] += o2[0].f[i] * std::fabs(eo);
    }
    if (std::fabs(lo) > 1e-4f) {
        std::vector<float> v = tgt;
        v.push_back(ratio(target.lm68, 62, 66, 54, 48));
        v.push_back(lo < 0 ? 0.0f : 1.0f);
        std::vector<Ort::Tensor> i2 = {tensor("input", {1, (int64_t)v.size()}, v)}, o2;
        if (!run1("live_portrait_lip_retargeter.onnx", i2, o2, err) || o2[0].f.size() < 63) return false;
        for (int i = 0; i < 63; ++i) src[i] += o2[0].f[i] * std::fabs(lo);
    }
    // the stitcher keeps the edited face joined to the rest
    {
        std::vector<Ort::Tensor> i2 = {tensor("source", {1, 21, 3}, src), tensor("target", {1, 21, 3}, tgt)}, o2;
        if (!run1("live_portrait_stitcher.onnx", i2, o2, err) || o2[0].f.size() < 63) return false;
        src.assign(o2[0].f.begin(), o2[0].f.begin() + 63);
    }
    // the generator draws the face anew
    Ort::Tensor fv = feat[0];
    fv.name = "feature_volume";
    std::vector<Ort::Tensor> i3 = {fv, tensor("source", {1, 21, 3}, src), tensor("target", {1, 21, 3}, tgt)}, o3;
    if (!run1("live_portrait_generator.onnx", i3, o3, err)) return false;
    const auto& g = o3[0].f;
    const int N = (int)std::lround(std::sqrt(g.size() / 3.0));
    if (N < 16) { err = "LivePortrait's generator gave too little"; return false; }
    Crop out(N, N);
    for (int i = 0; i < N * N; ++i) for (int c = 0; c < 3; ++c) out.px[(size_t)i * 3 + c] = std::clamp(g[(size_t)c * N * N + i], 0.0f, 1.0f) * 255.0f;
    const Affine Mo = alignFace(k5, T_FFHQ_512, N);
    const int pad0[4] = {0, 0, 0, 0};
    pasteBack(frame, out, boxMask(N, N, st.maskBlur, pad0), Mo);
    (void)M;
    return true;
}

// ---------- the prompt ----------
// words and phrases onto the dials; "slight/a little" 0.35, "very/big/wide" 1, otherwise 0.7; "not"/"no" before a
// word flips it. Several clauses combine ("a big smile, eyes closed, looking left").
struct Word { const char* text; int edit; float value; };
static const Word WORDS[] = {
    // smiles and moods
    {"smile", ED_SMILE, 1}, {"smiling", ED_SMILE, 1}, {"grin", ED_SMILE, 1}, {"grinning", ED_SMILE, 1}, {"happy", ED_SMILE, 1},
    {"cheerful", ED_SMILE, 1}, {"joyful", ED_SMILE, 1}, {"beaming", ED_SMILE, 1}, {"smirk", ED_SMILE, 0.45f},
    {"frown", ED_SMILE, -1}, {"frowning", ED_SMILE, -1}, {"sad", ED_SMILE, -1}, {"unhappy", ED_SMILE, -1}, {"upset", ED_SMILE, -1},
    {"grim", ED_GRIM, 1}, {"grimace", ED_GRIM, 1}, {"grimacing", ED_GRIM, 1}, {"disgusted", ED_GRIM, 0.8f},
    {"pout", ED_POUT, 1}, {"pouting", ED_POUT, 1}, {"sulky", ED_POUT, 0.8f},
    {"purse", ED_PURSE, 1}, {"pursed", ED_PURSE, 1}, {"kiss", ED_PURSE, 1}, {"kissy", ED_PURSE, 1},
    {"neutral", -1, 0}, {"serious", -1, 0}, {"straight face", -1, 0}, {"blank", -1, 0},
    // eyes
    {"eyes closed", ED_EYE_OPEN, -1}, {"closed eyes", ED_EYE_OPEN, -1}, {"eyes shut", ED_EYE_OPEN, -1}, {"close eyes", ED_EYE_OPEN, -1},
    {"close the eyes", ED_EYE_OPEN, -1}, {"close her eyes", ED_EYE_OPEN, -1}, {"close his eyes", ED_EYE_OPEN, -1},
    {"blink", ED_EYE_OPEN, -1}, {"asleep", ED_EYE_OPEN, -1}, {"sleeping", ED_EYE_OPEN, -1}, {"sleepy", ED_EYE_OPEN, -0.5f},
    {"tired", ED_EYE_OPEN, -0.5f}, {"squint", ED_EYE_OPEN, -0.5f}, {"squinting", ED_EYE_OPEN, -0.5f},
    {"eyes open", ED_EYE_OPEN, 0.7f}, {"wide eyes", ED_EYE_OPEN, 1}, {"eyes wide", ED_EYE_OPEN, 1}, {"wide-eyed", ED_EYE_OPEN, 1},
    // gaze
    {"look left", ED_GAZE_H, -1}, {"looking left", ED_GAZE_H, -1}, {"look right", ED_GAZE_H, 1}, {"looking right", ED_GAZE_H, 1},
    {"look up", ED_GAZE_V, 1}, {"looking up", ED_GAZE_V, 1}, {"look down", ED_GAZE_V, -1}, {"looking down", ED_GAZE_V, -1},
    {"eyes left", ED_GAZE_H, -1}, {"eyes right", ED_GAZE_H, 1}, {"eyes up", ED_GAZE_V, 1}, {"eyes down", ED_GAZE_V, -1},
    // brows
    {"raised eyebrows", ED_EYEBROW, 1}, {"eyebrows raised", ED_EYEBROW, 1}, {"raise eyebrows", ED_EYEBROW, 1}, {"brows up", ED_EYEBROW, 1},
    {"furrowed", ED_EYEBROW, -1}, {"brows down", ED_EYEBROW, -1}, {"scowl", ED_EYEBROW, -1}, {"scowling", ED_EYEBROW, -1},
    // mouth
    {"mouth open", ED_LIP_OPEN, 0.8f}, {"open mouth", ED_LIP_OPEN, 0.8f}, {"open-mouthed", ED_LIP_OPEN, 0.8f}, {"gasp", ED_LIP_OPEN, 1},
    {"mouth closed", ED_LIP_OPEN, -0.6f}, {"closed mouth", ED_LIP_OPEN, -0.6f}, {"lips closed", ED_LIP_OPEN, -0.6f},
    {"talking", ED_LIP_OPEN, 0.5f}, {"speaking", ED_LIP_OPEN, 0.5f}, {"singing", ED_LIP_OPEN, 0.8f}, {"yawn", ED_LIP_OPEN, 1},
    // head (yaw +: turned towards the picture's right)
    {"turn left", ED_YAW, -0.5f}, {"turned left", ED_YAW, -0.5f}, {"head left", ED_YAW, -0.5f}, {"facing left", ED_YAW, -0.5f},
    {"turn right", ED_YAW, 0.5f}, {"turned right", ED_YAW, 0.5f}, {"head right", ED_YAW, 0.5f}, {"facing right", ED_YAW, 0.5f},
    {"chin up", ED_PITCH, 0.6f}, {"head up", ED_PITCH, 0.6f}, {"head back", ED_PITCH, 0.6f}, {"chin down", ED_PITCH, -0.6f}, {"head down", ED_PITCH, -0.6f},
    {"nod", ED_PITCH, -0.4f}, {"tilt left", ED_ROLL, -0.7f}, {"tilted left", ED_ROLL, -0.7f}, {"tilt right", ED_ROLL, 0.7f},
    {"tilted right", ED_ROLL, 0.7f}, {"head tilt", ED_ROLL, 0.6f}, {"tilted head", ED_ROLL, 0.6f}, {"tilt", ED_ROLL, 0.6f},
};

// whole expressions, several dials at once
struct Mood { const char* text; std::vector<std::pair<int, float>> set; };
static const std::vector<Mood>& moods() {
    static const std::vector<Mood> m = {
        {"laugh", {{ED_SMILE, 1}, {ED_LIP_OPEN, 0.7f}, {ED_EYE_OPEN, -0.3f}}},
        {"laughing", {{ED_SMILE, 1}, {ED_LIP_OPEN, 0.7f}, {ED_EYE_OPEN, -0.3f}}},
        {"big grin", {{ED_SMILE, 1}, {ED_LIP_OPEN, 0.35f}}},
        {"toothy", {{ED_SMILE, 1}, {ED_LIP_OPEN, 0.4f}}},
        {"surprised", {{ED_EYEBROW, 1}, {ED_EYE_OPEN, 0.9f}, {ED_LIP_OPEN, 0.6f}}},
        {"surprise", {{ED_EYEBROW, 1}, {ED_EYE_OPEN, 0.9f}, {ED_LIP_OPEN, 0.6f}}},
        {"shocked", {{ED_EYEBROW, 1}, {ED_EYE_OPEN, 1}, {ED_LIP_OPEN, 0.9f}}},
        {"amazed", {{ED_EYEBROW, 1}, {ED_EYE_OPEN, 0.8f}, {ED_LIP_OPEN, 0.5f}, {ED_SMILE, 0.3f}}},
        {"scared", {{ED_EYEBROW, 0.8f}, {ED_EYE_OPEN, 1}, {ED_LIP_OPEN, 0.4f}, {ED_GRIM, 0.4f}}},
        {"afraid", {{ED_EYEBROW, 0.8f}, {ED_EYE_OPEN, 1}, {ED_LIP_OPEN, 0.4f}, {ED_GRIM, 0.4f}}},
        {"angry", {{ED_EYEBROW, -1}, {ED_GRIM, 0.6f}, {ED_SMILE, -0.4f}}},
        {"mad", {{ED_EYEBROW, -1}, {ED_GRIM, 0.6f}, {ED_SMILE, -0.4f}}},
        {"furious", {{ED_EYEBROW, -1}, {ED_GRIM, 1}, {ED_LIP_OPEN, 0.3f}}},
        {"worried", {{ED_EYEBROW, 0.6f}, {ED_SMILE, -0.5f}}},
        {"confused", {{ED_EYEBROW, -0.4f}, {ED_ROLL, 0.4f}, {ED_MOUTH_H, 0.3f}}},
        {"thinking", {{ED_GAZE_V, 0.7f}, {ED_MOUTH_H, 0.3f}, {ED_PURSE, 0.3f}}},
        {"bored", {{ED_EYE_OPEN, -0.4f}, {ED_SMILE, -0.3f}, {ED_PITCH, -0.2f}}},
        {"flirty", {{ED_SMILE, 0.5f}, {ED_EYE_OPEN, -0.3f}, {ED_ROLL, 0.3f}, {ED_EYEBROW, 0.3f}}},
        {"smug", {{ED_SMILE, 0.45f}, {ED_EYE_OPEN, -0.3f}, {ED_PITCH, 0.3f}, {ED_MOUTH_H, 0.25f}}},
        {"crying", {{ED_SMILE, -1}, {ED_EYE_OPEN, -0.6f}, {ED_EYEBROW, 0.5f}, {ED_LIP_OPEN, 0.3f}}},
        {"wink", {{ED_EYE_OPEN, -0.5f}, {ED_SMILE, 0.5f}}},
    };
    return m;
}

static bool hasPhrase(const std::string& text, const std::string& p, size_t& at) {
    size_t from = 0;
    while ((at = text.find(p, from)) != std::string::npos) {
        const bool startOk = at == 0 || !std::isalpha((unsigned char)text[at - 1]);
        const size_t end = at + p.size();
        const bool endOk = end >= text.size() || !std::isalpha((unsigned char)text[end]);
        if (startOk && endOk) return true;
        from = at + 1;
    }
    return false;
}

// how strong the words just before a match make it
static float strength(const std::string& text, size_t at, bool& negated) {
    const std::string before = " " + text.substr(at > 40 ? at - 40 : 0, at > 40 ? 40 : at);
    negated = false;
    auto near = [&](const char* w) { return before.find(std::string(" ") + w + " ") != std::string::npos; };
    // only the clause the word is in
    const size_t cut = before.find_last_of(",.;");
    const std::string clause = cut == std::string::npos ? before : " " + before.substr(cut + 1);
    auto inClause = [&](const char* w) { return clause.find(std::string(" ") + w + " ") != std::string::npos; };
    (void)near;
    if (inClause("not") || inClause("no") || inClause("without") || inClause("don't") || inClause("never")) negated = true;
    if (inClause("slight") || inClause("slightly") || inClause("little") || inClause("small") || inClause("soft") || inClause("faint") || inClause("subtle") || inClause("bit")) return 0.35f;
    if (inClause("very") || inClause("big") || inClause("huge") || inClause("wide") || inClause("really") || inClause("broad") || inClause("extremely") || inClause("totally") || inClause("fully")) return 1.0f;
    return 0.7f;
}

std::string promptToEdits(const std::string& prompt, float* edits) {
    std::string text = " ";
    for (char c : prompt) text += (char)std::tolower((unsigned char)c);
    text += " ";
    std::vector<std::string> said;
    std::vector<bool> taken(text.size(), false);   // a phrase's letters, so "big grin" doesn't also count as "grin"
    auto claim = [&](size_t at, size_t len) {
        for (size_t i = at; i < at + len; ++i) if (taken[i]) return false;
        for (size_t i = at; i < at + len; ++i) taken[i] = true;
        return true;
    };
    bool any = false;
    // longer phrases first
    for (const auto& m : moods()) {
        size_t at;
        if (!hasPhrase(text, m.text, at) || !claim(at, strlen(m.text))) continue;
        bool neg; const float s = strength(text, at, neg);
        const float k = s / 0.7f;   // slight 0.5x, plain 1x, very 1.4x
        for (auto [e, v] : m.set) edits[e] = std::clamp((neg ? -v : v) * k, -1.0f, 1.0f);
        said.push_back((neg ? "not " : "") + std::string(m.text));
        any = true;
    }
    std::vector<const Word*> words;
    for (const auto& w : WORDS) words.push_back(&w);
    std::stable_sort(words.begin(), words.end(), [](const Word* a, const Word* b) { return strlen(a->text) > strlen(b->text); });
    for (const Word* w : words) {
        size_t at;
        if (!hasPhrase(text, w->text, at) || !claim(at, strlen(w->text))) continue;
        bool neg; const float s = strength(text, at, neg);
        if (w->edit < 0) {   // neutral: every dial back to 0
            for (int i = 0; i < ED_COUNT; ++i) edits[i] = 0;
            said.push_back(w->text);
            any = true;
            continue;
        }
        float v = w->value * (s / 0.7f);
        if (std::fabs(w->value) >= 0.99f) v = (w->value > 0 ? 1 : -1) * s;   // a plain word: its strength from the clause
        if (neg) v = -v;
        edits[w->edit] = std::clamp(v, -1.0f, 1.0f);
        said.push_back((neg ? "not " : "") + std::string(w->text));
        any = true;
    }
    if (!any) return "";
    std::string r;
    for (size_t i = 0; i < said.size(); ++i) r += (i ? ", " : "") + said[i];
    return r;
}

}  // namespace Mimic
