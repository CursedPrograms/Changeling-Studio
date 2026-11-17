// Changeling Studio (C++) - every Changeling in one window, no Python: pick a Changeling on the left, tune it with
// sliders, see the picture before and after in the middle (drag the line), save it or render the video; the
// realtime Changelings work on the webcam. Part of Atelier.
//
//   ChangelingStudio [picture or video]
//   ChangelingStudio --capture out.bmp [--source face.jpg] [--module cosmeta] [--lips 0.8] [target]   (a screenshot, then quit)
#include "camera.h"
#include "cosmeta.h"
#include "lang.h"
#include "mimic.h"
#include "picture.h"
#include "splash.h"
#include "video.h"

#include <SDL.h>
#include <SDL_opengl.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl2.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

namespace fs = std::filesystem;

// ---------- the Atelier look: Changeling's greys, the Atelier blue for the controls; solid, no gradients, no shadows ----------
static const ImU32 C_BG = IM_COL32(0x1a, 0x1a, 0x1a, 255), C_PANEL = IM_COL32(0x24, 0x24, 0x24, 255),
                   C_LINE = IM_COL32(0x3a, 0x3a, 0x3a, 255), C_TEXT = IM_COL32(0xe6, 0xe6, 0xe6, 255),
                   C_DIM = IM_COL32(0x9a, 0x9a, 0x9a, 255), C_BLUE = IM_COL32(0x00, 0x7b, 0xb7, 255),
                   C_DEEP = IM_COL32(0x00, 0x51, 0x84, 255), C_WELL = IM_COL32(0x14, 0x14, 0x14, 255);
static ImVec4 col4(ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); }
static void applyStyle() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 0; s.FrameRounding = 2; s.GrabRounding = 2; s.PopupRounding = 2; s.ScrollbarRounding = 2;
    s.WindowBorderSize = 0; s.FrameBorderSize = 0; s.PopupBorderSize = 1;
    s.WindowPadding = ImVec2(12, 10); s.FramePadding = ImVec2(7, 4); s.ItemSpacing = ImVec2(6, 7);
    ImVec4* c = s.Colors;
    c[ImGuiCol_Text] = col4(C_TEXT); c[ImGuiCol_TextDisabled] = col4(C_DIM);
    c[ImGuiCol_WindowBg] = col4(C_PANEL); c[ImGuiCol_ChildBg] = col4(C_PANEL); c[ImGuiCol_PopupBg] = col4(IM_COL32(0x21, 0x21, 0x21, 255));
    c[ImGuiCol_Border] = col4(C_LINE); c[ImGuiCol_Separator] = col4(C_LINE);
    c[ImGuiCol_FrameBg] = col4(C_WELL); c[ImGuiCol_FrameBgHovered] = col4(IM_COL32(0x2e, 0x2e, 0x2e, 255)); c[ImGuiCol_FrameBgActive] = col4(IM_COL32(0x33, 0x33, 0x33, 255));
    c[ImGuiCol_MenuBarBg] = col4(C_BG);
    c[ImGuiCol_CheckMark] = col4(C_BLUE); c[ImGuiCol_SliderGrab] = col4(C_BLUE); c[ImGuiCol_SliderGrabActive] = col4(C_DEEP);
    c[ImGuiCol_Button] = col4(IM_COL32(0x33, 0x33, 0x33, 255)); c[ImGuiCol_ButtonHovered] = col4(C_DEEP); c[ImGuiCol_ButtonActive] = col4(C_BLUE);
    c[ImGuiCol_Header] = col4(IM_COL32(0x33, 0x33, 0x33, 255)); c[ImGuiCol_HeaderHovered] = col4(C_DEEP); c[ImGuiCol_HeaderActive] = col4(C_BLUE);
    c[ImGuiCol_PlotHistogram] = col4(C_BLUE);
}
static bool fileExists(const std::string& f) { std::error_code ec; return fs::exists(fs::u8path(f), ec); }
static std::string exeDir() { char* b = SDL_GetBasePath(); std::string s = b ? b : ""; if (b) SDL_free(b); return s; }
static std::string prefsFile() { char* p = SDL_GetPrefPath("Cursed Entertainment", "Changeling Studio"); std::string s = p ? std::string(p) + "studio.ini" : ""; if (p) SDL_free(p); return s; }

