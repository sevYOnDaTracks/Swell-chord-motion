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

// Voicing as in OneMotion (Chord tab, "Inversions" on by default): the chord is stacked in close position
// from the lowest chord tone at or above G3, so every chord stays in the same register.
// Inversions off: root position with the root between G3 and F#4. The bass sits between C2 and B2.
struct Voicing
{
    int key = 0, scale = 0;
    bool inversions = true;
};

constexpr int chordTarget = 55;   // G3
constexpr int maxChordTones = 17;

// Tones of the chord starting at inversion b (b may go past the chord size: next octave),
// stacked upwards. The root is left out when it would sit a half step above the previous note.
inline std::vector<int> stackChord (const std::vector<int>& iv, int rootBase, int b)
{
    const int n = (int) iv.size();
    std::vector<int> out;
    int prev = -9999;
    for (int q = 0; q < 40 && (int) out.size() < maxChordTones; ++q)
    {
        const int idx = b + q;
        const int a = ((idx % n) + n) % n;
        const int octave = (idx - a) / n;
        const int note = iv[(size_t) a] + rootBase + 12 * octave;
        if (a == 0 && note - prev == 1 && ! out.empty())
            continue;
        out.push_back (note);
        prev = note;
    }
    return out;
}

// Diatonic triad on the root of a sus chord: sus chords take the inversion of that triad
inline std::vector<int> inversionReference (const ChordItem& c, const Voicing& v)
{
    if (c.intervals != std::vector<int> { 0, 2, 7 } && c.intervals != std::vector<int> { 0, 5, 7 })
        return c.intervals;

    const auto& steps = scaleSteps (v.scale);
    for (int d = 0; d < 7; ++d)
    {
        if ((v.key + steps[(size_t) d]) % 12 != c.root)
            continue;
        std::vector<int> iv;
        for (int i = 0; i < 3; ++i)
        {
            const int idx = d + 2 * i;
            iv.push_back (steps[(size_t) (idx % 7)] + 12 * (idx / 7) - steps[(size_t) d]);
        }
        return iv;
    }
    return c.intervals;
}

// Every playable chord tone from the lowest one up (index i = arpeggio tone i + 1)
inline std::vector<int> chordNotes (const ChordItem& c, const Voicing& v)
{
    if (c.intervals.empty())
        return {};

    int rootBase = 48 + v.key + (c.root - v.key + 12) % 12;
    int b = 0;
    if (v.inversions)
    {
        // Lowest inversion whose first note is at or above the target
        const auto ref = inversionReference (c, v);
        b = -3 * (int) ref.size();
        while (stackChord (ref, rootBase, b).front() < chordTarget)
            ++b;
    }
    else
    {
        while (rootBase < chordTarget)       rootBase += 12;
        while (rootBase >= chordTarget + 12) rootBase -= 12;
    }
    return stackChord (c.intervals, rootBase, b);
}

inline int bassNote (const ChordItem& c) { return 36 + c.root; }

//==============================================================================
enum Voice { fullChord = -1, bass = -2, bassFifth = -3 };

struct Hit { int pos, len, voice; };   // in 16th notes; voice >= 0 = chord tone index (goes on up the stack)

struct Style
{
    const char* name;
    int length;      // pattern length in 16ths, repeated over the chord duration
    bool strum;
    std::vector<Hit> hits;
};

