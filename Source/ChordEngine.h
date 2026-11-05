#pragma once

#include <juce_core/juce_core.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <vector>

// Music theory + accompaniment styles. Pure functions, shared by the audio thread,
// the MIDI exporter and the editor.
namespace ChordEngine
{
// Keep the original parameter indices for sessions saved with earlier releases.
inline const juce::StringArray keyNames { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B",
                                         "Db", "D#", "Gb", "G#", "A#" };
inline int keyPitch (int key)
{
    static constexpr int pitches[] { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 1, 3, 6, 8, 10 };
    return pitches[juce::jlimit (0, 16, key)];
}

using ScaleSteps = std::array<int, 7>;
struct Scale { juce::String name; ScaleSteps steps; };

inline const std::vector<Scale>& scales()
{
    static const std::vector<Scale> result = []
    {
        struct Family { ScaleSteps steps; std::array<const char*, 7> modes; };
        const Family families[] {
            {{0,2,4,5,7,9,11}, {"Major", "Dorian", "Phrygian", "Lydian", "Mixolydian", "Minor", "Locrian"}},
            {{0,2,4,5,7,8,11}, {"Harmonic major", "Dorian b5", "Phrygian b4", "Lydian b3", "Mixolydian b2", "Lydian #2 #5", "Locrian bb7"}},
            {{0,2,3,5,7,8,11}, {"Harmonic minor", "Locrian #6", "Major #5", "Dorian #4", "Phrygian Dominant", "Lydian #2", "Super-Locrian bb7"}},
            {{0,2,3,5,7,9,11}, {"Melodic minor", "Dorian b9", "Lydian #5", "Lydian Dominant", "Mixolydian b6", "Locrian #2", "Super-Locrian"}},
            {{0,1,4,5,7,8,11}, {"Double harmonic major", "Lydian #2 #6", "Ultra-Phrygian", "Hungarian minor", "Oriental", "Ionian #2 #5", "Locrian bb3 bb7"}},
            {{0,1,3,5,7,9,11}, {"Neapolitan major", "Leading whole-tone", "Lydian Dominant #5", "Lydian Dominant b6", "Major locrian", "Semi-Locrian b4", "Super-Locrian bb3"}},
            {{0,1,3,5,7,8,11}, {"Neapolitan minor", "Lydian #6", "Mixolydian #5", "Aeolian #4", "Locrian Dominant", "Ionian #2", "Ultra-Locrian bb3"}}
        };
        std::vector<Scale> list;
        auto append = [&list] (const Family& family, size_t mode)
        {
            Scale s { family.modes[mode], {} };
            for (size_t i = 0; i < 7; ++i)
                s.steps[i] = (family.steps[(mode + i) % 7] - family.steps[mode] + 12) % 12;
            list.push_back (s);
        };
        append (families[0], 0);
        append (families[0], 5);
        for (size_t f = 1; f < 7; ++f) append (families[f], 0);
        for (size_t f = 0; f < 7; ++f)
            for (size_t m = 1; m < 7; ++m)
                if (f != 0 || m != 5) append (families[f], m);
        return list;
    }();
    return result;
}

inline const juce::StringArray scaleNames = []
{
    juce::StringArray names;
    for (const auto& s : scales()) names.add (s.name);
    return names;
}();

inline const ScaleSteps& scaleSteps (int scale)
{
    return scales()[(size_t) juce::jlimit (0, (int) scales().size() - 1, scale)].steps;
}

struct ChordItem
{
    int root = 0;                 // pitch class 0-11
    std::vector<int> intervals;   // semitones above the root, first one is 0
    int beats = 4;
    juce::String symbol;          // preserves aliases with the same tones, e.g. add2 and add9

