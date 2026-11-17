// Mimic - FaceFusion's face processors (see mimic.h). Each step follows FaceFusion's code: face_swapper, face_enhancer,
// face_debugger, face_selector and face_masker.
#include "mimic.h"
#include "emap.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>

namespace Mimic {

const std::vector<SwapModel>& swapModels() {
    static const std::vector<SwapModel> m = {
        {"inswapper 128", "inswapper_128.onnx", ST_INSWAPPER, T_ARCFACE_128, 128, {0, 0, 0}, {1, 1, 1}, nullptr, "InsightFace's: fast, the classic (non-commercial)"},
        {"inswapper 128 (fp16)", "inswapper_128_fp16.onnx", ST_INSWAPPER, T_ARCFACE_128, 128, {0, 0, 0}, {1, 1, 1}, nullptr, "The same at half precision: smaller"},
        {"HyperSwap 1a 256", "hyperswap_1a_256.onnx", ST_HYPERSWAP, T_ARCFACE_128, 256, {0.5f, 0.5f, 0.5f}, {0.5f, 0.5f, 0.5f}, nullptr, "FaceFusion's: sharp (runs on the CPU)"},
        {"HyperSwap 1b 256", "hyperswap_1b_256.onnx", ST_HYPERSWAP, T_ARCFACE_128, 256, {0.5f, 0.5f, 0.5f}, {0.5f, 0.5f, 0.5f}, nullptr, "FaceFusion's, second version (CPU)"},
        {"HyperSwap 1c 256", "hyperswap_1c_256.onnx", ST_HYPERSWAP, T_ARCFACE_128, 256, {0.5f, 0.5f, 0.5f}, {0.5f, 0.5f, 0.5f}, nullptr, "FaceFusion's, third version (CPU)"},
        {"SimSwap 256", "simswap_256.onnx", ST_SIMSWAP, T_ARCFACE_112_V1, 256, {0.485f, 0.456f, 0.406f}, {0.229f, 0.224f, 0.225f}, "crossface_simswap.onnx", "SimSwap"},
        {"SimSwap 512 (unofficial)", "simswap_unofficial_512.onnx", ST_SIMSWAP, T_ARCFACE_112_V1, 512, {0, 0, 0}, {1, 1, 1}, "crossface_simswap.onnx", "SimSwap at 512"},
        {"Ghost 1 256", "ghost_1_256.onnx", ST_GHOST, T_ARCFACE_112_V1, 256, {0.5f, 0.5f, 0.5f}, {0.5f, 0.5f, 0.5f}, "crossface_ghost.onnx", "Ghost (Sber AI)"},
        {"Ghost 2 256", "ghost_2_256.onnx", ST_GHOST, T_ARCFACE_112_V1, 256, {0.5f, 0.5f, 0.5f}, {0.5f, 0.5f, 0.5f}, "crossface_ghost.onnx", "Ghost, second"},
        {"Ghost 3 256", "ghost_3_256.onnx", ST_GHOST, T_ARCFACE_112_V1, 256, {0.5f, 0.5f, 0.5f}, {0.5f, 0.5f, 0.5f}, "crossface_ghost.onnx", "Ghost, third"},
        {"HiFiFace 256 (unofficial)", "hififace_unofficial_256.onnx", ST_HIFIFACE, T_MTCNN_512, 256, {0.5f, 0.5f, 0.5f}, {0.5f, 0.5f, 0.5f}, "crossface_hififace.onnx", "HiFiFace"},
        {"BlendSwap 256", "blendswap_256.onnx", ST_BLENDSWAP, T_FFHQ_512, 256, {0, 0, 0}, {1, 1, 1}, nullptr, "BlendSwap: takes the source's picture"},
        {"UniFace 256", "uniface_256.onnx", ST_UNIFACE, T_FFHQ_512, 256, {0.5f, 0.5f, 0.5f}, {0.5f, 0.5f, 0.5f}, nullptr, "UniFace: takes the source's picture"},
    };
    return m;
}

const std::vector<EnhanceModel>& enhanceModels() {
    static const std::vector<EnhanceModel> m = {
        {"GFPGAN 1.2", "gfpgan_1.2.onnx", T_FFHQ_512, 512, "Tencent ARC"},
        {"GFPGAN 1.3", "gfpgan_1.3.onnx", T_FFHQ_512, 512, "Tencent ARC"},
        {"GFPGAN 1.4", "gfpgan_1.4.onnx", T_FFHQ_512, 512, "Tencent ARC: natural, the usual choice"},
        {"CodeFormer", "codeformer.onnx", T_FFHQ_512, 512, "S-Lab: strong restoration, with a fidelity dial (non-commercial)"},
        {"GPEN 256", "gpen_bfr_256.onnx", T_ARCFACE_128, 256, "GPEN at 256"},
        {"GPEN 512", "gpen_bfr_512.onnx", T_FFHQ_512, 512, "GPEN at 512"},
        {"GPEN 1024", "gpen_bfr_1024.onnx", T_FFHQ_512, 1024, "GPEN at 1024: big faces"},
        {"GPEN 2048", "gpen_bfr_2048.onnx", T_FFHQ_512, 2048, "GPEN at 2048: very big faces, slow"},
        {"RestoreFormer++", "restoreformer_plus_plus.onnx", T_FFHQ_512, 512, "RestoreFormer++"},
    };
    return m;
}

const char* modeName(int m) { static const char* n[] = {"Many: every face", "One: the first face", "Reference: faces like one picked"}; return m >= 0 && m < SEL_COUNT ? n[m] : "?"; }
const char* orderName(int o) {
    static const char* n[] = {"Left to right", "Right to left", "Top to bottom", "Bottom to top", "Small to large", "Large to small", "Best to worst", "Worst to best"};
    return o >= 0 && o < ORD_COUNT ? n[o] : "?";
}
const char* occluderName(int o) { static const char* n[] = {"XSeg 1", "XSeg 2", "XSeg 3", "Many (all three)"}; return o >= 0 && o < OCC_COUNT ? n[o] : "?"; }
const char* regionName(int r) {
    static const char* n[] = {"Skin", "Left eyebrow", "Right eyebrow", "Left eye", "Right eye", "Glasses", "Nose", "Mouth", "Upper lip", "Lower lip"};
    return r >= 0 && r < RG_COUNT ? n[r] : "?";
}
const char* areaName(int a) { static const char* n[] = {"Upper face", "Lower face", "Mouth"}; return a >= 0 && a < AR_COUNT ? n[a] : "?"; }
const char* debugName(int d) { static const char* n[] = {"Bounding box", "Face points (5)", "Face points (5/68)", "Face points (68)", "Face points (68/5)", "Face mask"}; return d >= 0 && d < DB_COUNT ? n[d] : "?"; }

// MIMIC_PROFILE=1: how long each step takes, per frame
static const bool profile = std::getenv("MIMIC_PROFILE") != nullptr;
static double tick() { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

// ---------- the source ----------
bool addSource(Source& src, const Img& picture, std::string& err, Ort::Say say) {
    std::vector<Face> faces;
    if (!Faces::detect(picture, faces, err, 0.5f, say)) return false;
    if (faces.empty()) { err = "No face in that picture"; return false; }
    Face f = faces[0];   // the biggest
    if (!Faces::classifyFace(picture, f, err)) return false;
    src.faces.push_back(f);
    src.pictures.push_back(picture);
    finishSource(src);
    return true;
}

void finishSource(Source& src) {
    src.emb.clear(); src.embNorm.clear();
    if (src.faces.empty()) return;
    src.emb.assign(512, 0.0f);
    for (const auto& f : src.faces) for (int i = 0; i < 512; ++i) src.emb[i] += f.emb[i] / src.faces.size();
    double n = 0;
    for (float v : src.emb) n += (double)v * v;
    n = std::sqrt(std::max(n, 1e-12));
    src.embNorm.resize(512);
    for (int i = 0; i < 512; ++i) src.embNorm[i] = (float)(src.emb[i] / n);
}

// ---------- what the settings need ----------
static const char* occluderFile(int i) { static const char* f[] = {"xseg_1.onnx", "xseg_2.onnx", "xseg_3.onnx"}; return f[i]; }
static const char* parserFile(int p) { return p == 0 ? "bisenet_resnet_18.onnx" : "bisenet_resnet_34.onnx"; }

static std::vector<std::string> neededModels(const Settings& st) {
    std::vector<std::string> m;
    const auto& d = st.detect;
    if (d.model == DET_RETINAFACE || d.model == DET_MANY) m.push_back("retinaface_10g.onnx");
    if (d.model == DET_SCRFD || d.model == DET_MANY) m.push_back("scrfd_2.5g.onnx");
    if (d.model == DET_YOLOFACE || d.model == DET_MANY) m.push_back("yoloface_8n.onnx");
    if (d.model == DET_YUNET) m.push_back("yunet_2023_mar.onnx");
    m.push_back("fan_68_5.onnx");
    if (d.landmarkerScore > 0) {
        if (d.landmarker == LM_2DFAN4 || d.landmarker == LM_MANY) m.push_back("2dfan4.onnx");
        if (d.landmarker == LM_PEPPA || d.landmarker == LM_MANY) m.push_back("peppa_wutz.onnx");
    }
    m.push_back("arcface_w600k_r50.onnx");
    m.push_back("fairface.onnx");
    if (st.maskOcclusion) { if (st.occluder == OCC_MANY) for (int i = 0; i < 3; ++i) m.push_back(occluderFile(i)); else m.push_back(occluderFile(st.occluder)); }
    if (st.maskRegion) m.push_back(parserFile(st.parser));
    if (st.swap) {
        const auto& sm = swapModels()[std::clamp(st.swapModel, 0, (int)swapModels().size() - 1)];
        m.push_back(sm.file);
        if (sm.converter) m.push_back(sm.converter);
    }
    if (st.edit) for (const char* f : {"live_portrait_feature_extractor.onnx", "live_portrait_motion_extractor.onnx", "live_portrait_eye_retargeter.onnx",
                                        "live_portrait_lip_retargeter.onnx", "live_portrait_stitcher.onnx", "live_portrait_generator.onnx"}) m.push_back(f);
    if (st.enhance) m.push_back(enhanceModels()[std::clamp(st.enhanceModel, 0, (int)enhanceModels().size() - 1)].file);
    return m;
}

std::vector<std::string> modelsFor(const Settings& st) { return neededModels(st); }

std::vector<std::string> missingModels(const Settings& st) {
    std::vector<std::string> out;
    for (const auto& f : neededModels(st)) if (!Ort::have(f)) out.push_back(f);
    return out;
}

static std::map<std::string, std::vector<float>> emaps;
static std::mutex emapLock;

bool prepare(const Settings& st, std::string& err, Ort::Say say) {
    for (const auto& f : neededModels(st)) if (!Ort::open(f, err, say)) return false;
    if (st.swap) {
        const auto& sm = swapModels()[std::clamp(st.swapModel, 0, (int)swapModels().size() - 1)];
        if (sm.type == ST_INSWAPPER) {
            std::lock_guard<std::mutex> g(emapLock);
            auto& e = emaps[sm.file];
            if (e.empty() && !readEmap(Ort::findModel(sm.file), e, err)) return false;
        }
    }
    return true;
}

// ---------- the face selector ----------
static void sortFaces(std::vector<Face>& f, int order) {
    auto key = [&](const Face& a) -> float {
        switch (order) {
            case ORD_LEFT_RIGHT: case ORD_RIGHT_LEFT: return a.box[0];
            case ORD_TOP_BOTTOM: case ORD_BOTTOM_TOP: return a.box[1];
            case ORD_SMALL_LARGE: case ORD_LARGE_SMALL: return a.area();
            default: return a.score;
        }
    };
    const bool desc = order == ORD_RIGHT_LEFT || order == ORD_BOTTOM_TOP || order == ORD_LARGE_SMALL || order == ORD_BEST_WORST;
    std::stable_sort(f.begin(), f.end(), [&](const Face& a, const Face& b) { return desc ? key(a) > key(b) : key(a) < key(b); });
}

static bool needsClassify(const Settings& st) { return st.gender != -2 || st.race != -2 || st.ageStart > 0 || st.ageEnd < 100; }

static std::vector<Face> sortAndFilter(const Source& src, std::vector<Face> faces, const Settings& st) {
    sortFaces(faces, st.order);
    int gender = st.gender, race = st.race;
    if ((gender == -1 || race == -1) && !src.faces.empty()) {
        std::vector<Face> s = src.faces;
        sortFaces(s, ORD_LARGE_SMALL);
        if (gender == -1) gender = s[0].gender;
        if (race == -1) race = s[0].race;
    }
    std::vector<Face> out;
    for (const auto& f : faces) {
        if (gender >= 0 && f.gender != gender) continue;
        if (race >= 0 && f.race != race) continue;
        if ((st.ageStart > 0 || st.ageEnd < 100) && !(f.ageLo < st.ageEnd && st.ageStart < f.ageHi)) continue;
        out.push_back(f);
    }
    return out;
}

bool pickFaces(const Img& frame, const Source& src, const Settings& st, const Reference& ref, std::vector<Face>& picked, std::vector<Face>* all, std::string& err) {
    std::vector<Face> faces;
    if (!Faces::analyse(frame, st.detect, faces, err, needsClassify(st))) return false;
    if (all) *all = faces;
    picked.clear();
    if (st.mode == SEL_MANY) picked = sortAndFilter(src, faces, st);
    else if (st.mode == SEL_ONE) { auto f = sortAndFilter(src, faces, st); if (!f.empty()) picked.push_back(f[0]); }
    else {
        if (!ref.set) { err = "Pick a reference face first: click one in the preview"; return false; }
        for (const auto& f : faces) if (Faces::distance(f, ref.face) < st.referenceDistance) picked.push_back(f);
    }
    return true;
}

// ---------- the face masker ----------
static bool occlusionMask(const Crop& crop, int which, std::vector<float>& mask, std::string& err) {
    mask.assign((size_t)crop.w * crop.h, 1.0f);
    const int N = 256;
    for (int m = 0; m < 3; ++m) {
        if (which != OCC_MANY && which != m) continue;
        OrtSession* s = Ort::open(occluderFile(m), err);
        if (!s) return false;
        Ort::Tensor in;
        in.name = Ort::inputNames(s)[0];
        in.shape = {1, N, N, 3};
        in.f.resize((size_t)N * N * 3);
        for (int y = 0; y < N; ++y)
            for (int x = 0; x < N; ++x) {
                float v[3];
                sampleCrop(crop, (x + 0.5) * crop.w / N - 0.5, (y + 0.5) * crop.h / N - 0.5, v);
                for (int k = 0; k < 3; ++k) in.f[((size_t)y * N + x) * 3 + k] = v[2 - k] / 255.0f;   // BGR (DeepFaceLab's)
            }
        std::vector<Ort::Tensor> ins = {in}, outs;
        if (!Ort::run(s, ins, outs, err) || outs.empty()) return false;
        for (int y = 0; y < crop.h; ++y)
            for (int x = 0; x < crop.w; ++x) {
                float& d = mask[(size_t)y * crop.w + x];
                d = std::min(d, std::clamp(sampleMask(outs[0].f, N, N, (x + 0.5) * N / crop.w - 0.5, (y + 0.5) * N / crop.h - 0.5), 0.0f, 1.0f));
            }
    }
    gaussianBlur(mask, crop.w, crop.h, 5.0f * crop.w / 256.0f);
    for (auto& v : mask) v = (std::clamp(v, 0.5f, 1.0f) - 0.5f) * 2.0f;
    return true;
}

// the 68 points' areas (upper face, lower face, mouth) as a filled hull in the crop
static std::vector<float> areaMask(const Face& f, const Affine& M, int w, int h, const bool* areas) {
    static const std::vector<int> sets[AR_COUNT] = {
        {0, 1, 2, 31, 32, 33, 34, 35, 14, 15, 16, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17},
        {3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 35, 34, 33, 32, 31},
        {48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67}};
    std::vector<float> pts;
    for (int a = 0; a < AR_COUNT; ++a) {
        if (!areas[a]) continue;
        for (int i : sets[a]) { double x, y; M.apply(f.lm68[i * 2], f.lm68[i * 2 + 1], x, y); pts.push_back((float)x); pts.push_back((float)y); }
    }
    std::vector<float> m((size_t)w * h, 0.0f);
    fillConvex(m, w, h, pts);
    gaussianBlur(m, w, h, 5.0f * w / 256.0f);
    for (auto& v : m) v = (std::clamp(v, 0.5f, 1.0f) - 0.5f) * 2.0f;
    return m;
}

// BiSeNet's face parsing: the chosen regions (skin, brows, eyes, glasses, nose, mouth, lips)
static bool regionMask(const Crop& crop, int parser, const bool* regions, std::vector<float>& mask, std::string& err) {
    OrtSession* s = Ort::open(parserFile(parser), err);
    if (!s) return false;
    const int N = 512;
    static const int classes[RG_COUNT] = {1, 2, 3, 4, 5, 6, 10, 11, 12, 13};
    const float mean[3] = {0.485f, 0.456f, 0.406f}, sd[3] = {0.229f, 0.224f, 0.225f};
    Ort::Tensor in;
    in.name = Ort::inputNames(s)[0];
    in.shape = {1, 3, N, N};
    in.f.resize((size_t)3 * N * N);
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x) {
            float v[3];
            sampleCrop(crop, (x + 0.5) * crop.w / N - 0.5, (y + 0.5) * crop.h / N - 0.5, v);
            for (int k = 0; k < 3; ++k) in.f[(size_t)k * N * N + (size_t)y * N + x] = (v[k] / 255.0f - mean[k]) / sd[k];
        }
    std::vector<Ort::Tensor> ins = {in}, outs;
    if (!Ort::run(s, ins, outs, err) || outs.empty()) return false;
    const auto& o = outs[0].f;
    const int C = (int)(o.size() / ((size_t)N * N));
    std::vector<float> small((size_t)N * N, 0.0f);
    for (int i = 0; i < N * N; ++i) {
        int best = 0; float bv = o[i];
        for (int c = 1; c < C; ++c) if (o[(size_t)c * N * N + i] > bv) { bv = o[(size_t)c * N * N + i]; best = c; }
        for (int r = 0; r < RG_COUNT; ++r) if (regions[r] && classes[r] == best) { small[i] = 1.0f; break; }
    }
    mask.resize((size_t)crop.w * crop.h);
    for (int y = 0; y < crop.h; ++y)
        for (int x = 0; x < crop.w; ++x) mask[(size_t)y * crop.w + x] = sampleMask(small, N, N, (x + 0.5) * N / crop.w - 0.5, (y + 0.5) * N / crop.h - 0.5);
    gaussianBlur(mask, crop.w, crop.h, 5.0f * crop.w / 256.0f);
    for (auto& v : mask) v = (std::clamp(v, 0.5f, 1.0f) - 0.5f) * 2.0f;
    return true;
}

static void minInto(std::vector<float>& a, const std::vector<float>& b) { for (size_t i = 0; i < a.size() && i < b.size(); ++i) a[i] = std::min(a[i], b[i]); }

// ---------- the face swapper ----------
static std::map<std::string, std::vector<float>> converted;   // the identity through a converter, per converter (the source's)
static std::mutex convLock;

static bool convert(const char* converter, const std::vector<float>& emb, std::vector<float>& out, std::string& err) {
    OrtSession* s = Ort::open(converter, err);
    if (!s) return false;
    Ort::Tensor in;
    in.name = Ort::inputNames(s)[0];
    in.shape = {1, 512};
    in.f = emb;
    std::vector<Ort::Tensor> ins = {in}, outs;
    if (!Ort::run(s, ins, outs, err) || outs.empty() || outs[0].f.size() < 512) { if (err.empty()) err = "The identity converter gave nothing"; return false; }
    out.assign(outs[0].f.begin(), outs[0].f.begin() + 512);
    return true;
}

static std::vector<float> normalise(std::vector<float> v) {
    double n = 0;
    for (float x : v) n += (double)x * x;
    n = std::sqrt(std::max(n, 1e-12));
    for (auto& x : v) x = (float)(x / n);
    return v;
}

// what the swapper is given as "source" (an identity, or the source's picture)
static bool sourceInput(const SwapModel& sm, const Source& src, const Face& target, const Settings& st, Ort::Tensor& t, std::string& err) {
    t.name = "source";
    if (sm.type == ST_BLENDSWAP || sm.type == ST_UNIFACE) {   // the source's face picture
        const bool blend = sm.type == ST_BLENDSWAP;
        const int N = blend ? 112 : 256;
        const Affine M = alignFace(src.faces[0].kps, blend ? T_ARCFACE_112_V2 : T_FFHQ_512, N);
        const Crop c = warpCrop(src.pictures[0], M, N, N);
        t.shape = {1, 3, N, N};
        t.f.resize((size_t)3 * N * N);
        for (int i = 0; i < N * N; ++i) for (int k = 0; k < 3; ++k) t.f[(size_t)k * N * N + i] = c.px[(size_t)i * 3 + k] / 255.0f;
        return true;
    }
    std::vector<float> s(512);
    std::vector<float> tgt = target.embNorm;
    if (sm.type == ST_INSWAPPER) {
        std::lock_guard<std::mutex> g(emapLock);
        const auto& e = emaps[sm.file];
        double n = 0;
        for (float v : src.emb) n += (double)v * v;
        n = std::sqrt(std::max(n, 1e-12));
        for (int j = 0; j < 512; ++j) {
            double acc = 0;
            for (int i = 0; i < 512; ++i) acc += (double)src.emb[i] * e[(size_t)i * 512 + j];
            s[j] = (float)(acc / n);
        }
    } else if (sm.type == ST_HYPERSWAP) s = src.embNorm;
    else {   // Ghost, HiFiFace, SimSwap: through their converter (Ghost raw, the others unit length)
        std::lock_guard<std::mutex> g(convLock);
        char sig[96]; snprintf(sig, sizeof sig, ":%.6f:%.6f:%.6f", src.emb[0], src.emb[1], src.emb[511]);   // which identity
        const std::string key = std::string(sm.converter) + sig;
        auto it = converted.find(key);
        if (it == converted.end()) {
            std::vector<float> c;
            if (!convert(sm.converter, src.emb, c, err)) return false;
            it = converted.emplace(key, c).first;
        }
        s = sm.type == ST_GHOST ? it->second : normalise(it->second);
        if (sm.type == ST_GHOST) tgt = target.emb;
    }
    // lean towards the target's own identity by weight (0..1 -> +0.35..-0.35)
    const float w = 0.35f - 0.7f * std::clamp(st.weight, 0.0f, 1.0f);
    if (std::fabs(w) > 1e-4f && tgt.size() == 512) for (int i = 0; i < 512; ++i) s[i] = s[i] * (1 - w) + tgt[i] * w;
    t.shape = {1, 512};
    t.f = s;
    return true;
}

// the masks for a crop of the face, as the settings ask (box, occlusion; area and region come after the swap)
static const std::vector<float>& ovalMask(int size);
static bool preMasks(const Crop& crop, const Settings& st, std::vector<float>& mask, std::string& err) {
    // Deep-Live-Cam's look: its soft oval in the box's place (the occlusion, area and region masks still apply if on)
    mask = st.style == 1 ? ovalMask(crop.w) : st.maskBox ? boxMask(crop.w, crop.h, st.maskBlur, st.padding) : std::vector<float>((size_t)crop.w * crop.h, 1.0f);
    if (st.maskOcclusion) { std::vector<float> o; if (!occlusionMask(crop, st.occluder, o, err)) return false; minInto(mask, o); }
    return true;
}
static bool postMasks(const Crop& result, const Face& f, const Affine& M, const Settings& st, std::vector<float>& mask, std::string& err) {
    if (st.maskArea) minInto(mask, areaMask(f, M, result.w, result.h, st.areas));
    if (st.maskRegion) { std::vector<float> r; if (!regionMask(result, st.parser, st.regions, r, err)) return false; minInto(mask, r); }
    return true;
}

// ---------- the Deep-Live-Cam style (see Settings::style) ----------
// a soft oval in the crop (axes 0.44 of the side, feathered by a Gaussian of 12/128 of it): no box corners show
static const std::vector<float>& ovalMask(int size) {
    static std::map<int, std::vector<float>> cache;
    static std::mutex lock;
    std::lock_guard<std::mutex> g(lock);
    auto it = cache.find(size);
    if (it != cache.end()) return it->second;
    std::vector<float> m((size_t)size * size, 0.0f);
    const float c = size / 2.0f, r = size * 0.44f;
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) { const float dx = (x + 0.5f - c) / r, dy = (y + 0.5f - c) / r; if (dx * dx + dy * dy <= 1) m[(size_t)y * size + x] = 1; }
    gaussianBlur(m, size, size, size * 12.0f / 128.0f);
    return cache[size] = m;
}
// sRGB 0..255 <-> CIE Lab (D65)
static void toLab(const float* rgb, float* lab) {
    float l[3];
    for (int k = 0; k < 3; ++k) { const float v = rgb[k] / 255.0f; l[k] = v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f); }
    const float X = (0.4124f * l[0] + 0.3576f * l[1] + 0.1805f * l[2]) / 0.95047f, Y = 0.2126f * l[0] + 0.7152f * l[1] + 0.0722f * l[2], Z = (0.0193f * l[0] + 0.1192f * l[1] + 0.9505f * l[2]) / 1.08883f;
    auto f = [](float t) { return t > 0.008856f ? std::cbrt(t) : 7.787f * t + 16.0f / 116.0f; };
    const float fx = f(X), fy = f(Y), fz = f(Z);
    lab[0] = 116 * fy - 16; lab[1] = 500 * (fx - fy); lab[2] = 200 * (fy - fz);
}
static void fromLab(const float* lab, float* rgb) {
    const float fy = (lab[0] + 16) / 116, fx = fy + lab[1] / 500, fz = fy - lab[2] / 200;
    auto g = [](float t) { return t * t * t > 0.008856f ? t * t * t : (t - 16.0f / 116.0f) / 7.787f; };
    const float X = g(fx) * 0.95047f, Y = g(fy), Z = g(fz) * 1.08883f;
    const float l[3] = {3.2406f * X - 1.5372f * Y - 0.4986f * Z, -0.9689f * X + 1.8758f * Y + 0.0415f * Z, 0.0557f * X - 0.2040f * Y + 1.0570f * Z};
    for (int k = 0; k < 3; ++k) { const float v = std::max(0.0f, l[k]); rgb[k] = std::clamp((v <= 0.0031308f ? 12.92f * v : 1.055f * std::pow(v, 1 / 2.4f) - 0.055f) * 255.0f, 0.0f, 255.0f); }
}
// Reinhard's colour transfer: the result's Lab mean and spread made the original's, over the mask
static void colourTransfer(Crop& result, const Crop& original, const std::vector<float>& mask) {
    const size_t n = (size_t)result.w * result.h;
    std::vector<float> a(n * 3), b(n * 3);
    for (size_t i = 0; i < n; ++i) { toLab(&result.px[i * 3], &a[i * 3]); toLab(&original.px[i * 3], &b[i * 3]); }
    double sa[3] = {0}, sb[3] = {0}, qa[3] = {0}, qb[3] = {0}, wsum = 0;
    for (size_t i = 0; i < n; ++i) {
        const double w = mask[i]; if (w <= 0.05) continue;
        for (int k = 0; k < 3; ++k) { sa[k] += a[i * 3 + k] * w; sb[k] += b[i * 3 + k] * w; qa[k] += a[i * 3 + k] * a[i * 3 + k] * w; qb[k] += b[i * 3 + k] * b[i * 3 + k] * w; }
        wsum += w;
    }
    if (wsum < 16) return;
    float ma[3], mb[3], k_[3];
    for (int k = 0; k < 3; ++k) {
        ma[k] = (float)(sa[k] / wsum); mb[k] = (float)(sb[k] / wsum);
        const double va = std::max(1e-6, qa[k] / wsum - ma[k] * ma[k]), vb = std::max(1e-6, qb[k] / wsum - mb[k] * mb[k]);
        k_[k] = (float)std::clamp(std::sqrt(vb / va), 0.5, 2.0);
    }
    for (size_t i = 0; i < n; ++i) {
        float l[3]; for (int k = 0; k < 3; ++k) l[k] = (a[i * 3 + k] - ma[k]) * k_[k] + mb[k];
        fromLab(l, &result.px[i * 3]);
    }
}
// an unsharp mask on the result
static void sharpen(Crop& c, float amount) {
    if (amount <= 0) return;
    for (int k = 0; k < 3; ++k) {
        std::vector<float> ch((size_t)c.w * c.h);
        for (size_t i = 0; i < ch.size(); ++i) ch[i] = c.px[i * 3 + k];
        std::vector<float> bl = ch;
        gaussianBlur(bl, c.w, c.h, std::max(1.0f, c.w / 128.0f));
        for (size_t i = 0; i < ch.size(); ++i) c.px[i * 3 + k] = std::clamp(ch[i] + (ch[i] - bl[i]) * amount * 1.5f, 0.0f, 255.0f);
    }
}
// Poisson blending, in the crop: the result keeps its own gradients, its edge takes the original's values (the
// difference at the edge spread smoothly inside: a membrane, solved coarse to fine)
static void poissonBlend(Crop& result, const Crop& original, const std::vector<float>& mask) {
    const int w = result.w, h = result.h;
    std::vector<char> inside((size_t)w * h);
    for (size_t i = 0; i < inside.size(); ++i) inside[i] = mask[i] > 0.5f;
    for (int k = 0; k < 3; ++k) {
        // the difference (original - result), known outside the region; a membrane inside
        std::vector<float> d((size_t)w * h);
        for (size_t i = 0; i < d.size(); ++i) d[i] = original.px[i * 3 + k] - result.px[i * 3 + k];
        // coarse start: the mean of the edge
        double e = 0; int ne = 0;
        for (int y = 1; y < h - 1; ++y) for (int x = 1; x < w - 1; ++x) {
            const size_t i = (size_t)y * w + x;
            if (!inside[i] && (inside[i - 1] || inside[i + 1] || inside[i - w] || inside[i + w])) { e += d[i]; ++ne; }
        }
        const float start = ne ? (float)(e / ne) : 0.0f;
        for (size_t i = 0; i < d.size(); ++i) if (inside[i]) d[i] = start;
        for (int it = 0; it < 400; ++it) {
            float change = 0;
            for (int y = 1; y < h - 1; ++y) for (int x = 1; x < w - 1; ++x) {
                const size_t i = (size_t)y * w + x;
                if (!inside[i]) continue;
                const float nv = d[i] + 1.9f * ((d[i - 1] + d[i + 1] + d[i - w] + d[i + w]) * 0.25f - d[i]);
                change = std::max(change, std::fabs(nv - d[i])); d[i] = nv;
            }
            if (change < 0.05f) break;
        }
        for (size_t i = 0; i < d.size(); ++i) if (inside[i]) result.px[i * 3 + k] = std::clamp(result.px[i * 3 + k] + d[i], 0.0f, 255.0f);
    }
}
// your own mouth kept: the 68 points' outer lips (48-59), reaching down towards the chin (8) by size, feathered;
// the original frame's pixels put back there
static void keepMouth(Img& frame, const Img& before, const Face& f, float size) {
    std::vector<float> pts;
    const float* L = f.lm68;
    float cx = 0, cy = 0;
    for (int i = 48; i < 60; ++i) { cx += L[i * 2]; cy += L[i * 2 + 1]; }
    cx /= 12; cy /= 12;
    const float grow = 1.12f;   // a little past the lips
    for (int i = 48; i < 60; ++i) { pts.push_back(cx + (L[i * 2] - cx) * grow); pts.push_back(cy + (L[i * 2 + 1] - cy) * grow); }
    // down towards the chin: the lower lip's points pulled towards point 8 by size
    for (int i = 54; i <= 60 && size > 0; ++i) {
        const int j = i == 60 ? 48 : i;
        pts.push_back(L[j * 2] + (L[16] - L[j * 2]) * size * 0.8f); pts.push_back(L[j * 2 + 1] + (L[17] - L[j * 2 + 1]) * size * 0.8f);
    }
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (size_t i = 0; i < pts.size(); i += 2) { x0 = std::min(x0, pts[i]); x1 = std::max(x1, pts[i]); y0 = std::min(y0, pts[i + 1]); y1 = std::max(y1, pts[i + 1]); }
    const float feather = std::max(2.0f, (x1 - x0) * 0.12f);
    const int bx0 = std::max(0, (int)(x0 - feather * 3)), by0 = std::max(0, (int)(y0 - feather * 3));
    const int bx1 = std::min(frame.w, (int)(x1 + feather * 3) + 1), by1 = std::min(frame.h, (int)(y1 + feather * 3) + 1);
    const int bw = bx1 - bx0, bh = by1 - by0;
    if (bw <= 2 || bh <= 2) return;
    for (size_t i = 0; i < pts.size(); i += 2) { pts[i] -= bx0; pts[i + 1] -= by0; }
    std::vector<float> m((size_t)bw * bh, 0.0f);
    fillConvex(m, bw, bh, pts);
    gaussianBlur(m, bw, bh, feather);
    for (int y = 0; y < bh; ++y)
        for (int x = 0; x < bw; ++x) {
            const float a = m[(size_t)y * bw + x];
            if (a <= 0.001f) continue;
            uint8_t* p = &frame.px[((size_t)(y + by0) * frame.w + x + bx0) * 3];
            const uint8_t* o = &before.px[((size_t)(y + by0) * frame.w + x + bx0) * 3];
            for (int k = 0; k < 3; ++k) p[k] = (uint8_t)(p[k] * (1 - a) + o[k] * a + 0.5f);
        }
}