inline const std::vector<Style>& styles()
{
    constexpr int C = fullChord, B = bass, F = bassFifth;
    static const std::vector<Style> list {
        { "Basic 1", 16, false, { { 0, 16, C }, { 0, 16, B } } },
        { "Basic 2", 16, false, { { 0, 4, C }, { 4, 4, C }, { 8, 4, C }, { 12, 4, C }, { 0, 16, B } } },
        { "Basic 3", 16, false, { { 0, 8, C }, { 8, 8, C }, { 0, 8, B }, { 8, 8, F } } },
        { "Pop 1", 16, false, { { 0, 2, C }, { 2, 2, C }, { 4, 2, C }, { 6, 2, C }, { 8, 2, C }, { 10, 2, C }, { 12, 2, C }, { 14, 2, C },
                                { 0, 8, B }, { 8, 8, B } } },
        { "Pop 2", 16, false, { { 0, 3, C }, { 3, 3, C }, { 6, 4, C }, { 10, 2, C }, { 12, 4, C }, { 0, 6, B }, { 6, 4, B }, { 10, 6, B } } },
        { "Dance 1", 16, false, { { 2, 2, C }, { 6, 2, C }, { 10, 2, C }, { 14, 2, C }, { 0, 2, B }, { 4, 2, B }, { 8, 2, B }, { 12, 2, B } } },
        { "Dance 2", 16, false, { { 0, 2, C }, { 3, 2, C }, { 6, 2, C }, { 10, 2, C }, { 12, 2, C }, { 2, 2, B }, { 6, 2, B }, { 10, 2, B }, { 14, 2, B } } },
        { "Rock", 16, false, { { 0, 2, C }, { 2, 2, C }, { 4, 2, C }, { 6, 2, C }, { 8, 2, C }, { 10, 2, C }, { 12, 2, C }, { 14, 2, C },
                               { 0, 2, B }, { 2, 2, B }, { 4, 2, B }, { 6, 2, B }, { 8, 2, B }, { 10, 2, B }, { 12, 2, B }, { 14, 2, B } } },
        { "Swing", 16, false, { { 4, 2, C }, { 12, 2, C }, { 0, 4, B }, { 4, 4, F }, { 8, 4, B }, { 12, 4, F } } },
        { "Waltz", 12, false, { { 0, 4, B }, { 4, 3, C }, { 8, 3, C } } },
        { "Reggae", 16, false, { { 4, 1, C }, { 12, 1, C }, { 0, 3, B }, { 6, 2, B }, { 8, 3, B } } },
        { "Bossa Nova", 16, false, { { 0, 2, C }, { 3, 2, C }, { 6, 2, C }, { 10, 2, C }, { 13, 2, C }, { 0, 6, B }, { 6, 2, F }, { 8, 6, B }, { 14, 2, F } } },
        { "Funk", 16, false, { { 0, 1, C }, { 3, 1, C }, { 6, 1, C }, { 10, 1, C }, { 11, 1, C }, { 14, 1, C },
                               { 0, 2, B }, { 3, 1, B }, { 7, 1, B }, { 10, 2, B }, { 14, 1, B } } },
        { "Strumming 1", 16, true, { { 0, 4, C }, { 4, 2, C }, { 6, 4, C }, { 10, 2, C }, { 12, 4, C }, { 0, 8, B }, { 8, 8, B } } },
        { "Strumming 2", 16, true, { { 0, 2, C }, { 2, 2, C }, { 4, 2, C }, { 6, 2, C }, { 8, 2, C }, { 10, 2, C }, { 12, 2, C }, { 14, 2, C },
                                     { 0, 16, B } } },
        { "Arp Up", 16, false, { { 0, 2, 0 }, { 2, 2, 1 }, { 4, 2, 2 }, { 6, 2, 3 }, { 8, 2, 4 }, { 10, 2, 5 }, { 12, 2, 6 }, { 14, 2, 7 }, { 0, 16, B } } },
        { "Arp Down", 16, false, { { 0, 2, 7 }, { 2, 2, 6 }, { 4, 2, 5 }, { 6, 2, 4 }, { 8, 2, 3 }, { 10, 2, 2 }, { 12, 2, 1 }, { 14, 2, 0 }, { 0, 16, B } } },
        { "Arp Up/Down", 16, false, { { 0, 2, 0 }, { 2, 2, 1 }, { 4, 2, 2 }, { 6, 2, 3 }, { 8, 2, 4 }, { 10, 2, 3 }, { 12, 2, 2 }, { 14, 2, 1 }, { 0, 16, B } } },
        { "Melodic 1", 16, false, { { 0, 1, 0 }, { 1, 1, 1 }, { 2, 1, 2 }, { 3, 1, 1 }, { 4, 1, 0 }, { 5, 1, 1 }, { 6, 1, 2 }, { 7, 1, 1 },
                                    { 8, 1, 0 }, { 9, 1, 1 }, { 10, 1, 2 }, { 11, 1, 1 }, { 12, 1, 0 }, { 13, 1, 1 }, { 14, 1, 2 }, { 15, 1, 1 }, { 0, 16, B } } },
        { "Melodic 2", 16, false, { { 0, 2, 0 }, { 2, 2, 2 }, { 4, 2, 1 }, { 6, 2, 3 }, { 8, 2, 2 }, { 10, 2, 4 }, { 12, 2, 3 }, { 14, 2, 5 }, { 0, 16, B } } },
    };
    return list;
}