static void loadFonts() {
    ImGuiIO& io = ImGui::GetIO();
    const char* ui = "C:/Windows/Fonts/segoeui.ttf";
    if (!fileExists(ui)) { io.Fonts->AddFontDefault(); return; }
    static const ImWchar ranges[] = {0x0020, 0x024F, 0x0400, 0x04FF, 0x2010, 0x2027, 0x2190, 0x21FF, 0x2600, 0x26FF, 0};
    auto add = [&](const char* file, float size) {
        ImFont* f = io.Fonts->AddFontFromFileTTF(file, size, nullptr, ranges);
        ImFontConfig m; m.MergeMode = true;
        if (fileExists("C:/Windows/Fonts/seguisym.ttf")) { static const ImWchar sym[] = {0x2190, 0x21FF, 0x2600, 0x26FF, 0}; io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/seguisym.ttf", size, &m, sym); }
        if (Lang::needsCJK()) {   // Japanese, Chinese or Korean: their font, for that language only
            const char* cjk = Lang::current() == "ko" ? "C:/Windows/Fonts/malgun.ttf" : Lang::current() == "zh" ? "C:/Windows/Fonts/msyh.ttc" : "C:/Windows/Fonts/YuGothM.ttc";
            const ImWchar* r = Lang::current() == "ko" ? io.Fonts->GetGlyphRangesKorean() : Lang::current() == "zh" ? io.Fonts->GetGlyphRangesChineseSimplifiedCommon() : io.Fonts->GetGlyphRangesJapanese();
            if (fileExists(cjk)) io.Fonts->AddFontFromFileTTF(cjk, size, &m, r);
        }
        return f;
    };
    add(ui, 16.0f);
    ImFont* title = add(fileExists("C:/Windows/Fonts/segoeuil.ttf") ? "C:/Windows/Fonts/segoeuil.ttf" : ui, 46.0f);
    ImFont* small = add(ui, 13.0f);
    Splash::setFonts(title, small);
}

static std::string fileDialog(bool save, const char* filter, const char* defExt, bool multi = false, std::vector<std::string>* many = nullptr) {
#ifdef _WIN32
    std::wstring f;
    for (const char* p = filter; *p; ++p) f += (*p == '|') ? L'\0' : (wchar_t)*p;
    f += L'\0'; f += L'\0';
    std::vector<wchar_t> buf(32768, 0);
    OPENFILENAMEW o{};
    o.lStructSize = sizeof(o); o.lpstrFilter = f.c_str(); o.lpstrFile = buf.data(); o.nMaxFile = (DWORD)buf.size();
    std::wstring ext = defExt ? std::wstring(defExt, defExt + strlen(defExt)) : L"";
    o.lpstrDefExt = defExt ? ext.c_str() : nullptr;
    o.Flags = save ? (OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR) : (OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR | (multi ? OFN_ALLOWMULTISELECT | OFN_EXPLORER : 0));
    if (!(save ? GetSaveFileNameW(&o) : GetOpenFileNameW(&o))) return "";
    if (multi && many) {
        std::wstring dir = buf.data();
        const wchar_t* p = buf.data() + dir.size() + 1;
        if (!*p) many->push_back(fs::path(dir).u8string());
        for (; *p; p += wcslen(p) + 1) many->push_back((fs::path(dir) / p).u8string());
        return many->empty() ? "" : many->front();
    }
    return fs::path(buf.data()).u8string();
#else
    (void)filter; (void)multi; (void)many;
    FILE* p = popen(save ? "zenity --file-selection --save --confirm-overwrite 2>/dev/null" : "zenity --file-selection 2>/dev/null", "r");
    if (!p) return {};
    char line[4096] = {0}; std::string r = fgets(line, sizeof line, p) ? line : ""; pclose(p);
    while (!r.empty() && (r.back() == '\n' || r.back() == '\r')) r.pop_back();
    if (save && !r.empty() && defExt && r.find('.') == std::string::npos) r += std::string(".") + defExt;
    return r;
#endif
}
static const char* PICTURES = "Pictures|*.png;*.jpg;*.jpeg;*.jfif;*.bmp;*.tga|All files|*.*|";
static const char* TARGETS = "Pictures and videos|*.png;*.jpg;*.jpeg;*.jfif;*.bmp;*.tga;*.mp4;*.mov;*.mkv;*.webm;*.avi;*.m4v|All files|*.*|";

static void upload(GLuint& tex, const Img& im) {
    if (im.empty()) return;
    if (!tex) glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, im.w, im.h, 0, GL_RGB, GL_UNSIGNED_BYTE, im.px.data());
}

// ---------- the Changelings ----------
enum Module { M_IMAGO, M_REALTIME, M_COSMETA, M_TIRESIAS, M_CHRONOS, M_NATIO, M_TONSOR, M_ECHO, M_COUNT };
struct ModuleInfo { const char* id; const char* name; const char* what; bool ready; const char* needs; };
static const ModuleInfo MODULES[M_COUNT] = {
    {"imago", "Imago", "A face onto a photo or a video.", true, ""},
    {"realtime", "Imago Realtime", "The face (and Cosmeta's look) live on the webcam, to record or for calls.", true, ""},
    {"cosmeta", "Cosmeta", "Makeup and hair colour: lips, blush, eye shadow, liner, brows, hair, skin.", true, ""},
    {"tiresias", "Tiresias", "Gender swap (StyleGAN2).", false, "StyleGAN2 + e4e as ONNX: the next step of the C++ Studio"},
    {"chronos", "Chronos", "Older or younger (StyleGAN2).", false, "StyleGAN2 + e4e as ONNX: the next step of the C++ Studio"},
    {"natio", "Natio", "Where a face is from (StyleGAN2).", false, "StyleGAN2 + e4e as ONNX: the next step of the C++ Studio"},
    {"tonsor", "Tonsor", "Hairstyles (HairFastGAN).", false, "HairFastGAN as ONNX: later"},
    {"echo", "Echo", "Voices (RVC).", false, "RVC as ONNX: later"},
};
static int module = M_IMAGO;

// ---------- the state ----------
static SDL_Window* window = nullptr;
static bool quit = false;
static Mimic::Settings settings;
static Mimic::Source source;
static Mimic::Reference reference;
static Cosmeta::Look look;
struct SourcePic { std::string path; Img thumb; GLuint tex = 0; bool ready = false; };
static std::vector<SourcePic> sourcePics;
static std::string targetPath; static bool targetIsVideo = false; static Video::Info videoInfo; static float videoAt = 0;
static Img original, result; static bool haveResult = false;
static GLuint originalTex = 0, resultTex = 0; static bool originalStale = false, resultStale = false;
static float wipe = 0.5f; static int view = 0;   // 0 before | after, 1 after, 2 before
static bool cosmetaOnImago = false;              // Imago: Cosmeta's look after the swap too (a chain)

// the worker: one job at a time off the window's thread
static std::mutex mtx; static std::deque<std::function<void()>> jobs; static std::vector<std::function<void()>> onMain;
static std::string status; static std::atomic<bool> busy{false}, workerQuit{false}, stopRender{false};
static std::atomic<float> progress{-1};
static std::thread worker;
static bool previewWanted = false;
static void say(const std::string& s) { std::lock_guard<std::mutex> g(mtx); status = s; }
static void post(std::function<void()> f) { std::lock_guard<std::mutex> g(mtx); onMain.push_back(std::move(f)); }
static void queue(std::function<void()> f) { std::lock_guard<std::mutex> g(mtx); jobs.push_back(std::move(f)); }
static void workerLoop() {
    while (!workerQuit) {
        std::function<void()> job;
        { std::lock_guard<std::mutex> g(mtx); if (!jobs.empty()) { job = std::move(jobs.front()); jobs.pop_front(); } }
        if (!job) { std::this_thread::sleep_for(std::chrono::milliseconds(5)); continue; }
        busy = true; job(); busy = false;
    }
}
static std::string base(const std::string& p) { return fs::u8path(p).filename().u8string(); }

