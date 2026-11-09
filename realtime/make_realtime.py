"""make_realtime.py - write the Changeling *-Realtime repos from this folder (live.py is the one master copy).

    python realtime/make_realtime.py            all three: Tiresias, Chronos, Natio
    python realtime/make_realtime.py Chronos    one

Each repo gets: live.py, realtime.json, the run scripts (.bat, .ps1, .sh), the README and the site page, the logo,
theme, license, FUNDING.yml, link.bf and a QR code to its site. Re-run it after changing live.py, then commit
and push each repo. Files the repo has that aren't written here (assets/ screenshots and samples) are kept.
"""
import json
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
STUDIO = os.path.dirname(HERE)
OWNER = "CursedPrograms"

APPS = {
    "Tiresias": dict(
        what="the other gender",
        tagline="Be the other gender, live on a video call",
        lede="One snapshot of you, swapped to the other gender by Changeling Tiresias, then worn live by Changeling "
             "CopyCat Realtime: your own expressions, the other you's face, out to the call through OBS Virtual "
             "Camera. The voice can follow (female or male, or Changeling Echo's trained voices).",
        options="**To female / To male** (or **Auto**: the opposite of the face it sees) and the strength",
        topic="gender-swap"),
    "Chronos": dict(
        what="younger or older",
        tagline="Be younger or older, live on a video call",
        lede="One snapshot of you, aged by Changeling Chronos, then worn live by Changeling CopyCat Realtime: your "
             "own expressions, younger or older, out to the call through OBS Virtual Camera.",
        options="the **age** (below 0 younger, above 0 older) and **keep gender** (Chronos holds a face's gender as "
                "it ages it)",
        topic="age-progression"),
    "Natio": dict(
        what="another ancestry",
        tagline="Another ancestry, live on a video call",
        lede="One snapshot of you, changed by Changeling Natio to how you might look born somewhere else in the "
             "world, then worn live by Changeling CopyCat Realtime: your own expressions, out to the call through "
             "OBS Virtual Camera. Made for curiosity; use it on yourself, not to mock anyone.",
        options="**Towards** (African, European, East Asian), the strength, and **keep gender**",
        topic="face-editing"),
}


def topics(app):
    return ["changeling", "cursed-entertainment", "python", "customtkinter", "stylegan2", "face-swap", "realtime",
            "video-calls", "virtual-camera", "obs", APPS[app]["topic"], "webcam"]


def description(app):
    a = APPS[app]
    return (f"Changeling {app} Realtime: {a['what']}, live on a video call - one snapshot of you, changed by "
            f"Changeling {app} (StyleGAN2), then worn live by Changeling CopyCat Realtime with your own expressions.")


def write(path, text, newline="\n"):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline=newline) as f:
        f.write(text)


RUN_BAT = r'''@echo off
setlocal
cd /d "%~dp0"
rem Changeling {app} Realtime runs in Changeling {app}'s environment: it needs Changeling {app} and Changeling
rem CopyCat Realtime next to this folder (as in Changeling Studio). Missing ones are fetched here.
set "STUDIO=%~dp0.."
for %%A in ({app} CopyCat-Realtime) do (
    if not exist "%STUDIO%\Changeling-%%A" (
        echo Getting Changeling %%A ...
        git clone https://github.com/{owner}/Changeling-%%A "%STUDIO%\Changeling-%%A" || goto :error
    )
)

rem The environment: Changeling {app}'s, or a sibling's (Tiresias, Chronos and Natio need the same things).
set "PY="
for %%A in ({app} Tiresias Chronos Natio) do (
    if not defined PY if exist "%STUDIO%\Changeling-%%A\psdenv\Lib\site-packages	orch" set "PY=%STUDIO%\Changeling-%%A\psdenv\Scripts\python.exe"
)
if not defined PY (
    echo Setting up Changeling {app} first, once: its environment and its models.
    call "%STUDIO%\Changeling-{app}\run.bat" --download-models
    set "PY=%STUDIO%\Changeling-{app}\psdenv\Scripts\python.exe"
)
if not exist "%STUDIO%\Changeling-CopyCat-Realtime\venv\Scripts\python.exe" (
    echo Changeling CopyCat Realtime isn't set up yet: start its run.bat once, then come back.
)

"%PY%" live.py %*
exit /b %ERRORLEVEL%

:error
echo.
echo Setup failed, see the messages above.
pause
exit /b 1
'''