inline juce::StringArray styleNames()
{
    juce::StringArray names;
    for (const auto& s : styles())
        names.add (s.name);
    return names;
}

//==============================================================================
// Arpeggio patterns (Chord tab of OneMotion). Each step is a list of chord tones, "1" = lowest note,
// numbers above the chord size wrap up an octave; "." is a rest, "13" plays tones 1 and 3 together.
struct ArpPattern
{
    juce::String name;
    std::vector<std::vector<int>> steps;   // 0-based tone indices per step, empty = rest
};

inline const std::vector<ArpPattern>& arpPatterns()
{
    static const std::vector<ArpPattern> list = []
    {
        const char* defs[] {
            "1 2", ". 1 2", "1 2 3", "1 3 2", ". 1 2 1", ". 1 2 3",
            "1 2 3 2", "1 2 3 4", "1 2 4 3", "1 3 2 3", "1 3 2 4", "1 4 3 2", "2 1 2 3",
            "1 2 3 4 3 2", "1 2 3 1 2 4", "1 2 3 2 3 2", "1 3 2 3 4 3", "2 1 2 3 4 3", "1 3 2 3 4 3 2 3",
            "13 2", "23 1"
        };

        std::vector<ArpPattern> out;
        for (auto* def : defs)
        {
            ArpPattern p;
            juce::StringArray parts;
            bool split = false;
            for (const auto& token : juce::StringArray::fromTokens (def, " ", {}))
            {
                std::vector<int> tones;
                if (token != ".")
                    for (auto ch : token)
                        tones.push_back ((int) (ch - '1'));
                split = split || tones.size() > 1;
                p.steps.push_back (tones);
                parts.add (token == "." ? "x" : token);
            }
            p.name = (split ? "Split " : "Arp ") + parts.joinIntoString ("-");
            out.push_back (std::move (p));
        }
        return out;
    }();
    return list;
}

// Index 0 = "Off": the style plays its own chord part
inline juce::StringArray arpNames()
{
    juce::StringArray names { "Off (style)" };
    for (const auto& a : arpPatterns())
        names.add (a.name);
    return names;
}

inline const juce::StringArray arpStepNames { "1/4", "1/8", "1/16", "1/8T", "1/16T" };

inline double arpStepBeats (int index)
{
    static const double beats[] { 1.0, 0.5, 0.25, 1.0 / 3.0, 1.0 / 6.0 };
    return beats[juce::jlimit (0, 4, index)];
}

struct ArpSettings
{
    int pattern = 0;     // 0 = off, otherwise arpPatterns()[pattern - 1]
    int step = 1;        // index in arpStepNames
    bool mirror = false; // count tones from the top note down

    bool operator== (const ArpSettings& o) const { return pattern == o.pattern && step == o.step && mirror == o.mirror; }
    bool operator!= (const ArpSettings& o) const { return ! (*this == o); }
};

//==============================================================================
struct NoteEvent
{
    double ppq;
    int note;
    bool on;
};

inline double totalBeats (const Progression& prog)
{
    double total = 0.0;
    for (const auto& c : prog)
        total += c.beats;
    return total;
}

// Chord tone k: up the stacked voicing, or from the top note down when mirrored
inline int chordTone (const std::vector<int>& stack, int chordSize, int k, bool mirror)
{
    if (! mirror)
        return k < (int) stack.size() ? stack[(size_t) k] : stack[(size_t) (k % chordSize)] + 12 * (k / chordSize);
    const int n = juce::jmin (chordSize, (int) stack.size());
    return stack[(size_t) (n - 1 - k % n)] - 12 * (k / n);
}