static bool findFaces(const Img& im, std::vector<Face>& faces, std::string& err) {
    if (!Faces::analyse(im, settings.detect, faces, err, false)) return false;
    if (faces.empty()) { DetectSettings wide = settings.detect; for (int& m : wide.margin) m = std::max(m, 60); if (!Faces::analyse(im, wide, faces, err, false)) return false; }
    return true;
}
// what the chosen Changeling does to one picture (the preview, each video frame, each webcam frame)
static bool processPicture(Img& im, int mod, bool cosmetaToo, std::string& err, int sequence = 0) {
    if (mod == M_IMAGO || mod == M_REALTIME) {
        Mimic::Settings st = settings; st.sequence = sequence;   // (a video's frames: smoothed one into the next)
        if (source.ready() && settings.swap) { if (Mimic::processFrame(im, source, st, reference, err) < 0) return false; }
        if (!cosmetaToo || !look.any()) return true;
    }
    if (look.any()) {
        std::vector<Face> faces;
        if (!findFaces(im, faces, err)) return false;
        if (!Cosmeta::apply(im, faces, look, err)) return false;
    }
    return true;
}

// a picture with a grey border round it (half its size each side): a close-up's face, small enough to be found
static Img padded(const Img& im) {
    Img o(im.w * 2, im.h * 2);
    std::fill(o.px.begin(), o.px.end(), (uint8_t)110);
    for (int y = 0; y < im.h; ++y) memcpy(&o.px[((size_t)(y + im.h / 2) * o.w + im.w / 2) * 3], &im.px[(size_t)y * im.w * 3], (size_t)im.w * 3);
    return o;
}
static void addSources(const std::vector<std::string>& files) {
    for (const auto& f : files) {
        sourcePics.push_back({f});
        const size_t i = sourcePics.size() - 1;
        queue([f, i] {
            Img im; std::string err;
            if (!loadPicture(f, im, err)) { say(err); return; }
            if (!Mimic::addSource(source, im, err, say)) {   // a close-up: again, with room round it
                im = padded(im); err.clear();
                if (!Mimic::addSource(source, im, err, say)) { say(base(f) + ": " + err); return; }
            }
            Mimic::finishSource(source);
            // the face's square, for the thumbnail
            const Face& fc = source.faces.back();
            const float cx = (fc.box[0] + fc.box[2]) / 2, cy = (fc.box[1] + fc.box[3]) / 2, side = std::max(fc.box[2] - fc.box[0], fc.box[3] - fc.box[1]) * 1.3f;
            Affine M; M.m[0] = M.m[4] = 72 / side; M.m[2] = -(cx - side / 2) * M.m[0]; M.m[5] = -(cy - side / 2) * M.m[4];
            const Crop c = warpCrop(im, M, 72, 72);
            Img t(72, 72); for (size_t k = 0; k < t.px.size(); ++k) t.px[k] = (uint8_t)std::clamp(c.px[k], 0.0f, 255.0f);
            post([i, t] { if (i < sourcePics.size()) { sourcePics[i].thumb = t; sourcePics[i].ready = true; } previewWanted = true; });
            say(T("The face is in"));
        });
    }
}
static void loadTarget(const std::string& path) {
    targetPath = path; targetIsVideo = Video::isVideo(path); haveResult = false; videoAt = 0;
    queue([path] {
        Img im; std::string err;
        if (Video::isVideo(path)) {
            Video::Info info;
            if (!Video::probe(path, info, err) || !Video::frameAt(path, 0, im, err)) { say(err); return; }
            post([info] { videoInfo = info; });
        } else if (!loadPicture(path, im, err)) { say(err); return; }
        post([im] { original = im; originalStale = true; previewWanted = true; });
        say(base(path));
    });
}
static void runPreview() {
    if (original.empty() || module >= M_TIRESIAS || module == M_REALTIME) return;
    const Img im = original; const int mod = module; const bool chain = cosmetaOnImago;
    queue([im, mod, chain] {
        Img out = im; std::string err;
        if (!processPicture(out, mod, chain, err)) { say(err); return; }
        post([out] { result = out; resultStale = true; haveResult = true; });
        say(T("Done"));
    });
}
static void seekVideo(float seconds) {
    const std::string p = targetPath;
    queue([p, seconds] { Img im; std::string err; if (Video::frameAt(p, seconds, im, err)) post([im] { original = im; originalStale = true; previewWanted = true; }); });
}
static void savePicture() {
    if (!haveResult) return;
    const std::string f = fileDialog(true, "PNG picture|*.png|JPEG picture|*.jpg|", "png");
    if (f.empty()) return;
    std::string err; say(savePicture(f, result, err) ? std::string(T("Saved ")) + base(f) : err);
}
static void renderVideo() {
    const std::string out = fileDialog(true, "MP4 video|*.mp4|", "mp4");
    if (out.empty()) return;
    const std::string in = targetPath; const int mod = module; const bool chain = cosmetaOnImago;
    stopRender = false; progress = 0;
    queue([in, out, mod, chain] {
        Video::Reader r; std::string err;
        if (!r.open(in, err)) { say(err); progress = -1; return; }
        Video::Writer w;
        if (!w.open(out, r.info.w, r.info.h, r.info.fps, in, 18, err)) { say(err); progress = -1; return; }
        Img f; long n = 0;
        const int seq = Mimic::beginSequence();
        while (!stopRender && r.next(f)) {
            if (!processPicture(f, mod, chain, err, seq)) { say(err); break; }
            w.write(f); ++n;
            progress = r.info.frames > 0 ? (float)n / r.info.frames : 0.5f;
            say(std::string(T("Rendering")) + " " + std::to_string(n) + (r.info.frames > 0 ? " / " + std::to_string(r.info.frames) : ""));
        }
        w.close();
        Mimic::endSequence(seq);
        say(stopRender ? std::string(T("Stopped")) : std::string(T("Saved ")) + base(out));
        progress = -1;
    });
}

