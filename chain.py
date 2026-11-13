"""chain.py - Changeling Studio's Chain: several Changelings, one after another, on the same photo.

Each step's result is the next step's photo: Imago (another face) > Chronos (older) > Cosmeta (makeup), or
Tiresias (the other gender) > Tonsor (a hairstyle). Every step runs its own Changeling's command line, in that
Changeling's own environment, one at a time (one model on the graphics card at once).

    python chain.py                         the Chain window (Studio's Chain button opens it too)
    python chain.py photo.jpg -o out.png --step "Tiresias to=female" --step "Chronos age=0.5"
    python chain.py --steps                 every step and its options

For a call: a chain's result is a face to wear live. "Use as persona" puts it in Changeling Imago Realtime's
personas/, "Wear it live" starts Imago Realtime wearing it (the same way the *-Realtime apps do).

Only Python's standard library (and Tk), like studio.py. The pictures are shown as PNG (Tk reads those itself);
a photo in another format is turned into a PNG first by one of the Changelings' environments.
"""
import argparse
import datetime
import os
import queue
import re
import shutil
import subprocess
import sys
import threading

HERE = os.path.dirname(os.path.abspath(__file__))
WIN = os.name == "nt"
WORK = os.path.join(os.environ.get("CHANGELING_GALLERY") or os.path.join(os.path.expanduser("~"), ".changeling"), "chain")
IMAGE_TYPES = [("Pictures", "*.png *.jpg *.jpeg *.webp *.bmp *.jfif"), ("All files", "*.*")]
NO_WINDOW = getattr(subprocess, "CREATE_NO_WINDOW", 0)
QUALITY = ["fast", "balanced", "best"]
RESULT = {"whole photo": ["--whole-photo"], "keep hair": ["--keep-hair"], "face frame": []}
GENDERS = ["auto", "female", "male", "off"]
COSMETA_PRESETS = ["none", "Natural", "Glam", "Smoky", "Goth", "Atelier blue", "Copper hair", "Platinum hair"]


# ---- where each Changeling's Python is
def _python(folder, env):
    return os.path.join(folder, env, "Scripts", "python.exe") if WIN else os.path.join(folder, env, "bin", "python")


def app_dir(name):
    return os.path.join(HERE, f"Changeling-{name}")


def app_python(name, env, siblings=()):
    """The Changeling's own environment; Tiresias, Chronos and Natio can use each other's (the same requirements)."""
    for n in (name, *siblings):
        py = _python(app_dir(n), env)
        if os.path.exists(py):
            return py
    return None


def gpu():
    return shutil.which("nvidia-smi") is not None


# ---- the steps: options are (key, kind, default, choices / range, label)
#   kind: choice, number, file (required), file? (optional), check
STYLEGAN = ("Tiresias", "Chronos", "Natio")


def _stylegan(opts):
    return ["--quality", opts["quality"], *RESULT[opts["result"]]]


