# Campus Combat — Unreal Engine 5.8 game

Mobile-first third-person shooter set on a college campus (Android, iOS, Windows).

- **Engine:** Unreal Engine 5.8 (C++ + Blueprint). Open `CollegeCampusGame.uproject`.
- **Character:** orange/white UEFN mannequin, lightweight native animation (`Source/CollegeCampusGame/Campus/`, clips in `Content/CampusAnims`).
- **Backend:** separate repo — https://github.com/shankar379/campuscombat-backend
- **Test server:** this PC on the shared Wi-Fi (`Config/DefaultGame.ini` → `CampusNetSettings`: host, API port 8080, game port 7777). No Firebase.

## Clone on a new machine
```
git lfs install
git clone https://github.com/shankar379/collegegame_unreal_engine_5.git
```
Then open the `.uproject` and let it compile. `Binaries/`, `Intermediate/`, `Saved/`, `DerivedDataCache/` are rebuilt automatically.
All assets (`.uasset`, `.umap`, models, textures, audio) are stored with **Git LFS**.

The previous (2025) prototype is kept in this repo's history (commits before the Campus Combat project).

## Credits
Third-party assets and the credit their licences require: [CREDITS.md](CREDITS.md).