// ---------- the webcam (the realtime Changelings) ----------
static Camera::Stream cam;
static std::vector<std::string> cameras; static int camIndex = 0;
static long long camSeen = 0; static Img live, liveOut; static GLuint liveTex = 0; static bool liveStale = false;
static std::atomic<bool> liveBusy{false};
static bool liveSwap = true, liveCosmeta = true;
static Video::Writer recorder; static bool recording = false; static std::string recordPath;
static int liveSequence = 0;   // the webcam session: its frames smoothed one into the next (Mimic::beginSequence)
static double liveFps = 0; static std::chrono::steady_clock::time_point liveT0 = std::chrono::steady_clock::now(); static int liveFrames = 0;
static void liveTick() {
    if (!cam.running()) return;
    Img f;
    if (!liveBusy && cam.latest(f, camSeen)) {   // the newest frame, when the last one's done (frames in between are dropped)
        liveBusy = true;
        std::thread([f]() mutable {
            std::string err;
            if (liveSwap && source.ready() && settings.swap) { Mimic::Settings st = settings; st.sequence = liveSequence; Mimic::processFrame(f, source, st, reference, err); }
            if (liveCosmeta && look.any()) { std::vector<Face> fs; if (findFaces(f, fs, err)) Cosmeta::apply(f, fs, look, err); }
            post([f] {
                liveOut = f; liveStale = true;
                if (recording) recorder.write(liveOut);
                ++liveFrames;
                const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - liveT0).count();
                if (s >= 1) { liveFps = liveFrames / s; liveFrames = 0; liveT0 = std::chrono::steady_clock::now(); }
            });
            liveBusy = false;
        }).detach();
    }
}