// smoothing between frames: per sequence, each face's last swapped crop (in its own aligned space, so it follows the
// face), found again by where its box is
struct Remembered { float cx, cy, size; int crop; std::vector<float> px; };
struct Track { std::vector<Remembered> prev, cur; };
static std::map<int, Track> tracks;
static std::mutex trackLock;
static int nextSequence = 1;
int beginSequence() { std::lock_guard<std::mutex> g(trackLock); const int id = nextSequence++; tracks[id] = Track(); return id; }
void endSequence(int id) { std::lock_guard<std::mutex> g(trackLock); tracks.erase(id); }
static void newFrame(int seq) {
    if (!seq) return;
    std::lock_guard<std::mutex> g(trackLock);
    auto it = tracks.find(seq); if (it == tracks.end()) return;
    it->second.prev = std::move(it->second.cur); it->second.cur.clear();
}
static void smoothWithLast(Crop& result, const Face& f, const Settings& st) {
    if (!st.sequence) return;
    const float cx = (f.box[0] + f.box[2]) / 2, cy = (f.box[1] + f.box[3]) / 2, size = std::max(1.0f, f.box[2] - f.box[0]);
    std::lock_guard<std::mutex> g(trackLock);
    auto it = tracks.find(st.sequence); if (it == tracks.end()) return;
    Track& t = it->second;
    const float s = std::clamp(st.dlcSmooth, 0.0f, 1.0f) * 0.75f;   // (never all of the last frame: it would freeze)
    if (s > 0)
        for (const auto& r : t.prev)
            if (r.crop == result.w && std::hypot(r.cx - cx, r.cy - cy) < 0.25f * size && std::fabs(r.size - size) < 0.25f * size) {
                for (size_t i = 0; i < result.px.size(); ++i) result.px[i] = result.px[i] * (1 - s) + r.px[i] * s;
                break;
            }
    t.cur.push_back({cx, cy, size, result.w, result.px});
}

