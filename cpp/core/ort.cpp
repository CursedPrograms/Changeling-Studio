// Mimic - ONNX Runtime (see ort.h)
#include "ort.h"
#include "onnxruntime_c_api.h"

#include <cstdlib>
#include <filesystem>
#include <map>
#include <mutex>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <urlmon.h>
#pragma comment(lib, "urlmon.lib")
#else
#include <dlfcn.h>
#endif

namespace fs = std::filesystem;

namespace Ort {

static const OrtApi* api = nullptr;
static OrtEnv* env = nullptr;
static bool gpuOn = false, wantGpu = true;
#ifdef _WIN32
static HMODULE lib = nullptr;
#else
static void* lib = nullptr;
#endif
static std::map<std::string, OrtSession*> sessions;
static std::mutex mtx;

static fs::path u8(const std::string& s) { return fs::u8path(s); }

static std::string exeDir() {
#ifdef _WIN32
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return fs::path(buf).parent_path().u8string();
#else
    std::error_code ec;
    return fs::read_symlink("/proc/self/exe", ec).parent_path().u8string();
#endif
}

static bool ok(OrtStatus* s, std::string& err) {
    if (!s) return true;
    err = std::string("ONNX Runtime: ") + api->GetErrorMessage(s);
    api->ReleaseStatus(s);
    return false;
}

bool init(bool gpu, std::string& err) {
    std::lock_guard<std::mutex> g(mtx);
    if (api) return true;
    wantGpu = gpu;
    typedef const OrtApiBase*(ORT_API_CALL * GetBase)();
    GetBase getBase = nullptr;
#ifdef _WIN32
    // DirectML.dll first, so onnxruntime.dll finds the one next to Mimic and not an old one in System32
    LoadLibraryW((u8(exeDir()) / "DirectML.dll").wstring().c_str());
    lib = LoadLibraryW((u8(exeDir()) / "onnxruntime.dll").wstring().c_str());
    if (!lib) lib = LoadLibraryW(L"onnxruntime.dll");
    if (lib) getBase = (GetBase)GetProcAddress(lib, "OrtGetApiBase");
#else
    lib = dlopen("libonnxruntime.so", RTLD_NOW);
    if (lib) getBase = (GetBase)dlsym(lib, "OrtGetApiBase");
#endif
    if (getBase) api = getBase()->GetApi(ORT_API_VERSION);
    if (!api) { err = "ONNX Runtime (onnxruntime.dll) isn't next to Mimic"; return false; }
    return ok(api->CreateEnv(ORT_LOGGING_LEVEL_ERROR, "Mimic", &env), err);
}

bool usingGpu() { return gpuOn; }
std::string device() { return gpuOn ? "GPU (DirectML)" : "CPU"; }

// FaceFusion's models (https://github.com/facefusion/facefusion-assets/releases/download/<release>/<file>), the
// F:\AIModels folder this PC keeps them in, and other names this PC already has them under
struct Source { const char* file; const char* release; const char* sub; const char* alias; };
static const Source SOURCES[] = {
    // faces: detectors, landmarkers, recogniser, classifier
    {"retinaface_10g.onnx", "models-3.0.0", "face", "det_10g.onnx"},
    {"scrfd_2.5g.onnx", "models-3.0.0", "face", nullptr},
    {"yoloface_8n.onnx", "models-3.0.0", "face", nullptr},
    {"yunet_2023_mar.onnx", "models-3.4.0", "face", "face_detection_yunet_2023mar.onnx"},
    {"2dfan4.onnx", "models-3.0.0", "face", nullptr},
    {"peppa_wutz.onnx", "models-3.0.0", "face", nullptr},
    {"fan_68_5.onnx", "models-3.0.0", "face", nullptr},
    {"arcface_w600k_r50.onnx", "models-3.0.0", "face", "w600k_r50.onnx"},
    {"fairface.onnx", "models-3.0.0", "face", nullptr},
    // masks
    {"xseg_1.onnx", "models-3.1.0", "faceswap", nullptr},
    {"xseg_2.onnx", "models-3.1.0", "faceswap", nullptr},
    {"xseg_3.onnx", "models-3.2.0", "faceswap", nullptr},
    {"bisenet_resnet_18.onnx", "models-3.1.0", "face", nullptr},
    {"bisenet_resnet_34.onnx", "models-3.0.0", "face", nullptr},
    // swappers and their identity converters
    {"inswapper_128.onnx", "models-3.0.0", "faceswap", nullptr},
    {"inswapper_128_fp16.onnx", "models-3.0.0", "faceswap", nullptr},
    {"hyperswap_1a_256.onnx", "models-3.3.0", "faceswap", nullptr},
    {"hyperswap_1b_256.onnx", "models-3.3.0", "faceswap", nullptr},
    {"hyperswap_1c_256.onnx", "models-3.3.0", "faceswap", nullptr},
    {"simswap_256.onnx", "models-3.0.0", "faceswap", nullptr},
    {"simswap_unofficial_512.onnx", "models-3.0.0", "faceswap", nullptr},
    {"ghost_1_256.onnx", "models-3.0.0", "faceswap", nullptr},
    {"ghost_2_256.onnx", "models-3.0.0", "faceswap", nullptr},
    {"ghost_3_256.onnx", "models-3.0.0", "faceswap", nullptr},
    {"hififace_unofficial_256.onnx", "models-3.1.0", "faceswap", nullptr},
    {"blendswap_256.onnx", "models-3.0.0", "faceswap", nullptr},
    {"uniface_256.onnx", "models-3.0.0", "faceswap", nullptr},
    {"crossface_ghost.onnx", "models-3.4.0", "faceswap", nullptr},
    {"crossface_hififace.onnx", "models-3.4.0", "faceswap", nullptr},
    {"crossface_simswap.onnx", "models-3.4.0", "faceswap", nullptr},
    // the face editor (LivePortrait)
    {"live_portrait_feature_extractor.onnx", "models-3.0.0", "faceswap", nullptr},
    {"live_portrait_motion_extractor.onnx", "models-3.0.0", "faceswap", nullptr},
    {"live_portrait_eye_retargeter.onnx", "models-3.0.0", "faceswap", nullptr},
    {"live_portrait_lip_retargeter.onnx", "models-3.0.0", "faceswap", nullptr},
    {"live_portrait_stitcher.onnx", "models-3.0.0", "faceswap", nullptr},
    {"live_portrait_generator.onnx", "models-3.0.0", "faceswap", nullptr},
    // restorers
    {"gfpgan_1.2.onnx", "models-3.0.0", "faceswap", nullptr},
    {"gfpgan_1.3.onnx", "models-3.0.0", "faceswap", nullptr},
    {"gfpgan_1.4.onnx", "models-3.0.0", "faceswap", nullptr},
    {"codeformer.onnx", "models-3.0.0", "faceswap", nullptr},
    {"gpen_bfr_256.onnx", "models-3.0.0", "faceswap", nullptr},
    {"gpen_bfr_512.onnx", "models-3.0.0", "faceswap", nullptr},
    {"gpen_bfr_1024.onnx", "models-3.0.0", "faceswap", nullptr},
    {"gpen_bfr_2048.onnx", "models-3.0.0", "faceswap", nullptr},
    {"restoreformer_plus_plus.onnx", "models-3.0.0", "faceswap", nullptr},
};

static const Source* sourceOf(const std::string& file) {
    for (const auto& s : SOURCES) if (file == s.file) return &s;
    return nullptr;
}
bool known(const std::string& file) { return sourceOf(file) != nullptr; }

static std::string appModels() {
#ifdef _WIN32
    const char* a = std::getenv("APPDATA");
    return std::string(a ? a : ".") + "/Atelier/models";
#else
    const char* h = std::getenv("HOME");
    return std::string(h ? h : ".") + "/.config/Atelier/models";
#endif
}

static std::string localModel(const std::string& file) {
    std::error_code ec;
    const Source* src = sourceOf(file);
    std::vector<std::string> spots = {exeDir() + "/models/" + file};
#ifdef _WIN32
    const char* home = std::getenv("USERPROFILE");
    if (fs::exists("F:/AIModels", ec)) {
        for (const char* sub : {src ? src->sub : "face", "face", "faceswap", ""}) {
            const std::string d = std::string("F:/AIModels/") + sub + (*sub ? "/" : "");
            spots.push_back(d + file);
            if (src && src->alias) spots.push_back(d + src->alias);
        }
    }
#else
    const char* home = std::getenv("HOME");
#endif
    if (src && src->alias && home) spots.push_back(std::string(home) + "/.insightface/models/buffalo_l/" + src->alias);
    spots.push_back(appModels() + "/" + file);
    for (const auto& p : spots) if (fs::exists(u8(p), ec) && fs::file_size(u8(p), ec) > 10000) return p;
    return "";
}

bool have(const std::string& file) { return !localModel(file).empty(); }

std::string findModel(const std::string& file, Say say) {
    const std::string here = localModel(file);
    if (!here.empty()) return here;
    const Source* src = sourceOf(file);
    if (!src) return "";
    std::error_code ec;
    std::string dest = appModels() + "/" + file;
#ifdef _WIN32
    if (fs::exists("F:/AIModels", ec)) dest = std::string("F:/AIModels/") + src->sub + "/" + file;   // this PC's model store
#endif
    fs::create_directories(u8(dest).parent_path(), ec);
    const std::string url = std::string("https://github.com/facefusion/facefusion-assets/releases/download/") + src->release + "/" + file;
    if (say) say("Getting " + file + " once, from FaceFusion's model releases...");
#ifdef _WIN32
    const bool got = URLDownloadToFileW(nullptr, fs::path(url).wstring().c_str(), u8(dest).wstring().c_str(), 0, nullptr) == S_OK;
#else
    const bool got = std::system(("curl -sL -o '" + dest + "' '" + url + "'").c_str()) == 0;
#endif
    return got && fs::exists(u8(dest), ec) && fs::file_size(u8(dest), ec) > 10000 ? dest : "";
}

OrtSession* open(const std::string& file, std::string& err, Say say) {
    if (!api && !init(true, err)) return nullptr;
    {
        std::lock_guard<std::mutex> g(mtx);
        auto it = sessions.find(file);
        if (it != sessions.end()) return it->second;
    }
    const std::string path = findModel(file, say);
    if (path.empty()) { err = "Can't find or download " + file; return nullptr; }
    if (say) say("Loading " + file + "...");
    OrtSessionOptions* so;
    if (!ok(api->CreateSessionOptions(&so), err)) return nullptr;
    api->SetSessionGraphOptimizationLevel(so, ORT_ENABLE_ALL);
    bool gpu = false;
    // models DirectML gets wrong: HyperSwap comes out smeared on it (tested 2026-11-11, ONNX Runtime 1.20.1 +
    // DirectML 1.15.4, RTX 2060), right on the CPU
    const bool cpuOnly = file.rfind("hyperswap_", 0) == 0;
#ifdef _WIN32
    if (wantGpu && !cpuOnly) {
        // DirectML wants these two; its provider is exported by the DirectML build only
        typedef OrtStatus*(ORT_API_CALL * AppendDml)(OrtSessionOptions*, int);
        auto appendDml = (AppendDml)GetProcAddress(lib, "OrtSessionOptionsAppendExecutionProvider_DML");
        if (appendDml) {
            api->DisableMemPattern(so);
            api->SetSessionExecutionMode(so, ORT_SEQUENTIAL);
            std::string e;
            gpu = ok(appendDml(so, 0), e);
        }
    }
#endif
    OrtSession* s = nullptr;
#ifdef _WIN32
    bool made = ok(api->CreateSession(env, u8(path).wstring().c_str(), so, &s), err);
#else
    bool made = ok(api->CreateSession(env, path.c_str(), so, &s), err);
#endif
    api->ReleaseSessionOptions(so);
    if (!made && gpu) {   // a model DirectML can't take: the CPU then
        OrtSessionOptions* cpu;
        api->CreateSessionOptions(&cpu);
        api->SetSessionGraphOptimizationLevel(cpu, ORT_ENABLE_ALL);
#ifdef _WIN32
        made = ok(api->CreateSession(env, u8(path).wstring().c_str(), cpu, &s), err);
#else
        made = ok(api->CreateSession(env, path.c_str(), cpu, &s), err);
#endif
        api->ReleaseSessionOptions(cpu);
        gpu = false;
    }
    if (!made) return nullptr;
    if (gpu) gpuOn = true;
    std::lock_guard<std::mutex> g(mtx);
    sessions[file] = s;
    return s;
}

std::vector<std::string> inputNames(OrtSession* s) {
    OrtAllocator* alloc;
    api->GetAllocatorWithDefaultOptions(&alloc);
    size_t n = 0;
    api->SessionGetInputCount(s, &n);
    std::vector<std::string> names;
    for (size_t i = 0; i < n; ++i) { char* c; api->SessionGetInputName(s, i, alloc, &c); names.push_back(c); alloc->Free(alloc, c); }
    return names;
}

bool run(OrtSession* s, std::vector<Tensor>& inputs, std::vector<Tensor>& outputs, std::string& err) {
    OrtMemoryInfo* mem;
    if (!ok(api->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &mem), err)) return false;
    std::vector<OrtValue*> in;
    std::vector<const char*> inNames;
    bool good = true;
    for (auto& t : inputs) {
        OrtValue* v = nullptr;
        if (t.isDouble) good = ok(api->CreateTensorWithDataAsOrtValue(mem, &t.d, sizeof(double), t.shape.data(), t.shape.size(), ONNX_TENSOR_ELEMENT_DATA_TYPE_DOUBLE, &v), err);
        else good = ok(api->CreateTensorWithDataAsOrtValue(mem, t.f.data(), t.f.size() * sizeof(float), t.shape.data(), t.shape.size(), ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &v), err);
        if (!good) break;
        in.push_back(v); inNames.push_back(t.name.c_str());
    }
    api->ReleaseMemoryInfo(mem);
    if (!good) { for (auto v : in) api->ReleaseValue(v); return false; }
    OrtAllocator* alloc;
    api->GetAllocatorWithDefaultOptions(&alloc);
    size_t nOut = 0;
    api->SessionGetOutputCount(s, &nOut);
    std::vector<char*> outNamesC(nOut);
    std::vector<const char*> outNames(nOut);
    for (size_t i = 0; i < nOut; ++i) { api->SessionGetOutputName(s, i, alloc, &outNamesC[i]); outNames[i] = outNamesC[i]; }
    std::vector<OrtValue*> res(nOut, nullptr);
    good = ok(api->Run(s, nullptr, inNames.data(), in.data(), in.size(), outNames.data(), nOut, res.data()), err);
    for (auto v : in) api->ReleaseValue(v);
    outputs.assign(nOut, {});
    for (size_t i = 0; i < nOut; ++i) {
        outputs[i].name = outNamesC[i];
        alloc->Free(alloc, outNamesC[i]);
        if (!good || !res[i]) continue;
        OrtTensorTypeAndShapeInfo* info;
        api->GetTensorTypeAndShape(res[i], &info);
        size_t dims = 0, count = 0;
        api->GetDimensionsCount(info, &dims);
        outputs[i].shape.resize(dims);
        api->GetDimensions(info, outputs[i].shape.data(), dims);
        api->GetTensorShapeElementCount(info, &count);
        ONNXTensorElementDataType type;
        api->GetTensorElementType(info, &type);
        api->ReleaseTensorTypeAndShapeInfo(info);
        if (type == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
            float* d; api->GetTensorMutableData(res[i], (void**)&d);
            outputs[i].f.assign(d, d + count);
        } else if (type == ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64) {
            int64_t* d; api->GetTensorMutableData(res[i], (void**)&d);
            outputs[i].i.assign(d, d + count);
        }
        api->ReleaseValue(res[i]);
    }
    return good;
}

}  // namespace Ort
