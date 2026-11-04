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
inline const juce::StringArray keyNames   { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
inline const juce::StringArray scaleNames { "Major", "Minor", "Harmonic major", "Harmonic minor", "Melodic minor",
                                            "Double harmonic major", "Neapolitan major", "Neapolitan minor" };

inline const std::array<int, 7>& scaleSteps (int scale)
{
    static const std::array<int, 7> scales[] {
        { 0, 2, 4, 5, 7, 9, 11 }, { 0, 2, 3, 5, 7, 8, 10 }, { 0, 2, 4, 5, 7, 8, 11 }, { 0, 2, 3, 5, 7, 8, 11 },
        { 0, 2, 3, 5, 7, 9, 11 }, { 0, 1, 4, 5, 7, 8, 11 }, { 0, 1, 3, 5, 7, 9, 11 }, { 0, 1, 3, 5, 7, 8, 11 }
    };
    return scales[juce::jlimit (0, 7, scale)];
}

struct ChordItem
{
    int root = 0;                 // pitch class 0-11
    std::vector<int> intervals;   // semitones above the root, first one is 0
    int beats = 4;

    bool operator== (const ChordItem& o) const { return root == o.root && intervals == o.intervals && beats == o.beats; }
};

using Progression = std::vector<ChordItem>;

inline juce::String chordSuffix (const std::vector<int>& iv)
{
    struct Quality { std::vector<int> iv; const char* name; };
    static const Quality qualities[] {
        { { 0, 4, 7 }, "" },          { { 0, 3, 7 }, "m" },          { { 0, 3, 6 }, "dim" },      { { 0, 4, 8 }, "aug" },
        { { 0, 2, 7 }, "sus2" },      { { 0, 5, 7 }, "sus4" },       { { 0, 4, 6 }, "b5" },       { { 0, 2, 6 }, "sus2b5" },
        { { 0, 4, 7, 11 }, "maj7" },  { { 0, 4, 7, 10 }, "7" },      { { 0, 3, 7, 10 }, "m7" },   { { 0, 3, 6, 10 }, "m7b5" },
        { { 0, 3, 6, 9 }, "dim7" },   { { 0, 3, 7, 11 }, "mMaj7" },  { { 0, 4, 8, 11 }, "maj7#5" }, { { 0, 4, 8, 10 }, "7#5" },
        { { 0, 4, 6, 10 }, "7b5" },   { { 0, 4, 6, 11 }, "maj7b5" }, { { 0, 3, 6, 11 }, "dimMaj7" },
    };

    for (const auto& q : qualities)
        if (q.iv == iv)
            return q.name;
    return "?";
}

// Note spelling as in OneMotion: degree m of the scale takes the letter m steps after the key's letter
// (F# minor = F# G# A B C# D E, D# major = D# E# F## G# A# B# C##). Notes outside the scale use flats.
inline juce::String noteName (int pc, int key, int scale)
{
    static const char* outside[] { "C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    static const int naturals[] { 0, 2, 4, 5, 7, 9, 11 };
    static const int keyLetter[] { 0, 0, 1, 2, 2, 3, 3, 4, 5, 5, 6, 6 };   // letter of each entry of keyNames
    static const char* accidentals[] { "bb", "b", "", "#", "##" };

    pc = ((pc % 12) + 12) % 12;
    key = juce::jlimit (0, 11, key);
    const auto& steps = scaleSteps (scale);
    for (int m = 0; m < 7; ++m)
    {
        if ((key + steps[(size_t) m]) % 12 != pc)
            continue;
        const int letter = (keyLetter[key] + m) % 7;
        int acc = pc - naturals[letter];
        if (acc < -2) acc += 12;
        if (acc > 2)  acc -= 12;
        return juce::String::charToString ("CDEFGAB"[letter]) + accidentals[acc + 2];
    }
    return outside[pc];
}

inline juce::String chordName (const ChordItem& c, int key, int scale) { return noteName (c.root, key, scale) + chordSuffix (c.intervals); }

// One cell of the palette: row 0 = sus2, 1 = triad, 2 = sus4, 3 = seventh.
// sus chords only exist when their notes belong to the scale (empty cell otherwise).
inline std::optional<ChordItem> paletteChord (int key, int scale, int degree, int row)
{
    const auto& s = scaleSteps (scale);
    auto inScale = [&] (int pc)
    {
        for (int step : s)
            if ((key + step) % 12 == pc % 12)
                return true;
        return false;
    };
    auto stacked = [&] (int count)
    {
        std::vector<int> iv;
        for (int i = 0; i < count; ++i)
        {
            const int idx = degree + 2 * i;
            iv.push_back (s[(size_t) (idx % 7)] + 12 * (idx / 7) - s[(size_t) degree]);
        }
        return iv;
    };

    ChordItem c;
    c.root = (key + s[(size_t) degree]) % 12;

    switch (row)
    {
        case 0:
            if (! inScale (c.root + 2) || ! inScale (c.root + 7)) return std::nullopt;
            c.intervals = { 0, 2, 7 };
            break;
        case 1: c.intervals = stacked (3); break;
        case 2:
            if (! inScale (c.root + 5) || ! inScale (c.root + 7)) return std::nullopt;
            c.intervals = { 0, 5, 7 };
            break;
        default: c.intervals = stacked (4); break;
    }
    return c;
}

// Voicing settings (Chord tab of OneMotion: "Inversions" on by default, chords from G3 up)
struct Voicing
{
    int key = 0, scale = 0;
    bool inversions = true;
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