static bool swapOne(Img& frame, const Face& target, const Source& src, const Settings& st, std::string& err) {
    const auto& sm = swapModels()[std::clamp(st.swapModel, 0, (int)swapModels().size() - 1)];
    OrtSession* s = Ort::open(sm.file, err);
    if (!s) return false;
    const int size = sm.size, boost = std::clamp(st.boost, 1, 4), big = size * boost;
    const Affine M = alignFace(target.kps, sm.tpl, big);
    const Crop crop = warpCrop(frame, M, big, big);
    std::vector<float> mask;
    if (!preMasks(crop, st, mask, err)) return false;
    Ort::Tensor srcT;
    if (!sourceInput(sm, src, target, st, srcT, err)) return false;
    const bool denorm = sm.type == ST_GHOST || sm.type == ST_HIFIFACE || sm.type == ST_HYPERSWAP || sm.type == ST_UNIFACE;
    const auto names = Ort::inputNames(s);
    Crop result(big, big);
    // pixel boost: pass (i, j) takes every boost-th pixel from offset (i, j), so each pass is the model's size
    for (int j = 0; j < boost; ++j)
        for (int i = 0; i < boost; ++i) {
            Ort::Tensor tgt;
            tgt.name = "target"; tgt.shape = {1, 3, size, size}; tgt.f.resize((size_t)3 * size * size);
            for (int y = 0; y < size; ++y)
                for (int x = 0; x < size; ++x) {
                    const float* p = &crop.px[((size_t)(y * boost + j) * big + (x * boost + i)) * 3];
                    for (int k = 0; k < 3; ++k) tgt.f[(size_t)k * size * size + (size_t)y * size + x] = (p[k] / 255.0f - sm.mean[k]) / sm.sd[k];
                }
            std::vector<Ort::Tensor> ins, outs;
            for (const auto& n : names) ins.push_back(n == "source" ? srcT : tgt);
            if (!Ort::run(s, ins, outs, err) || outs.empty()) return false;
            const auto& o = outs[0].f;
            for (int y = 0; y < size; ++y)
                for (int x = 0; x < size; ++x) {
                    float* p = &result.px[((size_t)(y * boost + j) * big + (x * boost + i)) * 3];
                    for (int k = 0; k < 3; ++k) {
                        float v = o[(size_t)k * size * size + (size_t)y * size + x];
                        if (denorm) v = v * sm.sd[k] + sm.mean[k];
                        p[k] = std::clamp(v, 0.0f, 1.0f) * 255.0f;
                    }
                }
        }
    if (!postMasks(result, target, M, st, mask, err)) return false;
    if (st.style != 1) { pasteBack(frame, result, mask, M); return true; }
    if (st.dlcColour) colourTransfer(result, crop, mask);
    smoothWithLast(result, target, st);
    sharpen(result, st.dlcSharpness);
    if (st.dlcPoisson) {   // its edge eroded a little, so the seam sits on solidly swapped pixels
        std::vector<float> core = mask; for (auto& v : core) v = v > 0.85f ? 1.0f : 0.0f;
        poissonBlend(result, crop, core);
    }
    Img before; if (st.dlcMouth) before = frame;
    pasteBack(frame, result, mask, M, std::clamp(st.dlcOpacity, 0.0f, 1.0f));
    if (st.dlcMouth) keepMouth(frame, before, target, std::clamp(st.dlcMouthSize, 0.0f, 1.0f));
    return true;
}