RUN_PS1 = r'''# Changeling {app} Realtime runs in Changeling {app}'s environment: it needs Changeling {app} and Changeling
# CopyCat Realtime next to this folder (as in Changeling Studio). Missing ones are fetched here.
$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot
$Studio = Split-Path $PSScriptRoot -Parent
foreach ($a in @("{app}", "CopyCat-Realtime")) {{
    $dir = Join-Path $Studio "Changeling-$a"
    if (-not (Test-Path $dir)) {{
        Write-Host "Getting Changeling $a ..."
        git clone "https://github.com/{owner}/Changeling-$a" $dir
    }}
}}

# The environment: Changeling {app}'s, or a sibling's (Tiresias, Chronos and Natio need the same things).
$Py = $null
foreach ($a in @("{app}", "Tiresias", "Chronos", "Natio")) {{
    $p = Join-Path $Studio "Changeling-$a\psdenv\Scripts\python.exe"
    $torch = Join-Path $Studio "Changeling-$a\psdenv\Lib\site-packages	orch"
    if (-not $Py -and (Test-Path $torch)) {{ $Py = $p }}
}}
if (-not $Py) {{
    Write-Host "Setting up Changeling {app} first, once: its environment and its models."
    & (Join-Path $Studio "Changeling-{app}\run.ps1") --download-models
    $Py = Join-Path $Studio "Changeling-{app}\psdenv\Scripts\python.exe"
}}
if (-not (Test-Path (Join-Path $Studio "Changeling-CopyCat-Realtime\venv\Scripts\python.exe"))) {{
    Write-Host "Changeling CopyCat Realtime isn't set up yet: start its run.ps1 once, then come back."
}}

& $Py live.py @args
'''

RUN_SH = r'''#!/usr/bin/env bash
# Changeling {app} Realtime runs in Changeling {app}'s environment: it needs Changeling {app} and Changeling
# CopyCat Realtime next to this folder (as in Changeling Studio). Missing ones are fetched here.
set -e
cd "$(dirname "$0")"
STUDIO="$(cd .. && pwd)"
for a in {app} CopyCat-Realtime; do
    if [ ! -d "$STUDIO/Changeling-$a" ]; then
        echo "Getting Changeling $a ..."
        git clone "https://github.com/{owner}/Changeling-$a" "$STUDIO/Changeling-$a"
    fi
done

# The environment: Changeling {app}'s, or a sibling's (Tiresias, Chronos and Natio need the same things).
PY=""
for a in {app} Tiresias Chronos Natio; do
    for p in "$STUDIO/Changeling-$a/psdenv/bin/python" "$STUDIO/Changeling-$a/psdenv/Scripts/python.exe"; do
        env="$(dirname "$(dirname "$p")")"
        if [ -z "$PY" ] && [ -x "$p" ] && { [ -d "$env/Lib/site-packages/torch" ] || ls -d "$env"/lib/python3*/site-packages/torch >/dev/null 2>&1; }; then PY="$p"; fi
    done
done
if [ -z "$PY" ]; then
    echo "Setting up Changeling {app} first, once: its environment and its models."
    bash "$STUDIO/Changeling-{app}/run.sh" --download-models
    PY="$STUDIO/Changeling-{app}/psdenv/bin/python"
fi

exec "$PY" live.py "$@"
'''

GITIGNORE = """# your snapshots and what was made of them stay on this PC
made/
__pycache__/
*.pyc
"""

FAMILY_LINE = """*Part of the Changeling family, all together in [Changeling Studio](https://github.com/CursedPrograms/Changeling-Studio):
[CopyCat](https://github.com/CursedPrograms/Changeling-CopyCat) ·
[CopyCat Realtime](https://github.com/CursedPrograms/Changeling-CopyCat-Realtime) ·
[Tiresias](https://github.com/CursedPrograms/Changeling-Tiresias) (gender) ·
[Chronos](https://github.com/CursedPrograms/Changeling-Chronos) (age) ·
[Natio](https://github.com/CursedPrograms/Changeling-Natio) (where in the world) ·
[Cosmeta](https://github.com/CursedPrograms/Changeling-Cosmeta) (makeup, hair colour) ·
[Echo](https://github.com/CursedPrograms/Changeling-Echo) (voices) ·
[Tonsor](https://github.com/CursedPrograms/Changeling-Tonsor) (hairstyles, beards) ·
realtime: [Tiresias](https://github.com/CursedPrograms/Changeling-Tiresias-Realtime) ·
[Chronos](https://github.com/CursedPrograms/Changeling-Chronos-Realtime) ·
[Natio](https://github.com/CursedPrograms/Changeling-Natio-Realtime) ·
[Cosmeta](https://github.com/CursedPrograms/Changeling-Cosmeta-Realtime).*"""

