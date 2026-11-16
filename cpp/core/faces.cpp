// Mimic - faces (see faces.h). The decoding follows FaceFusion's face_detector / face_landmarker / face_classifier.
#include "faces.h"
#include <algorithm>
#include <cmath>
#include <cstring>

const char* detectorName(int m) {
    static const char* n[] = {"RetinaFace", "SCRFD", "YOLO-Face", "YuNet", "Many (RetinaFace + SCRFD + YOLO-Face)"};
    return m >= 0 && m < DET_COUNT ? n[m] : "?";
}
const char* landmarkerName(int m) {
    static const char* n[] = {"2DFAN4", "PeppaWutz", "Many (the surer of both)"};
    return m >= 0 && m < LM_COUNT ? n[m] : "?";
}
const char* raceName(int r) {
    static const char* n[] = {"white", "black", "latino", "asian", "indian", "arabic"};
    return r >= 0 && r < R_COUNT ? n[r] : "?";
}

namespace Faces {

struct Raw { float box[4]; float score; float kps[10]; };

// the picture shrunk to fit size x size (never enlarged), top-left; returns the scale used
static double restrict(const Img& im, int size, Img& out) {
    const double scale = std::min(1.0, std::min((double)size / im.w, (double)size / im.h));
    const int sw = std::max(1, (int)std::lround(im.w * scale)), sh = std::max(1, (int)std::lround(im.h * scale));
    if (scale >= 1.0) { out = im; return 1.0; }
    Affine M; M.m[0] = scale; M.m[4] = scale;
    const Crop c = warpCrop(im, M, sw, sh);
    out = Img(sw, sh);
    for (size_t i = 0; i < out.px.size(); ++i) out.px[i] = (uint8_t)std::clamp(c.px[i] + 0.5f, 0.0f, 255.0f);
    return scale;
}

// an NCHW input: the picture top-left in size x size, each channel through f (bgr: swap R and B)
template <class F> static std::vector<float> nchw(const Img& im, int size, bool bgr, float pad, F f) {
    std::vector<float> in((size_t)3 * size * size, pad);
    for (int y = 0; y < im.h; ++y)
        for (int x = 0; x < im.w; ++x)
            for (int c = 0; c < 3; ++c) in[(size_t)c * size * size + (size_t)y * size + x] = f(im.px[((size_t)y * im.w + x) * 3 + (bgr ? 2 - c : c)]);
    return in;
}

// RetinaFace (insightface det_10g) and SCRFD 2.5g: strides 8/16/32, two anchors per cell, distances to the box
static bool anchorDetect(const char* model, const Img& im, int size, float minScore, std::vector<Raw>& out, std::string& err) {
    OrtSession* s = Ort::open(model, err);
    if (!s) return false;
    Img small;
    const double scale = restrict(im, size, small);
    Ort::Tensor in;
    in.name = Ort::inputNames(s)[0];
    in.shape = {1, 3, size, size};
    in.f = nchw(small, size, false, -127.5f / 128.0f, [](uint8_t v) { return (v - 127.5f) / 128.0f; });
    std::vector<Ort::Tensor> ins = {in}, outs;
    if (!Ort::run(s, ins, outs, err)) return false;
    if (outs.size() < 9) { err = std::string(model) + " gave too few outputs"; return false; }
    const int strides[3] = {8, 16, 32};
    for (int k = 0; k < 3; ++k) {
        const int st = strides[k], cols = size / st;
        const auto &sc = outs[k].f, &bb = outs[k + 3].f, &kp = outs[k + 6].f;
        for (size_t i = 0; i < sc.size(); ++i) {
            if (sc[i] < minScore) continue;
            const int cell = (int)i / 2;
            const float cx = (float)(cell % cols) * st, cy = (float)(cell / cols) * st;
            Raw r;
            r.score = sc[i];
            r.box[0] = (float)((cx - bb[i * 4] * st) / scale); r.box[1] = (float)((cy - bb[i * 4 + 1] * st) / scale);
            r.box[2] = (float)((cx + bb[i * 4 + 2] * st) / scale); r.box[3] = (float)((cy + bb[i * 4 + 3] * st) / scale);
            for (int p = 0; p < 5; ++p) { r.kps[p * 2] = (float)((cx + kp[i * 10 + p * 2] * st) / scale); r.kps[p * 2 + 1] = (float)((cy + kp[i * 10 + p * 2 + 1] * st) / scale); }
            out.push_back(r);
        }
    }
    return true;
}

// YOLO-Face (yoloface_8n): one 640 x 640 picture in 0..1; out: per candidate x, y, w, h, score, five points (x, y, conf)
static bool yoloDetect(const Img& im, float minScore, std::vector<Raw>& out, std::string& err) {
    OrtSession* s = Ort::open("yoloface_8n.onnx", err);
    if (!s) return false;
    const int size = 640;
    Img small;
    const double scale = restrict(im, size, small);
    Ort::Tensor in;
    in.name = Ort::inputNames(s)[0];
    in.shape = {1, 3, size, size};
    in.f = nchw(small, size, false, 0.0f, [](uint8_t v) { return v / 255.0f; });
    std::vector<Ort::Tensor> ins = {in}, outs;
    if (!Ort::run(s, ins, outs, err) || outs.empty()) return false;
    const auto& d = outs[0].f;
    const int rows = (int)(outs[0].shape.size() >= 2 ? outs[0].shape[1] : 20);
    const int n = (int)(d.size() / std::max(1, rows));
    for (int i = 0; i < n; ++i) {
        auto at = [&](int r) { return d[(size_t)r * n + i]; };
        if (at(4) <= minScore) continue;
        Raw r;
        r.score = at(4);
        r.box[0] = (float)((at(0) - at(2) / 2) / scale); r.box[1] = (float)((at(1) - at(3) / 2) / scale);
        r.box[2] = (float)((at(0) + at(2) / 2) / scale); r.box[3] = (float)((at(1) + at(3) / 2) / scale);
        for (int p = 0; p < 5; ++p) { r.kps[p * 2] = (float)(at(5 + p * 3) / scale); r.kps[p * 2 + 1] = (float)(at(6 + p * 3) / scale); }
        out.push_back(r);
    }
    return true;
}

// YuNet (OpenCV Zoo): BGR 0..255; per stride cls, obj, bbox (dx, dy, log w, log h), kps; one anchor per cell
static bool yunetDetect(const Img& im, float minScore, std::vector<Raw>& out, std::string& err) {
    OrtSession* s = Ort::open("yunet_2023_mar.onnx", err);
    if (!s) return false;
    const int size = 640;
    Img small;
    const double scale = restrict(im, size, small);
    Ort::Tensor in;
    in.name = Ort::inputNames(s)[0];
    in.shape = {1, 3, size, size};
    in.f = nchw(small, size, true, 0.0f, [](uint8_t v) { return (float)v; });
    std::vector<Ort::Tensor> ins = {in}, outs;
    if (!Ort::run(s, ins, outs, err)) return false;
    if (outs.size() < 12) { err = "YuNet gave too few outputs"; return false; }
    const int strides[3] = {8, 16, 32};
    for (int k = 0; k < 3; ++k) {
        const int st = strides[k], cols = size / st, n = cols * cols;
        const auto &cls = outs[k].f, &obj = outs[3 + k].f, &bb = outs[6 + k].f, &kp = outs[9 + k].f;
        for (int i = 0; i < n; ++i) {
            const float sc = cls[i] * obj[i];
            if (sc < minScore) continue;
            const float ax = (float)(i % cols) * st, ay = (float)(i / cols) * st;
            const float cx = bb[i * 4] * st + ax, cy = bb[i * 4 + 1] * st + ay;
            const float w = std::exp(bb[i * 4 + 2]) * st, h = std::exp(bb[i * 4 + 3]) * st;
            Raw r;
            r.score = sc;
            r.box[0] = (float)((cx - w / 2) / scale); r.box[1] = (float)((cy - h / 2) / scale);
            r.box[2] = (float)((cx + w / 2) / scale); r.box[3] = (float)((cy + h / 2) / scale);
            for (int p = 0; p < 5; ++p) { r.kps[p * 2] = (float)((kp[i * 10 + p * 2] * st + ax) / scale); r.kps[p * 2 + 1] = (float)((kp[i * 10 + p * 2 + 1] * st + ay) / scale); }
            out.push_back(r);
        }
    }
    return true;
}

// the picture turned by a multiple of 90 degrees (anticlockwise, like OpenCV's positive angles), and where a point
// in the turned picture was in the original
static Img turn(const Img& im, int angle) {
    if (angle == 0) return im;
    const bool swap = angle == 90 || angle == 270;
    Img o(swap ? im.h : im.w, swap ? im.w : im.h);
    for (int y = 0; y < o.h; ++y)
        for (int x = 0; x < o.w; ++x) {
            int sx, sy;
            if (angle == 90) { sx = im.w - 1 - y; sy = x; }
            else if (angle == 180) { sx = im.w - 1 - x; sy = im.h - 1 - y; }
            else { sx = y; sy = im.h - 1 - x; }
            memcpy(&o.px[((size_t)y * o.w + x) * 3], &im.px[((size_t)sy * im.w + sx) * 3], 3);
        }
    return o;
}
static void unturn(float& x, float& y, int angle, const Img& orig) {
    const float tx = x, ty = y;
    if (angle == 90) { x = orig.w - ty; y = tx; }
    else if (angle == 180) { x = orig.w - tx; y = orig.h - ty; }
    else if (angle == 270) { x = ty; y = orig.h - tx; }
}

static float iou(const float* a, const float* b) {
    const float ix = std::max(0.0f, std::min(a[2], b[2]) - std::max(a[0], b[0])), iy = std::max(0.0f, std::min(a[3], b[3]) - std::max(a[1], b[1]));
    const float inter = ix * iy, ua = (a[2] - a[0]) * (a[3] - a[1]) + (b[2] - b[0]) * (b[3] - b[1]) - inter;
    return ua > 0 ? inter / ua : 0;
}

// FAN 68/5: the 68 points estimated from the five, in the ffhq template's unit square
static bool estimate68(const float* kps5, float* lm68, std::string& err) {
    OrtSession* s = Ort::open("fan_68_5.onnx", err);
    if (!s) return false;
    const Affine M = alignFace(kps5, T_FFHQ_512, 1);
    Ort::Tensor in;
    in.name = Ort::inputNames(s)[0];
    in.shape = {1, 5, 2};
    in.f.resize(10);
    for (int p = 0; p < 5; ++p) { double x, y; M.apply(kps5[p * 2], kps5[p * 2 + 1], x, y); in.f[p * 2] = (float)x; in.f[p * 2 + 1] = (float)y; }
    std::vector<Ort::Tensor> ins = {in}, outs;
    if (!Ort::run(s, ins, outs, err) || outs.empty() || outs[0].f.size() < 136) { if (err.empty()) err = "FAN 68/5 gave nothing"; return false; }
    const Affine inv = M.inverse();
    for (int p = 0; p < 68; ++p) { double x, y; inv.apply(outs[0].f[p * 2], outs[0].f[p * 2 + 1], x, y); lm68[p * 2] = (float)x; lm68[p * 2 + 1] = (float)y; }
    return true;
}

static int faceAngle(const float* lm68) {
    double theta = std::atan2(lm68[16 * 2 + 1] - lm68[1], lm68[16 * 2] - lm68[0]) * 180.0 / 3.14159265358979;
    theta = std::fmod(theta + 360.0, 360.0);
    const int a = (int)std::lround(theta / 90.0) * 90 % 360;
    return a;
}

// a 68-point landmarker on the face's box: 256 x 256, the box's longer side ~195 px, turned upright; BGR 0..1
static bool landmark(const Img& im, const Face& f, int model, float* lm68, float& score, std::string& err) {
    const char* file = model == LM_PEPPA ? "peppa_wutz.onnx" : "2dfan4.onnx";
    OrtSession* s = Ort::open(file, err);
    if (!s) return false;
    const int N = 256;
    const double sc = 195.0 / std::max(1.0f, std::max(f.box[2] - f.box[0], f.box[3] - f.box[1]));
    Affine T; T.m[0] = sc; T.m[4] = sc;
    T.m[2] = (N - (f.box[0] + f.box[2]) * sc) * 0.5; T.m[5] = (N - (f.box[1] + f.box[3]) * sc) * 0.5;
    // OpenCV's getRotationMatrix2D round the centre, by the face's angle
    const double a = f.angle * 3.14159265358979 / 180.0, ca = std::cos(a), sa = std::sin(a);
    Affine R; R.m[0] = ca; R.m[1] = sa; R.m[2] = (1 - ca) * N / 2 - sa * N / 2; R.m[3] = -sa; R.m[4] = ca; R.m[5] = sa * N / 2 + (1 - ca) * N / 2;
    Affine M;   // R after T
    M.m[0] = R.m[0] * T.m[0] + R.m[1] * T.m[3]; M.m[1] = R.m[0] * T.m[1] + R.m[1] * T.m[4]; M.m[2] = R.m[0] * T.m[2] + R.m[1] * T.m[5] + R.m[2];
    M.m[3] = R.m[3] * T.m[0] + R.m[4] * T.m[3]; M.m[4] = R.m[3] * T.m[1] + R.m[4] * T.m[4]; M.m[5] = R.m[3] * T.m[2] + R.m[4] * T.m[5] + R.m[5];
    const Crop c = warpCrop(im, M, N, N);
    Ort::Tensor in;
    in.name = Ort::inputNames(s)[0];
    in.shape = {1, 3, N, N};
    in.f.resize((size_t)3 * N * N);
    for (int i = 0; i < N * N; ++i) for (int k = 0; k < 3; ++k) in.f[(size_t)k * N * N + i] = c.px[(size_t)i * 3 + (2 - k)] / 255.0f;
    std::vector<Ort::Tensor> ins = {in}, outs;
    if (!Ort::run(s, ins, outs, err) || outs.empty()) return false;
    const Affine inv = M.inverse();
    const auto& p = outs[0].f;
    if (p.size() < 68 * 3) { err = std::string(file) + " gave too little"; return false; }
    double conf = 0;
    for (int i = 0; i < 68; ++i) {
        double x, y;
        inv.apply(p[i * 3] / 64.0 * N, p[i * 3 + 1] / 64.0 * N, x, y);
        lm68[i * 2] = (float)x; lm68[i * 2 + 1] = (float)y;
        conf += p[i * 3 + 2];
    }
    if (model == LM_PEPPA) score = (float)std::clamp(conf / 68 / 0.95, 0.0, 1.0);
    else {   // 2DFAN4: the mean of each heatmap's peak
        double m = 0;
        if (outs.size() > 1 && outs[1].f.size() >= 68u * 64 * 64) {
            for (int i = 0; i < 68; ++i) m += *std::max_element(outs[1].f.begin() + (size_t)i * 4096, outs[1].f.begin() + (size_t)(i + 1) * 4096);
            m /= 68;
        }
        score = (float)std::clamp(m / 0.9, 0.0, 1.0);
    }
    return true;
}

bool analyse(const Img& im, const DetectSettings& st, std::vector<Face>& out, std::string& err, bool classify, Ort::Say say) {
    out.clear();
    // the margin: black round the frame, so faces at its edge are found whole
    const int mt = (int)(im.h * st.margin[0] / 200.0), mr = (int)(im.w * st.margin[1] / 200.0), mb = (int)(im.h * st.margin[2] / 200.0), ml = (int)(im.w * st.margin[3] / 200.0);
    Img framed = im;
    if (mt || mr || mb || ml) {
        framed = Img(im.w + ml + mr, im.h + mt + mb);
        for (int y = 0; y < im.h; ++y) memcpy(&framed.px[((size_t)(y + mt) * framed.w + ml) * 3], &im.px[(size_t)y * im.w * 3], (size_t)im.w * 3);
    }
    const int size = (st.model == DET_YOLOFACE || st.model == DET_YUNET) ? 640 : std::clamp(st.size, 160, 640) / 32 * 32;
    std::vector<Raw> raw;
    int angleCount = 0;
    for (int ai = 0; ai < 4; ++ai) {
        if (!st.angles[ai]) continue;
        ++angleCount;
        const int angle = ai * 90;
        const Img t = turn(framed, angle);
        std::vector<Raw> found;
        bool ok = true;
        if (st.model == DET_RETINAFACE || st.model == DET_MANY) ok = ok && anchorDetect("retinaface_10g.onnx", t, size, st.score, found, err);
        if (st.model == DET_SCRFD || st.model == DET_MANY) ok = ok && anchorDetect("scrfd_2.5g.onnx", t, size, st.score, found, err);
        if (st.model == DET_YOLOFACE || st.model == DET_MANY) ok = ok && yoloDetect(t, st.score, found, err);
        if (st.model == DET_YUNET) ok = ok && yunetDetect(t, st.score, found, err);
        if (!ok) return false;
        for (auto& r : found) {
            float xs[2] = {r.box[0], r.box[2]}, ys[2] = {r.box[1], r.box[3]};
            float bx[4] = {1e9f, 1e9f, -1e9f, -1e9f};
            for (float x : xs) for (float y : ys) { float px = x, py = y; unturn(px, py, angle, framed); bx[0] = std::min(bx[0], px); bx[1] = std::min(bx[1], py); bx[2] = std::max(bx[2], px); bx[3] = std::max(bx[3], py); }
            memcpy(r.box, bx, sizeof bx);
            for (int p = 0; p < 5; ++p) unturn(r.kps[p * 2], r.kps[p * 2 + 1], angle, framed);
            raw.push_back(r);
        }
    }
    // back out of the margin
    for (auto& r : raw) {
        r.box[0] -= ml; r.box[2] -= ml; r.box[1] -= mt; r.box[3] -= mt;
        for (int p = 0; p < 5; ++p) { r.kps[p * 2] -= ml; r.kps[p * 2 + 1] -= mt; }
    }
    // FaceFusion's NMS: stricter with several detectors or angles
    const float nms = st.model == DET_MANY ? 0.1f : angleCount == 2 ? 0.3f : angleCount == 3 ? 0.2f : angleCount == 4 ? 0.1f : 0.4f;
    std::sort(raw.begin(), raw.end(), [](const Raw& a, const Raw& b) { return a.score > b.score; });
    std::vector<Raw> keep;
    for (const auto& r : raw) {
        bool clash = false;
        for (const auto& k : keep) if (iou(r.box, k.box) > nms) { clash = true; break; }
        if (!clash) keep.push_back(r);
    }
    for (const auto& r : keep) {
        Face f;
        memcpy(f.box, r.box, sizeof f.box); f.score = r.score; memcpy(f.kps5, r.kps, sizeof f.kps5); memcpy(f.kps, r.kps, sizeof f.kps);
        if (!estimate68(f.kps5, f.lm68from5, err)) return false;
        memcpy(f.lm68, f.lm68from5, sizeof f.lm68);
        f.angle = faceAngle(f.lm68from5);
        if (st.landmarkerScore > 0) {
            float lm[136], s1 = 0;
            if (st.landmarker == LM_MANY) {
                float lm2[136], s2 = 0;
                if (!landmark(im, f, LM_2DFAN4, lm, s1, err) || !landmark(im, f, LM_PEPPA, lm2, s2, err)) return false;
                if (s2 > s1) { memcpy(lm, lm2, sizeof lm); s1 = s2; }
            } else if (!landmark(im, f, st.landmarker, lm, s1, err)) return false;
            f.landmarkerScore = s1;
            memcpy(f.lm68, lm, sizeof lm);
            if (s1 > st.landmarkerScore) {   // "5/68": eyes' centres, nose tip, mouth corners
                for (int e = 0; e < 2; ++e) {
                    float x = 0, y = 0;
                    for (int i = 36 + e * 6; i < 42 + e * 6; ++i) { x += lm[i * 2]; y += lm[i * 2 + 1]; }
                    f.kps[e * 2] = x / 6; f.kps[e * 2 + 1] = y / 6;
                }
                const int pts[3] = {30, 48, 54};
                for (int k = 0; k < 3; ++k) { f.kps[4 + k * 2] = lm[pts[k] * 2]; f.kps[5 + k * 2] = lm[pts[k] * 2 + 1]; }
            }
        }
        if (!embed(im, f, err, say)) return false;
        if (classify && !classifyFace(im, f, err)) return false;
        out.push_back(f);
    }
    std::sort(out.begin(), out.end(), [](const Face& a, const Face& b) { return a.area() > b.area(); });
    return true;
}

bool detect(const Img& im, std::vector<Face>& out, std::string& err, float minScore, Ort::Say say) {
    DetectSettings st;
    st.score = minScore;
    if (say) Ort::open("retinaface_10g.onnx", err, say);
    return analyse(im, st, out, err, false, say);
}

bool embed(const Img& im, Face& f, std::string& err, Ort::Say say) {
    OrtSession* s = Ort::open("arcface_w600k_r50.onnx", err, say);
    if (!s) return false;
    const Affine M = alignFace(f.kps, T_ARCFACE_112_V2, 112);
    const Crop c = warpCrop(im, M, 112, 112);
    Ort::Tensor in;
    in.name = Ort::inputNames(s)[0];
    in.shape = {1, 3, 112, 112};
    in.f.resize((size_t)3 * 112 * 112);
    for (int i = 0; i < 112 * 112; ++i)
        for (int k = 0; k < 3; ++k) in.f[(size_t)k * 112 * 112 + i] = c.px[(size_t)i * 3 + k] / 127.5f - 1.0f;
    std::vector<Ort::Tensor> ins = {in}, outs;
    if (!Ort::run(s, ins, outs, err) || outs.empty() || outs[0].f.size() < 512) { if (err.empty()) err = "ArcFace gave nothing"; return false; }
    f.emb.assign(outs[0].f.begin(), outs[0].f.begin() + 512);
    double n = 0;
    for (float v : f.emb) n += (double)v * v;
    n = std::sqrt(std::max(n, 1e-12));
    f.embNorm.resize(512);
    for (int i = 0; i < 512; ++i) f.embNorm[i] = (float)(f.emb[i] / n);
    return true;
}

// FairFace: 224 x 224 on the arcface_112_v2 template, RGB, ImageNet-normalised; out: race, gender, age (ids)
bool classifyFace(const Img& im, Face& f, std::string& err) {
    OrtSession* s = Ort::open("fairface.onnx", err);
    if (!s) return false;
    const int N = 224;
    const Affine M = alignFace(f.kps, T_ARCFACE_112_V2, N);
    const Crop c = warpCrop(im, M, N, N);
    const float mean[3] = {0.485f, 0.456f, 0.406f}, sd[3] = {0.229f, 0.224f, 0.225f};
    Ort::Tensor in;
    in.name = Ort::inputNames(s)[0];
    in.shape = {1, 3, N, N};
    in.f.resize((size_t)3 * N * N);
    for (int i = 0; i < N * N; ++i) for (int k = 0; k < 3; ++k) in.f[(size_t)k * N * N + i] = (c.px[(size_t)i * 3 + k] / 255.0f - mean[k]) / sd[k];
    std::vector<Ort::Tensor> ins = {in}, outs;
    if (!Ort::run(s, ins, outs, err) || outs.size() < 3) { if (err.empty()) err = "FairFace gave too little"; return false; }
    auto id = [](const Ort::Tensor& t) -> int {
        if (!t.i.empty()) return (int)t.i[0];
        if (!t.f.empty()) return (int)(std::max_element(t.f.begin(), t.f.end()) - t.f.begin());
        return -1;
    };
    const int race = id(outs[0]), gender = id(outs[1]), age = id(outs[2]);
    f.gender = gender == 1 ? G_FEMALE : G_MALE;
    static const int ages[9][2] = {{0, 2}, {3, 9}, {10, 19}, {20, 29}, {30, 39}, {40, 49}, {50, 59}, {60, 69}, {70, 100}};
    const int a = std::clamp(age, 0, 8);
    f.ageLo = ages[a][0]; f.ageHi = ages[a][1];
    f.race = race == 1 ? R_BLACK : race == 2 ? R_LATINO : (race == 3 || race == 4) ? R_ASIAN : race == 5 ? R_INDIAN : race == 6 ? R_ARABIC : R_WHITE;
    return true;
}

// 1 - cosine similarity, mapped 0..2 -> 0..1 as FaceFusion compares them
float distance(const Face& a, const Face& b) {
    if (a.embNorm.size() != 512 || b.embNorm.size() != 512) return 1.0f;
    double d = 0;
    for (int i = 0; i < 512; ++i) d += (double)a.embNorm[i] * b.embNorm[i];
    return (float)((1.0 - d) / 2.0);
}

}  // namespace Faces