STEPS = {
    "Imago": {
        "about": "another face on the photo (the face swap)",
        "env": "psdenv", "entry": "run.py",
        "options": [("face", "file", "", None, "Face to put on"),
                    ("sharper", "check", True, None, "Sharper face (GFPGAN)"),
                    ("many_faces", "check", False, None, "Every face in the photo")],
        "args": lambda i, o, opts: ["-s", opts["face"], "-t", i, "-o", o,
                                    "--frame-processor", *(["face_swapper", "face_enhancer"] if opts["sharper"] else ["face_swapper"]),
                                    *(["--many-faces"] if opts["many_faces"] else []),
                                    "--execution-provider", "cuda" if gpu() else "cpu"],
    },
    "Tiresias": {
        "about": "the other gender, still you",
        "env": "psdenv", "entry": "main.py", "siblings": STYLEGAN,
        "options": [("to", "choice", "auto", ["auto", "female", "male"], "To"),
                    ("strength", "number", 1.0, (0.2, 2.0), "Strength"),
                    ("quality", "choice", "balanced", QUALITY, "Quality"),
                    ("result", "choice", "whole photo", list(RESULT), "Result")],
        "args": lambda i, o, opts: [i, "-o", o, "--to", opts["to"], "--strength", f"{opts['strength']:.2f}", *_stylegan(opts)],
    },
    "Chronos": {
        "about": "younger or older",
        "env": "psdenv", "entry": "main.py", "siblings": STYLEGAN,
        "options": [("age", "number", 0.5, (-1.0, 1.0), "Age (- younger, + older)"),
                    ("keep_gender", "choice", "auto", GENDERS, "Keep gender"),
                    ("quality", "choice", "balanced", QUALITY, "Quality"),
                    ("result", "choice", "whole photo", list(RESULT), "Result")],
        "args": lambda i, o, opts: [i, "-o", o, "--age", f"{opts['age']:.2f}", "--keep-gender", opts["keep_gender"], *_stylegan(opts)],
    },
    "Natio": {
        "about": "born somewhere else in the world",
        "env": "psdenv", "entry": "main.py", "siblings": STYLEGAN,
        "options": [("towards", "choice", "european", ["african", "european", "east_asian"], "Towards"),
                    ("strength", "number", 1.0, (0.2, 1.5), "Strength"),
                    ("keep_gender", "choice", "auto", GENDERS, "Keep gender"),
                    ("quality", "choice", "balanced", QUALITY, "Quality"),
                    ("result", "choice", "whole photo", list(RESULT), "Result")],
        "args": lambda i, o, opts: [i, "-o", o, "--towards", opts["towards"], "--strength", f"{opts['strength']:.2f}",
                                    "--keep-gender", opts["keep_gender"], *_stylegan(opts)],
    },
    "Cosmeta": {
        "about": "makeup and hair colour, or makeup copied from a photo",
        "env": "venv", "entry": "cli.py",
        "options": [("preset", "choice", "Glam", COSMETA_PRESETS, "Look"),
                    ("reference", "file?", "", None, "Copy makeup from"),
                    ("paint", "check", False, None, "Copy it as face paint")],
        "args": lambda i, o, opts: [i, "-o", o, *(["--preset", opts["preset"]] if opts["preset"] != "none" else []),
                                    *(["--reference", opts["reference"]] if opts["reference"] else []),
                                    *(["--paint"] if opts["paint"] else [])],
    },
    "Tonsor": {
        "about": "a hairstyle from a photo (the result is the 1024 px face square)",
        "env": "psdenv", "entry": "main.py",
        "options": [("hairstyle", "file", "", None, "Hairstyle photo"),
                    ("colour", "file?", "", None, "Hair colour from")],
        "args": lambda i, o, opts: [i, opts["hairstyle"], "-o", o, *(["--colour", opts["colour"]] if opts["colour"] else [])],
    },
}


def defaults(app):
    return {k: d for k, _, d, _, _ in STEPS[app]["options"]}


def parse_step(text):
    """'Chronos age=0.5 keep_gender=off' -> ("Chronos", options). A value with spaces goes in quotes
    (face="D:/my photos/me.jpg"); in a choice, _ stands for a space (result=whole_photo, preset=Cursed_pink)."""
    parts = [p.replace('"', "") for p in re.findall(r'(?:[^\s"]|"[^"]*")+', text)]
    app = next((a for a in STEPS if a.lower() == parts[0].lower()), None)
    if app is None:
        raise SystemExit(f"No step called {parts[0]!r}: one of {', '.join(STEPS)}")
    opts = defaults(app)
    spec = {k: (kind, choices) for k, kind, _, choices, _ in STEPS[app]["options"]}
    for p in parts[1:]:
        key, _, value = p.partition("=")
        if key not in opts:
            raise SystemExit(f"{app} has no option {key!r}: {', '.join(opts)}")
        kind, choices = spec[key]
        if kind == "number":
            value = float(value)
        elif kind == "check":
            value = value.lower() in ("1", "true", "yes", "on")
        elif kind == "choice":
            value = next((c for c in choices if c.lower() == value.replace("_", " ").lower() or c.lower() == value.lower()), None)
            if value is None:
                raise SystemExit(f"{app} {key}: one of {', '.join(choices)}")
        opts[key] = value
    return app, opts


def check(app, opts):
    """Why this step can't run, or None."""
    spec = STEPS[app]
    why = installed(app)
    if why:
        return why
    for key, kind, _, _, label in spec["options"]:
        if kind == "file" and not opts.get(key):
            return f"{app}: pick the {label.lower()}"
        if kind.startswith("file") and opts.get(key) and not os.path.exists(opts[key]):
            return f"{app}: {opts[key]} isn't there"
    return None


def command(app, opts, src, out):
    spec = STEPS[app]
    # each step runs in its own folder: every photo it's given goes as a full path
    opts = {k: (os.path.abspath(v) if kind.startswith("file") and v else v)
            for (k, kind, _, _, _), v in zip(spec["options"], (opts[o[0]] for o in spec["options"]))}
    src, out = os.path.abspath(src), os.path.abspath(out)
    py = app_python(app, spec["env"], spec.get("siblings", ()))
    return [py, "-u", spec["entry"], *spec["args"](src, out, opts)]


# ---- running
def any_python():
    """A Changeling environment with OpenCV, for turning a photo into a PNG."""
    for app, env in (("Cosmeta", "venv"), ("Tiresias", "psdenv"), ("Chronos", "psdenv"), ("Imago", "psdenv")):
        py = _python(app_dir(app), env)
        if os.path.exists(py):
            return py
    return None


