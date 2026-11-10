"""studio.py - Changeling Studio: get every Changeling, set them up, share their models, and start them.

    studio.bat                 all of it, then the Studio window (studio.ps1, studio.sh the same)
    python studio.py fetch     1. clone the Changelings in apps.txt that aren't here, pull the rest
    python studio.py setup     2. each one's Python environment and requirements
    python studio.py models    3. every model, downloaded once and shared with every Changeling that needs it
    python studio.py window    the Studio window: every Changeling, one click to start it, and the Gallery
    python gallery.py          the Gallery on its own: everything you've made with any Changeling
    python chain.py            the Chain on its own: several Changelings one after another on one photo

Only Python's standard library, so it runs before anything is installed.

Sharing, so nothing big is downloaded twice:
- Environments: Changelings with the same requirements (Tiresias, Chronos, Natio) share one: the first one's, linked
  into the others (a directory junction on Windows).
- Models (models.json): each is looked for in every Changeling that uses it and in F:\\AIModels first; only when none has
  it is it downloaded, once, then put in every Changeling that needs it (a hard link on the same drive, so it takes no
  space twice; a copy across drives), and kept in F:\\AIModels too when that drive is there.
Each Changeling on its own still downloads its own models; this only makes Studio do it once for all of them.
"""
import hashlib
import json
import os
import shutil
import subprocess
import sys
import time
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
OWNER = "CursedPrograms"
SHARED = os.environ.get("CHANGELING_MODELS", r"F:\AIModels")
WIN = os.name == "nt"


def apps():
    with open(os.path.join(HERE, "apps.txt"), encoding="utf-8") as f:
        return [l.strip() for l in f if l.strip() and not l.startswith("#")]


def say(text):
    print(text, flush=True)


# ---- 1. fetch
def fetch():
    say("== 1. The Changelings")
    for name in apps():
        folder = os.path.join(HERE, name)
        if not os.path.isdir(os.path.join(folder, ".git")):
            say(f"  {name}: cloning")
            subprocess.run(["git", "clone", "-q", f"https://github.com/{OWNER}/{name}.git", folder], check=True)
        else:
            r = subprocess.run(["git", "-C", folder, "pull", "-q", "--ff-only"], capture_output=True, text=True)
            say(f"  {name}: " + ("up to date" if r.returncode == 0 else "not pulled (local changes): " +
                                 (r.stderr.strip().splitlines() or ["?"])[-1]))


# ---- 2. environments
def env_spec(name):
    """(env folder name, python version, requirements file, the stamp its run.bat checks) or None."""
    folder = os.path.join(HERE, name)
    req = os.path.join(folder, "requirements.txt")
    bat = os.path.join(folder, "run.bat")
    if not os.path.exists(req) or not os.path.exists(bat):
        return None
    text = open(bat, encoding="utf-8", errors="replace").read()
    env = "psdenv" if "psdenv" in text else "venv"
    version = "3.12" if "py -3.12" in text else "3.11"
    stamp = os.path.join(env, ".requirements") if ".requirements\"" in text or "\\.requirements" in text else \
        os.path.join(env, "requirements.installed")
    return env, version, req, stamp


def python_in(env_dir):
    return os.path.join(env_dir, "Scripts", "python.exe") if WIN else os.path.join(env_dir, "bin", "python")


def env_ready(env_dir, req, stamp_path):
    return os.path.exists(python_in(env_dir)) and os.path.exists(stamp_path) and \
        open(stamp_path, "rb").read() == open(req, "rb").read()


def link_dir(target, link):
    if WIN:
        subprocess.run(["cmd", "/c", "mklink", "/J", link, target], check=True, stdout=subprocess.DEVNULL)
    else:
        os.symlink(target, link, target_is_directory=True)