// The arpeggio restarts on every chord and loops until the chord ends
inline void addArpEvents (const std::vector<int>& notes, int chordSize, double chordStart, double chordBeats,
                          const ArpPattern& pattern, double stepBeats, bool mirror, std::vector<NoteEvent>& out)
{
    const auto numSteps = (long long) std::ceil (chordBeats / stepBeats - 1.0e-6);
    for (long long i = 0; i < numSteps; ++i)
    {
        const auto& tones = pattern.steps[(size_t) (i % (long long) pattern.steps.size())];
        const double t0 = chordStart + (double) i * stepBeats;
        const double t1 = chordStart + std::min ((double) (i + 1) * stepBeats, chordBeats) - std::min (0.02, stepBeats * 0.1);
        for (int k : tones)
        {
            const int note = chordTone (notes, chordSize, k, mirror);
            out.push_back ({ t0, note, true });
            out.push_back ({ t1, note, false });
        }
    }
}

inline void addChordEvents (const ChordItem& chord, const Voicing& voicing, double chordStart, const Style& style,
                            const ArpSettings& arp, std::vector<NoteEvent>& out)
{
    const auto notes = chordNotes (chord, voicing);
    const int chordSize = (int) chord.intervals.size();
    if (notes.empty())
        return;

    const bool useArp = arp.pattern > 0 && arp.pattern <= (int) arpPatterns().size();
    if (useArp)
        addArpEvents (notes, chordSize, chordStart, chord.beats, arpPatterns()[(size_t) (arp.pattern - 1)], arpStepBeats (arp.step), arp.mirror, out);

    const int bassPitch = bassNote (chord);
    const int chordLen = chord.beats * 4;

    for (int rep = 0; rep * style.length < chordLen; ++rep)
    {
        for (const auto& h : style.hits)
        {
            const int on = rep * style.length + h.pos;
            if (on >= chordLen)
                continue;
            if (useArp && h.voice != bass && h.voice != bassFifth)
                continue;   // the arpeggio replaces the chord part, the style keeps its bass

            const double t0 = chordStart + on / 4.0;
            const double t1 = chordStart + std::min (on + h.len, chordLen) / 4.0 - 0.02;
            auto add = [&] (int note, double delay)
            {
                out.push_back ({ t0 + delay, note, true });
                out.push_back ({ t1, note, false });
            };

            if (h.voice == fullChord)
            {
                for (int i = 0; i < chordSize && i < (int) notes.size(); ++i)
                    add (notes[(size_t) i], style.strum ? (double) i * 0.03 : 0.0);
            }
            else if (h.voice == bass)      add (bassPitch, 0.0);
            else if (h.voice == bassFifth) add (bassPitch + 7, 0.0);
            else
            {
                add (chordTone (notes, chordSize, h.voice, false), 0.0);
            }
        }
    }
}

// All events whose time falls in [start, end), the progression looping forever.
// Sorted with note-offs first on ties.
inline void collectEvents (double start, double end, const Progression& prog, const Voicing& voicing, int styleIndex, const ArpSettings& arp,
                           std::vector<NoteEvent>& out)
{
    out.clear();
    const double loop = totalBeats (prog);
    if (loop <= 0.0)
        return;

    const auto& style = styles()[(size_t) juce::jlimit (0, (int) styles().size() - 1, styleIndex)];
    const auto firstCycle = (long long) std::floor (start / loop) - 1;
    const auto lastCycle = (long long) std::floor (end / loop);

    for (auto cycle = firstCycle; cycle <= lastCycle; ++cycle)
    {
        double t = (double) cycle * loop;
        for (const auto& chord : prog)
        {
            if (t + chord.beats >= start && t < end)
                addChordEvents (chord, voicing, t, style, arp, out);
            t += chord.beats;
        }
    }

    out.erase (std::remove_if (out.begin(), out.end(), [=] (const NoteEvent& e) { return e.ppq < start || e.ppq >= end; }), out.end());
    std::stable_sort (out.begin(), out.end(), [] (const NoteEvent& a, const NoteEvent& b)
    {
        if (a.ppq != b.ppq)
            return a.ppq < b.ppq;
        return ! a.on && b.on;
    });
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