FOOTER = """---

<br>
<div align="center">
© Cursed Entertainment 2026
</div>
<br>
<div align="center">
<a href="https://cursed-entertainment.itch.io/" target="_blank">
    <img src="https://raw.githubusercontent.com/CursedPrograms/cursedentertainment/main/images/logos/logo-wide-grey.png"
        alt="CursedEntertainment Logo" style="width:250px;">
</a>
</div>
<br>
<div align="center">
  <a href="https://github.com/SynthWomb" target="_blank">
    <img src="https://raw.githubusercontent.com/SynthWomb/synth.womb/main/logos/synthwomb07.png" alt="SynthWomb" style="width:200px;"/>
  </a>
</div>
"""


def readme(app, has):
    a = APPS[app]
    repo = f"Changeling-{app}-Realtime"
    samples = f"\n![You, the new you, and CopyCat Realtime wearing it](assets/realtime_samples.jpg)\n" \
        if has("assets/realtime_samples.jpg") else ""
    window = "\n![The window](assets/window.png)\n" if has("assets/window.png") else ""
    return f"""[![Twitter: @NorowaretaGemu](https://img.shields.io/badge/X-@NorowaretaGemu-blue.svg?style=flat)](https://x.com/NorowaretaGemu)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

<div align="center">
  <a href="https://ko-fi.com/cursedentertainment">
    <img src="https://ko-fi.com/img/githubbutton_sm.svg" alt="ko-fi" style="width: 20%;"/>
  </a>
</div>

<p align="center"><img src="https://raw.githubusercontent.com/CursedPrograms/Changeling-Studio/main/site/logo.png" alt="Changeling" width="120"></p>

# Changeling {app} Realtime

**Website:** https://cursedprograms.github.io/{repo}/

{a['tagline']}. {a['lede']}
{samples}
{FAMILY_LINE}

## Why a snapshot, not every frame

[Changeling {app}](https://github.com/CursedPrograms/Changeling-{app}) edits a face with StyleGAN2: it finds the
face's place in StyleGAN's space (e4e), learns the face for a minute so the result still looks like you (pivotal
tuning), and moves it. That's far too slow for 30 frames a second. So it's done once, on one snapshot of you, and
[Changeling CopyCat Realtime](https://github.com/CursedPrograms/Changeling-CopyCat-Realtime) wears the result live: a
face swap that follows your expressions, head turns and talking, at camera speed. The new you looks steady, because
it's the same face in every frame.

## Use it

1. Python 3.9 - 3.12, an NVIDIA GPU (4 GB is enough), a webcam. For calls:
   [OBS](https://obsproject.com/) (its Virtual Camera) and, for the voice,
   [VB-Audio Virtual Cable](https://vb-audio.com/Cable/).
2. Start `run.bat` (or `run.ps1`, `run.sh`). It needs Changeling {app} and Changeling CopyCat Realtime next to
   this folder, as in [Changeling Studio](https://github.com/CursedPrograms/Changeling-Studio); any that are missing
   are cloned there, and Changeling {app} is set up the first time (its environment and models). No environment of
   its own: it runs in Changeling {app}'s.
3. **1 · You:** **Snapshot** (Space: a 3-second countdown) or a **Photo**. Face the camera, lit from the front.
4. **2 · The change:** {a['options']}; **Quality** (Fast takes seconds; Balanced about a minute and keeps you
   recognisable). **Make the new you** (Enter), with a completion bar. Everything you make is kept in `made/`
   (only on this PC) and listed under **Made before**.
5. **3 · Live:** **Persona's skin** (70 to start: the new you's skin tone and fine detail, like Chronos's
   wrinkles, which the face swap alone would leave as yours), the **voice** (off, female, male; **Echo** for Changeling Echo's trained voices) and **Go live**
   (L): CopyCat Realtime opens wearing the new you. **Phone as the camera**: CopyCat Realtime shows a QR code
   instead; open it on your phone and its camera and microphone are the input (the PC still does the work). In the call, pick **OBS Virtual Camera** as the camera (and
   **CABLE Output** as the microphone). Close CopyCat Realtime to come back and make another.
{window}
`run.bat --camera 1` (another webcam), `run.bat --source me.jpg` (a photo or a video instead of the webcam, for
trying it out without one).

## Its limits

- The live face is a 128-pixel swap: it carries the new you's features and, with **Persona's skin**, its tone
  and wrinkles, but softer than the photo Changeling {app} made. Your neck and ears keep your own skin tone.
- The swap carries the face, not the hair, neck or body: the new you's hair (Changeling {app} may change it) stays
  behind. A hood, a hat or a wig helps.
- One look per snapshot: to look different, make another.
- Use it on yourself, or on people who agreed; never to pass as someone real.

## Licenses

MIT. Changeling {app} is MIT (the StyleGAN2 FFHQ weights are NVIDIA's, non-commercial); Changeling CopyCat Realtime
uses InsightFace's inswapper (non-commercial research). So this is for personal and research use.

<p align="center"><img src="assets/site-qr.png" alt="QR code to the {repo} website" width="200"></p>

{FOOTER}"""


