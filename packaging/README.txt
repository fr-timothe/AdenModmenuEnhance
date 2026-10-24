Aden Modmenu v@VERSION@ - Animal Crossing: New Horizons (Eden emulator)
=================================================================

GAME VERSION
- Built and tested on 3.0.2.
- 3.0.3 and later: the mod finds its hook points by itself at startup (beta, untested).
  If it can't, it switches itself off and the game runs normally. Please share
  sdmc/aden/log.txt if that happens.

INSTALL
1. Open the Eden user folder: in Eden, File > Open Eden Folder.
   It is usually %APPDATA%\eden (paste that into the Explorer address bar).
   A portable Eden uses the "user" folder next to eden.exe.
2. Drag the "load" and "sdmc" folders from this zip into it.
   Merge them with the existing folders when Windows asks.
3. Start the game. The "Aden Modmenu" app appears on the Nook Phone.

UPDATING
Drag the folders again and replace the files. Mods you had in sdmc/aden/mods stay as they are.
If an older version sits in a folder with another name than AdenModmenu
(e.g. load\01006F8002326000\AdenModmenu_v0.5.3), delete that folder first.

CONTROLS (menu open)
  L / R            switch tab (Items, Player, Island, Mods, About)
  Up / Down        move
  B                close the menu
  Items:  arrows move in the grid, ZL/ZR item family, -/+ amount, Y search, X pockets or storage, A give
          Items from DLC you do not own are not listed.
  Player: Minus searches your player data again (after switching resident)
  Player/Island: Left/Right and ZL/ZR change the value, X max, Y zero, A on actions

MOD MANAGER
- Put romfs mods in sdmc/aden/mods/<ModName>/romfs/...
- A turns a mod on/off; restart the game to apply. Y rescans the folder.
- Code mods (exefs / .pchtxt / .ips) can't be loaded in-game: turn those on in Eden.

NOTES
- Back up your save before big edits (Eden: right-click game > Open Save Data Location).
- Log file: %APPDATA%\eden\sdmc\aden\log.txt

TROUBLESHOOTING
  The app isn't on my Nook Phone
    - Right-click the game > Properties (or Configure) > Add-Ons and make sure
      "AdenModmenu" is ticked.
    - These two files must exist:
        %APPDATA%\eden\load\01006F8002326000\AdenModmenu\exefs\subsdk9
        %APPDATA%\eden\sdmc\aden\items.txt
      A common mistake is an extra folder level, e.g. ...\load\load\...
    - If you see "Room Sketch" with its normal icon instead of "Aden Modmenu", the mod IS
      running but the aden folder isn't in sdmc. Tap Room Sketch: the menu opens anyway.
  "Player data not found"
    - Press A in the Items tab (or Minus in the Player tab) to search again. If it keeps failing,
      share the log.
  Still stuck?
    Post sdmc/aden/log.txt (Eden: %APPDATA%\eden\sdmc\aden\log.txt) with your report.

CREDITS
Save data layout, recipe list and villager names from NHSE by kwsch.
