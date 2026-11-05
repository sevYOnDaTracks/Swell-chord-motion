<p align="center">
  <img src="docs/main.png" alt="Swell Chord Motion" width="820">
</p>

<h1 align="center">Swell Chord Motion</h1>

<p align="center">
  Générateur de progressions d'accords — <b>Standalone</b>, <b>VST3</b> (Windows / Mac) et <b>AU</b> (Mac)<br>
  par <b>Swell-audio</b> · <a href="https://github.com/sevYOnDaTracks">@sevyondatracks</a>
</p>

---

## Le principe

Tu choisis une gamme, tu cliques sur les accords pour les écouter, tu les glisses dans ta progression, tu choisis un style d'accompagnement… et tu glisses le résultat en MIDI directement dans ton DAW.

Le fonctionnement s'inspire du [Chord Player de OneMotion](https://www.onemotion.com/chord-player/), en version plugin.

## Fonctionnalités

| | |
|---|---|
| **Palette d'accords** | Tous les accords de la gamme choisie, sur 4 lignes : sus2, accords de base, sus4 et 7e |
| **17 écritures de tonalité × 49 modes** | Les 8 gammes de base et leurs modes, avec des choix distincts pour C#/Db, D#/Eb, F#/Gb, G#/Ab et A#/Bb |
| **Chord progression** | Glisser-déposer, réorganisation des blocs, durée par accord (1 à 16 temps), Undo / Redo |
| **57 styles OneMotion** | Les mêmes styles que le Chord Player de OneMotion (Once, Basic 1-4, Pop 1-4, Dance, Rock 1-4, Swing, Waltz, Jazz Waltz, Reggae, Salsa, Bossa Nova, Samba, Merengue, Cumbia, Reggaeton, Tango, Flamenco 1-3, Ragtime, Country, Polyrhythm, Straddle run, Strumming, Boogie, Funk 1-5, Melodic 1-8, Mod 1-4), joués avec les mêmes règles : swing, sustain, anticipations, 3/4 et 6/8. Vérifiés note par note contre le site |
| **Arpèges** | 21 patterns façon OneMotion (`Arp 1-3-2-4`, `Arp x-1-2`, `Arp 1-2-3-4-3-2`, `Split 13-2`…), step 1/4 → 1/16T, option Mirror. Remplace la partie accords du style, la basse du style est gardée |
| **Accords et registre** | Les accords, leurs altérations et les cases vides suivent la palette OneMotion. Les plages de notes suivent la tonalité. Avec **Once**, le réglage initial reprend le projet de référence : en Fa♯ majeur, l’accord **B** joue **Si3–Ré♯4–Fa♯4** sur une basse **Si1**. **Ranges** permet de choisir séparément le début de la plage des accords ou de la basse et de revenir au réglage de référence. **Edit** donne accès à 82 types d’accords, dont add9, mmaj7 et 13#11 |
| **Instruments intégrés** | Piano, E-Piano, Organ, Pad, Pluck, Strings + reverb (Room, Hall, Church) |
| **MIDI Out** | Pilote tes propres VST avec la progression |
| **Synchro DAW** | Suit le tempo et la lecture de ton DAW |
| **DRAG MIDI** | Exporte la progression en MIDI avec la vélocité de chaque note. **VEL** règle la force des attaques de 1 à 127 pour la lecture et le fichier. **Result** affiche les notes, en ambre pour les accords et en bleu pour la basse |
| **SWELL KEY** | Un clic règle KEY et SCALE sur la gamme trouvée par le plugin Swell Key |

## Utilisation

1. **SCALE** : choisis la tonalité et la gamme. La palette se met à jour.
2. **Clic sur un accord** : tu l'entends tout de suite.
3. **Glisse-le** dans *Chord Progression*, ou **double-clique** dessus pour l'ajouter à la fin.
4. Dans la progression :
   - **glisse un bloc** pour le déplacer ;
   - **Edit** ou **clic droit** sur un bloc pour changer sa fondamentale, son type ou sa durée, le dupliquer ou le supprimer ;
   - **- / +** pour changer la durée de l'accord sélectionné.
5. **STYLE** et **INSTR** : choisis le rythme et le son, puis **PLAY**.
   - **ARP** : choisis un arpège (`1` = note la plus grave de l'accord, `x` = silence ; au-delà du nombre de notes on monte d'une octave). **STEP** règle la durée de chaque note, **MIRROR** compte les notes depuis le haut. `Off (style)` revient aux accords du style.
6. **DRAG MIDI** : glisse le bouton dans ton DAW. Un simple clic ouvre le dossier des exports (`Documents/Swell Chord Motion`).

> Astuce FL Studio : mets **INSTR** sur `MIDI Out`, puis route la sortie MIDI du plugin vers n'importe quel instrument.
> Comme l'export de OneMotion, les accords sortent sur le canal MIDI 1 et la basse sur le canal 2.
> En 3/4 et 6/8 (Waltz, Jazz Waltz, Flamenco 1-2, Polyrhythm), une durée de 4 vaut une mesure, comme sur OneMotion.

## Lancer le Standalone

**Windows** : double-clique sur

```
build\SwellChordMotion_artefacts\Release\Standalone\Swell Chord Motion.exe
```

Au premier lancement, clique sur **Options** (en haut à gauche) puis **Audio/MIDI Settings** pour choisir ta carte son.

**Mac** : ouvre `build-mac/SwellChordMotion_artefacts/Release/Standalone/Swell Chord Motion.app`.

## Installer le plugin

| Format | Fichier généré | Où le copier |
|---|---|---|
| VST3 Windows | `build\SwellChordMotion_artefacts\Release\VST3\Swell Chord Motion.vst3` | `C:\Program Files\Common Files\VST3\` |
| VST3 Mac | copié automatiquement | `~/Library/Audio/Plug-Ins/VST3/` |
| AU Mac | copié automatiquement | `~/Library/Audio/Plug-Ins/Components/` |

Relance ensuite un scan des plugins dans ton DAW.

## Compiler depuis les sources

Le projet utilise [JUCE 8](https://juce.com), téléchargé automatiquement par CMake.

### Windows

Prérequis : [Visual Studio Build Tools 2022](https://visualstudio.microsoft.com/downloads/) (charge « Développement Desktop en C++ »), CMake et Git.

```powershell
winget install Kitware.CMake
winget install Microsoft.VisualStudio.2022.BuildTools --override "--wait --passive --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
.\build.ps1
```

### Mac (Standalone + VST3 + AU, Apple Silicon et Intel)

```bash
xcode-select --install
brew install cmake
chmod +x build-mac.sh && ./build-mac.sh
```

## Structure du code

```
Source/
├── ChordEngine.h        théorie musicale, palette, noms d'accords
├── StyleEngine.h        les 57 styles OneMotion (notation, voicings, moteur de lecture)
├── SwellSynth.h         instruments intégrés
├── PluginProcessor.*    paramètres, progression (undo/redo), synchro DAW, export MIDI
├── PluginEditor.*       interface : palette, progression, clavier
└── SwellLookAndFeel.h   charte graphique Swell (couleurs, logo, boutons)
```

## Roadmap

- [ ] Instruments samplés (piano, Rhodes, guitare, cordes…)
- [ ] Onglet Melody
- [ ] Bibliothèque de progressions prêtes à l'emploi
- [ ] Sauvegarde / chargement de projets

---

<p align="center"><sub>Swell-audio · Chord Motion v1.4.2 · by @sevyondatracks</sub></p>