def page(app, has):
    a = APPS[app]
    repo = f"Changeling-{app}-Realtime"
    sample = f"""
  <section id="samples">
    <h2>What it looks like</h2>
    <p>You, the new you that Changeling {app} makes from one snapshot, and Changeling CopyCat Realtime wearing it live.
      Every face is an AI-generated person who doesn't exist. Click to enlarge.</p>
    <figure class="sheet"><img id="sheet" src="assets/realtime_samples.jpg" alt="You, the new you, and CopyCat Realtime wearing it" loading="lazy"></figure>
  </section>
""" if has("assets/realtime_samples.jpg") else ""
    window = '\n    <figure class="sheet"><img src="assets/window.png" alt="The window" loading="lazy"></figure>' \
        if has("assets/window.png") else ""
    return f"""<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Changeling {app} Realtime: {a['tagline'].lower()}</title>
<meta name="description" content="{description(app)} Part of the Changeling family.">
<meta name="color-scheme" content="dark">
<meta property="og:title" content="Changeling {app} Realtime">
<meta property="og:description" content="{a['tagline']}.">
<meta property="og:image" content="assets/realtime_samples.jpg">
<link rel="icon" href="https://cursedprograms.github.io/Changeling-Studio/site/icon.svg">
<link rel="stylesheet" href="https://cursedprograms.github.io/Changeling-Studio/site/changeling.css">
</head>
<body data-changeling="{app} Realtime">

<header>
  <div class="wrap">
    <img src="https://cursedprograms.github.io/Changeling-Studio/site/logo.svg" alt="The Changeling logo" width="110" height="110">
    <div class="eyebrow">Changeling</div>
    <h1>{app} Realtime</h1>
    <p class="tagline">{a['tagline']}</p>
    <p class="lede">{a['lede']}</p>
    <div class="cta">
      <a class="btn primary" href="https://github.com/{OWNER}/{repo}">View on GitHub</a>
      <a class="btn" href="#use">Use it</a>
      <a class="btn" href="#family">The Changeling family</a>
    </div>
    <div class="tags">
      <span class="tag">StyleGAN2</span><span class="tag">inswapper</span><span class="tag">OBS Virtual Camera</span>
      <span class="tag">CUDA</span><span class="tag">Python</span>
    </div>
  </div>
</header>

<main class="wrap">{sample}
  <section id="use">
    <h2>Use it</h2>
    <ol class="steps">
      <li>Python 3.9 - 3.12, an NVIDIA GPU (4 GB is enough), a webcam; OBS for its Virtual Camera.</li>
      <li>Start <code>run.bat</code> (or <code>run.ps1</code>, <code>run.sh</code>). It fetches Changeling {app} and Changeling CopyCat Realtime next to it if they're missing.</li>
      <li><strong>Snapshot</strong> (or a photo), choose the change, <strong>Make the new you</strong>, then <strong>Go live</strong>.</li>
    </ol>{window}
  </section>

  <section id="why">
    <h2>Why a snapshot</h2>
    <p>StyleGAN editing learns a face for about a minute: far too slow for every frame of a call. So Changeling {app}
      makes the new you once, and Changeling CopyCat Realtime wears it at camera speed, following your expressions.</p>
  </section>

  <section id="qr">
    <h2>This page on your phone</h2>
    <figure class="sheet"><img src="assets/site-qr.png" alt="QR code to this page" width="220" loading="lazy"></figure>
  </section>

  <section id="family"></section>
</main>

<div id="lightbox" class="lightbox" hidden><img alt=""></div>

<footer id="site-footer">&copy; Cursed Entertainment 2026</footer>
<script src="https://cursedprograms.github.io/Changeling-Studio/site/family.js"></script>
<script src="https://cursedprograms.github.io/RIFT/site/footer.js"></script>
</body>
</html>
"""