def setup():
    say("== 2. Environments and requirements")
    groups = {}
    for name in apps():
        folder = os.path.join(HERE, name)
        bat = os.path.join(folder, "run.bat")
        if os.path.exists(os.path.join(folder, "setup.bat" if WIN else "setup.sh")):
            say(f"  {name}: its own setup")             # Imago (from roop): a setup script that only sets up
            script = os.path.join(folder, "setup.bat" if WIN else "setup.sh")
            subprocess.run(["cmd", "/c", script] if WIN else ["bash", script], cwd=folder, check=True)
            continue
        if os.path.exists(bat) and "Applio" in open(bat, encoding="utf-8", errors="replace").read():
            say(f"  {name}: sets itself up the first time it starts (its voice engine, a few GB)")
            continue
        spec = env_spec(name)
        if spec is None:
            say(f"  {name}: runs in another Changeling's environment (its run.bat finds it)")
            continue
        env, version, req, stamp = spec
        key = (env, version, hashlib.sha1(open(req, "rb").read()).hexdigest())
        groups.setdefault(key, []).append(name)
    for (env, version, _), names in groups.items():
        # the owner: the one whose environment is already set up, else the first
        owner = next((n for n in names if env_ready(os.path.join(HERE, n, env), os.path.join(HERE, n, "requirements.txt"),
                                                    os.path.join(HERE, n, env_spec(n)[3]))), names[0])
        owner_dir = os.path.join(HERE, owner, env)
        req = os.path.join(HERE, owner, "requirements.txt")
        stamp = os.path.join(HERE, owner, env_spec(owner)[3])
        if not env_ready(owner_dir, req, stamp):
            say(f"  {owner}: making its environment (Python {version}) and installing its requirements ...")
            if not os.path.exists(python_in(owner_dir)):
                base = ["py", f"-{version}"] if WIN and shutil.which("py") else [sys.executable]
                subprocess.run(base + ["-m", "venv", owner_dir], check=True)
            py = python_in(owner_dir)
            subprocess.run([py, "-m", "pip", "install", "-q", "--upgrade", "pip"], check=True)
            subprocess.run([py, "-m", "pip", "install", "-r", req], check=True, cwd=os.path.join(HERE, owner))
            shutil.copy(req, stamp)
        else:
            say(f"  {owner}: environment ready")
        for other in names:
            if other == owner:
                continue
            other_dir = os.path.join(HERE, other, env)
            if os.path.islink(other_dir) or (WIN and os.path.isdir(other_dir) and _is_junction(other_dir)):
                say(f"  {other}: shares {owner}'s environment")
                continue
            if os.path.exists(other_dir):
                if env_ready(other_dir, os.path.join(HERE, other, "requirements.txt"), os.path.join(HERE, other, env_spec(other)[3])):
                    say(f"  {other}: has its own environment, ready")
                    continue
                say(f"  {other}: its half-made environment is replaced by {owner}'s")
                shutil.rmtree(other_dir)
            link_dir(owner_dir, other_dir)
            say(f"  {other}: shares {owner}'s environment (linked)")


def _is_junction(path):
    try:
        return bool(os.readlink(path))
    except (OSError, ValueError):
        return False


# ---- 3. models
def _download(url, path, tries=20):
    part = path + ".part"
    for attempt in range(tries):
        have = os.path.getsize(part) if os.path.exists(part) else 0
        try:
            req = urllib.request.Request(url, headers={"Range": f"bytes={have}-"} if have else {})
            with urllib.request.urlopen(req, timeout=60) as r, open(part, "ab" if have else "wb") as out:
                if have and r.status != 206:
                    out.truncate(0)
                    have = 0
                total = have + int(r.headers.get("Content-Length", 0))
                last = 0.0
                while True:
                    chunk = r.read(1 << 20)
                    if not chunk:
                        break
                    out.write(chunk)
                    have += len(chunk)
                    if time.monotonic() - last > 10:
                        last = time.monotonic()
                        say(f"      {have / 2**20:.0f} / {total / 2**20:.0f} MB")
            if total and have < total:
                raise OSError(f"stopped at {have} of {total} bytes")
            os.replace(part, path)
            return
        except OSError as e:
            if attempt == tries - 1:
                raise
            say(f"      {e}; trying again")
            time.sleep(min(60, 3 * (attempt + 1)))


def _place(src, dst):
    """dst gets the same file: a hard link on the same drive (no space twice), else a copy."""
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    try:
        os.link(src, dst)
    except OSError:
        shutil.copy2(src, dst)


def models():
    say("== 3. Models (each downloaded once, then shared)")
    manifest = json.load(open(os.path.join(HERE, "models.json"), encoding="utf-8"))
    here = set(apps())
    for m in manifest["models"]:
        dests = [d for d in m["used_by"] if d.split("/")[0] in here and os.path.isdir(os.path.join(HERE, d.split("/")[0]))]
        if not dests:
            continue
        paths = [os.path.join(HERE, *d.split("/"), m["file"]) for d in dests]
        shared = os.path.join(SHARED, *m["shared"].split("/"), m["file"]) if os.path.isdir(SHARED) else None
        have = next((p for p in paths + ([shared] if shared else []) if p and os.path.exists(p)), None)
        if have is None:
            target = shared or paths[0]
            os.makedirs(os.path.dirname(target), exist_ok=True)
            say(f"  {m['file']}: downloading ({m['mb']} MB), once")
            _download(m["url"], target)
            have = target
        placed = 0
        for p in paths + ([shared] if shared else []):
            if p and not os.path.exists(p):
                _place(have, p)
                placed += 1
        say(f"  {m['file']}: " + (f"shared with {placed} more" if placed else "everywhere already"))


