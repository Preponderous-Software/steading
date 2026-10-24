steading
========

A small colony sim: a settlement of settlers gathers wood, stone and
berries, builds huts and grows through summers and winters. Win by
surviving 5 winters or reaching 60 settlers. Built on the Strata engine,
with OpenUSD as the world, save and mod format.

Play
----

Windows:  double-click steading.bat (not steading.exe: the launcher points
          the game at its content and libraries). Windows may say it
          protected your PC, because the game is not signed: choose
          "More info", then "Run anyway".
Linux:    ./steading.sh   (needs OpenGL 4.5, which any current Mesa or
          vendor driver has, and glibc 2.39 or newer: Ubuntu 24.04,
          Debian 13, Fedora 40 and later)

Press H in the game to print the controls. Space pauses, +/- change the
speed, left-drag designates tiles for gathering, right-click plans a hut,
Tab switches to the free-fly camera, F5 saves, F9 reloads the last save,
Esc quits (it saves first).

Saves
-----

The game autosaves every 500 ticks and when you quit, into a slot
directory: saves/default under the directory you start it from, unless
you pass another one.

  steading --slot DIR        keep the save somewhere else
  steading --new SEED        start a new colony in an empty slot
  steading --mods frost_huts load mods (they live in mods/; a save
                             remembers its mods and refuses a different list)

A save is checksummed, a damaged one is refused, and the previous save is
always kept and loaded instead.

What is in this folder
----------------------

steading.sh / steading.bat   the launcher
bin/steading, steading.exe   the game
content/, mods/              the game's content and the two demo mods
lib/, bin/, plugin/          OpenUSD and oneTBB (shared libraries)
licenses/, THIRD-PARTY.md    licences of everything above
