// Mimic - inswapper's emap (see emap.cpp)
#pragma once
#include <string>
#include <vector>

bool readEmap(const std::string& onnxPath, std::vector<float>& out, std::string& err);