TO_PNG = ("import sys, cv2, numpy as np\n"
          "img = cv2.imdecode(np.fromfile(sys.argv[1], np.uint8), cv2.IMREAD_COLOR)\n"
          "if img is None: sys.exit('cannot read ' + sys.argv[1])\n"
          "k = float(sys.argv[3]) / max(img.shape[:2]) if len(sys.argv) > 3 else 1\n"
          "img = cv2.resize(img, (round(img.shape[1] * k), round(img.shape[0] * k)), interpolation=cv2.INTER_AREA) if k < 1 else img\n"
          "ok, buf = cv2.imencode('.png', img); buf.tofile(sys.argv[2])\n")


def to_png(src, dst, max_side=None):
    """src (any picture) as a PNG at dst, at most max_side pixels across."""
    try:
        from PIL import Image
        img = Image.open(src).convert("RGB")
        if max_side:
            img.thumbnail((max_side, max_side))
        img.save(dst)
        return
    except ImportError:
        pass
    py = any_python()
    if py is None:
        raise RuntimeError("No Changeling environment to read the photo with (studio.py setup)")
    r = subprocess.run([py, "-c", TO_PNG, src, dst, *([str(max_side)] if max_side else [])],
                       capture_output=True, text=True, creationflags=NO_WINDOW)
    if r.returncode or not os.path.exists(dst):
        raise RuntimeError((r.stderr or r.stdout).strip().splitlines()[-1] if (r.stderr or r.stdout).strip() else
                           f"can't read {src}")


def run_step(app, opts, src, out, say, procs=None):
    """Run one step; say(text) gets its output line by line. True when it made `out`."""
    cmd = command(app, opts, src, out)
    env = dict(os.environ, PYTHONUNBUFFERED="1", PYTHONIOENCODING="utf-8")
    proc = subprocess.Popen(cmd, cwd=app_dir(app), stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env,
                            creationflags=NO_WINDOW)
    if procs is not None:
        procs.append(proc)
    tail, line = [], b""
    while True:
        chunk = proc.stdout.read1(4096) if hasattr(proc.stdout, "read1") else proc.stdout.read(1)
        if not chunk:
            break
        for ch in re.split(rb"(\r|\n)", chunk):
            if ch in (b"\r", b"\n"):
                text = line.decode("utf-8", "replace").strip()
                line = b""
                if text:
                    tail = (tail + [text])[-8:]
                    say(text)
            else:
                line += ch
    if line.strip():
        tail.append(line.decode("utf-8", "replace").strip())
        say(tail[-1])
    code = proc.wait()
    ok = code == 0 and os.path.exists(out) and os.path.getsize(out) > 0
    return ok, tail


def new_work_dir():
    folder = os.path.join(WORK, datetime.datetime.now().strftime("%Y-%m-%d_%H%M%S"))
    os.makedirs(folder, exist_ok=True)
    return folder


def remember(path):
    """Note the saved result in the Gallery (gallery.py makes its small picture with Pillow or OpenCV; when this
    Python has neither, a Changeling environment notes it)."""
    sys.path.insert(0, HERE)
    try:
        import importlib.util
        if importlib.util.find_spec("PIL") or importlib.util.find_spec("cv2"):
            from gallery import remember as note
            note(path, "Studio chain")
            return
    except Exception:  # noqa: BLE001
        pass
    py = any_python()
    code = f"import sys; sys.path.insert(0, {HERE!r}); from gallery import remember; remember(sys.argv[1], 'Studio chain')"
    if py:
        subprocess.run([py, "-c", code, path], creationflags=NO_WINDOW)
    else:
        from gallery import remember as note
        note(path, "Studio chain")


def persona_path(result):
    folder = os.path.join(HERE, "Changeling-Imago-Realtime", "personas")
    os.makedirs(folder, exist_ok=True)
    name = "chain_" + datetime.datetime.now().strftime("%Y-%m-%d_%H%M%S") + os.path.splitext(result)[1]
    return os.path.join(folder, name)


def wear_live(persona):
    folder = os.path.join(HERE, "Changeling-Imago-Realtime")
    py = _python(folder, "venv")
    return subprocess.Popen([py if os.path.exists(py) else sys.executable, "app.py", "--persona", persona], cwd=folder)


