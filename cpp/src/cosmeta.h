// Changeling Studio - Cosmeta in C++: makeup and hair colour from the face parser (BiSeNet, 19 classes: skin, brows,
// eyes, lips, hair...). Each face is aligned, zoomed out so the whole head fits, parsed, and each look is laid on its
// own region, keeping the skin's light and texture; then put back. Every amount is a slider (0 = untouched).
#pragma once
#include "faces.h"

#include <string>
#include <vector>

namespace Cosmeta {

struct Look {
    float lip = 0, lipColour[3] = {0.72f, 0.10f, 0.24f}, gloss = 0;          // lipstick, and a shine on it
    float blush = 0, blushColour[3] = {0.93f, 0.42f, 0.48f};
    float shadow = 0, shadowColour[3] = {0.42f, 0.24f, 0.52f};             // eye shadow, on the lids up to the brows
    float liner = 0;                                                       // eyeliner: dark, round the eyes
    float brow = 0, browColour[3] = {0.20f, 0.13f, 0.08f};                // brows filled in and tinted
    float hair = 0, hairColour[3] = {0.55f, 0.13f, 0.10f};                // hair colour (keeps its light and strands)
    float smooth = 0;                                                      // the skin smoothed (its pores kept a little)
    float glow = 0;                                                        // the skin brightened, a soft glow
    float feather = 0.5f;                                                  // how soft every edge is
    bool any() const { return lip > 0 || gloss > 0 || blush > 0 || shadow > 0 || liner > 0 || brow > 0 || hair > 0 || smooth > 0 || glow > 0; }
};

// the look on every face in the picture (faces from Faces::detect / analyse)
bool apply(Img& im, const std::vector<Face>& faces, const Look& look, std::string& err);
const char* parserModel();   // the face parser it uses (Ort::findModel fetches it once)

}