    bool operator== (const ChordItem& o) const { return root == o.root && intervals == o.intervals && beats == o.beats && symbol == o.symbol; }
};

using Progression = std::vector<ChordItem>;

struct Quality
{
    const char* suffix;
    std::vector<int> intervals;
    const char* group;
    const char* menuLabel;
};

inline const std::vector<Quality>& qualities()
{
    static const std::vector<Quality> list {
        { "", {0,4,7}, "Major", "Major" },
        { "(b5)", {0,4,6}, "Major", "(b5)" },
        { "add2", {0,2,4,7}, "Major", "add2" },
        { "add4", {0,4,5,7}, "Major", "add4" },
        { "add9", {0,2,4,7}, "Major", "add9" },
        { "add11", {0,4,5,7}, "Major", "add11" },
        { "6", {0,4,7,9}, "Major", "6" },
        { "6/9", {0,2,4,7,9}, "Major", "6/9" },
        { "6/11", {0,4,5,7,9}, "Major", "6/11" },
        { "7", {0,4,7,10}, "Major", "7" },
        { "7/6", {0,4,7,9,10}, "Major", "7/6" },
        { "7b5", {0,4,6,10}, "Major", "7b5" },
        { "7b5b9", {0,1,4,6,10}, "Major", "7b5b9" },
        { "7b5#9", {0,3,4,6,10}, "Major", "7b5#9" },
        { "7b9", {0,1,4,7,10}, "Major", "7b9" },
        { "7#5", {0,4,8,10}, "Major", "7#5" },
        { "7#5b9", {0,1,4,8,10}, "Major", "7#5b9" },
        { "7#5#9", {0,3,4,8,10}, "Major", "7#5#9" },
        { "7#9", {0,3,4,7,10}, "Major", "7#9" },
        { "7#11", {0,4,6,7,10}, "Major", "7#11" },
        { "9", {0,2,4,7,10}, "Major", "9" },
        { "9b5", {0,2,4,6,10}, "Major", "9b5" },
        { "9#5", {0,2,4,8,10}, "Major", "9#5" },
        { "11", {0,2,4,5,7,10}, "Major", "11" },
        { "11b9", {0,1,4,5,7,10}, "Major", "11b9" },
        { "13", {0,2,4,5,7,9,10}, "Major", "13" },
        { "13b9", {0,1,4,5,7,9,10}, "Major", "13b9" },
        { "13#11", {0,2,4,6,7,9,10}, "Major", "13#11" },
        { "aug", {0,4,8}, "Major", "aug" },
        { "aug6", {0,4,7,10}, "Major", "aug6" },
        { "maj7", {0,4,7,11}, "Major", "maj7" },
        { "maj7/6", {0,4,7,9,11}, "Major", "maj7/6" },
        { "maj7b5", {0,4,6,11}, "Major", "maj7b5" },
        { "maj7b9", {0,1,4,7,11}, "Major", "maj7b9" },
        { "maj7#5", {0,4,8,11}, "Major", "maj7#5" },
        { "maj7#9", {0,3,4,7,11}, "Major", "maj7#9" },
        { "maj7#11", {0,4,6,7,11}, "Major", "maj7#11" },
        { "maj9", {0,2,4,7,11}, "Major", "maj9" },
        { "maj9#5", {0,2,4,8,11}, "Major", "maj9#5" },
        { "maj9#11", {0,2,4,6,7,11}, "Major", "maj9#11" },
        { "maj11", {0,2,4,5,7,11}, "Major", "maj11" },
        { "maj13", {0,2,4,5,7,9,11}, "Major", "maj13" },
        { "maj13#11", {0,2,4,6,7,9,11}, "Major", "maj13#11" },
        { "m", {0,3,7}, "Minor", "Minor" },
        { "madd2", {0,2,3,7}, "Minor", "madd2" },
        { "madd4", {0,3,5,7}, "Minor", "madd4" },
        { "madd9", {0,2,3,7}, "Minor", "madd9" },
        { "madd11", {0,3,5,7}, "Minor", "madd11" },
        { "m6", {0,3,7,9}, "Minor", "m6" },
        { "mb6", {0,3,7,8}, "Minor", "mb6" },
        { "m6/9", {0,2,3,7,9}, "Minor", "m6/9" },
        { "m6/11", {0,3,5,7,9}, "Minor", "m6/11" },
        { "m7", {0,3,7,10}, "Minor", "m7" },
        { "m7/4", {0,3,5,7,10}, "Minor", "m7/4" },
        { "m7/6", {0,3,7,9,10}, "Minor", "m7/6" },
        { "m7b5", {0,3,6,10}, "Minor", "m7b5" },
        { "m7b5b9", {0,1,3,6,10}, "Minor", "m7b5b9" },
        { "m7b9", {0,1,3,7,10}, "Minor", "m7b9" },
        { "m9", {0,2,3,7,10}, "Minor", "m9" },
        { "m9b5", {0,2,3,6,10}, "Minor", "m9b5" },
        { "m11", {0,2,3,5,7,10}, "Minor", "m11" },
        { "m13", {0,2,3,5,7,9,10}, "Minor", "m13" },
        { "mmaj7", {0,3,7,11}, "Minor", "mmaj7" },
        { "mmaj7b9", {0,1,3,7,11}, "Minor", "mmaj7b9" },
        { "mmaj9", {0,2,3,7,11}, "Minor", "mmaj9" },
        { "dim", {0,3,6}, "Diminished", "dim" },
        { "dim7", {0,3,6,9}, "Diminished", "dim7" },
        { "dim7b9", {0,1,3,6,9}, "Diminished", "dim7b9" },
        { "dim9", {0,2,3,6,9}, "Diminished", "dim9" },
        { "sus2", {0,2,7}, "Suspended", "sus2" },
        { "sus24", {0,2,5,7}, "Suspended", "sus24" },
        { "sus4", {0,5,7}, "Suspended", "sus4" },
        { "6sus2", {0,2,7,9}, "Suspended", "6sus2" },
        { "6sus4", {0,5,7,9}, "Suspended", "6sus4" },
        { "6sus4", {0,2,5,7,9}, "Suspended", "6/9sus4" },
        { "7sus2", {0,2,7,10}, "Suspended", "7sus2" },
        { "7sus4", {0,5,7,10}, "Suspended", "7sus4" },
        { "9sus4", {0,2,5,7,10}, "Suspended", "9sus4" },
        { "13sus4", {0,2,5,7,9,10}, "Suspended", "13sus4" },
        { "maj7sus2", {0,2,7,11}, "Suspended", "maj7sus2" },
        { "maj7sus4", {0,5,7,11}, "Suspended", "maj7sus4" },
        { "5", {0,7}, "Power", "5" },
        // Chords an earlier release could store, outside the editor menu.
        { "sus2b5", {0,2,6}, nullptr, nullptr },
        { "dimMaj7", {0,3,6,11}, nullptr, nullptr }
    };
    return list;
}

inline juce::String chordSuffix (const std::vector<int>& iv)
{
    for (const auto& q : qualities())
        if (q.intervals == iv) return q.suffix;
    // Legacy sessions may contain a stack that has no conventional chord symbol.
    juce::StringArray offsets;
    for (int interval : iv) offsets.add (juce::String (interval));
    return "(" + offsets.joinIntoString (",") + ")";
}

// Note spelling as in OneMotion: degree m of the scale takes the letter m steps after the key's letter
// (F# minor = F# G# A B C# D E, D# major = D# E# F## G# A# B# C##). Notes outside the scale use flats.
inline juce::String noteName (int pc, int key, int scale)
{
    static const char* outside[] { "C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    static const int naturals[] { 0, 2, 4, 5, 7, 9, 11 };
    static const int keyLetter[] { 0, 0, 1, 2, 2, 3, 3, 4, 5, 5, 6, 6, 1, 1, 4, 4, 5 };   // letter of each entry of keyNames

    pc = ((pc % 12) + 12) % 12;
    key = juce::jlimit (0, 16, key);
    const auto& steps = scaleSteps (scale);
    for (int m = 0; m < 7; ++m)
    {
        if ((keyPitch (key) + steps[(size_t) m]) % 12 != pc)
            continue;
        const int letter = (keyLetter[key] + m) % 7;
        const int accidental = (pc - naturals[letter] + 18) % 12 - 6;
        return juce::String::charToString ("CDEFGAB"[letter])
               + juce::String::repeatedString (accidental < 0 ? "b" : "#", std::abs (accidental));
    }
    return outside[pc];
}

inline juce::String chordName (const ChordItem& c, int key, int scale) { return noteName (c.root, key, scale) + (c.symbol.isEmpty() ? chordSuffix (c.intervals) : c.symbol); }

// One cell of the palette: row 0 = sus2, 1 = triad, 2 = sus4, 3 = seventh.
// sus chords only exist when their notes belong to the scale (empty cell otherwise).
inline std::optional<ChordItem> paletteChord (int key, int scale, int degree, int row)
{
    const auto& steps = scaleSteps (scale);
    const int rootStep = steps[(size_t) degree];
    // Per-row search order is part of the palette. A scale can admit several qualities
    // on the same root, and OneMotion displays only the first match.
    static const std::array<std::vector<const Quality*>, 4> rows = []
    {
        const std::array<std::vector<const char*>, 4> priority {{
            {"sus2"}, {"", "m", "dim", "aug", "(b5)"}, {"sus4"},
            {"7", "maj7", "m7", "m7b5", "mmaj7", "maj7#5", "dim7", "7b5"}
        }};
        std::array<std::vector<const Quality*>, 4> result;
        for (size_t r = 0; r < priority.size(); ++r)
            for (const auto* name : priority[r])
                for (const auto& q : qualities())
                    if (std::strcmp (name, q.suffix) == 0) { result[r].push_back (&q); break; }
        return result;
    }();
    for (const auto* q : rows[(size_t) row])
    {
        const bool fits = std::all_of (q->intervals.begin(), q->intervals.end(), [&] (int interval)
        {
            return std::binary_search (steps.begin(), steps.end(), (rootStep + interval) % 12);
        });
        if (fits) return ChordItem { (keyPitch (key) + rootStep) % 12, q->intervals, 4, q->suffix };
    }
    return std::nullopt;
}

// The reference project offsets both style ranges by three semitones. Store the
// chord and bass ranges separately: either dotted range can move in OneMotion.
constexpr int referenceRangeShift = -3;
struct Voicing
{
    int key = 0, scale = 0;
    int chordShift = referenceRangeShift, bassShift = referenceRangeShift;

    bool operator!= (const Voicing& other) const
    {
        return key != other.key || scale != other.scale || chordShift != other.chordShift || bassShift != other.bassShift;
    }
};

constexpr int chordTarget = 55;   // G3

// Arpeggio chosen in the ARP menu (replaces the chord part of the style)
struct ArpSettings
{
    int pattern = 0;     // 0 = off, otherwise arpLists()[pattern - 1]
    int step = 1;        // index in arpStepNames
    bool mirror = false; // count tones from the top note down

    bool operator== (const ArpSettings& o) const { return pattern == o.pattern && step == o.step && mirror == o.mirror; }
    bool operator!= (const ArpSettings& o) const { return ! (*this == o); }
};

//==============================================================================
struct NoteEvent
{
    enum Kind { noteEvent, expression, pitchBend };

    double ppq;
    int note;
    bool on;
    int value = 100;     // velocity, CC11 value or pitch bend value
    Kind kind = noteEvent;
    int channel = 1;     // like OneMotion's MIDI export: 1 = chords, 2 = bass
};

constexpr int midiTicksPerQuarter = 4800;
constexpr int defaultVelocity = 89; // 70% of MIDI full scale in the reference player
inline int scaledVelocity (int value, int strength)
{
    return juce::jlimit (1, 127, juce::roundToInt ((double) value * strength / defaultVelocity));
}

inline double totalBeats (const Progression& prog)
{
    double total = 0.0;
    for (const auto& c : prog)
        total += c.beats;
    return total;
}

// Index of the chord playing at a given beat (-1 when the progression is empty)
inline int chordAt (const Progression& prog, double beat)
{
    const double loop = totalBeats (prog);
    if (loop <= 0.0)
        return -1;

    double pos = std::fmod (beat, loop);
    if (pos < 0.0)
        pos += loop;

    for (size_t i = 0; i < prog.size(); ++i)
    {
        if (pos < prog[i].beats)
            return (int) i;
        pos -= prog[i].beats;
    }
    return (int) prog.size() - 1;
}
} // namespace ChordEngine