# ---- the command line
def cli(args):
    steps = [parse_step(s) for s in args.step]
    if not steps:
        raise SystemExit("Give at least one --step")
    for app, opts in steps:
        why = check(app, opts)
        if why:
            raise SystemExit(why)
    work = new_work_dir()
    current = os.path.join(work, "0_photo.png")
    to_png(args.photo, current)
    for n, (app, opts) in enumerate(steps, 1):
        out = os.path.join(work, f"{n}_{app.lower()}.png")
        print(f"== {n}. {app}", flush=True)
        ok, tail = run_step(app, opts, current, out, lambda t: print("   " + t, flush=True))
        if not ok:
            raise SystemExit(f"{app} didn't make a picture: " + (tail[-1] if tail else "no output"))
        current = out
    final = args.output or os.path.join(work, "result.png")
    if os.path.splitext(final)[1].lower() == ".png":
        shutil.copy(current, final)
    else:
        to_png_or_jpg(current, final)
    print(f"saved {final}", flush=True)
    if args.remember:
        remember(final)
    if args.persona:
        p = persona_path(final)
        shutil.copy(final, p)
        print(f"persona {p}", flush=True)


def installed(app):
    """Why the Changeling can't run at all (not here, no environment), or None."""
    spec = STEPS[app]
    if not os.path.isdir(app_dir(app)):
        return f"Changeling {app} isn't here (studio.py fetch gets it)"
    if not os.path.exists(os.path.join(app_dir(app), spec["entry"])):
        return f"Changeling {app} has no {spec['entry']}"
    if app_python(app, spec["env"], spec.get("siblings", ())) is None:
        return f"Changeling {app}'s environment isn't set up (studio.py setup, or start it once)"
    return None


def steps_json():
    """Every step for other programs (LayerTack's Filter > Changeling builds its dialogs from this)."""
    import json
    out = []
    for app, spec in STEPS.items():
        out.append({"app": app, "about": spec["about"], "missing": installed(app) or "",
                    "options": [{"key": k, "kind": kind, "default": d, "label": label,
                                 **({"choices": list(c)} if kind == "choice" else {}),
                                 **({"min": c[0], "max": c[1]} if kind == "number" else {})}
                                for k, kind, d, c, label in spec["options"]]})
    print(json.dumps({"steps": out}), flush=True)


def list_steps():
    for app, spec in STEPS.items():
        print(f"{app}: {spec['about']}")
        for key, kind, default, choices, label in spec["options"]:
            what = ("|".join(choices) if kind == "choice" else f"{choices[0]}..{choices[1]}" if kind == "number" else
                    "a photo" + (" (optional)" if kind == "file?" else "") if kind.startswith("file") else "on|off")
            print(f"    {key}={what}   {label} (default {default!r})")


# ---- the window
GREY, PANEL, LINE, TEXT, MUTED, PINK, DEEP = "#1a1a1a", "#242424", "#3a3a3a", "#e6e6e6", "#9a9a9a", "#007bb7", "#005184"
FONT = "Segoe UI"


