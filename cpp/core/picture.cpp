// Mimic - pictures in and out (see picture.h)
#include "picture.h"
#ifdef _WIN32
#define STBIW_WINDOWS_UTF8
#endif
#include "stb_image.h"
#include "stb_image_write.h"
#include <cctype>
#include <cstring>

bool loadPicture(const std::string& path, Img& im, std::string& err) {
    int w, h, n;
    uint8_t* px = stbi_load(path.c_str(), &w, &h, &n, 3);
    if (!px) { err = "Can't open " + path + " (" + stbi_failure_reason() + ")"; return false; }
    im = Img(w, h);
    memcpy(im.px.data(), px, im.px.size());
    stbi_image_free(px);
    return true;
}

bool savePicture(const std::string& path, const Img& im, std::string& err, int q) {
    const size_t dot = path.find_last_of('.');
    std::string ext = dot == std::string::npos ? "" : path.substr(dot + 1);
    for (auto& c : ext) c = (char)tolower((unsigned char)c);
    const bool ok = ext == "png" ? stbi_write_png(path.c_str(), im.w, im.h, 3, im.px.data(), im.w * 3) != 0
                                 : stbi_write_jpg(path.c_str(), im.w, im.h, 3, im.px.data(), q) != 0;
    if (!ok) err = "Can't write " + path;
    return ok;
}
