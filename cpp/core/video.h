// Mimic - video through ffmpeg: frames come in as raw RGB from one ffmpeg, go out to another, which
// encodes them (H.264) and copies the original's sound across.
#pragma once
#include "image.h"
#include <cstdio>
#include <string>
#include <vector>

namespace Video {

std::string ffmpeg();    // its path ("" when it isn't on this PC): next to Mimic, PATH, C:\ffmpeg\bin, winget's
bool isVideo(const std::string& path);

struct Info { int w = 0, h = 0; double fps = 0; long frames = 0; double seconds = 0; };
bool probe(const std::string& path, Info& info, std::string& err);

// one frame at a time
struct Reader {
    FILE* pipe = nullptr; Info info;
    bool open(const std::string& path, std::string& err, double startSeconds = 0);
    bool next(Img& frame);
    void close();
    ~Reader() { close(); }
};
// FaceFusion's output settings
struct Output {
    std::string videoEncoder = "libx264";   // libx264, libx265, libvpx-vp9, h264_nvenc, hevc_nvenc, h264_amf, hevc_amf, h264_qsv, hevc_qsv
    std::string preset = "veryfast";        // ultrafast .. veryslow
    int videoQuality = 80;                  // 0..100
    double fps = 0;                         // 0: the target's
    std::string audioEncoder = "aac";       // aac, libmp3lame, libopus, libvorbis, flac, copy
    int audioQuality = 80;                  // 0..100
    int audioVolume = 100;                  // 0..100 (%)
    long trimStart = 0, trimEnd = 0;        // frames; end 0: to the end
};
const std::vector<std::string>& videoEncoders();
const std::vector<std::string>& presets();
const std::vector<std::string>& audioEncoders();

struct Writer {
    FILE* pipe = nullptr; int w = 0, h = 0;
    // the sound comes from audioFrom (trimmed the same way)
    bool open(const std::string& out, int w, int h, double fps, const std::string& audioFrom, const Output& o, std::string& err);
    bool open(const std::string& out, int w, int h, double fps, const std::string& audioFrom, int crf, std::string& err);
    bool write(const Img& frame);
    bool close();   // waits for ffmpeg to finish the file
    ~Writer() { close(); }
};

// one frame at a time in seconds (the preview's scrubber)
bool frameAt(const std::string& path, double seconds, Img& frame, std::string& err);

}  // namespace Video
