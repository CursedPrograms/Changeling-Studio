// Mimic - FaceFusion's face processors in C++: the face swapper (15 models), the face enhancer (9 models) and the
// face debugger, with FaceFusion's face selector (many / one / reference; order; age, gender and race filters) and
// face masker (box, occlusion, area and region masks).
#pragma once
#include "faces.h"
#include <string>
#include <vector>

namespace Mimic {

// ---- the face swapper's models ----
enum SwapType { ST_INSWAPPER, ST_HYPERSWAP, ST_SIMSWAP, ST_GHOST, ST_HIFIFACE, ST_BLENDSWAP, ST_UNIFACE };
struct SwapModel {
    const char* name; const char* file; int type; Template tpl; int size;
    float mean[3], sd[3];
    const char* converter;   // the identity converter (Ghost, HiFiFace, SimSwap)
    const char* about;
};
const std::vector<SwapModel>& swapModels();

// ---- the face enhancer's models ----
struct EnhanceModel { const char* name; const char* file; Template tpl; int size; const char* about; };
const std::vector<EnhanceModel>& enhanceModels();

enum SelectorMode { SEL_MANY, SEL_ONE, SEL_REFERENCE, SEL_COUNT };
enum Order { ORD_LEFT_RIGHT, ORD_RIGHT_LEFT, ORD_TOP_BOTTOM, ORD_BOTTOM_TOP, ORD_SMALL_LARGE, ORD_LARGE_SMALL, ORD_BEST_WORST, ORD_WORST_BEST, ORD_COUNT };
enum Occluder { OCC_XSEG1, OCC_XSEG2, OCC_XSEG3, OCC_MANY, OCC_COUNT };
enum Region { RG_SKIN, RG_LEFT_EYEBROW, RG_RIGHT_EYEBROW, RG_LEFT_EYE, RG_RIGHT_EYE, RG_GLASSES, RG_NOSE, RG_MOUTH, RG_UPPER_LIP, RG_LOWER_LIP, RG_COUNT };
enum Area { AR_UPPER, AR_LOWER, AR_MOUTH, AR_COUNT };
enum Debug { DB_BOX, DB_5, DB_5_68, DB_68, DB_68_5, DB_MASK, DB_COUNT };

const char* modeName(int m);
const char* orderName(int o);
const char* occluderName(int o);
const char* regionName(int r);
const char* areaName(int a);
const char* debugName(int d);

// ---- the face editor (LivePortrait): FaceFusion's 14 dials, each -1..1 ----
enum Edit { ED_EYEBROW, ED_GAZE_H, ED_GAZE_V, ED_EYE_OPEN, ED_LIP_OPEN, ED_GRIM, ED_POUT, ED_PURSE, ED_SMILE,
            ED_MOUTH_H, ED_MOUTH_V, ED_PITCH, ED_YAW, ED_ROLL, ED_COUNT };
const char* editName(int e);       // "Smile", "Eyes open", ...
const char* editHint(int e);       // what -1 and +1 do
// a prompt ("a big smile, eyes closed, looking left") onto the dials; returns what it understood ("" = nothing)
std::string promptToEdits(const std::string& prompt, float* edits);

struct Settings {
    DetectSettings detect;
    // the face selector
    int mode = SEL_MANY;
    int order = ORD_LARGE_SMALL;
    int gender = -2;               // -2 any, -1 like the source's, else G_MALE / G_FEMALE
    int race = -2;                 // -2 any, -1 like the source's, else a Race
    int ageStart = 0, ageEnd = 100;
    float referenceDistance = 0.3f;
    // the face masker
    bool maskBox = true, maskOcclusion = false, maskArea = false, maskRegion = false;
    int occluder = OCC_XSEG1;
    int parser = 1;                // 0 bisenet_resnet_18, 1 bisenet_resnet_34
    bool areas[AR_COUNT] = {true, true, true};
    bool regions[RG_COUNT] = {true, true, true, true, true, true, true, true, true, true};
    float maskBlur = 0.3f;
    int padding[4] = {0, 0, 0, 0};
    // the swap's style: 0 Classic (FaceFusion's: the box and the masks above), 1 Deep-Live-Cam's look: a soft oval
    // instead of the box, the swapped face's colour matched to the skin round it, your own mouth kept if you like,
    // a Poisson blend into the picture, sharpness and opacity (after hacksider/Deep-Live-Cam's ideas, written anew)
    int style = 0;
    bool dlcColour = true;         // the swapped face's colour and light matched to the face it replaces
    bool dlcPoisson = false;       // blended in by its gradients (no seam), slower
    bool dlcMouth = false;         // your own mouth kept (talking, teeth, tongue look like you)
    float dlcMouthSize = 0.3f;     // how far the kept mouth reaches: 0 the lips, 1 down to the chin
    float dlcOpacity = 1.0f;       // the swapped face over yours: 0 none, 1 all
    float dlcSharpness = 0.0f;     // 0 none .. 1 strong
    float dlcSmooth = 0.3f;        // video and the webcam: each face blended with its last frame's (steadier, less
                                   // flicker; 0 none .. 1 most). Only within a sequence (below)
    int sequence = 0;              // the frames of one video or one webcam session (beginSequence); 0: a still picture
    // the processors, in FaceFusion's order
    bool swap = true;
    int swapModel = 0;             // index into swapModels()
    int boost = 1;                 // pixel boost: the crop is boost x the model's size
    float weight = 0.5f;           // 0 more of the source, 1 more of the target's own identity
    bool enhance = true;
    int enhanceModel = 2;          // index into enhanceModels() (GFPGAN 1.4)
    float enhanceBlend = 0.8f;
    float enhanceWeight = 0.5f;    // CodeFormer's fidelity
    bool edit = false;             // the face editor
    float edits[ED_COUNT] = {0};
    bool debug = false;
    bool debugItems[DB_COUNT] = {false, false, true, false, false, true};
};

// the person to become: their identity, averaged over every source picture (the biggest face in each)
struct Source {
    std::vector<Face> faces;
    std::vector<Img> pictures;     // kept for BlendSwap and UniFace (they take the source's picture)
    std::vector<float> emb, embNorm;
    bool ready() const { return emb.size() == 512; }
};
bool addSource(Source& src, const Img& picture, std::string& err, Ort::Say say = nullptr);
void finishSource(Source& src);

struct Reference { Face face; bool set = false; };

// every face the settings pick in this frame (analysed: points, identity, age/gender/race when the filters need it)
bool pickFaces(const Img& frame, const Source& src, const Settings& st, const Reference& ref, std::vector<Face>& picked, std::vector<Face>* all, std::string& err);
// a video or a webcam session: its frames may be smoothed one into the next (Settings::sequence = the id); end it after
int beginSequence();
void endSequence(int id);
// the processors on a picture or frame, in place; returns how many faces were done (-1 on error)
int processFrame(Img& frame, const Source& src, const Settings& st, const Reference& ref, std::string& err, Ort::Say say = nullptr);
inline int swapFrame(Img& frame, const Source& src, const Settings& st, const Reference& ref, std::string& err, Ort::Say say = nullptr) { return processFrame(frame, src, st, ref, err, say); }
// load (and fetch, once) what these settings need
bool prepare(const Settings& st, std::string& err, Ort::Say say = nullptr);
// the models these settings use (the splash counts them as they load)
std::vector<std::string> modelsFor(const Settings& st);
// the models these settings need that aren't on this PC yet
std::vector<std::string> missingModels(const Settings& st);

}  // namespace Mimic
