// Changeling Studio - the webcam for the realtime Changelings, through ffmpeg (DirectShow on Windows, V4L2 on Linux):
// raw RGB frames on their own thread, the newest one kept. No extra libraries: the same ffmpeg the videos use.
#pragma once
#include "image.h"

#include <atomic>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace Camera {

std::vector<std::string> list();   // the cameras on this PC, by name

class Stream {
public:
    bool open(const std::string& name, int w, int h, std::string& err);   // w x h: asked for (it's scaled to it)
    bool latest(Img& out, long long& seen);   // the newest frame, when there's one newer than seen
    void close();
    bool running() const { return run_; }
    double fps() const { return fps_; }
    ~Stream() { close(); }
private:
    FILE* pipe_ = nullptr;
    std::thread th_;
    std::mutex mu_;
    Img frame_;
    long long count_ = 0;
    std::atomic<bool> run_{false};
    std::atomic<double> fps_{0};
    int w_ = 0, h_ = 0;
};

}
