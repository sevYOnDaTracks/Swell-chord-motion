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

inline juce::String chordName (const ChordItem& c) { return keyNames[c.root] + chordSuffix (c.intervals); }

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

// Close voicing with every root between G#2 and G3; bass two octaves under the chord root
inline std::vector<int> chordNotes (const ChordItem& c)
{
    const int base = 48 + c.root;   // same octave for every root, so A and B sit above G
    std::vector<int> notes;
    for (int i : c.intervals)
        notes.push_back (base + i);
    return notes;
}

inline int bassNote (const ChordItem& c) { return 36 + c.root; }

//==============================================================================
enum Voice { fullChord = -1, bass = -2, bassFifth = -3 };

struct Hit { int pos, len, voice; };   // in 16th notes; voice >= 0 = chord tone index (wraps up octaves)

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

inline void addChordEvents (const ChordItem& chord, double chordStart, const Style& style, std::vector<NoteEvent>& out)
{
    const auto notes = chordNotes (chord);
    if (notes.empty())
        return;

    const int bassPitch = bassNote (chord);
    const int chordLen = chord.beats * 4;

    for (int rep = 0; rep * style.length < chordLen; ++rep)
    {
        for (const auto& h : style.hits)
        {
            const int on = rep * style.length + h.pos;
            if (on >= chordLen)
                continue;

            const double t0 = chordStart + on / 4.0;
            const double t1 = chordStart + std::min (on + h.len, chordLen) / 4.0 - 0.02;
            auto add = [&] (int note, double delay)
            {
                out.push_back ({ t0 + delay, note, true });
                out.push_back ({ t1, note, false });
            };

            if (h.voice == fullChord)
            {
                for (size_t i = 0; i < notes.size(); ++i)
                    add (notes[i], style.strum ? (double) i * 0.03 : 0.0);
            }
            else if (h.voice == bass)      add (bassPitch, 0.0);
            else if (h.voice == bassFifth) add (bassPitch + 7, 0.0);
            else
            {
                const int n = (int) notes.size();
                add (notes[(size_t) (h.voice % n)] + 12 * (h.voice / n), 0.0);
            }
        }
    }
}

// All events whose time falls in [start, end), the progression looping forever.
// Sorted with note-offs first on ties.
inline void collectEvents (double start, double end, const Progression& prog, int styleIndex, std::vector<NoteEvent>& out)
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
                addChordEvents (chord, t, style, out);
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