// ---------- the face enhancer ----------
static bool enhanceOne(Img& frame, const Face& target, const Settings& st, std::string& err) {
    const auto& em = enhanceModels()[std::clamp(st.enhanceModel, 0, (int)enhanceModels().size() - 1)];
    OrtSession* s = Ort::open(em.file, err);
    if (!s) return false;
    const int N = em.size;
    const Affine M = alignFace(target.kps, em.tpl, N);
    const Crop crop = warpCrop(frame, M, N, N);
    std::vector<float> mask;
    const int pad0[4] = {0, 0, 0, 0};
    mask = boxMask(N, N, st.maskBlur, pad0);
    if (st.maskOcclusion) { std::vector<float> o; if (!occlusionMask(crop, st.occluder, o, err)) return false; minInto(mask, o); }
    std::vector<Ort::Tensor> ins, outs;
    for (const auto& n : Ort::inputNames(s)) {
        Ort::Tensor t;
        t.name = n;
        if (n == "weight") { t.isDouble = true; t.d = std::clamp(st.enhanceWeight, 0.0f, 1.0f); }
        else {
            t.shape = {1, 3, N, N};
            t.f.resize((size_t)3 * N * N);
            for (int i = 0; i < N * N; ++i) for (int k = 0; k < 3; ++k) t.f[(size_t)k * N * N + i] = (crop.px[(size_t)i * 3 + k] / 255.0f - 0.5f) / 0.5f;
        }
        ins.push_back(t);
    }
    if (!Ort::run(s, ins, outs, err) || outs.empty()) return false;
    Crop out(N, N);
    const float* o = outs[0].f.data();
    parallelRows(0, N, [&](int y) {
        for (int i = y * N; i < (y + 1) * N; ++i)
            for (int k = 0; k < 3; ++k) out.px[(size_t)i * 3 + k] = (std::clamp(o[(size_t)k * N * N + i], -1.0f, 1.0f) + 1.0f) * 127.5f;
    });
    pasteBack(frame, out, mask, M, std::clamp(st.enhanceBlend, 0.0f, 1.0f));
    return true;
}

