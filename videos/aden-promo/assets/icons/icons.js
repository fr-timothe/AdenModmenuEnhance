/* Item icons for the Items grid: original drawings, 48x48 viewBox, in the soft style of the
   game's inventory icons (rounded shapes, warm fills, a darker outline, one highlight).
   window.ADEN_ICONS[name] -> SVG markup. No game assets. */
(function () {
  const shade = (hex, k) => {
    const n = parseInt(hex.slice(1), 16);
    const f = (v) => Math.round(k < 0 ? v * (1 + k) : v + (255 - v) * k);
    return "#" + [n >> 16, (n >> 8) & 255, n & 255].map((v) => f(v).toString(16).padStart(2, "0")).join("");
  };
  const o = (f) => `fill="${f}" stroke="${shade(f, -0.38)}" stroke-width="1.7" stroke-linejoin="round"`;
  const R = (x, y, w, h, r, f) => `<rect x="${x}" y="${y}" width="${w}" height="${h}" rx="${r}" ${o(f)}/>`;
  const Ci = (cx, cy, r, f) => `<circle cx="${cx}" cy="${cy}" r="${r}" ${o(f)}/>`;
  const El = (cx, cy, rx, ry, f) => `<ellipse cx="${cx}" cy="${cy}" rx="${rx}" ry="${ry}" ${o(f)}/>`;
  const Pa = (d, f) => `<path d="${d}" ${o(f)}/>`;
  const Ln = (d, c, w = 1.6) => `<path d="${d}" fill="none" stroke="${c}" stroke-width="${w}" stroke-linecap="round" stroke-linejoin="round"/>`;
  const Dot = (cx, cy, r, c) => `<circle cx="${cx}" cy="${cy}" r="${r}" fill="${c}"/>`;
  const HL = (cx, cy, rx, ry) => `<ellipse cx="${cx}" cy="${cy}" rx="${rx}" ry="${ry}" fill="#fff" opacity="0.5"/>`;

  /* palette */
  const W1 = "#E6B676", W2 = "#C88F52", W3 = "#93603A";      /* light, mid, dark wood */
  const IW = "#6E4A33", IW2 = "#8C6345", MET = "#A9B3B8";   /* ironwood and its metal */
  const LIN = "#F7F1E1", RED = "#E8574F", BLU = "#5B8FD6", YEL = "#F2C94C", GRN = "#7DBF5A";

  const ring = (cx, cy, rx, ry, c) => `<ellipse cx="${cx}" cy="${cy}" rx="${rx}" ry="${ry}" fill="none" stroke="${c}" stroke-width="1.2"/>`;
  const log = (x, y, len, r, bark, end) =>
    R(x, y - r, len, 2 * r, r, bark) + El(x + len - r * 0.4, y, r * 0.72, r, end) +
    ring(x + len - r * 0.4, y, r * 0.36, r * 0.5, shade(end, -0.3)) + HL(x + len * 0.42, y - r * 0.5, len * 0.26, r * 0.18);
  const logEnd = (cx, cy, r, end) => Ci(cx, cy, r, end) + ring(cx, cy, r * 0.55, r * 0.55, shade(end, -0.3)) + Dot(cx, cy, 1.2, shade(end, -0.3));

  const I = {
    /* ---------------- WOOD search ---------------- */
    "wood": log(6, 32, 32, 7, "#B47E48", "#EECF98") + log(10, 18, 30, 7, "#B47E48", "#EECF98"),
    "softwood": log(5, 25, 34, 10, "#D9B07A", "#F6E4BC"),
    "hardwood": logEnd(15, 32, 9, "#C3905A") + logEnd(33, 32, 9, "#C3905A") + logEnd(24, 16, 9, "#C3905A") + HL(21, 11, 3, 1.5),
    "wooden bookshelf":
      R(9, 4, 30, 40, 3, W2) + R(12, 7, 24, 10, 1, W3) + R(12, 20, 24, 10, 1, W3) + R(12, 33, 24, 8, 1, W3) +
      R(13, 8, 4, 9, 1, RED) + R(17.5, 9, 4, 8, 1, BLU) + R(22, 8, 4, 9, 1, GRN) +
      R(14, 21, 4, 9, 1, YEL) + R(18.5, 22, 4, 8, 1, "#E07A9B") + R(27, 23, 8, 7, 1, LIN) + R(14, 34, 10, 7, 1, BLU),
    "wooden chair": R(14, 5, 20, 19, 4, W2) + Ln("M19 9 L19 21 M24 9 L24 21 M29 9 L29 21", W3) + R(10, 23, 28, 7, 3, W1) +
      R(12, 29, 5, 14, 2, W3) + R(31, 29, 5, 14, 2, W3) + HL(18, 25, 5, 1.3),
    "wooden chest": R(8, 7, 32, 35, 4, W2) + R(11, 11, 26, 8, 2, W1) + R(11, 21, 26, 8, 2, W1) + R(11, 31, 26, 8, 2, W1) +
      Dot(24, 15, 1.8, W3) + Dot(24, 25, 1.8, W3) + Dot(24, 35, 1.8, W3) + R(10, 41, 5, 4, 1, W3) + R(33, 41, 5, 4, 1, W3),
    "wooden double bed": R(5, 9, 38, 16, 5, W2) + R(5, 21, 38, 15, 4, LIN) + R(5, 28, 38, 10, 4, "#86B8DB") +
      El(15, 23, 7, 3.4, "#FFFFFF") + El(33, 23, 7, 3.4, "#FFFFFF") + R(6, 37, 5, 6, 2, W3) + R(37, 37, 5, 6, 2, W3) + HL(14, 31, 5, 1.4),
    "wooden end table": R(9, 13, 30, 7, 3, W1) + R(12, 19, 24, 14, 2, W2) + R(15, 22, 18, 7, 2, W1) + Dot(24, 25.5, 1.7, W3) +
      R(12, 32, 5, 11, 2, W3) + R(31, 32, 5, 11, 2, W3) + HL(19, 15, 6, 1.3),
    "wooden full-length mirror": R(12, 3, 24, 38, 11, W2) + R(16, 7, 16, 30, 8, "#C4E6F0") +
      Ln("M20 14 L25 9 M20 20 L29 11", "#FFFFFF", 2) + R(10, 40, 10, 4, 2, W3) + R(28, 40, 10, 4, 2, W3),
    "wooden mini table": R(21, 22, 6, 18, 2, W3) + El(24, 40, 10, 3, W3) + El(24, 20, 18, 7, W1) + HL(18, 18, 7, 1.8),
    "wooden simple bed": R(5, 11, 7, 30, 3, W2) + R(37, 19, 6, 22, 3, W2) + R(5, 29, 38, 8, 3, W2) + R(10, 21, 29, 9, 3, LIN) +
      R(19, 20, 20, 10, 3, "#E8A0A0") + El(15, 22, 4.5, 3, "#FFFFFF") + HL(26, 22.5, 6, 1.3),
    "wooden table": R(4, 15, 40, 7, 3, W1) + R(7, 21, 34, 4, 1, W2) + R(8, 24, 5, 18, 2, W3) + R(35, 24, 5, 18, 2, W3) + HL(14, 17, 8, 1.4),
    "wooden wardrobe": R(9, 6, 30, 37, 4, W2) + R(7, 3, 34, 6, 2, W3) + R(12, 10, 11, 30, 2, W1) + R(25, 10, 11, 30, 2, W1) +
      Dot(21, 25, 1.7, W3) + Dot(27, 25, 1.7, W3) + HL(16, 15, 2, 4),
    "wooden waste bin": Pa("M11 13 L37 13 L34 43 L14 43 Z", W2) + Ln("M18 16 L19 40 M24 16 L24 40 M30 16 L29 40", W3, 1.3) +
      El(24, 13, 13, 3.6, W1) + El(24, 13, 9.5, 2.2, W3),
    "wooden-block toy": R(5, 28, 17, 15, 2, RED) + R(26, 28, 17, 15, 2, GRN) + R(13, 15, 22, 13, 2, BLU) +
      Pa("M15 15 L24 4 L33 15 Z", YEL) + HL(11, 31, 3, 1.4) + HL(19, 18, 4, 1.4),
    "wooden-block bed": R(9, 23, 31, 11, 3, YEL) + R(11, 18, 27, 8, 3, LIN) + El(17, 19, 4.5, 2.8, "#FFFFFF") +
      R(4, 13, 7, 29, 2, RED) + R(37, 18, 7, 24, 2, BLU) + Ci(7.5, 10, 3.6, GRN) + Pa("M37 18 L40.5 12 L44 18 Z", YEL),
    "wooden-block chair": R(12, 6, 24, 17, 2, RED) + R(9, 22, 30, 8, 2, BLU) + R(10, 30, 7, 13, 2, YEL) + R(31, 30, 7, 13, 2, GRN) +
      HL(18, 10, 4, 1.5) + Ci(24, 14, 3.2, YEL),
    "ironwood bed": R(5, 8, 38, 16, 3, IW) + R(5, 13, 38, 3.5, 1, MET) + R(5, 21, 38, 14, 3, LIN) + R(5, 28, 38, 9, 3, "#CDB98E") +
      El(15, 23, 6.5, 3, "#FFFFFF") + El(33, 23, 6.5, 3, "#FFFFFF") + R(6, 36, 5, 7, 1.5, MET) + R(37, 36, 5, 7, 1.5, MET),
    "ironwood chair": R(13, 5, 22, 19, 3, IW) + R(13, 11, 22, 3.5, 1, MET) + R(10, 23, 28, 7, 2, IW2) +
      R(12, 29, 4, 14, 1.5, MET) + R(32, 29, 4, 14, 1.5, MET) + Ln("M14 37 L34 37", shade(MET, -0.35), 1.4) + HL(18, 25, 5, 1.2),
    "ironwood dresser": R(5, 7, 38, 6, 2, "#5A3A26") + R(7, 12, 34, 30, 3, IW) + R(10, 15, 13, 11, 2, IW2) + R(25, 15, 13, 11, 2, IW2) +
      R(10, 28, 28, 11, 2, IW2) + R(14, 19.5, 5, 2, 1, MET) + R(29, 19.5, 5, 2, 1, MET) + R(20, 32.5, 8, 2, 1, MET) + R(8, 41, 5, 4, 1, MET) + R(35, 41, 5, 4, 1, MET),

    /* ---------------- every item, page 1 ---------------- */
    "acoustic guitar": R(22, 2, 5, 22, 2, W3) + R(20.5, 1, 8, 6, 2, "#5E3B24") + Ci(24.5, 33, 11, "#E3A04A") + Ci(24.5, 21, 8, "#E3A04A") +
      Ci(24.5, 27, 3.4, "#5B4A3A") + R(20, 36, 9, 3, 1, W3) + HL(19, 30, 2.5, 4),
    "aloha shirt": Pa("M11 9 L19 5 L24 9 L29 5 L37 9 L44 18 L37 22 L36 43 L12 43 L11 22 L4 18 Z", RED) +
      Ln("M19 5 L24 13 L29 5", shade(RED, -0.38)) + Dot(16, 25, 2.6, "#FFFFFF") + Dot(31, 31, 2.6, "#FFFFFF") + Dot(21, 37, 2.6, "#FFFFFF") +
      Dot(9, 15, 2, "#FFFFFF") + Dot(16, 25, 1, YEL) + Dot(31, 31, 1, YEL) + Dot(21, 37, 1, YEL),
    "apple": Pa("M24 14 C14 8 6 16 8 27 C10 38 18 43 24 40 C30 43 38 38 40 27 C42 16 34 8 24 14 Z", RED) +
      Ln("M24 14 L25 6", "#7A4E31", 2.4) + Pa("M25.5 9 C29 4 35 4.5 37 6.5 C34 10 29 11 25.5 9 Z", GRN) + HL(15, 22, 3.5, 6),
    "iron nugget": Pa("M7 32 L11 18 L22 10 L35 12 L42 24 L39 37 L25 42 L12 40 Z", "#9EA7AC") +
      Ln("M11 18 L22 22 L35 12 M22 22 L25 42 M22 22 L42 24", "#C6CDD1", 1.4) + HL(19, 16, 4, 2),
    "anchor statue": `<circle cx="24" cy="8" r="4.5" fill="none" stroke="#56636E" stroke-width="3.6"/>` + R(21.5, 11, 5, 28, 2, "#71808C") +
      R(13, 15, 22, 4.5, 2, "#71808C") + Pa("M7 28 C8 40 19 43 24 43 C29 43 40 40 41 28 L36 29 C35 36 29 38.5 24 38.5 C19 38.5 13 36 12 29 Z", "#71808C") +
      Pa("M5 30 L9 24 L13 31 Z", "#71808C") + Pa("M43 30 L39 24 L35 31 Z", "#71808C"),
    "sea bass": Pa("M36 24 L45 15 L43 24 L45 33 Z", "#7F96A8") + Pa("M15 16 L22 9 L28 16 Z", "#7F96A8") + El(22, 25, 16, 9.5, "#8FA6B8") +
      El(22, 29, 11, 4, "#C9D6DF") + Dot(11, 23, 2.4, "#FFFFFF") + Dot(10.6, 23, 1.2, "#2E3A44") + Ln("M16 20 C18 24 18 27 16 31", "#6B8294", 1.2),
    "frog chair": R(8, 18, 32, 24, 11, GRN) + El(24, 33, 10, 6, "#CDE8A8") + Ci(15, 15, 5.5, GRN) + Ci(33, 15, 5.5, GRN) +
      Dot(15, 14.5, 2.8, "#FFFFFF") + Dot(33, 14.5, 2.8, "#FFFFFF") + Dot(15.4, 14.8, 1.4, "#2E3A2E") + Dot(33.4, 14.8, 1.4, "#2E3A2E") +
      Ln("M18 25 C21 28 27 28 30 25", "#3F7A33", 1.6),
    "bamboo piece": R(13, 4, 9, 40, 4, "#8CC152") + R(26, 9, 9, 35, 4, "#76AE45") + Ln("M13 16 L22 16 M13 29 L22 29 M26 21 L35 21 M26 33 L35 33", "#4F8A2D", 1.5) +
      Pa("M35 15 C40 10 44 10 45 11 C42 15 39 16 35 15 Z", GRN) + HL(16, 10, 1.2, 4),
    "kimono": Pa("M9 8 L20 6 L24 12 L28 6 L39 8 L45 25 L36 25 L36 43 L12 43 L12 25 L3 25 Z", "#E9A3C9") +
      R(12, 24, 24, 6, 1, "#7A5BA6") + Ln("M20 6 L27 24 M28 6 L22 15", "#FFFFFF", 1.8) + Dot(17, 36, 2, "#FFFFFF") + Dot(30, 35, 2, "#FFFFFF") + Dot(8, 18, 1.6, "#FFFFFF"),
    "red tulips": Ln("M14 22 L17 43 M24 16 L24 43 M34 22 L31 43", "#4F8A2D", 2.2) + Pa("M24 40 C18 34 15 30 14 26 C19 28 22 33 24 40 Z", GRN) +
      Pa("M24 40 C30 34 33 30 34 26 C29 28 26 33 24 40 Z", GRN) +
      [[14, 18], [24, 12], [34, 18]].map(([x, y]) => Pa(`M${x - 6} ${y} C${x - 6} ${y - 8} ${x - 3} ${y - 10} ${x - 3} ${y - 10} L${x} ${y - 6} L${x + 3} ${y - 10} C${x + 3} ${y - 10} ${x + 6} ${y - 8} ${x + 6} ${y} C${x + 6} ${y + 6} ${x - 6} ${y + 6} ${x - 6} ${y} Z`, RED)).join(""),
    "cherry": Ln("M15 24 C17 15 21 10 27 6 M33 22 C32 14 30 10 27 6", "#5E7A2E", 1.8) + Pa("M27 6 C31 2 37 3 39 5 C35 8 31 8 27 6 Z", GRN) +
      Ci(15, 32, 9, "#D9363E") + Ci(33, 30, 9, "#D9363E") + HL(11.5, 29, 2.2, 3.4) + HL(29.5, 27, 2.2, 3.4),
    "clay": Pa("M7 34 C5 24 13 18 20 19 C24 12 34 12 38 20 C44 24 43 36 36 39 C28 43 14 42 7 34 Z", "#D28C5C") +
      Ln("M15 30 C19 27 24 28 27 31", "#B0703F", 1.4) + HL(27, 19, 5, 2.2),
    "festive tree": R(21, 35, 6, 8, 1, W3) + Pa("M24 7 L36 21 L30 21 L41 35 L7 35 L18 21 L12 21 Z", "#4E9B57") +
      Dot(19, 28, 2.2, RED) + Dot(30, 30, 2.2, YEL) + Dot(25, 19, 2.2, BLU) + Dot(16, 33, 1.8, YEL) + Dot(34, 25, 1.8, RED) +
      Pa("M24 1.5 L25.8 5.2 L29.6 5.6 L26.7 8.1 L27.6 12 L24 10 L20.4 12 L21.3 8.1 L18.4 5.6 L22.2 5.2 Z", YEL),
    "royal crown": Pa("M8 34 L6 14 L16 23 L24 9 L32 23 L42 14 L40 34 Z", YEL) + R(8, 31, 32, 9, 2, "#E0A93A") +
      Dot(24, 35.5, 2.8, RED) + Dot(15, 35.5, 2.1, BLU) + Dot(33, 35.5, 2.1, BLU) + Ci(6, 13, 2.6, YEL) + Ci(24, 8, 2.6, YEL) + Ci(42, 13, 2.6, YEL) + HL(17, 26, 3, 1.4),
    "star fragment": Pa("M24 5 L29 18 L43 18.5 L32 27 L36 41 L24 33 L12 41 L16 27 L5 18.5 L19 18 Z", "#F6D24B") +
      Pa("M24 14 L26.5 21 L33 21.5 L28 25.5 L29.6 32 L24 28.5 L18.4 32 L20 25.5 L15 21.5 L21.5 21 Z", "#FBE58A") + HL(20, 16, 2, 1.2),
    "peach": Pa("M24 12 C12 10 6 20 8 30 C10 40 20 43 24 42 C28 43 38 40 40 30 C42 20 36 10 24 12 Z", "#F7A99A") +
      Ln("M24 13 C20 22 21 34 24 42", "#E07A6E", 1.4) + Pa("M24.5 12 C27 6 33 5 36 6 C34 10 29 12 24.5 12 Z", GRN) + HL(15, 21, 3.4, 5),
    "gold nugget": Pa("M6 30 L12 17 L25 12 L38 16 L43 28 L36 39 L20 41 L9 38 Z", YEL) +
      Ln("M12 17 L23 24 L38 16 M23 24 L20 41 M23 24 L43 28", "#FBE7A1", 1.4) + HL(20, 17, 4, 2),
    "sailor's tee": Pa("M11 9 L18 5 L30 5 L37 9 L44 18 L37 22 L36 43 L12 43 L11 22 L4 18 Z", "#FFFFFF") +
      Pa("M17 5.5 L24 17 L31 5.5 Z", "#2F4A7A") + Ln("M6 16 L10 13 M38 13 L42 16", "#2F4A7A", 2) + Pa("M21 17 L24 23 L27 17 Z", RED),
    "Bunny Day bag": Ln("M15 18 C15 7 33 7 33 18", "#C9A86A", 2.6) + R(9, 17, 30, 26, 6, "#F7E3A1") +
      El(17, 30, 4, 5.5, "#E9A3C9") + El(25, 33, 4, 5.5, "#A9CDEB") + El(32, 28, 3.6, 5, "#B6DE96") + HL(15, 21, 5, 1.4),
  };
  window.ADEN_ICONS = {};
  for (const k in I) window.ADEN_ICONS[k] = `<svg viewBox="0 0 48 48" width="46" height="46" xmlns="http://www.w3.org/2000/svg">${I[k]}</svg>`;
})();
