# AJAZZ AK820 Pro — Protocole HID « legacy » (firmware feature-report)

Spécification obtenue par décompilation de l'app Windows officielle
`DeviceDriver.exe` V1.0.0.5 (adr. Ghidra notées `FUN_…`), croisée avec les
projets vespovios/wsclx `ak820pro-modder` (crates `legacy*.rs`, vérifiés sur
matériel 0C45:800A) et gohv/MemerGamer (fixtures).

Applicable aux cartes à collection vendor **usage page 0xFF13** (contrôle,
rapports *feature* 65 octets) + **0xFF68** (TFT, sorties 4096 octets).
Identifiés USB : VID `0C45` (Sonix), PIDs `8009`/`800A` (filaire),
`FDFD` (dongle 2.4G, config via interface MI_03), `7140` (ISP — ne jamais toucher).

## Transports

### T1 — Rapports feature 65 octets (protocole principal)
- 1 octet report-ID (0) + 64 octets de charge. `HidD_SetFeature` de taille
  `0x41` (FUN_00451440, appelée par FUN_0044eed0).
- Toute écriture est précédée/suivie de `Sleep(cmd_delaytime)` = **35 ms**
  (config.xml). Les lectures attendent 50 ms.
- Écriture multi-paquets : len ≥ 0x42 → envoie `len>>6` paquets de 64 octets,
  10 réessais par paquet en cas d'échec (FUN_0044eed0).

### T2 — Rapports feature 33 octets, checksum (opérations unitaires)
- Même report-ID 0, taille totale `0x21`. Somme des octets 0..64 du buffer
  préparé écrite en **byte 8** (checksum) — FUN_0044f5f0.
- Réponse validée par égalité des octets 1..3 avec la requête ; 20 réessais,
  Sleep 10 ms.

## Transaction générique (T1)

```
START (04 18)        → réessayer tant que réponse vide ou [04,18,00,FF]
<cmd> (04 XX …)      → data packets (64 o chacun, écho sans statut)
COMMIT (04 02)       → ack [04,02,*,01]
FINISH (04 F0, flag) → ack ; envoyé même en cas d'échec
```
Ack générique : `reply[0]==4 && reply[1]==cmd && reply[3]==1`.
Paquet de contrôle : `[04, cmd, subcmd, 0,0,0,0,0, value_lo, value_hi, 0…]`
(FUN_0044b900 : `04 18` puis `04 13` avec byte8=1).

## Commandes T1

| Cmd | Nom | Dir | byte8 (value) | Données |
|----:|-----|-----|---------------|---------|
| `0x02` | COMMIT | W | 0 | — |
| `0x11` | ÉCRITURE keymap **couche base** | W | 9 (nb blocs) | 576 o : 144 slots × 4 + `AA 55` |
| `0x13` | Écriture effet LED | W | 1 | paquet mode (ci-dessous) |
| `0x17` | Écriture veille | W | 1 | data[8] = 0/1/2/3 (0, 1, 5, 30 min) |
| `0x18` | START | W | flag (1 réglages, 0 image/RGB) | — |
| `0x19` | Ouverture session MACRO | W | — | — |
| `0x20` | Lecture batterie | R | — | req **sans préfixe 04** : `[20 01]`, rép `[20 01 00 pct]` (FF = filaire pleine) |
| `0x23` | Écriture banque RGB custom | W | 9 (nb blocs) | 576 o : 144 × `[id, r, g, b]` + `AA 55` |
| `0x27` | ÉCRITURE keymap **couche Fn** | W | 9 | idem 0x11 |
| `0x28` | Horloge | W | 1 | `[00 01 5A YY MM DD hh mm ss 00 04 … AA 55]` |
| `0x72` | Config image TFT | W | `slot` en byte2, nb blocs 4K en byte8 | puis chunks 4096 o **en sorties** sur interface 0xFF68, ack 64 o par chunk |
| `0xF0` | FINISH | W | flag | — |
| `0xF5` | **Lecture** banque RGB custom | R | nb blocs (3 ou 9) | rép : n × 64 o, slot i = `[id, r, g, b]` @ `i*4` ; puis `04 02` |

Aucune lecture de keymap/macros : l'app officielle garde l'état côté client
(`%APPDATA%\AJAZZ AK820Pro Driver\config.xml`).

### Paquet mode LED (cmd 0x13)
16 octets utiles, fin `AA 55` en 14..15 (trailer exact du driver, 0x55aa LE) :
```
[0]=mode [1..3]=R,G,B  [8]=colorful  [9]=luminosité 0-5
[10]=vitesse 0-5  [11]=direction 0=L,1=D,2=U,3=R  [14..15]=AA 55
```
**Modes** — liste complète du firmware (clés de langue 521-540 du driver,
aussi utilisées par la liste « temps réel » FUN_0042b3b0, ids 1..19) :

| Id | Clé lan | Nom |
|---:|---|---|
| `0x00` | 540 | LED Off (Éteindre) |
| `0x01` | 521 | Static (vrai fixe — NE PAS re-mapper) |
| `0x02` | 522 | Single On |
| `0x03` | 523 | Single Off |
| `0x04` | 524 | Glittering |
| `0x05` | 525 | Falling |
| `0x06` | 526 | Colourful |
| `0x07` | 527 | Breath |
| `0x08` | 528 | Spectrum |
| `0x09` | 529 | Outward |
| `0x0A` | 530 | Scrolling |
| `0x0B` | 531 | Rolling |
| `0x0C` | 532 | Rotating |
| `0x0D` | 533 | Explode |
| `0x0E` | 534 | Launch |
| `0x0F` | 535 | Ripples |
| `0x10` | 536 | Flowing |
| `0x11` | 537 | Pulsating |
| `0x12` | 538 | Tilt |
| `0x13` | 539 | Shuttle |
| `0x80` | — | Custom (RGB par touche) |

