"""copy_models_to_F.py - every model the Changelings use, copied to F:\\AIModels (the user's one place for models),
grouped by what they are. The apps keep theirs where they are; this is the shared copy other apps (and a fresh
setup) take from instead of downloading again. Files already there (same size) are skipped.

    python copy_models_to_F.py
"""
import os
import shutil

STUDIO = os.path.dirname(os.path.abspath(__file__))
F = r"F:\AIModels"
COPIES = [   # (source, destination folder on F)
    ("Changeling-Tiresias/pretrained/e4e_ffhq_encode.pt", "stylegan2"),
    ("Changeling-Tiresias/pretrained/face_detection_yunet_2023mar.onnx", "face"),
    ("Changeling-Tiresias/pretrained/fairface_gender_int8.onnx", "face"),
    ("Changeling-Tiresias/pretrained/gender.npy", "stylegan2/directions"),
    ("Changeling-Chronos/pretrained/age.npy", "stylegan2/directions"),
    ("Changeling-Natio/pretrained/race_black.npy", "stylegan2/directions"),
    ("Changeling-Natio/pretrained/race_white.npy", "stylegan2/directions"),
    ("Changeling-Natio/pretrained/race_yellow.npy", "stylegan2/directions"),
    ("Changeling-Natio/pretrained/eyes_open.npy", "stylegan2/directions"),
    ("Changeling-Cosmeta/models/face_parsing_int8.onnx", "face"),
    ("Changeling-Cosmeta/models/dmt.onnx", "makeup"),
    ("Changeling-Cosmeta/models/face_parsing_bisenet.pth", "face"),
    ("Changeling-Simia/models/inswapper_128.onnx", "faceswap"),
    ("Changeling-Simia/models/GFPGANv1.4.pth", "faceswap"),
    ("Changeling-Simia-Realtime/models/xseg_1.onnx", "faceswap"),
    ("Changeling-Echo/Applio/rvc/models/pretraineds/hifi-gan/f0G40k.pth", "rvc"),
    ("Changeling-Echo/Applio/rvc/models/pretraineds/hifi-gan/f0D40k.pth", "rvc"),
    ("Changeling-Echo/Applio/rvc/models/predictors/rmvpe.pt", "rvc"),
    ("Changeling-Echo/Applio/rvc/models/embedders/contentvec/pytorch_model.bin", "rvc/contentvec"),
    ("Changeling-Echo/Applio/rvc/models/embedders/contentvec/config.json", "rvc/contentvec"),
    ("Changeling-Echo/voices/echo_female.pth", "rvc/voices"),
    ("Changeling-Echo/voices/echo_female.index", "rvc/voices"),
    ("Changeling-Echo/voices/echo_male.pth", "rvc/voices"),
    ("Changeling-Echo/voices/echo_male.index", "rvc/voices"),
]


def main():
    for src, dest in COPIES:
        src = os.path.join(STUDIO, src)
        if not os.path.exists(src):
            print("not here (yet):", os.path.relpath(src, STUDIO))
            continue
        out = os.path.join(F, dest, os.path.basename(src))
        if os.path.exists(out) and os.path.getsize(out) == os.path.getsize(src):
            continue
        os.makedirs(os.path.dirname(out), exist_ok=True)
        shutil.copy2(src, out)
        print(f"{os.path.getsize(src) / 2**20:7.1f} MB  {out}")


if __name__ == "__main__":
    main()
