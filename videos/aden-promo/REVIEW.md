# Review

- Creative calls: one continuous take of the real panel (rebuilt from src/menu.c at x1.5), tabs
  changing as the game does (instant redraw), camera pushes of 1.30-1.32x on the widget in use,
  captions as ACNH dialogue bubbles, logo carried into the first bubble's name tag, end card made
  by the panel growing into the whole frame. Silent on purpose (README autoplay).
- Measured: `hyperframes check` passes with 0 errors (lint, runtime, layout, motion, contrast).
  Looked at: snapshots of every scene and the decoded MP4 stills.
- Kept warnings:
  - contrast_aa_failure (33): the menu's real colours (dim #A39380 text on #F7F2E2, tab names
    #E6FFF8 on teal). Changing them would misrepresent the UI.
  - text_below_floor: the panel text is the game's 8 px debug font x1.5 (21 px). The pushes bring
    it to about 27 px; the captions carry the story at 46 px.
  - line_unread: false positives from the per-character typewriter spans in the bubbles.
  - first_frame_empty: the logo is on screen at 0 s (a background-image div the check does not
    count as a key image).
  - film_silent: there's no audio, as asked.
  - nested_structure / file_too_large: it's a single file like the BetterAimaira reference,
    because the logo carry and the camera cross scene boundaries.
- Weakest shot: About (2.6 s): it's short, and nothing changes in it but the page.
- Invented content: item IDs, the player and villager names, the mod folder names and the
  counts are plausible sample data, not taken from a save.