NB : le combo principal de l'onglet éclairage (lan 370-378) n'expose qu'un
sous-ensemble (9 modes) ; les clés 521-540 et la liste temps réel couvrent
les 20. Les ids 0x02/0x03 existent donc bien dans le firmware. Le chemin
mono du driver (FUN_00434090) n'envoie que `[mode, 0×13, AA 55]` ; le
chemin colorful (FUN_0042b100) ajoute couleur + flag + réglages. La colonne
`colorful` de t_light_data vaut 0 = statique, 1 = rainbow, 2 = rainbow
inversé.

### Banque keymap (cmd 0x11 / 0x27)
144 slots de 4 octets, indexés par le **light_index** du layout XML
(FUN_0044ba10 / FUN_00418420 : `data[i*4]=i, [i*4+1..3] = page, p1, p2, p3`),
terminés par `AA 55`. Pages décodées du switch FUN_00418420 :

| Page | Signification | p1 | p2 | p3 |
|-----:|---------------|----|----|----|
| `0x00` | Touche par défaut | | | |
| `0x01` | Souris | sous-action | bouton | |
| `0x02` | Clavier (Custom Fn / raccourci Windows) | modificateur (01=Win, 02=Shift?, 04=Alt, 08=Ctrl) | code HID | |
| `0x03` | Multimédia (consumer) | code | (Multi-key : code2) | |
| `0x05` | 03 = touche désactivée ; 02,code = type étendu | | | |
| `0x06` | Macro | index macro | déclenchement (0 une fois, 1 N fois, 2 boucle) | répétitions |
| `0x07` | 3 paramètres (type 13) | p1 | p2 | p3 |

Cas spéciaux type 2 : code 0xE0-0xE7 (modificateur seul) → `[02, code, 00]`
via FUN_00451ca0 (conversion code interne → wire).

### Macros (FUN_0042d6f0)
1. `04 19` (T1, ack)
2. `04 15` value=nbBlocs+1 (`local_50`), puis **multi-paquets** : index 400 o
   (100 slots × u32 LE — ici u16 offset par slot) + blocs à partir de l'offset
   400, trailer `AA 55` ; chaque action = 4 o `[delay_lo, delay_hi, key, flags]`
   ; flags : clavier `B0`(press)/`30`(release), souris `90`/`10`
   (boutons 1=G,2=D?/4=M selon UI), délai pur flags `50` (min 10 ms).
3. `04 02` COMMIT, Sleep 30 ms.

### Écran TFT (FUN_00422980, confirmé legacy_tft.rs)
1. START flag 0
2. `04 72 <slot>` value = nb blocs de 4096 o (slot 1..3, défaut 1)
3. chunks 4096 o en **output reports** sur l'interface TFT + ack 64 o lu
   sur la même interface (1 s timeout)
4. `04 02` COMMIT ; FINISH flag 0 en cas d'échec.
Format payload : header 256 o (gif_headlength) : `[N, delay1, …, delayN-1,
00, FF…]` délais en **2 ms**, puis N frames RGB565 LE 128×128 (32 768 o),
pad `FF` au multiple de 4096. Max recommandé legacy : 30 frames
(config.xml officiel : gif_maxframes 140).

**Interface TFT selon la carte** (vérifié matériellement le 2026-10-31) :
- carte « family B » (0C45:8009, produit « AK820 », contrôle 0xFF13) :
  données TFT sur **0xFF68** — descripteur
  `06 68ff 09 61 [09 62 … 81 02 : in 64 o][09 63 … 96 0010 91 02 : out 4096 o] c0`
- cartes « family A » (web-output) : 0xFF67 (sorties 4104 o).
Chercher FF68 puis FF67 en secours. Ack de chaque chunk = 64 o sur la
même interface. Upload test complet validé : 17 chunks ackés + COMMIT ok.

## Commandes T2 (33 o, checksum byte 8)

| Cmd | Sens | Payload |
|----:|------|---------|
| `0x05` | Sélection mode custom + luminosité | `[05 10 00 … bright<<8 …]` |
| `0x0F` | Appliquer réglages (mode sans fil) | — |
| `0x14` | Écriture paramètre i/N | `[14 <taille> <idx> 0 <val…>]` — 7 params (layout A) / 21 params de 28 o (dernier 16 o) pour 144 LED |
| `0x1F` | **Appliquer** (bouton OK réglages) | — |
| `0x21`-transport touche unique | `[00 03|0D 04 <slot> <page p1 p2 p3> <csum>]` — 03 = base, 0D = Fn |

## État de la couche app officielle
- Persistance : `config.xml` app-side, exports XML (macros, keymap, light).
- Retard/attente : 35 ms (cmd_delaytime) avant chaque écriture, 50 ms avant lecture.
- Deux layouts matériel : 48 LED mono (0xC0/3 blocs) et 144 LED RGB (0x240/9 blocs).
- Le driver envoie systématiquement COMMIT puis FINISH ; FINISH sans COMMIT →
  status FF et clavier muet ~2 s.