// ---------- the window ----------
static bool slider(const char* label, float* v, float lo, float hi, const char* fmt, const char* tip) {
    ImGui::TextUnformatted(T(label));
    ImGui::SetNextItemWidth(-1);
    char id[64]; snprintf(id, sizeof id, "##%s", label);
    const bool ch = ImGui::SliderFloat(id, v, lo, hi, fmt);
    if (tip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", T(tip));
    return ch;
}
static bool colourSlider(const char* label, float* amount, float* colour, const char* tip) {
    ImGui::TextUnformatted(T(label));
    char id[64]; snprintf(id, sizeof id, "##c%s", label);
    bool ch = ImGui::ColorEdit3(id, colour, ImGuiColorEditFlags_NoInputs);
    ImGui::SameLine(); ImGui::SetNextItemWidth(-1);
    snprintf(id, sizeof id, "##%s", label);
    ch |= ImGui::SliderFloat(id, amount, 0, 1, "%.2f");
    if (tip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", T(tip));
    return ch;
}
static void heading(const char* t) { ImGui::Spacing(); ImGui::PushStyleColor(ImGuiCol_Text, col4(C_BLUE)); ImGui::TextUnformatted(T(t)); ImGui::PopStyleColor(); ImGui::Separator(); }

static void facePanel() {
    heading("The face to become");
    if (ImGui::Button(T("Add face pictures..."), ImVec2(-1, 0))) { std::vector<std::string> many; if (!fileDialog(false, PICTURES, nullptr, true, &many).empty()) addSources(many); }
    for (size_t i = 0; i < sourcePics.size(); ++i) {
        if (i % 4) ImGui::SameLine();
        if (sourcePics[i].ready && !sourcePics[i].tex) upload(sourcePics[i].tex, sourcePics[i].thumb);
        if (sourcePics[i].tex) ImGui::Image((ImTextureID)(intptr_t)sourcePics[i].tex, ImVec2(64, 64));
        else ImGui::Button("...", ImVec2(64, 64));
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", base(sourcePics[i].path).c_str());
    }
    if (!sourcePics.empty() && ImGui::SmallButton(T("Clear"))) { sourcePics.clear(); source = Mimic::Source(); previewWanted = true; }
    if (sourcePics.empty()) ImGui::TextDisabled("%s", T("Several pictures of one person are averaged."));
}
static void imagoControls(bool live) {
    bool ch = false;
    heading("Swap");
    const auto& sm = Mimic::swapModels();
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##swapmodel", sm[settings.swapModel].name)) {
        for (int i = 0; i < (int)sm.size(); ++i) if (ImGui::Selectable(sm[i].name, i == settings.swapModel)) { settings.swapModel = i; ch = true; }
        ImGui::EndCombo();
    }
    float boost = (float)settings.boost;
    if (slider("Pixel boost", &boost, 1, 4, "%.0fx", "The face swapped at 2-4x its size, in several passes: sharper, slower")) { settings.boost = (int)std::lround(boost); ch = true; }
    ch |= slider("Identity weight", &settings.weight, 0, 1, "%.2f", "0: more of the face to become; 1: more of the person's own look");
    ch |= slider("Mask blur", &settings.maskBlur, 0, 1, "%.2f", "How soft the swapped face's edge is");
    // the style: Classic (FaceFusion's box and masks) or Deep-Live-Cam's look, as a two-way switch
    heading("Style");
    {
        const char* styles[] = {"Classic", "Deep-Live-Cam"};
        const float bw = (ImGui::GetContentRegionAvail().x - 4) / 2;
        for (int k = 0; k < 2; ++k) {
            if (k) ImGui::SameLine(0, 4);
            const bool on = settings.style == k;
            if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            if (ImGui::Button(T(styles[k]), ImVec2(bw, 0)) && !on) { settings.style = k; ch = true; }
            if (on) ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", k == 0 ? T("Classic: FaceFusion's box and masks") : T("Deep-Live-Cam's look: a soft oval, the colour matched to the skin, your own mouth if you like, a Poisson blend"));
        }
    }
    if (settings.style == 1) {
        ch |= ImGui::Checkbox(T("Match the colour"), &settings.dlcColour);
        ImGui::SameLine(); ch |= ImGui::Checkbox(T("Poisson blend"), &settings.dlcPoisson);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", T("Blended in by its gradients, so no seam shows. Slower."));
        ch |= ImGui::Checkbox(T("Keep my mouth"), &settings.dlcMouth);
        if (settings.dlcMouth) ch |= slider("Mouth reaches", &settings.dlcMouthSize, 0, 1, "%.2f", "0: just the lips; 1: down to the chin");
        ch |= slider("Opacity", &settings.dlcOpacity, 0, 1, "%.2f", "How much of the swapped face shows over yours");
        ch |= slider("Sharpness", &settings.dlcSharpness, 0, 1, "%.2f", "Sharpens the swapped face");
        ch |= slider("Smoothing", &settings.dlcSmooth, 0, 1, "%.2f", "Video and the webcam: each face blended with its last frame's, so it flickers less (more trails a little)");
    }
    heading("Restore");
    ch |= ImGui::Checkbox(T("Face enhancer"), &settings.enhance);
    if (settings.enhance) {
        const auto& em = Mimic::enhanceModels();
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##enh", em[settings.enhanceModel].name)) {
            for (int i = 0; i < (int)em.size(); ++i) if (ImGui::Selectable(em[i].name, i == settings.enhanceModel)) { settings.enhanceModel = i; ch = true; }
            ImGui::EndCombo();
        }
        ch |= slider("Enhancer blend", &settings.enhanceBlend, 0, 1, "%.2f", "How much of the restored face shows");
    }
    heading("Which faces");
    const char* modes[] = {T("Every face"), T("One face"), T("One person (click them)")};
    int mode = std::clamp(settings.mode, 0, 2);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::Combo("##mode", &mode, modes, 3)) { settings.mode = mode; ch = true; }
    float margin = (float)settings.detect.margin[0];
    if (slider("Detector margin", &margin, 0, 100, "%.0f%%", "Room round the picture: faces that fill it (close-ups) or touch its edge are found")) { for (int& m : settings.detect.margin) m = (int)margin; ch = true; }
    ch |= slider("Detector score", &settings.detect.score, 0.1f, 0.95f, "%.2f", "How sure it must be that it's a face");
    if (!live) { ch |= ImGui::Checkbox(T("Cosmeta's look too"), &cosmetaOnImago); if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", T("After the swap, the makeup set in Cosmeta (a chain)")); }
    if (ch) previewWanted = true;
}
static void cosmetaControls() {
    bool ch = false;
    heading("Lips");
    ch |= colourSlider("Lipstick", &look.lip, look.lipColour, "The lips' colour (their light and lines stay)");
    ch |= slider("Gloss", &look.gloss, 0, 1, "%.2f", "A shine on the lips' highlights");
    heading("Eyes");
    ch |= colourSlider("Eye shadow", &look.shadow, look.shadowColour, "On the lids, up towards the brows");
    ch |= slider("Eyeliner", &look.liner, 0, 1, "%.2f", "A dark line round the eyes");
    ch |= colourSlider("Brows", &look.brow, look.browColour, "Filled in and tinted");
    heading("Skin");
    ch |= colourSlider("Blush", &look.blush, look.blushColour, "On the cheeks");
    ch |= slider("Smooth", &look.smooth, 0, 1, "%.2f", "The skin smoothed (some texture kept)");
    ch |= slider("Glow", &look.glow, 0, 1, "%.2f", "The skin brightened softly");
    heading("Hair");
    ch |= colourSlider("Hair colour", &look.hair, look.hairColour, "Its light and strands stay");
    heading("Edges");
    ch |= slider("Feather", &look.feather, 0, 1, "%.2f", "How soft every edge is");
    if (ImGui::Button(T("No makeup"), ImVec2(-1, 0))) { const Cosmeta::Look d; look.lip = look.gloss = look.blush = look.shadow = look.liner = look.brow = look.hair = look.smooth = look.glow = 0; (void)d; ch = true; }
    if (ch) previewWanted = true;
}
static void realtimeControls() {
    heading("Webcam");
    if (cameras.empty() && ImGui::Button(T("Find cameras"), ImVec2(-1, 0))) cameras = Camera::list();
    if (!cameras.empty()) {
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##cam", cameras[std::min(camIndex, (int)cameras.size() - 1)].c_str())) {
            for (int i = 0; i < (int)cameras.size(); ++i) if (ImGui::Selectable(cameras[i].c_str(), i == camIndex)) camIndex = i;
            ImGui::EndCombo();
        }
        if (!cam.running()) {
            if (ImGui::Button(T("Start"), ImVec2(-1, 30))) { std::string err; if (!cam.open(cameras[camIndex], 1280, 720, err)) say(err); else { liveSequence = Mimic::beginSequence(); say(T("Live")); } }
        } else {
            if (ImGui::Button(T("Stop"), ImVec2(-1, 30))) { if (recording) { recorder.close(); recording = false; } cam.close(); Mimic::endSequence(liveSequence); liveSequence = 0; }
            ImGui::Text("%s %.0f fps (%s %.0f)", T("Showing"), liveFps, T("camera"), cam.fps());
        }
    }
    ImGui::Checkbox(T("Swap the face"), &liveSwap);
    if (liveSwap && !source.ready()) ImGui::TextDisabled("%s", T("Add a face to become above."));
    ImGui::Checkbox(T("Cosmeta's look"), &liveCosmeta);
    if (cam.running()) {
        if (!recording) {
            if (ImGui::Button(T("Record"), ImVec2(-1, 0))) {
                recordPath = fileDialog(true, "MP4 video|*.mp4|", "mp4");
                std::string err;
                if (!recordPath.empty()) { if (recorder.open(recordPath, 1280, 720, 30, "", 20, err)) recording = true; else say(err); }
            }
        } else if (ImGui::Button(T("Stop recording"), ImVec2(-1, 0))) { recorder.close(); recording = false; say(std::string(T("Saved ")) + base(recordPath)); }
    }
    ImGui::TextDisabled("%s", T("Tip: turn the face enhancer off for speed."));
}
static void leftPanel(ImVec2 pos, ImVec2 size) {
    ImGui::SetNextWindowPos(pos); ImGui::SetNextWindowSize(size);
    ImGui::Begin("##left", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);
    // the Changelings: a row of choices, the chosen one's controls under it
    for (int i = 0; i < M_COUNT; ++i) {
        const bool on = module == i;
        if (on) ImGui::PushStyleColor(ImGuiCol_Button, col4(C_BLUE));
        if (!MODULES[i].ready) ImGui::PushStyleColor(ImGuiCol_Text, col4(C_DIM));
        if (i % 2) ImGui::SameLine();
        if (ImGui::Button(T(MODULES[i].name), ImVec2((ImGui::GetContentRegionAvail().x - (i % 2 ? 0 : 6)) * (i % 2 ? 1.0f : 0.5f), 26))) { module = i; previewWanted = true; }
        if (!MODULES[i].ready) ImGui::PopStyleColor();
        if (on) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", T(MODULES[i].what));
    }
    ImGui::Separator();
    ImGui::BeginChild("##controls");
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("%s", T(MODULES[module].what));
    switch (module) {
    case M_IMAGO: facePanel(); imagoControls(false); break;
    case M_REALTIME: realtimeControls(); facePanel(); imagoControls(true); break;
    case M_COSMETA: cosmetaControls(); break;
    default:
        ImGui::Spacing();
        ImGui::TextWrapped("%s", T("Not in the C++ Studio yet:"));
        ImGui::TextWrapped("%s", T(MODULES[module].needs));
        ImGui::Spacing();
        if (ImGui::Button(T("Open the Python Studio"), ImVec2(-1, 0))) {
#ifdef _WIN32
            const fs::path bat = fs::u8path(exeDir()) / ".." / "studio.bat";
            ShellExecuteW(nullptr, L"open", (fs::exists(bat) ? bat : fs::path(L"https://github.com/CursedPrograms/Changeling-Studio")).wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#endif
        }
    }
    // what to do with it
    if (module != M_REALTIME && module < M_TIRESIAS) {
        heading("Result");
        if (ImGui::Button(T("Open a picture or video..."), ImVec2(-1, 0))) { const std::string f = fileDialog(false, TARGETS, nullptr); if (!f.empty()) loadTarget(f); }
        ImGui::BeginDisabled(!haveResult); if (ImGui::Button(T("Save the picture..."), ImVec2(-1, 0))) savePicture(); ImGui::EndDisabled();
        if (targetIsVideo) {
            if (progress < 0) { if (ImGui::Button(T("Render the video..."), ImVec2(-1, 0))) renderVideo(); }
            else { ImGui::ProgressBar(progress, ImVec2(-1, 0)); if (ImGui::Button(T("Stop"), ImVec2(-1, 0))) stopRender = true; }
        }
    }
    ImGui::PopTextWrapPos();
    ImGui::EndChild();
    ImGui::End();
}
static void canvas(ImVec2 pos, ImVec2 size) {
    ImGui::SetNextWindowPos(pos); ImGui::SetNextWindowSize(size);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, col4(C_BG));
    ImGui::Begin("##canvas", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const bool liveView = module == M_REALTIME;
    const Img& shown = liveView ? liveOut : original;
    if (shown.empty()) {
        const ImVec2 c(pos.x + size.x / 2, pos.y + size.y / 2);
        auto centred = [&](const char* t, float y, ImU32 col) { const ImVec2 ts = ImGui::CalcTextSize(t); dl->AddText(ImVec2(c.x - ts.x / 2, y), col, t); };
        if (liveView) centred(T("Pick a camera and press Start"), c.y - 10, C_DIM);
        else if (module >= M_TIRESIAS) centred(T(MODULES[module].needs), c.y - 10, C_DIM);
        else { centred(T("Drop a picture or a video here"), c.y - 20, C_TEXT); centred(T("Only use faces you have the right to use."), c.y + 8, C_DIM); }
        ImGui::End(); ImGui::PopStyleColor();
        return;
    }
    const float pad = 20, s = std::min((size.x - pad * 2) / shown.w, (size.y - pad * 2) / shown.h), dw = shown.w * s, dh = shown.h * s;
    const ImVec2 a(pos.x + (size.x - dw) / 2, pos.y + (size.y - dh) / 2 - (targetIsVideo && !liveView ? 14 : 0)), b(a.x + dw, a.y + dh);
    if (liveView) dl->AddImage((ImTextureID)(intptr_t)liveTex, a, b);
    else {
        const bool after = haveResult && resultTex;
        if (view == 2 || !after) dl->AddImage((ImTextureID)(intptr_t)originalTex, a, b);
        else if (view == 1) dl->AddImage((ImTextureID)(intptr_t)resultTex, a, b);
        else {
            const float x = a.x + dw * wipe;
            dl->AddImage((ImTextureID)(intptr_t)originalTex, a, ImVec2(x, b.y), ImVec2(0, 0), ImVec2(wipe, 1));
            dl->AddImage((ImTextureID)(intptr_t)resultTex, ImVec2(x, a.y), b, ImVec2(wipe, 0), ImVec2(1, 1));
            dl->AddLine(ImVec2(x, a.y), ImVec2(x, b.y), C_BLUE, 2.0f);
            dl->AddCircleFilled(ImVec2(x, a.y + dh / 2), 9, C_BLUE);
            auto label = [&](const char* t, float lx) {
                const ImVec2 ts = ImGui::CalcTextSize(t);
                dl->AddRectFilled(ImVec2(lx - 6, a.y + 6), ImVec2(lx + ts.x + 6, a.y + 10 + ts.y), IM_COL32(0x1a, 0x1a, 0x1a, 210), 2);
                dl->AddText(ImVec2(lx, a.y + 8), C_TEXT, t);
            };
            label(T("Before"), a.x + 14); label(T("After"), b.x - 14 - ImGui::CalcTextSize(T("After")).x);
        }
        ImGui::SetCursorScreenPos(a);
        ImGui::InvisibleButton("view", ImVec2(dw, dh));
        if (ImGui::IsItemActive() && view == 0) wipe = std::clamp((ImGui::GetIO().MousePos.x - a.x) / dw, 0.0f, 1.0f);
        if (targetIsVideo && videoInfo.seconds > 0) {   // the scrubber: which frame the preview shows
            ImGui::SetCursorScreenPos(ImVec2(a.x, b.y + 8)); ImGui::SetNextItemWidth(dw);
            if (ImGui::SliderFloat("##t", &videoAt, 0, (float)videoInfo.seconds, "%.1f s") && !busy) seekVideo(videoAt);
        }
    }
    if (busy && !liveView) {
        const float t = (float)ImGui::GetTime() * 6; const ImVec2 c(b.x - 22, b.y - 22);
        dl->AddCircleFilled(c, 16, IM_COL32(0x1a, 0x1a, 0x1a, 200));
        dl->PathArcTo(c, 10, t, t + 4.2f, 24); dl->PathStroke(C_BLUE, 0, 2.5f);
    }
    if (recording) { dl->AddCircleFilled(ImVec2(a.x + 20, a.y + 20), 7, IM_COL32(0xe0, 0x30, 0x30, 255)); dl->AddText(ImVec2(a.x + 32, a.y + 12), C_TEXT, "REC"); }
    ImGui::End(); ImGui::PopStyleColor();
}
static bool showAbout = false;
static void menuBar() {
    if (!ImGui::BeginMainMenuBar()) return;
    if (ImGui::BeginMenu(T("File"))) {
        if (ImGui::MenuItem(T("Open a picture or video..."), "Ctrl+O")) { const std::string f = fileDialog(false, TARGETS, nullptr); if (!f.empty()) loadTarget(f); }
        if (ImGui::MenuItem(T("Add face pictures..."))) { std::vector<std::string> many; if (!fileDialog(false, PICTURES, nullptr, true, &many).empty()) addSources(many); }
        if (ImGui::MenuItem(T("Save the picture..."), "Ctrl+S", false, haveResult)) savePicture();
        ImGui::Separator();
        if (ImGui::MenuItem(T("Quit"))) quit = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(T("View"))) {
        if (ImGui::MenuItem(T("Before | After"), "1", view == 0)) view = 0;
        if (ImGui::MenuItem(T("After"), "2", view == 1)) view = 1;
        if (ImGui::MenuItem(T("Before"), "3", view == 2)) view = 2;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(T("Language"))) {
        for (const auto& p : Lang::packs())
            if (ImGui::MenuItem(p.name.c_str(), nullptr, Lang::current() == p.code)) {
                Lang::load(exeDir() + "lang", p.code);
                std::ofstream(fs::u8path(prefsFile())) << "language=" << p.code << "\n";
                if (Lang::needsCJK()) say(T("Restart the Studio for this language's letters"));
            }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(T("Help"))) { if (ImGui::MenuItem(T("About Changeling Studio"))) showAbout = true; ImGui::EndMenu(); }
    std::string line; { std::lock_guard<std::mutex> g(mtx); line = status; }
    const float w = ImGui::CalcTextSize(line.c_str()).x;
    ImGui::SameLine(ImGui::GetWindowWidth() - w - 16); ImGui::TextDisabled("%s", line.c_str());
    ImGui::EndMainMenuBar();
}
static void about() {
    if (showAbout) { ImGui::OpenPopup("##about"); showAbout = false; }
    ImGui::SetNextWindowSize(ImVec2(Splash::W, Splash::H));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    if (ImGui::BeginPopupModal("##about", nullptr, ImGuiWindowFlags_NoDecoration)) {
        const ImVec2 p = ImGui::GetWindowPos();
        Splash::draw(ImGui::GetWindowDrawList(), p.x, p.y, nullptr, -1);
        if (ImGui::IsMouseClicked(0) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();
}
static void frame() {
    std::vector<std::function<void()>> todo;
    { std::lock_guard<std::mutex> g(mtx); todo.swap(onMain); }
    for (auto& f : todo) f();
    if (originalStale) { upload(originalTex, original); originalStale = false; }
    if (resultStale) { upload(resultTex, result); resultStale = false; }
    if (liveStale) { upload(liveTex, liveOut); liveStale = false; }
    if (previewWanted && !busy && progress < 0) {
        bool empty; { std::lock_guard<std::mutex> g(mtx); empty = jobs.empty(); }
        if (empty) { previewWanted = false; runPreview(); }
    }
    if (module == M_REALTIME) liveTick();
    ImGuiIO& io = ImGui::GetIO();
    if (!io.WantTextInput) {
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O)) { const std::string f = fileDialog(false, TARGETS, nullptr); if (!f.empty()) loadTarget(f); }
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)) savePicture();
        if (ImGui::IsKeyPressed(ImGuiKey_1)) view = 0;
        if (ImGui::IsKeyPressed(ImGuiKey_2)) view = 1;
        if (ImGui::IsKeyPressed(ImGuiKey_3)) view = 2;
    }
    menuBar();
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float side = 330, top = ImGui::GetFrameHeight();
    leftPanel(ImVec2(vp->Pos.x, vp->Pos.y + top), ImVec2(side, vp->Size.y - top));
    canvas(ImVec2(vp->Pos.x + side, vp->Pos.y + top), ImVec2(vp->Size.x - side, vp->Size.y - top));
    about();
}

