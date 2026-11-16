// Mimic - pictures in and out (stb): PNG, JPG, BMP, TGA
#pragma once
#include "image.h"
#include <string>

bool loadPicture(const std::string& path, Img& im, std::string& err);
bool savePicture(const std::string& path, const Img& im, std::string& err, int jpegQuality = 95);
