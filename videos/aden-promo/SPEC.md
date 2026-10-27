# Aden Modmenu promo: spec

GOAL:    a player who has never seen the mod understands in one viewing that it is a menu inside
         the game itself, and has seen each of its five tabs do one real thing.
OUTPUT:  HyperFrames, 1920 x 1080, 30 fps, silent (README hero, autoplaying WebP).
TIME:    34.4 s x 30 = 1032 frames. No music, so no beat grid.

LOOK:    the medium is the game's own UI, rebuilt from src/menu.c.
         - Game space 1280 x 720 scaled x1.5 into the frame; the panel sits where the game
           draws it (x 15..585, y 10..710 in 720p), i.e. 22..878 px of the frame.
         - Panel #F7F2E2, radius 34; header #5DC2AB 100 px; selection pill #FBDD75;
           ink #5B4A3A; dim #A39380; status/warn #D0503F; dark teal #3E9C88; white tab pill.
         - Debug-font look: Source Code Pro 600, 8 px advance per column, rows 20 px from
           (30, 116), exactly the GX/GY grid of the code. Scale 1.6 and 2.0 for "big" prints.
         - Ground: the island behind the phone, out of focus: grass #8CCB67, river #6FCFD6,
           sand #F1E0AE, sea #58C3CC, tree crowns #4E9B57, blurred 10 px, cream wash 18 %.
         - Captions are ACNH dialogue bubbles: #FFF8E1 blob, Nunito 800 46 px #5B4A3A,
           a tilted name-tag pill (Nunito 900 34 px) in a colour taken from the menu's own
           palettes (FAMCOL, VCOL, BADGE), flat 8 px offset shadow at 18 % brown, no blur.
OBJECTS: the Nook Phone panel (header, tab row, the tab's real widgets), the A logo
         (assets/A_logo_1024.png), the dialogue bubble.
MOTION:  UI acts like the game: state changes are instant cuts (it redraws every frame);
         pills glide 0.12 s power2.out so the eye can follow the D-pad.
         Camera push 1.30-1.32x, 0.5 s power2.inOut, held while the UI acts, pulled back
         0.45 s before each tab change so the tab row is on screen when L/R is pressed.
         Bubble text types in like ACNH dialogue (0.022 s a character). Tag pops with
         back.out(1.6) (position/scale only); bubble exits blur 6 px + fade in 0.2 s.
SHOTS:   0.0-3.2   logo sting: A lands (back.out), wordmark per letter; the A carries into
                   the first bubble's name tag while the phone panel slides in from the left.
         3.2-6.2   "In-game mod menu for Animal Crossing: New Horizons", Items tab at rest.
         6.2-12.2  Items: Y, the key pill types WOOD (86 found), B, grid of 20, pill walks to
                   hardwood, amount 1 -> 40, A: "Added 40 x hardwood to your pockets".
         12.2-17.6 Player: currency cards, A opens the digits, X rolls Bells 12,480 -> 99,999.
         17.6-23.0 Island: X sets the sell ticket to 660 and the week's bars level up; down the
                   villager list to Marshal, A: friendship 87 -> 255, level dots fill.
         23.0-28.0 Mods: A flips the Stack 99 switch on; a folder mod card switches on and asks
                   for a restart.
         28.0-30.6 About: version 0.6.0-beta.14, then the Credits page.
         30.6-34.4 the panel grows into the whole frame (object becomes the world); end card:
                   A + "Aden Modmenu", "For the Eden emulator, ACNH 3.0.2", GitHub repo.
BANNED:  dark void ground, glow, neon, purple/blue gradients, glass cards, three cards in a
         row, UI tilted in 3D, invented buttons or labels not in menu.c, Nintendo assets,
         breathing/floating loops, titles that drift after landing, click rings.
QA:      t = 1.5, 4.5, 8.0, 10.6, 15.2, 21.5, 25.4, 29.6, 33.5: every label matches menu.c,
         nothing clipped by the panel edge or the bubble, bubble inside title-safe.
