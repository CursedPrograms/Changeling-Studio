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
static bool preMasks(const Crop& crop, const Settings& st, std::vector<float>& mask, std::string& err) {
    mask = st.maskBox ? boxMask(crop.w, crop.h, st.maskBlur, st.padding) : std::vector<float>((size_t)crop.w * crop.h, 1.0f);
    if (st.maskOcclusion) { std::vector<float> o; if (!occlusionMask(crop, st.occluder, o, err)) return false; minInto(mask, o); }
    return true;
}
static bool postMasks(const Crop& result, const Face& f, const Affine& M, const Settings& st, std::vector<float>& mask, std::string& err) {
    if (st.maskArea) minInto(mask, areaMask(f, M, result.w, result.h, st.areas));
    if (st.maskRegion) { std::vector<float> r; if (!regionMask(result, st.parser, st.regions, r, err)) return false; minInto(mask, r); }
    return true;
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
    pasteBack(frame, result, mask, M);
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
