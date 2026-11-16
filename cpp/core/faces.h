// Mimic - finding faces and knowing about them, as FaceFusion does: a detector (RetinaFace, SCRFD, YOLO-Face, YuNet,
// or several at once) at a chosen size, turned 0/90/180/270 degrees for sideways faces, with a margin round the frame;
// 68 face points from a landmarker (2DFAN4 or PeppaWutz), or estimated from the detector's five (FAN 68/5); who they
// are (ArcFace); and their age, gender and race (FairFace), for the face selector.
#pragma once
#include "image.h"
#include "ort.h"
#include <string>
#include <vector>

enum DetectorModel { DET_RETINAFACE, DET_SCRFD, DET_YOLOFACE, DET_YUNET, DET_MANY, DET_COUNT };
enum LandmarkerModel { LM_2DFAN4, LM_PEPPA, LM_MANY, LM_COUNT };
enum Gender { G_MALE, G_FEMALE };
enum Race { R_WHITE, R_BLACK, R_LATINO, R_ASIAN, R_INDIAN, R_ARABIC, R_COUNT };

const char* detectorName(int m);
const char* landmarkerName(int m);
const char* raceName(int r);

struct DetectSettings {
    int model = DET_RETINAFACE;
    int size = 640;                    // 160, 320, 480, 512 or 640 (YOLO-Face and YuNet: 640)
    bool angles[4] = {true, false, false, false};   // 0, 90, 180, 270 degrees
    float score = 0.5f;
    int margin[4] = {0, 0, 0, 0};      // top, right, bottom, left: 0..100 (% of half the frame)
    int landmarker = LM_2DFAN4;
    float landmarkerScore = 0.5f;      // 0: never use the landmarker's points; above: use them when it's this sure
};

struct Face {
    float box[4] = {0, 0, 0, 0};       // x1, y1, x2, y2
    float score = 0;                   // the detector's
    float kps5[10] = {0};              // the detector's five points (eyes, nose, mouth corners)
    float lm68[136] = {0};             // 68 points: the landmarker's, or estimated from the five
    float lm68from5[136] = {0};        // 68 points estimated from the five (FAN 68/5)
    float kps[10] = {0};               // the five the swap and restorers use ("5/68": from the 68 when the landmarker is sure)
    float landmarkerScore = 0;
    int angle = 0;                     // 0, 90, 180 or 270: how the face is turned
    std::vector<float> emb, embNorm;   // ArcFace's 512 numbers, raw and unit length
    int gender = -1, ageLo = 0, ageHi = 100, race = -1;   // FairFace (-1: not known)
    float area() const { return (box[2] - box[0]) * (box[3] - box[1]); }
};

namespace Faces {
// every face in the picture, analysed (points, identity; classify: age/gender/race too)
bool analyse(const Img& im, const DetectSettings& st, std::vector<Face>& out, std::string& err, bool classify, Ort::Say say = nullptr);
// the default settings, identity included (the source pictures, the window's preview)
bool detect(const Img& im, std::vector<Face>& out, std::string& err, float minScore = 0.5f, Ort::Say say = nullptr);
bool embed(const Img& im, Face& f, std::string& err, Ort::Say say = nullptr);
bool classifyFace(const Img& im, Face& f, std::string& err);
// FaceFusion's face distance: 0 the same person .. 1 strangers
float distance(const Face& a, const Face& b);
}