def qr(app, out):
    """Pink on white in a dark frame, the repo's name under it (like the other repos' QR codes)."""
    import segno
    from PIL import Image, ImageDraw, ImageFont
    url = f"https://cursedprograms.github.io/Changeling-{app}-Realtime/"
    code = segno.make(url, error="m")
    tmp = out + ".tmp.png"
    code.save(tmp, scale=10, border=2, dark="#e0408a", light="#ffffff")
    q = Image.open(tmp).convert("RGB")
    os.remove(tmp)
    pad, frame, text_h = 16, 6, 64
    W = q.width + 2 * (pad + frame)
    img = Image.new("RGB", (W, q.height + 2 * (pad + frame) + text_h), "#121216")
    d = ImageDraw.Draw(img)
    d.rectangle([pad, pad, pad + q.width + 2 * frame - 1, pad + q.height + 2 * frame - 1], fill="#e0408a")
    img.paste(q, (pad + frame, pad + frame))
    try:
        font = ImageFont.truetype("arialbd.ttf", 24)
    except OSError:
        font = ImageFont.load_default()
    label = f"Changeling-{app}-Realtime"
    tw = d.textlength(label, font=font)
    d.text(((W - tw) / 2, q.height + 2 * (pad + frame) + 16), label, fill="#e0408a", font=font)
    img.save(out)


def make(app):
    repo = os.path.join(STUDIO, f"Changeling-{app}-Realtime")
    src = os.path.join(STUDIO, f"Changeling-{app}")
    os.makedirs(repo, exist_ok=True)
    if not os.path.exists(os.path.join(repo, ".git")):
        subprocess.run(["git", "init", "-q", "-b", "main"], cwd=repo, check=True)
        subprocess.run(["git", "remote", "add", "origin", f"https://github.com/{OWNER}/Changeling-{app}-Realtime.git"],
                       cwd=repo, check=True)

    def has(rel):
        return os.path.exists(os.path.join(repo, rel))

    shutil.copy(os.path.join(HERE, "live.py"), repo)
    write(os.path.join(repo, "realtime.json"), json.dumps({"app": app}, indent=2) + "\n")
    for name in ("theme.json", "logo.ico", "logo.png", "LICENSE", "link.bf"):
        shutil.copy(os.path.join(src, name), repo)
    os.makedirs(os.path.join(repo, ".github"), exist_ok=True)
    shutil.copy(os.path.join(src, ".github", "FUNDING.yml"), os.path.join(repo, ".github"))
    fill = dict(app=app, owner=OWNER)
    write(os.path.join(repo, "run.bat"), RUN_BAT.replace("{app}", app).replace("{owner}", OWNER), "\r\n")
    write(os.path.join(repo, "run.ps1"), RUN_PS1.format(**fill), "\r\n")
    write(os.path.join(repo, "run.sh"), RUN_SH.replace("{app}", app).replace("{owner}", OWNER))
    write(os.path.join(repo, ".gitignore"), GITIGNORE)
    write(os.path.join(repo, ".gitattributes"), "*.sh text eol=lf\n*.bat text eol=crlf\n*.ps1 text eol=crlf\n")
    os.makedirs(os.path.join(repo, "assets"), exist_ok=True)
    qr(app, os.path.join(repo, "assets", "site-qr.png"))
    write(os.path.join(repo, "README.md"), readme(app, has))
    write(os.path.join(repo, "index.html"), page(app, has))
    print("written:", repo)


if __name__ == "__main__":
    for name in (sys.argv[1:] or list(APPS)):
        make(name)
