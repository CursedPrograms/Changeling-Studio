// Mimic - ONNX Runtime, loaded at run time: the DirectML build (any DirectX 12 GPU: NVIDIA, AMD, Intel)
// when its DLLs are next to Mimic, else the CPU. Plus where the models are, and fetching the ones that aren't.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct OrtSession;

namespace Ort {

using Say = std::function<void(const std::string&)>;

bool init(bool gpu, std::string& err);   // once; gpu = try DirectML first
bool usingGpu();                          // DirectML took
std::string device();                     // "GPU (DirectML)" or "CPU"

// a model by FaceFusion's file name for it: next to Mimic (models/), F:\AIModels\<sub>\ (also under this PC's own
// names for it), ~/.insightface/models/buffalo_l, %APPDATA%\Atelier\models; fetched once from FaceFusion's model
// releases when it's nowhere
bool known(const std::string& file);      // in FaceFusion's model list
bool have(const std::string& file);       // on this PC already (no download needed)
std::string findModel(const std::string& file, Say say = nullptr);
OrtSession* open(const std::string& file, std::string& err, Say say = nullptr);   // cached by file name

struct Tensor {
    std::string name;
    std::vector<int64_t> shape;
    std::vector<float> f;     // float data
    std::vector<int64_t> i;   // int64 outputs (class ids)
    double d = 0;             // a double scalar (CodeFormer's weight), when isDouble
    bool isDouble = false;
};
// run with named inputs; outputs come back in the model's order (float)
bool run(OrtSession* s, std::vector<Tensor>& inputs, std::vector<Tensor>& outputs, std::string& err);
std::vector<std::string> inputNames(OrtSession* s);

}  // namespace Ort