// ---------- the face debugger ----------
static bool debugOne(Img& frame, const Face& f, const Settings& st, std::string& err) {
    static const uint8_t red[3] = {255, 48, 48}, green[3] = {64, 220, 96}, pink[3] = {224, 64, 138}, blue[3] = {80, 160, 255}, yellow[3] = {240, 210, 60};
    const float r = std::max(1.5f, (f.box[2] - f.box[0]) / 160.0f);
    if (st.debugItems[DB_MASK]) {
        const int N = 256;
        const auto& sm = swapModels()[std::clamp(st.swapModel, 0, (int)swapModels().size() - 1)];
        const Affine M = alignFace(f.kps, sm.tpl, N);
        const Crop crop = warpCrop(frame, M, N, N);
        std::vector<float> mask;
        if (!preMasks(crop, st, mask, err) || !postMasks(crop, f, M, st, mask, err)) return false;
        maskOutline(frame, mask, N, N, M, r * 0.8f, yellow);
    }
    if (st.debugItems[DB_BOX]) rect(frame, f.box, r, red);
    auto points = [&](const float* p, int n, const uint8_t* col) { for (int i = 0; i < n; ++i) dot(frame, p[i * 2], p[i * 2 + 1], r * 1.4f, col); };
    if (st.debugItems[DB_68]) points(f.lm68, 68, blue);
    if (st.debugItems[DB_68_5]) points(f.lm68from5, 68, green);
    if (st.debugItems[DB_5]) points(f.kps5, 5, red);
    if (st.debugItems[DB_5_68]) points(f.kps, 5, pink);
    return true;
}