# ---- the window
def window():
    import tkinter as tk
    import webbrowser
    GREY, PANEL, LINE, TEXT, MUTED, PINK, DEEP = "#1a1a1a", "#242424", "#3a3a3a", "#e6e6e6", "#9a9a9a", "#e0408a", "#c2186b"
    info = {n: d for n, d in json.load(open(os.path.join(HERE, "models.json"), encoding="utf-8"))["apps"].items()}
    root = tk.Tk()
    root.title("Changeling Studio")
    root.configure(bg=GREY)
    root.geometry("760x820")
    ico = os.path.join(HERE, "site", "logo.ico")
    if WIN and os.path.exists(ico):
        root.iconbitmap(ico)
    tk.Label(root, text="CHANGELING", fg=MUTED, bg=GREY, font=("Segoe UI", 9, "bold")).pack(anchor="w", padx=22, pady=(18, 0))
    tk.Label(root, text="Studio", fg=PINK, bg=GREY, font=("Segoe UI", 26, "bold")).pack(anchor="w", padx=22)
    tk.Label(root, text="every Changeling, one click to start it", fg=MUTED, bg=GREY, font=("Segoe UI", 10)).pack(anchor="w", padx=22)

    def gallery():
        import gallery as g                     # gallery.py, next to studio.py
        g.show(root)
    def chain():
        import chain as c                       # chain.py, next to studio.py: Changelings one after another
        c.show(root)
    tools = tk.Frame(root, bg=GREY)
    tools.pack(anchor="w", padx=22, pady=(10, 0))
    tk.Button(tools, text="Gallery: everything you've made", bg=DEEP, fg="white", activebackground=PINK,
              activeforeground="white", relief="flat", bd=0, padx=16, pady=6, font=("Segoe UI", 10, "bold"),
              cursor="hand2", command=gallery).pack(side="left")
    tk.Button(tools, text="Chain: several on one photo", bg=DEEP, fg="white", activebackground=PINK,
              activeforeground="white", relief="flat", bd=0, padx=16, pady=6, font=("Segoe UI", 10, "bold"),
              cursor="hand2", command=chain).pack(side="left", padx=(8, 0))
    body = tk.Frame(root, bg=GREY)
    body.pack(fill="both", expand=True, padx=16, pady=12)
    canvas = tk.Canvas(body, bg=GREY, highlightthickness=0)
    bar = tk.Scrollbar(body, orient="vertical", command=canvas.yview)
    inner = tk.Frame(canvas, bg=GREY)
    inner.bind("<Configure>", lambda e: canvas.configure(scrollregion=canvas.bbox("all")))
    canvas.create_window((0, 0), window=inner, anchor="nw")
    canvas.configure(yscrollcommand=bar.set)
    canvas.pack(side="left", fill="both", expand=True)
    bar.pack(side="right", fill="y")
    canvas.bind_all("<MouseWheel>", lambda e: canvas.yview_scroll(int(-e.delta / 120), "units"))

    def launch(name):
        folder = os.path.join(HERE, name)
        if WIN:
            subprocess.Popen(["cmd", "/c", "start", f"Changeling {name[11:]}", "cmd", "/c", "run.bat"], cwd=folder)
        else:
            subprocess.Popen(["bash", "run.sh"], cwd=folder)

    for name in apps():
        here = os.path.isdir(os.path.join(HERE, name))
        card = tk.Frame(inner, bg=PANEL, highlightbackground=LINE, highlightthickness=1)
        card.pack(fill="x", pady=5, padx=4)
        text = tk.Frame(card, bg=PANEL)
        text.pack(side="left", fill="x", expand=True, padx=14, pady=10)
        tk.Label(text, text=name.replace("Changeling-", "").replace("-", " "), fg=TEXT, bg=PANEL,
                 font=("Segoe UI", 13, "bold")).pack(anchor="w")
        tk.Label(text, text=info.get(name, ""), fg=MUTED, bg=PANEL, font=("Segoe UI", 9), wraplength=470,
                 justify="left").pack(anchor="w")
        btn = tk.Button(card, text="Start" if here else "Not fetched", bg=DEEP if here else LINE, fg="white",
                        activebackground=PINK, activeforeground="white", relief="flat", bd=0, padx=18, pady=8,
                        font=("Segoe UI", 10, "bold"), cursor="hand2" if here else "arrow",
                        state="normal" if here else "disabled", command=lambda n=name: launch(n))
        btn.pack(side="right", padx=14)
        git = tk.Label(card, text="GitHub", fg="#f29cc0", bg=PANEL, cursor="hand2", font=("Segoe UI", 9))
        git.pack(side="right")
        git.bind("<Button-1>", lambda e, n=name: webbrowser.open(f"https://github.com/{OWNER}/{n}"))
    tk.Label(root, text="Each one opens in its own window. © Cursed Entertainment 2026", fg="#6f6f6f", bg=GREY,
             font=("Segoe UI", 9)).pack(pady=(0, 12))
    root.mainloop()


if __name__ == "__main__":
    steps = sys.argv[1:] or ["fetch", "setup", "models", "window"]
    for step in steps:
        {"fetch": fetch, "setup": setup, "models": models, "window": window}[step]()
