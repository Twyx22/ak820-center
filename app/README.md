# AK820 Center — app Linux native pour le clavier AJAZZ AK820 Pro

Réimplémentation open-source, compilée nativement pour Linux, de l'app
Windows officielle AJAZZ AK820 Pro (V1.0.0.5, décompilée — voir
[`../PROTOCOL.md`](../PROTOCOL.md)).

## Fonctionnalités

| Onglet | Contenu |
|---|---|
| Éclairage | 20 effets embarqués + couleur + arc-en-ciel, luminosité/vitesse/direction (0-5) |
| Touches | Remap couche **base** et couche **Fn** : défaut, désactivée, clavier+modificateurs, multimédia, souris, macro |
| RGB personnalisé | Peinture touche par touche (144 LED), lecture/écriture de la banque |
| Macros | Jusqu'à 100 macros (index 400 o + blocs, budget 3072 o firmware) |
| Écran TFT | Import GIF/images → RGB565 128×128, chunks 4096 o, slots 1-3 |
| Réglages | Veille (jamais/1/5/30 min), synchro horloge, batterie |

## Compilation

```bash
sudo pacman -S --needed qt6-base hidapi cmake gcc
cmake -S app -B app/build -DCMAKE_BUILD_TYPE=Release
cmake --build app/build -j$(nproc)
```

## Installation manuelle

```bash
sudo install -Dm755 app/build/ak820-center /usr/local/bin/ak820-center
sudo install -Dm644 extracted/app/layouts/rgb-keyboard.xml /usr/share/ak820-center/rgb-keyboard.xml
sudo install -Dm644 extracted/app/skins/theme1/logo/logo.png /usr/share/ak820-center/icon.png
sudo install -Dm644 app/packaging/ak820-center.desktop /usr/share/applications/
# permissions HID (obligatoire) :
sudo cp app/packaging/99-ajazz-ak820pro.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules && sudo udevadm trigger
# puis : reconnexion de session OU usermod -aG input $USER
```

## Paquet Arch (PKGBUILD)

```bash
cd app/packaging && makepkg -si
```

## Outil CLI de test

`tools/ak820ctl` (voir sources) : `probe`, `battery [dongle]`, `clock`,
`sleep 0-3`, `light <mode> <rrggbb> [bright] [speed] [dir] [rainbow]`,
`readrgb 3|9`.

## Protocole

Uniquement le mode **filaire** (USB-C) : interface vendor usage page `0xFF13`,
rapports feature 65 octets, transactions `START 04 18 → cmd → COMMIT 04 02 →
FINISH 04 F0` avec 35 ms entre écritures. Détails complets dans
[`../PROTOCOL.md`](../PROTOCOL.md). Le clavier ne fournit pas la lecture du
keymap/macros : l'état est persisté côté application
(`~/.config/ak820-center/state.json`), comme le fait l'app officielle avec
son `config.xml`.

## État / limites connues

- Le mode dongle 2.4G expose une interface vendor différente (`0xFF60`) :
  éclairage/remap via le dongle est **non encore vérifié** sur matériel.
- Certaines sous-valeurs (souris p1/p2, pages étendues 0x07) restent à
  confirmer matériellement — tables isolées dans `keymapwidget.cpp`.
- Écran TFT : le nombre de frames legacy vérifié est ≤ 30 (l'app officielle
  annonce 140 ; le header de 256 o du config.xml est implémenté).
