// family.js - what every Changeling's site shares: the family list (one place to add a Changeling: here)
// and the sample sheet opening full size. Linked from Changeling Studio's Pages by every Changeling.
//
// A page puts <section id="family"></section> where the family goes and <body data-changeling="Chronos">
// to say which one it is (that card is marked, not linked).
(function () {
  var FAMILY = [
    ["Studio", "Changeling-Studio", "Every Changeling in one program, chains included."],
    ["CopyCat", "Changeling-CopyCat", "One face onto any photo or video."],
    ["CopyCat Realtime", "Changeling-CopyCat-Realtime", "The face swap, live on the webcam, with voice and recording."],
    ["Tiresias", "Changeling-Tiresias", "Gender swap for photos."],
    ["Chronos", "Changeling-Chronos", "Age a face forwards or backwards."],
    ["Cosmetics", "Changeling-Cosmetics", "Makeup, hair colour and length, beards."]
  ];

  var slot = document.getElementById("family");
  if (slot && !slot.dataset.filled) {
    var here = document.body.getAttribute("data-changeling");
    slot.dataset.filled = "1";
    var h = document.createElement("h2");
    h.textContent = "The Changeling family";
    var p = document.createElement("p");
    p.innerHTML = 'All of them together: <a href="https://github.com/CursedPrograms/Changeling-Studio">Changeling Studio</a>.';
    var grid = document.createElement("div");
    grid.className = "grid";
    FAMILY.forEach(function (f) {
      var card = document.createElement("div");
      card.className = "card" + (f[0] === here ? " here" : "");
      var title = document.createElement("h3");
      if (f[0] === here) {
        title.textContent = f[0] + " (this one)";
      } else {
        var a = document.createElement("a");
        a.href = "https://cursedprograms.github.io/" + f[1] + "/";
        a.textContent = f[0];
        title.appendChild(a);
      }
      var what = document.createElement("p");
      what.className = "what";
      what.textContent = f[2];
      card.appendChild(title);
      card.appendChild(what);
      grid.appendChild(card);
    });
    slot.appendChild(h);
    slot.appendChild(p);
    slot.appendChild(grid);
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
