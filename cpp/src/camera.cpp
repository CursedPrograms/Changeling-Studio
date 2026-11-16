// Changeling Studio - the webcam: see camera.h.
#include "camera.h"
#include "video.h"

#include <chrono>
#include <cstring>

#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#endif

namespace Camera {

std::vector<std::string> list() {
    std::vector<std::string> out;
    const std::string ff = Video::ffmpeg();
    if (ff.empty()) return out;
#ifdef _WIN32
    // ffmpeg names DirectShow's devices on stderr: [dshow] "Name" (video)
    const std::string cmd = "\"\"" + ff + "\" -hide_banner -list_devices true -f dshow -i dummy 2>&1\"";
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) return out;
    char line[1024];
    while (fgets(line, sizeof line, p)) {
        const std::string s = line;
        if (s.find("(video)") == std::string::npos) continue;
        const size_t a = s.find('"'), b = s.find('"', a + 1);
        if (a != std::string::npos && b != std::string::npos) out.push_back(s.substr(a + 1, b - a - 1));
    }
    pclose(p);
#else
    for (int i = 0; i < 10; ++i) { const std::string d = "/dev/video" + std::to_string(i); if (FILE* f = fopen(d.c_str(), "rb")) { fclose(f); out.push_back(d); } }
#endif
    return out;
}

bool Stream::open(const std::string& name, int w, int h, std::string& err) {
    close();
    const std::string ff = Video::ffmpeg();
    if (ff.empty()) { err = "ffmpeg isn't on this PC (the webcam comes through it)"; return false; }
    w_ = w; h_ = h;
#ifdef _WIN32
    const std::string in = "-f dshow -rtbufsize 64M -i video=\"" + name + "\"";
    const std::string cmd = "\"\"" + ff + "\" -hide_banner -loglevel error " + in + " -vf scale=" + std::to_string(w) + ":" + std::to_string(h) +
                            ":force_original_aspect_ratio=increase,crop=" + std::to_string(w) + ":" + std::to_string(h) + " -f rawvideo -pix_fmt rgb24 - 2>NUL\"";
    pipe_ = popen(cmd.c_str(), "rb");
#else
    const std::string cmd = "\"" + ff + "\" -hide_banner -loglevel error -f v4l2 -i \"" + name + "\" -vf scale=" + std::to_string(w) + ":" + std::to_string(h) +
                            ":force_original_aspect_ratio=increase,crop=" + std::to_string(w) + ":" + std::to_string(h) + " -f rawvideo -pix_fmt rgb24 - 2>/dev/null";
    pipe_ = popen(cmd.c_str(), "r");
#endif
    if (!pipe_) { err = "The webcam didn't start"; return false; }
    run_ = true;
    th_ = std::thread([this] {
        Img f(w_, h_);
        const size_t n = f.px.size();
        auto t0 = std::chrono::steady_clock::now(); int frames = 0;
        while (run_) {
            if (fread(f.px.data(), 1, n, pipe_) != n) { run_ = false; break; }
            { std::lock_guard<std::mutex> g(mu_); frame_ = f; ++count_; }
            ++frames;
            const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            if (s >= 1) { fps_ = frames / s; frames = 0; t0 = std::chrono::steady_clock::now(); }
        }
    });
    // the first frame within a few seconds, or it didn't work (another program has the camera, or it's not there)
    for (int i = 0; i < 60; ++i) {
        { std::lock_guard<std::mutex> g(mu_); if (count_ > 0) return true; }
        if (!run_) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    close();
    err = "No picture from " + name + " (is another program using it?)";
    return false;
}

bool Stream::latest(Img& out, long long& seen) {
    std::lock_guard<std::mutex> g(mu_);
    if (count_ == seen || frame_.empty()) return false;
    out = frame_; seen = count_;
    return true;
}

void Stream::close() {
    run_ = false;
    if (pipe_) { pclose(pipe_); pipe_ = nullptr; }   // ends ffmpeg; the thread's read returns
    if (th_.joinable()) th_.join();
    fps_ = 0;
}

}
