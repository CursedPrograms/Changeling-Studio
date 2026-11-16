// Mimic - video through ffmpeg (see video.h)
#include "video.h"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#define popenW(c, m) _wpopen(c, m)
#define pcloseW _pclose
#endif

namespace fs = std::filesystem;

namespace Video {

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

std::string ffmpeg() {
    static std::string found;
    static bool looked = false;
    if (looked) return found;
    looked = true;
    std::error_code ec;
#ifdef _WIN32
    const char* name = "ffmpeg.exe";
    std::vector<fs::path> spots = {fs::u8path(exeDir()) / name, fs::u8path(exeDir()) / "ffmpeg" / "bin" / name, "C:/ffmpeg/bin/ffmpeg.exe"};
    wchar_t buf[MAX_PATH];
    if (SearchPathW(nullptr, L"ffmpeg.exe", nullptr, MAX_PATH, buf, nullptr)) spots.insert(spots.begin() + 2, fs::path(buf));
    if (const char* la = std::getenv("LOCALAPPDATA")) {   // winget's Gyan.FFmpeg
        const fs::path pk = fs::u8path(la) / "Microsoft" / "WinGet" / "Packages";
        if (fs::exists(pk, ec))
            for (const auto& d : fs::directory_iterator(pk, ec))
                if (d.path().filename().u8string().rfind("Gyan.FFmpeg", 0) == 0)
                    for (const auto& b : fs::directory_iterator(d.path(), ec)) spots.push_back(b.path() / "bin" / name);
    }
#else
    std::vector<fs::path> spots = {fs::u8path(exeDir()) / "ffmpeg", "/usr/bin/ffmpeg", "/usr/local/bin/ffmpeg"};
#endif
    for (const auto& p : spots) if (fs::exists(p, ec)) { found = p.u8string(); break; }
    return found;
}

static std::string ffprobe() {
    const std::string f = ffmpeg();
    if (f.empty()) return "";
    fs::path p = fs::u8path(f).parent_path() / fs::u8path(f).filename().u8string().replace(0, 6, "ffprobe");
    std::error_code ec;
    return fs::exists(p, ec) ? p.u8string() : "";
}

bool isVideo(const std::string& path) {
    std::string e = fs::u8path(path).extension().u8string();
    for (auto& c : e) c = (char)tolower((unsigned char)c);
    for (const char* v : {".mp4", ".mov", ".mkv", ".webm", ".avi", ".m4v", ".gif", ".wmv"}) if (e == v) return true;
    return false;
}

static std::wstring q(const std::string& s) { return L"\"" + fs::u8path(s).wstring() + L"\""; }

#ifdef _WIN32
// _wpopen runs through cmd /c, which strips the outer quotes: the whole line goes in one more pair
static FILE* run(const std::wstring& cmd, const wchar_t* mode) { return popenW((L"\"" + cmd + L"\"").c_str(), mode); }
#endif

bool probe(const std::string& path, Info& info, std::string& err) {
    const std::string fp = ffprobe();
    if (fp.empty()) { err = "ffprobe (with ffmpeg) isn't on this PC"; return false; }
    FILE* p = run(q(fp) + L" -v error -select_streams v:0 -show_entries stream=width,height,r_frame_rate,nb_frames:format=duration -of default=nw=1 " + q(path), L"r");
    if (!p) { err = "Can't run ffprobe"; return false; }
    char line[512];
    while (fgets(line, sizeof line, p)) {
        std::string l(line);
        while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
        const size_t eq = l.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = l.substr(0, eq), v = l.substr(eq + 1);
        if (k == "width") info.w = atoi(v.c_str());
        else if (k == "height") info.h = atoi(v.c_str());
        else if (k == "nb_frames") info.frames = atol(v.c_str());
        else if (k == "duration") info.seconds = atof(v.c_str());
        else if (k == "r_frame_rate") { const size_t sl = v.find('/'); const double a = atof(v.c_str()), b = sl == std::string::npos ? 1 : atof(v.c_str() + sl + 1); info.fps = b > 0 ? a / b : 0; }
    }
    pcloseW(p);
    if (info.w <= 0 || info.h <= 0) { err = "Not a video ffmpeg can read: " + path; return false; }
    if (info.fps <= 0) info.fps = 25;
    if (info.frames <= 0 && info.seconds > 0) info.frames = (long)(info.seconds * info.fps + 0.5);
    return true;
}

bool Reader::open(const std::string& path, std::string& err, double start) {
    close();
    if (!probe(path, info, err)) return false;
    wchar_t ss[64] = L"";
    if (start > 0) swprintf(ss, 64, L" -ss %.3f", start);
    pipe = run(q(ffmpeg()) + L" -v error" + ss + L" -i " + q(path) + L" -f rawvideo -pix_fmt rgb24 -", L"rb");
    if (!pipe) { err = "Can't run ffmpeg"; return false; }
    return true;
}

bool Reader::next(Img& frame) {
    if (!pipe) return false;
    if (frame.w != info.w || frame.h != info.h) frame = Img(info.w, info.h);
    return fread(frame.px.data(), 1, frame.px.size(), pipe) == frame.px.size();
}

void Reader::close() { if (pipe) { pcloseW(pipe); pipe = nullptr; } }

const std::vector<std::string>& videoEncoders() {
    static const std::vector<std::string> v = {"libx264", "libx265", "libvpx-vp9", "h264_nvenc", "hevc_nvenc", "h264_amf", "hevc_amf", "h264_qsv", "hevc_qsv"};
    return v;
}
const std::vector<std::string>& presets() {
    static const std::vector<std::string> v = {"ultrafast", "superfast", "veryfast", "faster", "fast", "medium", "slow", "slower", "veryslow"};
    return v;
}
const std::vector<std::string>& audioEncoders() {
    static const std::vector<std::string> v = {"aac", "libmp3lame", "libopus", "libvorbis", "flac", "copy"};
    return v;
}


// FaceFusion's quality (0..100) to each encoder's own scale, and its preset names to NVENC/AMF/QSV ones
static std::wstring videoArgs(const Output& o) {
    const std::string& e = o.videoEncoder;
    const int q = std::clamp(o.videoQuality, 0, 100);
    wchar_t b[256];
    auto pick = [&](const char* a, const char* m, const char* z) {   // fast / medium / slow buckets
        const auto& p = presets();
        const int i = (int)(std::find(p.begin(), p.end(), o.preset) - p.begin());
        return i <= 2 ? a : i <= 5 ? m : z;
    };
    if (e == "libx264" || e == "libx265") swprintf(b, 256, L" -c:v %hs -crf %d -preset %hs", e.c_str(), (int)std::lround(51 - q * 0.51), o.preset.c_str());
    else if (e == "libvpx-vp9") swprintf(b, 256, L" -c:v libvpx-vp9 -crf %d -b:v 0", (int)std::lround(63 - q * 0.63));
    else if (e == "h264_nvenc" || e == "hevc_nvenc") swprintf(b, 256, L" -c:v %hs -cq %d -preset %hs", e.c_str(), (int)std::lround(51 - q * 0.51), pick("p1", "p4", "p7"));
    else if (e == "h264_amf" || e == "hevc_amf") swprintf(b, 256, L" -c:v %hs -qp_i %d -qp_p %d -quality %hs", e.c_str(), (int)std::lround(51 - q * 0.51), (int)std::lround(51 - q * 0.51), pick("speed", "balanced", "quality"));
    else swprintf(b, 256, L" -c:v %hs -global_quality %d -preset %hs", e.c_str(), (int)std::lround(51 - q * 0.51), pick("veryfast", "medium", "veryslow"));
    return b;
}
static std::wstring audioArgs(const Output& o) {
    if (o.audioEncoder == "copy" && o.audioVolume == 100) return L" -c:a copy";
    const int q = std::clamp(o.audioQuality, 0, 100);
    wchar_t b[200];
    const std::string e = o.audioEncoder == "copy" ? "aac" : o.audioEncoder;
    if (e == "flac") swprintf(b, 200, L" -c:a flac");
    else if (e == "libvorbis") swprintf(b, 200, L" -c:a libvorbis -q:a %.1f", q / 10.0);
    else swprintf(b, 200, L" -c:a %hs -b:a %dk", e.c_str(), 32 + q * 288 / 100);
    std::wstring r = b;
    if (o.audioVolume != 100) { swprintf(b, 200, L" -af volume=%.2f", o.audioVolume / 100.0); r += b; }
    return r;
}

bool Writer::open(const std::string& out, int w_, int h_, double fps, const std::string& audioFrom, const Output& o, std::string& err) {
    close();
    w = w_; h = h_;
    wchar_t head[256];
    swprintf(head, 256, L" -v error -y -f rawvideo -pix_fmt rgb24 -s %dx%d -r %.6f -i -", w, h, fps);
    // the sound, cut to the same frames
    std::wstring audioIn = L" -i " + q(audioFrom);
    if (o.trimStart > 0 || o.trimEnd > 0) {
        wchar_t t[128];
        const double srcFps = fps > 0 ? fps : 25;
        swprintf(t, 128, L" -ss %.4f", o.trimStart / srcFps);
        std::wstring tt = t;
        if (o.trimEnd > o.trimStart) { swprintf(t, 128, L" -to %.4f", o.trimEnd / srcFps); tt += t; }
        audioIn = tt + audioIn;
    }
    std::wstring rate;
    if (o.fps > 0) { wchar_t r[48]; swprintf(r, 48, L" -r %.4f", o.fps); rate = r; }
    // H.264/H.265 need even sizes: the odd pixel is cropped off
    const std::wstring body = L" -map 0:v:0 -map 1:a? -vf crop=trunc(iw/2)*2:trunc(ih/2)*2" + videoArgs(o) + L" -pix_fmt yuv420p" + rate + audioArgs(o) + L" -shortest ";
    pipe = run(q(ffmpeg()) + head + audioIn + body + q(out), L"wb");
    if (!pipe) { err = "Can't run ffmpeg to write " + out; return false; }
    return true;
}

bool Writer::open(const std::string& out, int w_, int h_, double fps, const std::string& audioFrom, int crf, std::string& err) {
    Output o;
    o.videoQuality = std::clamp((int)std::lround((51 - crf) / 0.51), 0, 100);
    o.preset = "medium";
    o.audioEncoder = "copy";
    return open(out, w_, h_, fps, audioFrom, o, err);
}

bool Writer::write(const Img& frame) { return pipe && fwrite(frame.px.data(), 1, frame.px.size(), pipe) == frame.px.size(); }

bool Writer::close() {
    if (!pipe) return true;
    const int rc = pcloseW(pipe);
    pipe = nullptr;
    return rc == 0;
}

bool frameAt(const std::string& path, double seconds, Img& frame, std::string& err) {
    Reader r;
    if (!r.open(path, err, seconds)) return false;
    const bool got = r.next(frame);
    r.close();
    if (!got) err = "No frame there";
    return got;
}

}  // namespace Video