int main(int argc, char** argv) {
    std::string file, capture, selftest; std::vector<std::string> sources;
    std::vector<std::string> args;
#ifdef _WIN32
    { int n; LPWSTR* wa = CommandLineToArgvW(GetCommandLineW(), &n); if (wa) { for (int i = 1; i < n; ++i) args.push_back(fs::path(wa[i]).u8string()); LocalFree(wa); } }
#else
    for (int i = 1; i < argc; ++i) args.push_back(argv[i]);
#endif
    (void)argc; (void)argv;
    for (size_t i = 0; i < args.size(); ++i) {
        const bool more = i + 1 < args.size();
        if (args[i] == "--capture" && more) capture = args[++i];
        else if (args[i] == "--source" && more) sources.push_back(args[++i]);
        else if (args[i] == "--module" && more) { const std::string m = args[++i]; for (int k = 0; k < M_COUNT; ++k) if (m == MODULES[k].id) module = k; }
        else if (args[i] == "--lips" && more) look.lip = (float)atof(args[++i].c_str());   // tests: a look
        else if (args[i] == "--hair" && more) look.hair = (float)atof(args[++i].c_str());
        else if (args[i] == "--makeup" && more) { const float v = (float)atof(args[++i].c_str()); look.lip = look.blush = look.shadow = look.liner = look.brow = v; look.smooth = v * 0.6f; }
        else if (args[i] == "--lang" && more) Lang::load(exeDir() + "lang", args[++i]);
        else if (args[i] == "--no-enhance") settings.enhance = false;
        else if (args[i] == "--dlc") { settings.style = 1; settings.dlcMouth = true; }   // tests: the Deep-Live-Cam style
        else if (args[i] == "--selftest" && more) selftest = args[++i];
        else file = args[i];
    }
    if (Lang::current() == "en") {   // the language chosen last time
        std::ifstream p(fs::u8path(prefsFile())); std::string line;
        while (std::getline(p, line)) if (line.rfind("language=", 0) == 0) Lang::load(exeDir() + "lang", line.substr(9));
    }
    settings.detect.model = DET_SCRFD;   // InsightFace's det_10g: on most PCs already (buffalo_l), and fast
    for (int& m : settings.detect.margin) m = 25;   // room round the frame: close-ups that fill the picture are found too
    if (!selftest.empty()) {   // tests, no window: each step of Cosmeta on a picture, said on stderr; the result beside it
        std::string e; Img im;
        if (!Ort::init(true, e)) { std::fprintf(stderr, "ort: %s\n", e.c_str()); return 1; }
        std::fprintf(stderr, "ort: %s\n", Ort::device().c_str());
        if (!loadPicture(selftest, im, e)) { std::fprintf(stderr, "load: %s\n", e.c_str()); return 1; }
        std::vector<Face> faces;
        const bool ok = findFaces(im, faces, e);
        std::fprintf(stderr, "faces: %d (%s) %s\n", (int)faces.size(), ok ? "ok" : "failed", e.c_str());
        if (!Cosmeta::apply(im, faces, look, e)) { std::fprintf(stderr, "cosmeta: %s\n", e.c_str()); return 1; }
        savePicture(selftest + ".cosmeta.png", im, e);
        std::fprintf(stderr, "saved\n");
        return 0;
    }
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) return 1;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3); SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE); SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    window = SDL_CreateWindow("Changeling Studio", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1500, 900, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_HIDDEN);
    if (!window) return 1;
    SDL_SetWindowMinimumSize(window, 1000, 640);
    SDL_GLContext gl = SDL_GL_CreateContext(window);
    SDL_GL_MakeCurrent(window, gl); SDL_GL_SetSwapInterval(1);
    SDL_Surface* icon = SDL_LoadBMP((exeDir() + "assets/icon.bmp").c_str());
    if (icon) SDL_SetWindowIcon(window, icon);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    loadFonts();
    applyStyle();
    // the splash, while the face models load
    SDL_Window* splashWin = nullptr;
    if (capture.empty()) {
        splashWin = SDL_CreateWindow("Changeling Studio", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, (int)Splash::W, (int)Splash::H, SDL_WINDOW_OPENGL | SDL_WINDOW_BORDERLESS);
        if (splashWin && SDL_GL_MakeCurrent(splashWin, gl) != 0) { SDL_DestroyWindow(splashWin); splashWin = nullptr; SDL_GL_MakeCurrent(window, gl); }
        if (splashWin && icon) SDL_SetWindowIcon(splashWin, icon);
    }
    if (icon) SDL_FreeSurface(icon);
    ImGui_ImplSDL2_InitForOpenGL(splashWin ? splashWin : window, gl);
    ImGui_ImplOpenGL3_Init("#version 330");
    SDL_EventState(SDL_DROPFILE, SDL_ENABLE);
    static std::atomic<bool> ready{false};
    worker = std::thread(workerLoop);
    queue([] {
        std::string e;
        if (!Ort::init(true, e)) { say(e); ready = true; return; }
        if (!Mimic::prepare(settings, e, say)) say(e);
        else { std::string pe; Ort::findModel(Cosmeta::parserModel(), say); say(std::string(T("Ready, on the ")) + Ort::device()); }
        ready = true;
    });
    if (splashWin) {
        const Uint32 start = SDL_GetTicks(); bool skip = false;
        for (;;) {
            SDL_Event e;
            while (SDL_PollEvent(&e)) { ImGui_ImplSDL2_ProcessEvent(&e); if (e.type == SDL_QUIT) quit = true; if (e.type == SDL_MOUSEBUTTONDOWN || (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE)) skip = true; }
            const Uint32 t = SDL_GetTicks() - start;
            if (skip || quit || (ready && t >= 1500) || t >= 30000) break;
            ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplSDL2_NewFrame(); ImGui::NewFrame();
            std::string line; { std::lock_guard<std::mutex> g(mtx); line = status; }
            Splash::draw(ImGui::GetBackgroundDrawList(), 0, 0, ready ? T("Ready") : line.c_str(), ready ? 1.0f : std::min(0.9f, t / 8000.0f + 0.05f));
            ImGui::Render();
            int sw, sh; SDL_GL_GetDrawableSize(splashWin, &sw, &sh);
            glViewport(0, 0, sw, sh); glClearColor(0.1f, 0.1f, 0.1f, 1); glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            SDL_GL_SwapWindow(splashWin);
        }
        ImGui_ImplSDL2_Shutdown();
        SDL_DestroyWindow(splashWin);
        SDL_GL_MakeCurrent(window, gl);
        ImGui_ImplSDL2_InitForOpenGL(window, gl);
    }
    if (capture.empty()) { SDL_ShowWindow(window); SDL_MaximizeWindow(window); SDL_RaiseWindow(window); }
    if (!sources.empty()) addSources(sources);
    if (!file.empty()) loadTarget(file);
    int frames = 0, idle = 0;
    while (!quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL2_ProcessEvent(&e);
            if (e.type == SDL_QUIT) quit = true;
            if (e.type == SDL_DROPFILE) {   // on the left: faces to become; anywhere else: the picture or video
                const std::string f = e.drop.file; SDL_free(e.drop.file);
                int mx, my; SDL_GetMouseState(&mx, &my);
                if (mx < 330 && module != M_COSMETA) addSources({f}); else loadTarget(f);
            }
        }
        ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplSDL2_NewFrame(); ImGui::NewFrame();
        frame();
        ImGui::Render();
        int w, h; SDL_GL_GetDrawableSize(window, &w, &h);
        glViewport(0, 0, w, h); glClearColor(0.1f, 0.1f, 0.1f, 1); glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        ++frames;
        if (!capture.empty()) {   // tests: a screenshot once everything's quiet
            bool quiet; { std::lock_guard<std::mutex> g(mtx); quiet = jobs.empty() && onMain.empty(); }
            idle = (quiet && !busy && !previewWanted && ready) ? idle + 1 : 0;
            if ((frames >= 8 && idle >= 10) || frames > 6000) {
                std::vector<unsigned char> px((size_t)w * h * 4), flip(px.size());
                glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
                for (int y = 0; y < h; ++y) memcpy(&flip[(size_t)y * w * 4], &px[(size_t)(h - 1 - y) * w * 4], (size_t)w * 4);
                SDL_Surface* s = SDL_CreateRGBSurfaceWithFormatFrom(flip.data(), w, h, 32, w * 4, SDL_PIXELFORMAT_ABGR8888);
                SDL_SaveBMP(s, capture.c_str()); SDL_FreeSurface(s);
                if (haveResult) { std::string err; savePicture(capture + ".result.png", result, err); }
                break;
            }
        }
        SDL_GL_SwapWindow(window);
    }
    if (recording) recorder.close();
    cam.close();
    stopRender = true; workerQuit = true;
    if (worker.joinable()) worker.join();
    ImGui_ImplOpenGL3_Shutdown(); ImGui_ImplSDL2_Shutdown(); ImGui::DestroyContext();
    SDL_GL_DeleteContext(gl); SDL_DestroyWindow(window); SDL_Quit();
    return 0;
}
