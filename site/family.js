// family.js - what every Changeling's site shares: the family (one place to add a Changeling: here) and the
// sample sheet opening full size. Linked from Changeling Studio's Pages by every Changeling.
//
// A page puts <section id="family"></section> where the family goes and <body data-changeling="Chronos">
// to say which one it is (that card is marked, not linked). <section id="family" data-chapters> (Studio's
// page) shows each one as a chapter with its own samples, full width.
(function () {
  var PAGES = "https://cursedprograms.github.io/";
  // name, repo, what it does, its samples (on its own Pages site)
  var FAMILY = [
    ["CopyCat", "Changeling-CopyCat", "One face onto any photo or video, with a completion bar and GFPGAN clean-up.",
     "samples/changeling_sample.jpg"],
    ["CopyCat Realtime", "Changeling-CopyCat-Realtime", "The face swap live on the webcam for video calls, with the persona's skin, a female/male voice (or Echo's) and recording.",
     "samples/persona_samples.jpg"],
    ["Tiresias", "Changeling-Tiresias", "Gender swap for photos: it reads the face, picks the other gender, and learns the face so it still looks like you.",
     "assets/tiresias_samples.jpg"],
    ["Chronos", "Changeling-Chronos", "Age a face forwards or backwards, or a five-age timeline, holding the face's gender.",
     "assets/chronos_samples.jpg"],
    ["Natio", "Changeling-Natio", "Your face as it might look born somewhere else in the world.",
     "assets/natio_samples.jpg"],
    ["Cosmeta", "Changeling-Cosmeta", "Makeup, hair colour and length, and beards: colour, stubble, shave.",
     "assets/cosmetics_samples.jpg"],
    ["Echo", "Changeling-Echo", "Train the voices the Changelings speak with (RVC, from Piper or your own recordings), and speak in them live.",
     "assets/window.png"],
    ["Tonsor", "Changeling-Tonsor", "Anyone's hairstyle on you: the cut from one photo, the colour from another (HairFastGAN).",
     "assets/tonsor_samples.jpg"],
    ["Tiresias Realtime", "Changeling-Tiresias-Realtime", "The other gender live on a call: one snapshot, swapped by Tiresias, worn live by CopyCat Realtime.",
     "assets/realtime_samples.jpg"],
    ["Chronos Realtime", "Changeling-Chronos-Realtime", "Younger or older live on a call: one snapshot, aged by Chronos, worn live by CopyCat Realtime.",
     "assets/realtime_samples.jpg"],
    ["Natio Realtime", "Changeling-Natio-Realtime", "Another ancestry live on a call: one snapshot, changed by Natio, worn live by CopyCat Realtime.",
     "assets/realtime_samples.jpg"],
    ["Cosmeta Realtime", "Changeling-Cosmeta-Realtime", "Makeup, hair colour and beards live on a call: BiSeNet on the GPU, Cosmeta's own looks, every frame.",
     "assets/realtime_samples.jpg"],
    ["Studio", "Changeling-Studio", "Every Changeling in one place; one window and chains coming.", "site/logo.png"]
  ];

  function el(tag, cls, text) {
    var e = document.createElement(tag);
    if (cls) e.className = cls;
    if (text) e.textContent = text;
    return e;
  }

  function link(f, text) {
    var a = el("a", null, text || f[0]);
    a.href = PAGES + f[1] + "/";
    return a;
  }

  function picture(f, cls) {
    var img = el("img", cls);
    img.src = PAGES + f[1] + "/" + f[3];
    img.alt = f[0] + ": samples";
    img.loading = "lazy";
    img.onerror = function () { img.remove(); };        // not published yet: no picture
    return img;
  }

  var slot = document.getElementById("family");
  if (slot && !slot.dataset.filled) {
    slot.dataset.filled = "1";
    var here = document.body.getAttribute("data-changeling");
    var chapters = slot.hasAttribute("data-chapters");
    slot.appendChild(el("h2", null, chapters ? "The Changelings" : "The Changeling family"));
    if (!chapters) {
      var p = el("p");
      p.innerHTML = 'All of them together: <a href="https://github.com/CursedPrograms/Changeling-Studio">Changeling Studio</a>.';
      slot.appendChild(p);
    }
    var holder = el("div", chapters ? "chapters" : "grid");
    FAMILY.forEach(function (f) {
      if (chapters && f[0] === here) return;           // Studio's own page: the others, as chapters
      if (chapters) {
        var ch = el("article", "chapter");
        var title = el("h3");
        title.appendChild(link(f, "Changeling " + f[0]));
        ch.appendChild(title);
        ch.appendChild(el("p", null, f[2]));
        var a = link(f, "");
        a.appendChild(picture(f, "chapter-sample"));
        ch.appendChild(a);
        var more = el("p", "chapter-links");
        more.appendChild(link(f, "Its site"));
        more.appendChild(document.createTextNode("  ·  "));
        var gh = el("a", null, "GitHub");
        gh.href = "https://github.com/CursedPrograms/" + f[1];
        more.appendChild(gh);
        ch.appendChild(more);
        holder.appendChild(ch);
        return;
      }
      var card = el("div", "card" + (f[0] === here ? " here" : ""));
      if (f[0] !== here) card.appendChild(picture(f, "card-sample"));
      var h = el("h3");
      if (f[0] === here) h.textContent = f[0] + " (this one)"; else h.appendChild(link(f));
      card.appendChild(h);
      card.appendChild(el("p", "what", f[2]));
      holder.appendChild(card);
    });
    slot.appendChild(holder);
  }

  // the sample sheet opens full size (click, or Esc / click again to close)
  var sheet = document.getElementById("sheet");
  var box = document.getElementById("lightbox");
  if (sheet && box) {
    var img = box.querySelector("img");
    var close = function () { box.hidden = true; img.removeAttribute("src"); };
    sheet.addEventListener("click", function () { img.src = sheet.src; img.alt = sheet.alt; box.hidden = false; });
    box.addEventListener("click", close);
    document.addEventListener("keydown", function (e) { if (e.key === "Escape" && !box.hidden) close(); });
  }
})();