bool editOne(Img& frame, const Face& target, const Settings& st, std::string& err);   // editor.cpp

int processFrame(Img& frame, const Source& src, const Settings& st, const Reference& ref, std::string& err, Ort::Say say) {
    if (st.swap && !src.ready()) { err = "Pick the face to become first (or turn the face swapper off)"; return -1; }
    if (!prepare(st, err, say)) return -1;
    newFrame(st.sequence);
    double t = tick(), tPick, tSwap = 0, tEnh = 0;
    std::vector<Face> targets;
    if (!pickFaces(frame, src, st, ref, targets, nullptr, err)) return -1;
    tPick = tick() - t; t = tick();
    if (st.swap) for (const auto& f : targets) if (!swapOne(frame, f, src, st, err)) return -1;
    if (st.edit) for (const auto& f : targets) if (!editOne(frame, f, st, err)) return -1;
    tSwap = tick() - t; t = tick();
    if (st.enhance) for (const auto& f : targets) if (!enhanceOne(frame, f, st, err)) return -1;
    tEnh = tick() - t;
    if (st.debug) for (const auto& f : targets) if (!debugOne(frame, f, st, err)) return -1;
    if (profile) std::fprintf(stderr, "faces %.0f  swap %.0f  enhance %.0f ms\n", tPick, tSwap, tEnh);
    return (int)targets.size();
}

}  // namespace Mimic