def show(parent=None, photo=None, steps=()):
    """The Chain window. photo, steps ([(app, options)]): start with these (python chain.py photo.jpg --step ... --window)."""
    import tkinter as tk
    from tkinter import filedialog, ttk

    own = parent is None
    win = tk.Tk() if own else tk.Toplevel(parent)
    win.title("Changeling Studio: Chain")
    win.configure(bg=GREY)
    win.geometry("1180x800")
    ico = os.path.join(HERE, "site", "logo.ico")
    if WIN and os.path.exists(ico):
        try:
            win.iconbitmap(ico)
        except tk.TclError:
            pass

    state = {"photo": None, "steps": [], "results": [], "busy": False, "procs": [], "final": None, "cancel": False,
             "images": {}}
    events = queue.Queue()

    def button(parent, text, command, primary=True, **kw):
        kw = {"padx": 12, "pady": 5, **kw}
        b = tk.Button(parent, text=text, command=command, bg=DEEP if primary else LINE, fg="white",
                      activebackground=PINK, activeforeground="white", disabledforeground="#777777", relief="flat",
                      bd=0, font=(FONT, 9, "bold"), cursor="hand2", **kw)
        b.colour = DEEP if primary else LINE
        if kw.get("state") == "disabled":
            b.configure(bg=PANEL)
        return b

    def able(b, on):
        """A button on or off: an off one goes grey, so pink always means you can press it."""
        b.configure(state="normal" if on else "disabled", bg=b.colour if on else PANEL)

    def label(parent, text, size=9, colour=MUTED, bold=False, bg=GREY, **kw):
        return tk.Label(parent, text=text, fg=colour, bg=bg, font=(FONT, size, "bold" if bold else "normal"), **kw)

    def menu(parent, var, choices, bg=PANEL):
        m = tk.OptionMenu(parent, var, *choices)
        m.configure(bg=LINE, fg=TEXT, activebackground=DEEP, activeforeground="white", relief="flat", bd=0,
                    highlightthickness=0, font=(FONT, 9), cursor="hand2")
        m["menu"].configure(bg=PANEL, fg=TEXT, activebackground=DEEP, activeforeground="white", font=(FONT, 9))
        return m

    # header
    label(win, "CHANGELING STUDIO", 9, MUTED, True).pack(anchor="w", padx=22, pady=(16, 0))
    label(win, "Chain", 24, PINK, True).pack(anchor="w", padx=22)
    label(win, "several Changelings one after another on one photo: each step's result is the next step's photo",
          10).pack(anchor="w", padx=22)

    body = tk.Frame(win, bg=GREY)
    body.pack(fill="both", expand=True, padx=16, pady=10)
    left = tk.Frame(body, bg=GREY, width=440)
    left.pack(side="left", fill="y")
    left.pack_propagate(False)
    right = tk.Frame(body, bg=GREY)
    right.pack(side="left", fill="both", expand=True, padx=(14, 0))

    # left: the photo, the steps
    photo_row = tk.Frame(left, bg=PANEL, highlightbackground=LINE, highlightthickness=1)
    photo_row.pack(fill="x", pady=(0, 8))
    photo_name = label(photo_row, "No photo yet", 10, TEXT, bg=PANEL, anchor="w")
    photo_name.pack(side="left", fill="x", expand=True, padx=12, pady=10)
    button(photo_row, "Pick a photo", lambda: pick_photo()).pack(side="right", padx=10)

    add_row = tk.Frame(left, bg=GREY)
    add_row.pack(fill="x", pady=(0, 6))
    label(add_row, "Add a step", 10, TEXT, True).pack(side="left")
    add_var = tk.StringVar(value="Tiresias")
    menu(add_row, add_var, list(STEPS)).pack(side="left", padx=8)
    button(add_row, "Add", lambda: add_step(add_var.get())).pack(side="left")
    add_about = label(left, STEPS["Tiresias"]["about"], 9, anchor="w", justify="left", wraplength=420)
    add_about.pack(fill="x")
    add_var.trace_add("write", lambda *_: add_about.configure(text=STEPS[add_var.get()]["about"]))

    steps_box = tk.Frame(left, bg=GREY)
    steps_canvas = tk.Canvas(steps_box, bg=GREY, highlightthickness=0)
    style = ttk.Style(win)
    style.theme_use("clam")                     # the one theme whose scrollbar takes colours
    style.configure("Chain.Vertical.TScrollbar", background=LINE, troughcolor=GREY, bordercolor=GREY,
                    arrowcolor=MUTED, lightcolor=LINE, darkcolor=LINE)
    style.map("Chain.Vertical.TScrollbar", background=[("active", DEEP)])
    style.configure("Chain.Horizontal.TScale", background=DEEP, troughcolor=GREY, bordercolor=PANEL,
                    lightcolor=DEEP, darkcolor=DEEP)
    style.map("Chain.Horizontal.TScale", background=[("active", PINK)])
    steps_bar = ttk.Scrollbar(steps_box, orient="vertical", command=steps_canvas.yview, style="Chain.Vertical.TScrollbar")
    steps_inner = tk.Frame(steps_canvas, bg=GREY)
    steps_inner.bind("<Configure>", lambda e: steps_canvas.configure(scrollregion=steps_canvas.bbox("all")))
    steps_canvas.create_window((0, 0), window=steps_inner, anchor="nw", width=420)
    steps_canvas.configure(yscrollcommand=steps_bar.set)
    steps_box.pack(fill="both", expand=True, pady=8)
    steps_canvas.pack(side="left", fill="both", expand=True)
    steps_bar.pack(side="right", fill="y")
    steps_canvas.bind("<Enter>", lambda e: steps_canvas.bind_all(
        "<MouseWheel>", lambda ev: steps_canvas.yview_scroll(int(-ev.delta / 120), "units")))
    steps_canvas.bind("<Leave>", lambda e: steps_canvas.unbind_all("<MouseWheel>"))

    run_row = tk.Frame(left, bg=GREY)
    run_row.pack(fill="x")
    run_btn = button(run_row, "Run the chain", lambda: run())
    run_btn.configure(font=(FONT, 11, "bold"), padx=18, pady=8)
    run_btn.pack(side="left")
    stop_btn = button(run_row, "Stop", lambda: stop(), primary=False, state="disabled")
    stop_btn.pack(side="left", padx=8)

    # right: the results, one per step, and the big picture
    strip = tk.Frame(right, bg=GREY)
    strip.pack(fill="x")
    big = tk.Label(right, bg=PANEL, fg=MUTED, text="Pick a photo, add steps, Run.", font=(FONT, 11),
                   highlightbackground=LINE, highlightthickness=1)
    big.pack(fill="both", expand=True, pady=8)
    big_caption = label(right, "", 10, TEXT)
    big_caption.pack(anchor="w")
    status = label(right, "", 9, MUTED, anchor="w", justify="left", wraplength=680)
    status.pack(fill="x", pady=(2, 6))
    end_row = tk.Frame(right, bg=GREY)
    end_row.pack(fill="x")
    save_btn = button(end_row, "Save the result ...", lambda: save(), state="disabled")
    save_btn.pack(side="left")
    persona_btn = button(end_row, "Use as Imago Realtime persona", lambda: use_persona(), primary=False, state="disabled")
    persona_btn.pack(side="left", padx=8)
    live_btn = button(end_row, "Wear it live", lambda: go_live(), primary=False, state="disabled")
    live_btn.pack(side="left")
    label(win, "Each step runs in its own Changeling's environment, one at a time. © Cursed Entertainment 2026",
          9, "#6f6f6f").pack(pady=(0, 10))

    # ---- pictures
    def picture(path, box):
        """A Tk picture of a PNG, made smaller (whole steps) to fit a box x box square."""
        img = tk.PhotoImage(file=path)
        k = max(1, -(-max(img.width(), img.height()) // box))
        return img.subsample(k) if k > 1 else img

    def show_big(path, caption):
        try:
            w, h = max(200, big.winfo_width() - 10), max(200, big.winfo_height() - 10)
            img = picture(path, min(w, h) if min(w, h) > 200 else 520)
        except tk.TclError as e:
            big.configure(image="", text=str(e))
            return
        state["images"]["big"] = img
        big.configure(image=img, text="")
        big_caption.configure(text=caption)

    def rebuild_strip():
        for w in strip.winfo_children():
            w.destroy()
        for n, (title, path) in enumerate(state["results"]):
            cell = tk.Frame(strip, bg=PANEL, highlightbackground=PINK if n == len(state["results"]) - 1 else LINE,
                            highlightthickness=1, cursor="hand2")
            cell.pack(side="left", padx=(0, 6))
            try:
                img = picture(path, 110)
                state["images"][f"t{n}"] = img
                pic = tk.Label(cell, image=img, bg=PANEL)
            except tk.TclError:
                pic = tk.Label(cell, text="?", bg=PANEL, fg=MUTED, width=14, height=6)
            pic.pack(padx=4, pady=(4, 0))
            name = label(cell, title, 8, TEXT, bg=PANEL)
            name.pack(pady=(0, 4))
            for w in (cell, pic, name):
                w.bind("<Button-1>", lambda e, p=path, t=title: show_big(p, t))

    # ---- the photo and the steps
    def pick_photo():
        if state["busy"]:
            return
        path = filedialog.askopenfilename(parent=win, title="A photo with a face", filetypes=IMAGE_TYPES)
        if path:
            load_photo(path)

    def load_photo(path):
        work = new_work_dir()
        png = os.path.join(work, "0_photo.png")
        status.configure(text="Reading the photo ...")
        win.update_idletasks()
        try:
            to_png(path, png)
        except Exception as e:  # noqa: BLE001
            status.configure(text=f"Can't read that photo: {e}")
            return
        state.update(photo=path, work=work, start=png, final=None, results=[("Photo", png)])
        photo_name.configure(text=os.path.basename(path))
        rebuild_strip()
        show_big(png, "The photo")
        status.configure(text="Now add steps, then Run.")
        for b in (save_btn, persona_btn, live_btn):
            able(b, False)

    def add_step(app, opts=None):
        if state["busy"]:
            return
        step = {"app": app, "vars": {}}
        state["steps"].append(step)
        opts = opts or defaults(app)
        for key, kind, default, choices, _ in STEPS[app]["options"]:
            value = opts.get(key, default)
            step["vars"][key] = (tk.BooleanVar(value=value) if kind == "check" else
                                 tk.DoubleVar(value=value) if kind == "number" else tk.StringVar(value=value))
        redraw_steps()

    def move(step, d):
        s = state["steps"]
        i = s.index(step)
        if 0 <= i + d < len(s):
            s[i], s[i + d] = s[i + d], s[i]
            redraw_steps()

    def remove(step):
        if not state["busy"]:
            state["steps"].remove(step)
            redraw_steps()

    def pick_file(var, title, app):
        start = {"Cosmeta": os.path.join(app_dir("Cosmeta"), "makeup_refs"),
                 "Tonsor": os.path.join(app_dir("Tonsor"), "hairstyles")}.get(app)
        path = filedialog.askopenfilename(parent=win, title=title, filetypes=IMAGE_TYPES,
                                          initialdir=start if start and os.path.isdir(start) else None)
        if path:
            var.set(path)

    def redraw_steps():
        for w in steps_inner.winfo_children():
            w.destroy()
        if not state["steps"]:
            label(steps_inner, "No steps yet: pick one above and Add.", 9).pack(anchor="w", pady=6)
        for n, step in enumerate(state["steps"], 1):
            app = step["app"]
            card = tk.Frame(steps_inner, bg=PANEL, highlightbackground=LINE, highlightthickness=1)
            card.pack(fill="x", pady=4)
            top = tk.Frame(card, bg=PANEL)
            top.pack(fill="x", padx=10, pady=(8, 2))
            label(top, f"{n}.  {app}", 12, TEXT, True, bg=PANEL).pack(side="left")
            for text, cmd in (("x", lambda s=step: remove(s)), ("down", lambda s=step: move(s, 1)),
                              ("up", lambda s=step: move(s, -1))):
                button(top, text, cmd, primary=False, padx=6, pady=1).pack(side="right", padx=(4, 0))
            label(card, STEPS[app]["about"], 8, bg=PANEL, anchor="w").pack(fill="x", padx=10)
            for key, kind, default, choices, title in STEPS[app]["options"]:
                row = tk.Frame(card, bg=PANEL)
                row.pack(fill="x", padx=10, pady=2)
                var = step["vars"][key]
                if kind == "check":
                    tk.Checkbutton(row, text=title, variable=var, bg=PANEL, fg=TEXT, selectcolor=GREY,
                                   activebackground=PANEL, activeforeground=PINK, font=(FONT, 9), bd=0,
                                   highlightthickness=0).pack(side="left")
                    continue
                label(row, title, 9, MUTED, bg=PANEL, width=20, anchor="w").pack(side="left")
                if kind == "choice":
                    menu(row, var, choices).pack(side="left")
                elif kind == "number":
                    shown = tk.Label(row, text=f"{var.get():.2f}", bg=PANEL, fg=TEXT, font=(FONT, 9), width=5)

                    def moved(v, var=var, shown=shown):
                        var.set(round(float(v) * 20) / 20)          # steps of 0.05
                        shown.configure(text=f"{var.get():.2f}")
                    ttk.Scale(row, from_=choices[0], to=choices[1], value=var.get(), orient="horizontal", length=170,
                              command=moved, style="Chain.Horizontal.TScale").pack(side="left")
                    shown.pack(side="left", padx=6)
                else:
                    shown = tk.Label(row, textvariable=var, bg=PANEL, fg=TEXT, font=(FONT, 8), width=18, anchor="w")
                    shown.pack(side="left")
                    button(row, "Pick", lambda v=var, t=title, a=app: pick_file(v, t, a), primary=False,
                           padx=8, pady=1).pack(side="left", padx=4)
                    if kind == "file?":
                        button(row, "None", lambda v=var: v.set(""), primary=False, padx=8, pady=1).pack(side="left")
            tk.Frame(card, bg=PANEL, height=6).pack()

    def options(step):
        out = {}
        for key, kind, _, _, _ in STEPS[step["app"]]["options"]:
            v = step["vars"][key].get()
            out[key] = round(float(v), 2) if kind == "number" else v
        return out

    # ---- running
    def run():
        if state["busy"]:
            return
        if not state["photo"]:
            status.configure(text="First pick a photo.")
            return
        if not state["steps"]:
            status.configure(text="Add at least one step.")
            return
        plan = [(s["app"], options(s)) for s in state["steps"]]
        for app, opts in plan:
            why = check(app, opts)
            if why:
                status.configure(text=why)
                return
        work = new_work_dir()
        start = os.path.join(work, "0_photo.png")
        shutil.copy(state["start"], start)
        state.update(busy=True, cancel=False, procs=[], final=None, persona=None, work=work, results=[("Photo", start)])
        rebuild_strip()
        for b in (save_btn, persona_btn, live_btn, run_btn):
            able(b, False)
        able(stop_btn, True)
        threading.Thread(target=worker, args=(plan, start, work), daemon=True).start()

    def worker(plan, current, work):
        for n, (app, opts) in enumerate(plan, 1):
            if state["cancel"]:
                events.put(("done", False, "Stopped."))
                return
            out = os.path.join(work, f"{n}_{app.lower()}.png")
            events.put(("status", f"{n} of {len(plan)}: {app} is working (loading its models first) ..."))
            ok, tail = run_step(app, opts, current, out, lambda t, a=app, k=n: events.put(("status", f"{k} of {len(plan)}: {a}: {t}")),
                                state["procs"])
            if not ok:
                events.put(("done", False, "Stopped." if state["cancel"] else
                            f"{app} didn't make a picture:\n" + "\n".join(tail[-3:] or ["no output"])))
                return
            events.put(("result", f"{n}. {app}", out))
            current = out
        events.put(("done", True, current))

    def stop():
        state["cancel"] = True
        for p in state["procs"]:
            if p.poll() is None:
                p.kill()

    def poll():
        try:
            while True:
                ev = events.get_nowait()
                if ev[0] == "status":
                    status.configure(text=ev[1][:300])
                elif ev[0] == "result":
                    state["results"].append((ev[1], ev[2]))
                    rebuild_strip()
                    show_big(ev[2], ev[1])
                elif ev[0] == "done":
                    finished(ev[1], ev[2])
        except queue.Empty:
            pass
        if win.winfo_exists():
            win.after(150, poll)

    def finished(ok, what):
        state["busy"] = False
        able(run_btn, True)
        able(stop_btn, False)
        if not ok:
            status.configure(text=what)
            return
        state["final"] = what
        for b in (save_btn, persona_btn, live_btn):
            able(b, True)
        status.configure(text="Done: every step finished. Save it where you like.")
        save()

    def chain_name():
        stem = os.path.splitext(os.path.basename(state["photo"] or "photo"))[0]
        return stem + "_" + "_".join(s["app"].lower() for s in state["steps"])

    def save():
        if not state["final"]:
            return
        path = filedialog.asksaveasfilename(parent=win, title="Save the chain's result", defaultextension=".png",
                                            initialdir=os.path.dirname(state["photo"] or HERE),
                                            initialfile=chain_name() + ".png",
                                            filetypes=[("PNG", "*.png"), ("JPEG", "*.jpg")])
        if not path:
            status.configure(text="Not saved yet (it's kept in " + state["work"] + "); Save the result ... any time.")
            return
        try:
            if os.path.splitext(path)[1].lower() == ".png":
                shutil.copy(state["final"], path)
            else:
                to_png_or_jpg(state["final"], path)
        except Exception as e:  # noqa: BLE001
            status.configure(text=f"Not saved: {e}")
            return
        threading.Thread(target=remember, args=(path,), daemon=True).start()
        status.configure(text=f"Saved {path} (it's in the Gallery too).")

    def use_persona():
        if not state["final"]:
            return
        p = persona_path(state["final"])
        shutil.copy(state["final"], p)
        state["persona"] = p
        status.configure(text=f"In Imago Realtime's personas: {os.path.basename(p)} (pick it there, or Wear it live).")
        return p

    def go_live():
        if not state["final"]:
            return
        p = state.get("persona") or use_persona()
        if not os.path.isdir(os.path.join(HERE, "Changeling-Imago-Realtime")):
            status.configure(text="Changeling Imago Realtime isn't here (studio.py fetch gets it).")
            return
        wear_live(p)
        status.configure(text="Imago Realtime is starting, wearing the chain's face.")

    redraw_steps()
    for app, opts in steps:
        add_step(app, opts)
    if photo:
        win.after(300, lambda: load_photo(photo))
    win.after(150, poll)
    if own:
        win.mainloop()
    return win


def to_png_or_jpg(src, dst):
    """Save src (a PNG) as dst, a JPEG or anything OpenCV writes."""
    try:
        from PIL import Image
        Image.open(src).convert("RGB").save(dst, quality=95)
        return
    except ImportError:
        pass
    py = any_python()
    code = ("import sys, cv2, numpy as np\nimg = cv2.imdecode(np.fromfile(sys.argv[1], np.uint8), cv2.IMREAD_COLOR)\n"
            "ok, buf = cv2.imencode('.' + sys.argv[2].rsplit('.', 1)[-1], img, [cv2.IMWRITE_JPEG_QUALITY, 95]); buf.tofile(sys.argv[2])\n")
    r = subprocess.run([py, "-c", code, src, dst], capture_output=True, text=True, creationflags=NO_WINDOW)
    if r.returncode or not os.path.exists(dst):
        raise RuntimeError("couldn't write " + dst)


def main():
    ap = argparse.ArgumentParser(description="Changeling Studio's Chain: Changelings one after another on one photo.")
    ap.add_argument("photo", nargs="?", help="the photo (omit to open the window)")
    ap.add_argument("-o", "--output", help="where to save the result (default: in ~/.changeling/chain/...)")
    ap.add_argument("--step", action="append", default=[], help='a step: "App key=value ...", in order (--steps lists them)')
    ap.add_argument("--steps", action="store_true", help="list every step and its options")
    ap.add_argument("--json", action="store_true", help="with --steps: as JSON, and whether each Changeling is set up")
    ap.add_argument("--remember", action="store_true", help="note the result in the Gallery")
    ap.add_argument("--persona", action="store_true", help="also put the result in Imago Realtime's personas/")
    ap.add_argument("--window", action="store_true", help="open the window with this photo and these steps instead")
    args = ap.parse_args()
    if args.steps:
        steps_json() if args.json else list_steps()
    elif args.photo and not args.window:
        cli(args)
    else:
        show(photo=args.photo, steps=[parse_step(s) for s in args.step])


if __name__ == "__main__":
    main()
