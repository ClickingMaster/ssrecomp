# Port data

This folder is the staging area for the SD card. It is deliberately empty of
game content:

- Put your built `ssrecomp` here (ignored by git).
- Put your own `soulsilver.nds` here if you want `deploy_to_sd.sh` to copy it
  (ignored by git), or pass `SSR_ROM=/path/to/rom` instead.
- For the optional Spanish entry, supply `soulsilver_es.nds` here or use
  `SSR_ROM_ES=/path/to/your/spanish.nds`. It uses `soulsilver_es.sav`.
- `ssrecomp.cfg` and `gamecontrollerdb.txt` are the tracked templates.

Nothing in this folder except the two templates should be committed.
