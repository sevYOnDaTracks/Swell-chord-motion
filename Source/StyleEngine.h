#pragma once

#include "ChordEngine.h"
#include <cmath>
#include <limits>
#include <map>
#include <string>

// Accompaniment styles, ported from OneMotion's Chord Player: the same 57 presets, written in the same
// pattern notation, played by the same rules (tick grid, shuffle, sustain pedal, "first" / "last beat"
// conditions, "@" anticipations, envelopes). Pure functions, shared by the audio thread and the MIDI export.
namespace ChordEngine
{
//==============================================================================
// Pattern notation: space separated steps, "." = rest. A step holds one or more items:
//   1..9  chord tone (1 = lowest)        x = every chord tone        2* = tones from 2 up
//   0#    whole chord, only on the first hit of a chord               0 = nothing
//   + / - octave up / down   f fifth   t third   < / > scale step   \ / half step   & fit in range
//   s sustain   ! half length   ' very short   , no length   m softer   # first hit only   ? last beat only
//   S2 / S4 sus2 / sus4   T third only   @ the next chord starts here when this is its last beat
//   (volume=v) (pitch=p) envelopes
struct PatternItem
{
    enum Condition { always, first, notFirst, lastBeat };
    enum Modify { noModify, sus2, sus4, onlyThird };

    int n = 0;
    bool remaining = false, sustain = false, fifth = false, third = false, fit = false, dir = false, next = false;
    int octave = 0, halfSteps = 0, scaleSteps = 0;
    double duration = 1.0, velocity = 1.0, offset = 0.0;
    Condition condition = always;
    Modify modify = noModify;
};

struct PatternStep
{
    std::vector<PatternItem> items;
    bool early = false;
    bool hasVolume = false, hasPitch = false;
    double volume = 1.0, pitch = 0.0;
};

struct Pattern
{
    std::vector<std::optional<PatternStep>> steps;   // one entry per step, empty = rest
    int length() const { return (int) steps.size(); }
};

namespace detail
{
// Same matches as OneMotion's item regex: _?[@^`]?-?[x0-9](S[24]|T)?[tf+\-&%<>/\\*]*[s!',]?m?[#?]?(\(.+?\))*
inline size_t matchItem (const std::string& s, size_t p)
{
    auto at = [&] (size_t i) { return i < s.size() ? s[i] : '\0'; };
    auto in = [] (char c, const char* set) { return c != '\0' && std::strchr (set, c) != nullptr; };

    const size_t start = p;
    if (at (p) == '_') ++p;
    if (in (at (p), "@^`")) ++p;
    if (at (p) == '-') ++p;
    if (! (at (p) == 'x' || (at (p) >= '0' && at (p) <= '9')))
        return 0;
    ++p;
    if (at (p) == 'S' && in (at (p + 1), "24")) p += 2;
    else if (at (p) == 'T') ++p;
    while (in (at (p), "tf+-&%<>/\\*")) ++p;
    if (in (at (p), "s!',")) ++p;
    if (at (p) == 'm') ++p;
    if (in (at (p), "#?")) ++p;
    while (at (p) == '(')
    {
        const auto close = s.find (')', p + 2);
        if (close == std::string::npos)
            break;
        p = close + 1;
    }
    return p - start;
}

inline void parseItem (std::string e, double& offsetAccum, PatternStep& step)
{
    PatternItem t;
    auto last = [&] { return e.empty() ? '\0' : e.back(); };
    auto pop = [&] { e.pop_back(); };

    if (! e.empty() && e[0] == '_') { offsetAccum += 0.125; t.offset = offsetAccum; e.erase (0, 1); }
    if (! e.empty() && e[0] == '^') { t.next = true; step.early = true; e.erase (0, 1); }
    if (! e.empty() && e[0] == '@') { step.early = true; e.erase (0, 1); }
    if (! e.empty() && e[0] == '`') e.erase (0, 1);

    while (last() == ')')
    {
        const auto open = e.rfind ('('), eq = e.rfind ('=');
        const auto key = e.substr (open + 1, eq - open - 1);
        const double value = std::atof (e.substr (eq + 1, e.size() - eq - 2).c_str());
        if (key == "volume") { step.hasVolume = true; step.volume = value; }
        if (key == "pitch")  { step.hasPitch = true;  step.pitch = value; }
        e.erase (open);
    }

    if (last() == '#') { pop(); t.condition = PatternItem::first; }
    if (last() == '?') { pop(); t.condition = PatternItem::lastBeat; }
    if (last() == 'm') { t.velocity = 0.75; pop(); }
    if (last() == 's') { t.sustain = true; pop(); }
    if (last() == '!') { t.duration = 0.5; pop(); }
    if (last() == '\'') { t.duration = 0.1; pop(); }
    if (last() == ',') { t.duration = 0.0; pop(); }
    while (last() == '&') { t.fit = true; pop(); }
    while (last() == '%') { t.dir = true; pop(); }
    while (last() == '+') { ++t.octave; pop(); }
    while (last() == '-') { --t.octave; pop(); }
    while (last() == '\\') { --t.halfSteps; pop(); }
    while (last() == '/') { ++t.halfSteps; pop(); }
    while (last() == '<') { --t.scaleSteps; pop(); }
    while (last() == '>') { ++t.scaleSteps; pop(); }
    while (last() == 'f') { t.fifth = true; pop(); }
    while (last() == 't') { t.third = true; pop(); }
    if (e.size() >= 2 && e[e.size() - 2] == 'S')
    {
        t.modify = e.back() == '2' ? PatternItem::sus2 : PatternItem::sus4;
        e.erase (e.size() - 2);
    }
    if (last() == 'T') { t.modify = PatternItem::onlyThird; pop(); }
    if (e == "x" || last() == '*')
    {
        pop();
        if (t.condition == PatternItem::first)
            t.condition = PatternItem::always;
        t.remaining = true;
    }

    const int d = e.empty() ? 0 : std::atoi (e.c_str()) - 1;
    t.n = d;
    if (t.condition == PatternItem::first)
    {
        if (d == -1)
        {
            t.remaining = true;
            t.n = 0;
        }
        else
        {
            t.condition = t.fifth ? PatternItem::notFirst : PatternItem::always;
            PatternItem whole;
            whole.remaining = true;
            whole.sustain = t.sustain;
            whole.condition = PatternItem::first;
            step.items.push_back (whole);
        }
    }
    if (t.remaining || t.condition == PatternItem::first || d > -1)
        step.items.push_back (t);
}
} // namespace detail

inline Pattern parsePattern (const std::string& list)
{
    Pattern pattern;
    for (const auto& token : juce::StringArray::fromTokens (juce::String (list), " ", {}))
    {
        const std::string s = token.toStdString();
        if (s.empty() || s == ".")
        {
            pattern.steps.emplace_back();
            continue;
        }

        PatternStep step;
        double offset = 0.0;
        for (size_t p = 0; p < s.size();)
        {
            if (const auto len = detail::matchItem (s, p))
            {
                detail::parseItem (s.substr (p, len), offset, step);
                p += len;
            }
            else
            {
                ++p;
            }
        }
        pattern.steps.emplace_back (std::move (step));
    }
    return pattern;
}

//==============================================================================
struct TimeSignature
{
    int beats = 4;              // beats per bar
    double beatScale = 1.0;     // quarter notes per beat
    int beatDiv = 2;
    int numerator = 4, denominator = 4;
};

inline TimeSignature timeSignature (const juce::String& name)
{
    if (name == "3/4") return { 3, 1.0, 2, 3, 4 };
    if (name == "6/8") return { 2, 1.5, 3, 6, 8 };
    return {};
}

struct Part
{
    Pattern pattern;
    int stepNum = 1, stepDen = 1;   // step length as a fraction of the bar
    bool loop = true, mirror = false, doubleOctave = false;
    int cropLength = 0;             // the pattern restarts every N bars (0 = never)
    int numNotes = 0;
    int octave = 4;
    int octaveOffset = 7;           // semitones above C of `octave`: chords start from G3 by default
    double noteDuration = 1.0, velocity = 0.7;
};

enum class Sustain { none, chord, twoBeats };

struct StyleDef
{
    juce::String name;
    Part chord, bass;
    double shuffle = 0.0;           // 2:1 = 1/3
    Sustain sustain = Sustain::none;
    TimeSignature time;
    int tempo = 0;                  // preset tempo, 0 = keep
    bool hasEnvelopes = false;
};

namespace detail
{
// OneMotion's chord / bass styles (the ones the presets use, plus every arpeggio)
struct BaseStyle
{
    const char* value;
    const char* list;
    int stepNum = 0, stepDen = 0;   // 0 = none
    int beatDiv = 0;
    bool customStep = false, arp = false, loop = true;
    int cropLength = -1;            // -1 = none given
};

inline const std::vector<BaseStyle>& baseStyles()
{
    static const std::vector<BaseStyle> list {
        { "once", "xs", 1, 1, 0, false, false, false },
        { "bar", "xs", 1, 1 },
        { "backbeat", ". x", 1, 4 },
        { "beat", "x", 0, 0, 1 },
        { "offbeat", ". x", 0, 0, 2 },
        { "pulse", "x", 0, 0, 0, true },
        { "split-13-2", "13 2", 0, 0, 0, true },
        { "split-23-1", "23 1", 0, 0, 0, true },
        { "arp-1-2", "1 2", 0, 0, 0, true },
        { "arp-x-1-2", ". 1 2", 0, 0, 0, true },
        { "arp-1-2-3", "1 2 3", 0, 0, 0, true },
        { "arp-1-3-2", "1 3 2", 0, 0, 0, true },
        { "arp-x-1-2-1", ". 1 2 1", 0, 0, 0, true },
        { "arp-x-1-2-3", ". 1 2 3", 0, 0, 0, true },
        { "arp-1-2-3-2", "1 2 3 2", 0, 0, 0, true },
        { "arp-1-2-3-4", "1 2 3 4", 0, 0, 0, true },
        { "arp-1-2-4-3", "1 2 4 3", 0, 0, 0, true },
        { "arp-1-3-2-3", "1 3 2 3", 0, 0, 0, true },
        { "arp-1-3-2-4", "1 3 2 4", 0, 0, 0, true },
        { "arp-1-4-3-2", "1 4 3 2", 0, 0, 0, true },
        { "arp-2-1-2-3", "2 1 2 3", 0, 0, 0, true },
        { "arp-1-2-3-4-3-2", "1 2 3 4 3 2", 0, 0, 0, true },
        { "arp-1-2-3-1-2-4", "1 2 3 1 2 4", 0, 0, 0, true },
        { "arp-1-2-3-2-3-2", "1 2 3 2 3 2", 0, 0, 0, true },
        { "arp-1-3-2-3-4-3", "1 3 2 3 4 3", 0, 0, 0, true },
        { "arp-2-1-2-3-4-3", "2 1 2 3 4 3", 0, 0, 0, true },
        { "arp-1-3-2-3-4-3-2-3", "1 3 2 3 4 3 2 3", 0, 0, 0, true },
        { "straddle-run-1", "13 2 24 3 35 4 46 5 57 6 46 5 35 4 24 3", 1, 8, 0, true, false, true, 0 },
        { "bass-octave-1", "1 1+", 0, 0, 0, true },
        { "bass-5th-up", "1s 1fs#", 1, 4, 0, true },
        { "bass-boogie-2", "1 1+ 1>> 1+ 1>>>> 1+ 1>>>>> 1>>>>", 1, 8 },
        { "modulation-1", "0s#(volume=1) 0(volume=0.5)", 1, 16 },
        { "modulation-2", "0s#(volume=1) 0(volume=0.5)", 1, 32 },
        { "modulation-3", "0s#(volume=0.5) 0(volume=0) 0(volume=1) 0(volume=0)", 1, 16 },
        { "modulation-4", "0s#(pitch=-0.1) 0s#(pitch=0.1) 0s#(pitch=-0.2) 0s#(pitch=-0.2) 0s#(pitch=-0.3) 0s#(pitch=-0.3)", 1, 32 },
        { "arpeggio", "", 0, 0, 0, true, true },
    };
    return list;
}

inline const BaseStyle& baseStyle (const juce::String& value)
{
    for (const auto& b : baseStyles())
        if (value == b.value)
            return b;
    jassertfalse;
    return baseStyles().front();
}

// A preset as OneMotion writes it
struct PartSrc
{
    juce::String style, arp;
    int stepNum = 0, stepDen = 0;
    bool mirror = false, doubleOctave = false;
    int numNotes = 0, octave = 0, octaveOffset = std::numeric_limits<int>::min();
    double noteDuration = 1.0;
};

struct PresetSrc
{
    juce::String name;
    PartSrc chord, bass;
    juce::String shuffle = "1:1", sustain, time = "4/4";
    int tempo = 0;

    PresetSrc& c (const char* s, const char* a = "", int sn = 0, int sd = 0) { chord.style = s; chord.arp = a; chord.stepNum = sn; chord.stepDen = sd; return *this; }
    PresetSrc& b (const char* s, const char* a = "", int sn = 0, int sd = 0) { bass.style = s; bass.arp = a; bass.stepNum = sn; bass.stepDen = sd; return *this; }
    PresetSrc& bOct (int o) { bass.octave = o; return *this; }
    PresetSrc& cOct (int o) { chord.octave = o; return *this; }
    PresetSrc& cOffset (int o) { chord.octaveOffset = o; return *this; }
    PresetSrc& cNotes (int n) { chord.numNotes = n; return *this; }
    PresetSrc& mirror() { chord.mirror = true; return *this; }
    PresetSrc& bDouble() { bass.doubleOctave = true; return *this; }
    PresetSrc& dur (double d) { chord.noteDuration = d; bass.noteDuration = d; return *this; }
    PresetSrc& swing() { shuffle = "2:1"; return *this; }
    PresetSrc& sus (const char* s) { sustain = s; return *this; }
    PresetSrc& ts (const char* t) { time = t; return *this; }
    PresetSrc& bpm (int t) { tempo = t; return *this; }
};

inline PresetSrc P (const char* name) { PresetSrc p; p.name = name; return p; }

inline const std::vector<PresetSrc>& presetSources()
{
    static const std::vector<PresetSrc> list {
        P ("Once").c ("once").b ("once"),
        P ("Basic 1").c ("split-23-1", "", 1, 8).b ("once").bOct (3).sus ("chord"),
        P ("Basic 2").c ("arp-1-2-3-2", "", 1, 8).b ("once").bOct (3).sus ("chord"),
        P ("Basic 3").c ("arp-1-3-2-3", "", 1, 8).mirror().b ("once").bOct (3).sus ("chord"),
        P ("Basic 4").c ("arp-1-2-3-1-2-4", "", 1, 8).b ("once").bOct (3).sus ("chord"),
        P ("Pop 1").c ("beat").b ("arpeggio", "1!1+! . . . . . . @1f! 1!1+! . . . . 1s1+s . .", 1, 8).bpm (120).swing(),
        P ("Pop 2").c ("arpeggio", "x . . . . x? . . 0# . . . . x . .", 1, 16)
                   .b ("arpeggio", "11+ . . 1 . . 1+m? 1fm? 11+ . . 1 1+ . 1+m 1fm", 1, 16).bpm (90).sus ("chord"),
        P ("Pop 3").c ("arpeggio", "xs . . . 1s3s 2s . @xs . . . . 12 . 3- .", 1, 16)
                   .b ("arpeggio", "1s1+s . . 1+! . . @1s . . . . 1+! . 1+! . .", 1, 16).bpm (90).sus ("chord"),
        P ("Pop 4").c ("arpeggio", "xS2 xT . . xm . . . xm . . . xm . . . x . . . xm . . . xm . . . 3 1 2 3 xS2 xT . . xm . . . xm . . . xm . . . x . . . xm . . . xm . . . 1 2 3 1", 1, 16).cNotes (3)
                   .b ("arpeggio", "1 . . @1f# . . . . 1 . . @1f# . 1f# . .", 1, 8).bOct (3).bpm (90).sus ("chord"),
        P ("Dance 1").c ("arpeggio", "x . . @x . . @x . . x . . x . . x", 1, 16).b ("bass-octave-1").sus ("2-beats"),
        P ("Dance 2").c ("arpeggio", ". . x . . x . . x . . x . 1 2* 1", 1, 16)
                     .b ("arpeggio", "x . . . x . . @x . . x . . . . .", 1, 16).bOct (3).sus ("chord"),
        P ("Rock 1").c ("arpeggio", "xs . . . . . . . . . x! . . x! . .", 1, 16)
                    .b ("arpeggio", "xs x! xs x! xs x! xs x! xs x! xs x! xs x! xs x! xs x! xs x! xs x! xs x! xs x! xs x! xs x! xs @1<!", 1, 8),
        P ("Rock 2").c ("arpeggio", "xs . . xs . . x! .", 1, 8)
                    .b ("arpeggio", "1s . . . 1+ . . . . . 1s . 1+ . . . 1s . . . 1+ . . . . . 1s . 1+ . 1s .", 1, 16).bpm (120),
        P ("Rock 3").c ("arpeggio", ". . . . xs . . @2 . 13 . . x . 2 13 . . 1! . xs . . @2 . 13 . . x . 2 13", 1, 16)
                    .b ("arpeggio", "1 . . . . . . . . . 1 . . . . .", 1, 16).bOct (3).bpm (120),
        P ("Rock 4").c ("arpeggio", "234 . 234 . 1 234 . @234 . 1 234 . 1 234 . 1", 1, 8).b ("bass-octave-1", "", 1, 8).bpm (120).sus ("chord"),
        P ("Swing 1").c ("arpeggio", ". . . xs# . . 0!# .", 1, 8)
                     .b ("arpeggio", "1s . . . . . . @1f& 1s . . . . . . @1f& 1s . . . . . . @1f& 1s . . . . . @1f& 1<%", 1, 8).bOct (3).swing().bpm (130),
        P ("Swing 2").c ("arpeggio", "x! . . @xs . . . . . x! . . . x! . .", 1, 8)
                     .b ("arpeggio", R"(1s . 1fs . 1+s 1 1fs# . 1+s . 1fs . 1s @1// 1/s . 1s . 1fs . 1+s 1 1fs# . 1+s . 1fs . 1s @1// 1/ 1+)", 1, 8).swing().bpm (130),
        P ("Waltz").ts ("3/4").c ("arpeggio", ". x x", 1, 3).b ("arpeggio", "1 . . 1f# . .", 1, 3).bOct (3).sus ("chord"),
        P ("Jazz Waltz").ts ("3/4").c ("arpeggio", ". x! . . xs .", 1, 6)
                        .b ("arpeggio", "1 . . @1 . @1f 1 . . @1 . @1f 1 . . @1 . @1f 1 . . @1 . .", 1, 6).bOct (3).swing().bpm (120),
        P ("Reggae").c ("arpeggio", ". . x! . . . x! . . . x! 231+ . . x! . . . x! . . . x! . . . x! 2!3!1+! . 2!3!1+! x! .", 1, 16)
                    .b ("arpeggio", "1 1 . . . 1+! . .", 1, 16).swing().bpm (80),
        P ("Salsa 1").c ("arpeggio", "14 2 3 14 . 23 @0 14 . 23 . @14 . 23 @0 14", 1, 16).cNotes (3)
                     .b ("arpeggio", "1s . . @1f&s# . . @1s . . . . @1f&s# . . @1s . . . . @1f&s# . . @1s . . . . @1f&s# . . . .", 1, 16).bOct (3),
        P ("Salsa 2").c ("arpeggio", "3# . 4 1# . 2 @0 3m# . 4 . @1# . 2 @0 3", 1, 16)
                     .b ("arpeggio", "1s . . @1f&s# . . @1s . . . . @1f&s# . . @1s . . . . @1f&s# . . @1s . . . . @1f&s# . . . .", 1, 16).bOct (3),
        P ("Bossa Nova 1").c ("arpeggio", "x! . xs . . x . @x! . xs . . x! . x! .", 1, 8)
                          .b ("arpeggio", R"(1s . . 1 1fs# . . @1s . . . 1 1fs# . . @1f- 1s . . 1 1fs# . . @1s . . . 1 1fs# . @1\\ 1\)", 1, 8).bOct (3).bpm (120),
        P ("Bossa Nova 2").c ("arpeggio", ". . . . x . . . . . x! . . . . . . . . . xs . . . . . . . x! . . .", 1, 16)
                          .b ("arpeggio", "1! . . . . . 1f! . 1fs# . . . . . @1s . . . 1!m . . . 1 . 1+! . 1<+! . 1f! . 1f<! .", 1, 16).bpm (120),
        P ("Samba").c ("arpeggio", "x! . x @1! . x! . @x! . x! . @x! . x! . 1!", 1, 8)
                   .b ("arpeggio", "1s . . . 1f&s# . . . 1s . . . 1f&s# . . @1f&!", 1, 8).bOct (3).bpm (120),
        P ("Merengue").c ("arpeggio", ". x! . x! . . x! . . . x! . . . x! .", 1, 16)
                      .b ("arpeggio", "1s . . . 1f&s# . . . 1s . . . 1f&s# . . .", 1, 16).bOct (3).bpm (120),
        P ("Cumbia").c ("arpeggio", ". x!", 1, 8).b ("arpeggio", "1 . 2! 3!", 1, 8).bOct (3).bpm (100),
        P ("Reggaeton").c ("arpeggio", ". . . x! . . x .", 1, 16).b ("arpeggio", "1 . 1f&# . 1 . 1f&# 1?", 1, 8).bOct (3).bpm (100),
        P ("Tango").c ("arpeggio", ". . . 1 2*! . 1 . . . . 1 2*! . 2*! .", 1, 8)
                   .b ("arpeggio", "1s . . 1f# 1+# . 1f# .", 1, 8).bOct (3).bpm (120).sus ("chord"),
        P ("Flamenco 1").ts ("6/8").c ("arpeggio", ". xs .", 1, 6).b ("arpeggio", "11f . 1+s", 1, 6).bOct (3).dur (0.3).bpm (100),
        P ("Flamenco 2").ts ("6/8").c ("arpeggio", ". . 13 2 1s3s . . . 1m3 . 2s .", 1, 12)
                        .b ("arpeggio", "11f . . . . . 1s1fs . . . . .", 1, 12).bOct (3).dur (0.3).bpm (100),
        P ("Flamenco 3").c ("arpeggio", ". . . . 1s 2s 3s 4s 1,2,3,4, . . . 1!2!3!4! . . . . . . . 1s . . . 2s3s4s . . . . . . .", 1, 32).cNotes (3)
                        .b ("arpeggio", "1s . 2s 3s", 1, 4).bOct (3).bpm (100),
        P ("Ragtime").c ("offbeat")
                     .b ("arpeggio", "1 . 1f- . 1 . 1f- . 1 . 1f- . 1 . 1f- . 1 . 1f- . 1 . 1f- . 1 . 1f- . 1 . 1f- @1f&?", 1, 8).bOct (3),
        P ("Country 1").c ("backbeat").b ("arpeggio", "1s . . @2 3 . 2 3", 1, 8).bOct (3).swing().sus ("2-beats"),
        P ("Country 2").c ("arpeggio", ". . 123 1 . 1 23 1", 1, 8)
                       .b ("arpeggio", "1s . . . 1f . 1f> . 1s . . . 1f> . 1f .", 1, 8).bOct (3).swing().sus ("chord"),
        P ("Polyrhythm").ts ("6/8").c ("arp-1-2", "", 1, 6).mirror().b ("bass-5th-up", "", 1, 4).bOct (3).sus ("chord").bpm (80),
        P ("Straddle run").c ("straddle-run-1").cNotes (3).b ("once").bDouble().sus ("chord"),
        P ("Strumming 1").c ("arpeggio", "2*# . . 1m 2*m 1m @2*# . . 1m 2*m 1m 2*# . . .", 1, 16).b ("once").bOct (3).sus ("chord"),
        P ("Strumming 2").c ("arpeggio", "2*# . 2*m 1m 2*m . @2*# . 2*m 1m 2*m . 2*# . . .", 1, 16).b ("once").bOct (3).sus ("chord"),
        P ("Boogie").c ("arpeggio", ". x! . . x . . . x . . @x! . . . .", 1, 8).b ("bass-boogie-2").bOct (3).swing(),
        P ("Funk 1").c ("backbeat").b ("arpeggio", "1s . . . . . @1<+! . 1+! . 1f<! 1f! . 1<+! 1+ .", 1, 16).swing(),
        P ("Funk 2").c ("backbeat").b ("arpeggio", "1s . . . . . @1f<! 1f! 1<+! . . 1+! . . @1s . . . . . . . . . 1+ . . . 1<+ . . .", 1, 16).swing(),
        P ("Funk 3").c ("backbeat").b ("arpeggio", "1s . . 1!m . . @1<+! . 1+! . . . . . . . 1! . . . . . @1<+! . 1+! . . 1!m . . 1<+! 1+", 1, 16).swing(),
        P ("Funk 4").c ("backbeat").b ("arpeggio", "1s . . 1+! . 1ms! @1<+m! . 1+! . . . . . . . 1s . . 1+! . . . . . 1f<<+! 1f<+! 1f<<+! 1+! 1<+! 1f! 1f<!", 1, 16).swing(),
        P ("Funk 5").c ("arpeggio", "xs . . @x! . . . . . . . . x! . . . xs . . @x! . . . . . . . . . . . .", 1, 16)
                    .b ("arpeggio", "1 . . . . . . @1<+ 1+! 1<+ 1f! 1f<! . . @1< . 1 . . . . . . @1+! 1s . 1+! . 1<s . 1<+! .", 1, 16).swing(),
        P ("Melodic 1").c ("arpeggio", ". xs", 1, 2).b ("arpeggio", "1 1f 2<<+ 2<+ 2+ 2<+ 2<<+ 1f 1 1f 2<<+ 2<+ 2+ . . 1f", 1, 8).bOct (3).sus ("chord"),
        P ("Melodic 2").c ("arpeggio", ". 1 2 4 3+ 2 4 3+", 1, 8).cOffset (0).b ("pulse", "", 1, 1).bOct (3).sus ("chord"),
        P ("Melodic 3").c ("arpeggio", ". 1 2 3 4 2 3 4 1 2 3 1 2 4 2 3", 1, 16).cOffset (0).b ("pulse", "", 1, 1).bOct (3).sus ("chord").bpm (80),
        P ("Melodic 4").c ("arpeggio", ". . . x . . x .", 1, 8).b ("arpeggio", "1 1f 1+ . . 1f# . .", 1, 8).bOct (3).sus ("chord"),
        P ("Melodic 5").c ("arpeggio", ". 1 2 3 4 3 2 1 . 1 2 3 4 2 3 1", 1, 8).cOffset (0).b ("pulse", "", 1, 1).bOct (3).sus ("chord"),
        P ("Melodic 6").c ("arpeggio", "23 1 2 3 1 2 3 1 23 1 2 3> 1 2 3 1", 1, 8).b ("once").bOct (3).sus ("chord"),
        P ("Melodic 7").c ("arpeggio", "345 . 1 . 2 . @234 . . . . . 4<< 4< 4 . . . 234 . . . @23 . . . 21 . . . . .", 1, 16).cOct (4).cNotes (3)
                       .b ("arpeggio", "1 . . 0# . . . . 1 . . 0# . . . 3", 1, 8).bOct (3).sus ("chord"),
        P ("Melodic 8").c ("arpeggio", "2 1 @2> 1 2>> 1 2>>> @2 1 2> 1 @2>> 1 2> 1 @2<", 1, 8)
                       .b ("arpeggio", "1 . . . . . . . 0# . . . . 1+ . 1+", 1, 8).bOct (3).sus ("chord"),
        P ("Mod 1").c ("modulation-1").b ("modulation-1").bpm (100),
        P ("Mod 2").c ("modulation-2").b ("modulation-2").bpm (100),
        P ("Mod 3").c ("modulation-3").b ("modulation-3").bpm (100),
        P ("Mod 4").c ("modulation-4").b ("modulation-4").bpm (100),
    };
    return list;
}

// Loads one part the way OneMotion loads a preset (step, loop and crop rules included)
inline Part makePart (const PartSrc& src, const TimeSignature& ts, bool isBass)
{
    const auto& base = baseStyle (src.style);
    Part part;
    part.pattern = parsePattern (src.arp.isNotEmpty() ? src.arp.toStdString() : std::string (base.list));

    // (customStep ? preset step : style step) || style step || one step per beat subdivision
    int sn = base.customStep ? src.stepNum : base.stepNum;
    int sd = base.customStep ? src.stepDen : base.stepDen;
    if (sn == 0 && base.stepNum != 0) { sn = base.stepNum; sd = base.stepDen; }
    if (sn == 0) { sn = 1; sd = ts.beats * (base.beatDiv != 0 ? base.beatDiv : ts.beatDiv); }
    part.stepNum = sn;
    part.stepDen = sd;

    part.loop = base.loop;
    part.cropLength = base.cropLength >= 0 ? base.cropLength : (base.customStep && ! base.arp ? 1 : 0);
    part.mirror = src.mirror;
    part.doubleOctave = src.doubleOctave;
    part.numNotes = src.numNotes;
    part.noteDuration = src.noteDuration;
    if (isBass)
    {
        part.octave = src.octave != 0 ? src.octave : 2;
        part.octaveOffset = 0;
    }
    else
    {
        part.octave = src.octave != 0 ? src.octave : 4;
        part.octaveOffset = src.octaveOffset != std::numeric_limits<int>::min() ? src.octaveOffset : 7;
    }
    return part;
}
} // namespace detail

inline const std::vector<StyleDef>& styleDefs()
{
    static const std::vector<StyleDef> list = []
    {
        std::vector<StyleDef> out;
        for (const auto& src : detail::presetSources())
        {
            StyleDef s;
            s.name = src.name;
            s.time = timeSignature (src.time);
            s.chord = detail::makePart (src.chord, s.time, false);
            s.bass = detail::makePart (src.bass, s.time, true);
            s.shuffle = src.shuffle == "2:1" ? 1.0 / 3.0 : 0.0;
            s.sustain = src.sustain == "chord" ? Sustain::chord : src.sustain == "2-beats" ? Sustain::twoBeats : Sustain::none;
            s.tempo = src.tempo;
            for (const auto* part : { &s.chord, &s.bass })
                for (const auto& st : part->pattern.steps)
                    if (st && (st->hasVolume || st->hasPitch))
                        s.hasEnvelopes = true;
            out.push_back (std::move (s));
        }
        return out;
    }();
    return list;
}

inline juce::StringArray styleNames()
{
    juce::StringArray names;
    for (const auto& s : styleDefs())
        names.add (s.name);
    return names;
}

inline const StyleDef& styleDef (int index) { return styleDefs()[(size_t) juce::jlimit (0, (int) styleDefs().size() - 1, index)]; }

// Where each chord starts and ends, in quarter notes. Lengths count quarters of a bar, like OneMotion
// when it switches to 3/4 or 6/8: positions are scaled to the new bar and rounded to its beats
// (4-2-2 in 3/4 gives 3-2-1).
inline std::vector<double> chordBoundaries (const Progression& prog, int styleIndex)
{
    const auto& ts = styleDef (styleIndex).time;
    std::vector<double> bounds { 0.0 };
    double cumulative = 0.0;
    for (const auto& c : prog)
    {
        cumulative += c.beats;
        const double beats = std::floor (cumulative * ts.beats / 4.0 + 0.5);
        bounds.push_back (beats * ts.beatScale);
    }
    return bounds;
}

inline double loopLength (const Progression& prog, int styleIndex) { return chordBoundaries (prog, styleIndex).back(); }

// Index of the chord playing at a given time of the looping progression (-1 when empty)
inline int chordAtTime (const Progression& prog, int styleIndex, double time)
{
    const auto bounds = chordBoundaries (prog, styleIndex);
    const double loop = bounds.back();
    if (prog.empty() || loop <= 0.0)
        return -1;
    double pos = std::fmod (time, loop);
    if (pos < 0.0)
        pos += loop;
    for (size_t i = 0; i < prog.size(); ++i)
        if (pos < bounds[i + 1])
            return (int) i;
    return (int) prog.size() - 1;
}

//==============================================================================
// Arpeggios of the Chord tab: replace the chord part of the style (bass, sustain and swing stay)
inline const std::vector<juce::String>& arpLists()
{
    static const std::vector<juce::String> list {
        "1 2", ". 1 2", "1 2 3", "1 3 2", ". 1 2 1", ". 1 2 3",
        "1 2 3 2", "1 2 3 4", "1 2 4 3", "1 3 2 3", "1 3 2 4", "1 4 3 2", "2 1 2 3",
        "1 2 3 4 3 2", "1 2 3 1 2 4", "1 2 3 2 3 2", "1 3 2 3 4 3", "2 1 2 3 4 3", "1 3 2 3 4 3 2 3",
        "13 2", "23 1"
    };
    return list;
}

inline juce::StringArray arpNames()
{
    juce::StringArray names { "Off (style)" };
    for (const auto& l : arpLists())
    {
        const bool split = l.containsChar ('1') && juce::StringArray::fromTokens (l, " ", {})[0].length() > 1;
        names.add ((split ? "Split " : "Arp ") + l.replace (".", "x").replace (" ", "-"));
    }
    return names;
}

inline const juce::StringArray arpStepNames { "1/4", "1/8", "1/16", "1/8T", "1/16T" };

inline Part arpPart (const ArpSettings& arp)
{
    static const int dens[] { 4, 8, 16, 12, 24 };
    Part part;
    part.pattern = parsePattern (arpLists()[(size_t) juce::jlimit (0, (int) arpLists().size() - 1, arp.pattern - 1)].toStdString());
    part.stepNum = 1;
    part.stepDen = dens[juce::jlimit (0, 4, arp.step)];
    part.cropLength = 1;
    part.mirror = arp.mirror;
    return part;
}

//==============================================================================
namespace detail
{
// OneMotion's chord voicing: tones stacked from inversion b, up to 17 notes. When the pattern uses fewer
// tones than the chord has, the root goes first, then the fifth... A root a half step above the previous
// note is skipped.
inline std::vector<int> stackVoicing (const std::vector<int>& positions, int rootBase, int b, int numNotes, bool isBass)
{
    const int count = (int) positions.size();
    std::vector<bool> present ((size_t) count, true);
    int remaining = count;

    auto indexOf = [&] (int value)
    {
        for (int k = 0; k < count; ++k)
            if (present[(size_t) k] && positions[(size_t) k] == value)
                return k;
        return -1;
    };
    auto drop = [&] (int value)
    {
        const int k = indexOf (value);
        if (k >= 0) { present[(size_t) k] = false; --remaining; }
    };

    if (! isBass)
    {
        if (remaining > numNotes && present[0]) { present[0] = false; --remaining; }
        if (remaining > numNotes) drop (7);
        if (indexOf (9) > -1 && remaining > numNotes) drop (5);
        if (indexOf (5) > -1 && remaining > numNotes) drop (2);
        if (remaining > numNotes) drop (4);
    }

    int octave = b >= 0 ? b / count : -((-b - 1) / count) - 1;
    int a = ((b % count) + count) % count;
    int prev = -9999;
    std::vector<int> out;
    for (int q = 0; q < 17; ++q)
    {
        if (present[(size_t) a])
        {
            const int note = positions[(size_t) a] + rootBase + 12 * octave;
            if (a == 0 && note - prev == 1)
                --q;
            else
            {
                out.push_back (note);
                prev = note;
            }
        }
        if (++a == count) { a = 0; ++octave; }
    }
    return out;
}

inline std::vector<int> scaleTriad (int root, const Voicing& v)
{
    const auto& steps = scaleSteps (v.scale);
    for (int d = 0; d < 7; ++d)
    {
        if ((v.key + steps[(size_t) d]) % 12 != root)
            continue;
        std::vector<int> iv;
        for (int i = 0; i < 3; ++i)
        {
            const int idx = d + 2 * i;
            iv.push_back (steps[(size_t) (idx % 7)] + 12 * (idx / 7) - steps[(size_t) d]);
        }
        return iv;
    }
    return {};
}
} // namespace detail

// Chord notes as OneMotion plays them (Chord tab: inversions on, chords from G3 up)
inline std::vector<int> chordStack (const ChordItem& c, const Voicing& v, int numNotes, int target)
{
    if (c.intervals.empty())
        return {};

    // sus chords take the inversion of the scale triad on the same root
    auto ref = c.intervals;
    if (c.intervals == std::vector<int> { 0, 2, 7 } || c.intervals == std::vector<int> { 0, 5, 7 })
        if (auto triad = detail::scaleTriad (c.root, v); ! triad.empty())
            ref = triad;

    int rootBase = 48 + v.key + (c.root - v.key + 12) % 12;
    auto firstNote = [&] (int b, int base)
    {
        const auto s = detail::stackVoicing (ref, base, b, numNotes, false);
        return s.empty() ? base : s.front();
    };

    int inversion = 0;
    if (v.inversions)
    {
        int n = 0, best = 99999;
        for (int i = 0; i < 20; ++i)
        {
            const int d = firstNote (n, rootBase);
            const int u = d - target < 0 ? 999 : d - target;
            if (u > best)
                break;
            best = u;
            inversion = n;
            n += target > d ? 1 : -1;
        }
    }
    else
    {
        const int o = firstNote (0, rootBase);
        int n = 0;
        while (12 * n + o < target) ++n;
        while (12 * n + o >= target + 12) --n;
        rootBase += 12 * n;
    }
    return detail::stackVoicing (c.intervals, rootBase, inversion, numNotes, false);
}

inline std::vector<int> bassStack (const ChordItem& c, int octave)
{
    if (c.intervals.empty())
        return {};
    return detail::stackVoicing (c.intervals, 12 * octave + c.root, 0, 999, true);
}

// Default voicing (Once style), used by the test tools
inline std::vector<int> chordNotes (const ChordItem& c, const Voicing& v) { return chordStack (c, v, 4, chordTarget); }

//==============================================================================
namespace detail
{
struct PartInfo
{
    int maxPos = 0, maxOctave = 0, numNotes = 4;
    bool dynamic = false;
};

inline PartInfo analyse (const Part& part)
{
    PartInfo info;
    for (const auto& st : part.pattern.steps)
        if (st)
            for (const auto& it : st->items)
            {
                info.maxOctave = std::max (info.maxOctave, it.octave);
                info.maxPos = std::max (info.maxPos, it.n);
                info.dynamic = info.dynamic || it.remaining;
            }
    info.numNotes = part.numNotes != 0 ? part.numNotes : (info.dynamic || info.maxPos == 0 ? 4 : info.maxPos + 1);
    return info;
}

// Ticks per beat, as OneMotion's finest common step
inline int beatDivFor (const Part& chord, const Part& bass, const TimeSignature& ts, double shuffle)
{
    const double u = (double) chord.stepNum / chord.stepDen, m = (double) bass.stepNum / bass.stepDen;
    int p = 1, q = 1;
    for (int i = 0; i < 100 && std::abs (u / p - m / q) > 1.0e-9; ++i)
        (u / p > m / q) ? ++p : ++q;
    double y = u / p;
    const double v = shuffle == 0.0 ? 1.0 / 4.0 / ts.beats : 1.0 / ts.beats;
    while (y > v + 1.0e-12)
        y /= ts.beats;
    return std::max (1, (int) std::lround (1.0 / y / ts.beats));
}

struct Note
{
    int part, pitch;
    double start, scheduledEnd;   // scheduledEnd < 0: none
    bool pedal;                   // release waits for the sustain pedal
    double velocity;
};
} // namespace detail

// One loop of the progression in the given style, in quarter notes from 0 to totalBeats
// keptLoop = 1: a loop as it sounds when the progression repeats (0 = the very first one, for tests)
inline std::vector<NoteEvent> renderProgression (const Progression& prog, const Voicing& voicing, int styleIndex, const ArpSettings& arp,
                                                 int keptLoop = 1)
{
    using namespace detail;
    std::vector<NoteEvent> out;
    if (prog.empty())
        return out;

    const auto& style = styleDef (styleIndex);
    const Part chordPart = arp.pattern > 0 ? arpPart (arp) : style.chord;
    const Part& bassPart = style.bass;
    const auto& ts = style.time;
    const bool styleSustain = style.sustain != Sustain::none;
    const PartInfo chordInfo = analyse (chordPart);

    const int beatDiv = beatDivFor (chordPart, bassPart, ts, style.shuffle);
    const double tick = ts.beatScale / beatDiv;   // quarter notes
    const int ticksPerBar = ts.beats * beatDiv;
    const auto bounds = chordBoundaries (prog, styleIndex);
    const double loopEnd = bounds.back();

    // Chord spans in ticks
    std::vector<int> chordTicks;
    for (size_t k = 0; k < prog.size(); ++k)
        chordTicks.push_back (std::max (1, (int) std::lround ((bounds[k + 1] - bounds[k]) / tick)));

    // Voicings per chord
    const int chordTargetNote = 12 * chordPart.octave + chordPart.octaveOffset;
    std::vector<std::vector<int>> chordNotesOf, bassNotesOf;
    for (const auto& c : prog)
    {
        chordNotesOf.push_back (chordStack (c, voicing, chordInfo.numNotes, chordTargetNote));
        bassNotesOf.push_back (bassStack (c, bassPart.octave));
    }

    constexpr double releaseGap = 0.02 * 100.0 / 60.0;   // OneMotion lets go 20 ms early (in beats at 100 BPM)
    std::vector<Note> notes;
    std::vector<double> pedalReleases;
    std::vector<std::pair<double, double>> volumeEvents, pitchEvents;

    // A key let go at time t (with the pedal down, it keeps sounding until the pedal is released)
    auto releaseNote = [] (Note& n, double t, bool pedalDown)
    {
        if (pedalDown)
            n.pedal = true;
        else if (n.scheduledEnd < 0.0 || t < n.scheduledEnd)
            n.scheduledEnd = t;
    };

    // Playback state
    int P = 0, E = 0, current = 0, lastChanged = -1;
    bool pedal = true;
    bool firstHit[2] { true, true };
    int bassHeld = -1;
    int chordDiff = 0, bassDiff = 0;

    auto setChord = [&] (int idx, bool keepFlags)
    {
        if (idx != lastChanged && ! keepFlags)
        {
            pedal = true;
            firstHit[0] = firstHit[1] = true;
            lastChanged = idx;
        }
        if (idx != current)
        {
            const auto& a = chordNotesOf[(size_t) current];
            const auto& b = chordNotesOf[(size_t) idx];
            if (! a.empty() && ! b.empty() && a[0] != b[0]) chordDiff = b[0] - a[0];
            const auto& ba = bassNotesOf[(size_t) current];
            const auto& bb = bassNotesOf[(size_t) idx];
            bassDiff = (! ba.empty() && ! bb.empty()) ? bb[0] - ba[0] : 0;
        }
        current = idx;
    };
    setChord (0, false);

    // Start time of every tick (shuffle stretches even ticks and shortens odd ones)
    auto tickTime = [&] (long long i)
    {
        const double pair = 2.0 * tick;
        return (double) (i / 2) * pair + ((i % 2) != 0 ? tick * (1.0 + style.shuffle) : 0.0);
    };

    const long long totalTicks = [&] { long long t = 0; for (int c : chordTicks) t += c; return t; }();

    // Two loops: the second one starts like every later loop (anticipations and held notes carried over)
    for (int cycle = 0; cycle < (keptLoop == 0 ? 1 : 2); ++cycle)
    {
    const double cycleStart = cycle * loopEnd;
    P = 0;
    E = 0;
    if (cycle > 0)
        setChord (0, false);

    for (long long i = 0; i < totalTicks; ++i)
    {
        const double a = cycleStart + tickTime (i);
        bool pedalDoneThisTick = false;

        if (style.sustain == Sustain::twoBeats && i % (2 * beatDiv) == 0)
            pedal = true;

        for (int partIndex = 0; partIndex < 2; ++partIndex)
        {
            const bool isBass = partIndex == 1;
            const Part& part = isBass ? bassPart : chordPart;
            const long long y = part.loop ? i : i - E;
            const int arpLength = part.pattern.length();
            if (arpLength == 0)
                continue;

            // Which pattern step falls on this tick
            const long long stepTicksNum = (long long) part.stepNum * ticksPerBar;   // ticks per step = this / stepDen
            if (! part.loop && (y < 0 || y * part.stepDen >= (long long) arpLength * stepTicksNum))
                continue;
            const long long yc = part.cropLength > 0 ? y % ((long long) ticksPerBar * part.cropLength) : y;
            if ((yc * part.stepDen) % stepTicksNum != 0)
                continue;
            const int v = (int) ((yc * part.stepDen / stepTicksNum) % arpLength);
            const auto& stepOpt = part.pattern.steps[(size_t) v];
            if (! stepOpt)
                continue;
            const auto& step = *stepOpt;

            // "@": in the last beat of a chord, the next chord starts here
            if (! step.items.empty())
            {
                const bool lastBeat = i - E >= chordTicks[(size_t) P] - beatDiv;
                if (lastBeat && (step.items[0].next || step.early) && prog.size() > 1)
                    setChord ((P + 1) % (int) prog.size(), step.items[0].next || pedalDoneThisTick);
            }

            // Envelopes (Mod styles)
            if (style.hasEnvelopes && ! isBass)
            {
                if (v == 0)
                {
                    volumeEvents.push_back ({ a, 1.0 });
                    pitchEvents.push_back ({ a, 0.0 });
                }
                if (step.hasVolume) volumeEvents.push_back ({ a, step.volume });
                if (step.hasPitch)  pitchEvents.push_back ({ a, step.pitch });
            }

            const auto& chordList = chordNotesOf[(size_t) current];
            const auto& list = isBass ? bassNotesOf[(size_t) current] : chordList;
            const auto& chord = prog[(size_t) current];
            const int diff = isBass ? bassDiff : chordDiff;
            bool clearFirst = false;
            const bool inFirstTick = i - E == 0;
            const bool inLastBeat = i - E >= (long long) (chordTicks[(size_t) P] - beatDiv);

            // One note of an item (port of OneMotion's note function)
            auto noteFor = [&] (const PatternItem& e, int t) -> std::optional<int>
            {
                if (t < 0 || list.empty())
                    return std::nullopt;
                int c = (! isBass && part.mirror) ? chordInfo.maxPos - t : t;
                int d = e.octave;
                if (! isBass && part.mirror)
                    d = chordInfo.maxOctave - d;
                if (c < 0 || c >= (int) list.size())
                    return std::nullopt;
                int u = list[(size_t) c];

                auto closest = [&] (int interval)
                {
                    size_t p = 0;
                    while (p + 1 < list.size() && std::abs (list[p] - list[0] - interval) > std::abs (list[p + 1] - list[0] - interval))
                        ++p;
                    return list[p] - list[0];
                };
                if (e.third) u += closest (4);
                if (e.fifth) u += closest (7);

                const int h = (e.dir && diff < 0) ? -1 : 1;
                int m = e.scaleSteps * h;

                // Scale of the chord: the song's scale seen from the chord root
                std::array<int, 7> y { 0, 2, 4, 5, 7, 9, 11 };
                {
                    const auto& steps = scaleSteps (voicing.scale);
                    const int rel = (chord.root - voicing.key + 12) % 12;
                    for (int k = 0; k < 7; ++k)
                        if (steps[(size_t) k] == rel)
                        {
                            for (int j = 0; j < 7; ++j)
                                y[(size_t) j] = (steps[(size_t) ((k + j) % 7)] - rel + 12) % 12;
                            break;
                        }
                }
                const int interval = ((u % 12) - chord.root + 24) % 12;
                if (interval == 3 || interval == 4)
                {
                    if (e.modify == PatternItem::sus2) { if (! (y[1] - y[0] > 1)) return std::nullopt; --m; }
                    if (e.modify == PatternItem::sus4) { if (! (y[4] - y[3] > 1)) return std::nullopt; ++m; }
                }
                if (e.modify == PatternItem::onlyThird && interval != 3 && interval != 4)
                    return std::nullopt;

                if (m != 0)
                {
                    if (! isBass && part.mirror)
                        m = -m;
                    const int g = ((u - chord.root) % 12 + 12) % 12;
                    int b = -1;
                    for (int k = 0; k < 7; ++k)
                        if (y[(size_t) k] == g)
                            b = k;
                    if (b > -1)
                    {
                        int x = y[(size_t) (((b + m) % 7 + 7) % 7)] - g;
                        if (m > 0 && x <= 0) x += 12;
                        if (m < 0 && x >= 0) x -= 12;
                        u += x;
                    }
                    else
                    {
                        u += 2 * m;
                    }
                }
                u += e.halfSteps * h;
                if (e.fit)
                {
                    const int k = 12 * part.octave + part.octaveOffset - 12 * d;
                    while (u < k) u += 12;
                    while (u >= k + 12) u -= 12;
                }
                return u + 12 * d;
            };

            // Plays one note, with the sustain pedal and the "first hit" logic
            auto play = [&] (const PatternItem& item, std::optional<int> note)
            {
                if (pedal && note)
                {
                    pedalReleases.push_back (a - releaseGap);
                    pedal = false;
                    pedalDoneThisTick = true;
                }
                if (! note)
                    return;

                auto& first = firstHit[partIndex];
                if (first)
                {
                    if (item.condition == PatternItem::notFirst)
                        return;
                    if (item.remaining || (item.n == 0 && item.halfSteps == 0 && item.scaleSteps == 0))
                        clearFirst = true;
                }
                else if (item.condition == PatternItem::first)
                    return;

                if (inLastBeat) { /* last-beat items allowed */ }
                else if (item.condition == PatternItem::lastBeat)
                    return;

                const double w = a + item.offset * ts.beatScale;
                const double accent = (y % ticksPerBar == 0) ? 1.0 : ((y % beatDiv == 0 || inFirstTick) ? 0.9 : 0.8);
                const bool held = item.sustain && ! styleSustain;
                const double q = item.duration * part.noteDuration * ts.beatScale / 2.0;
                const double vel = part.velocity * item.velocity;

                if (isBass)
                {
                    // OneMotion remembers the last held bass key and lets it go on every later bass note
                    if (bassHeld >= 0)
                        for (auto& n : notes)
                            if (n.part == 1 && n.pitch == bassHeld && n.start < a - releaseGap)
                                releaseNote (n, a - releaseGap, styleSustain);
                    if (held)
                        bassHeld = *note;
                }

                if (q == 0.0 && ! held)
                {
                    // "," : no new note, the key is let go now
                    for (auto& n : notes)
                        if (n.part == partIndex && n.pitch == *note && n.start < w)
                            releaseNote (n, w, styleSustain);
                    return;
                }

                {
                    const double end = held ? -1.0 : w + q * accent;
                    notes.push_back ({ partIndex, *note, w, end, styleSustain && ! held, vel });
                    if (part.doubleOctave)
                        notes.push_back ({ partIndex, *note + 12, w, end, styleSustain && ! held, vel });
                }
            };

            for (const auto& item : step.items)
            {
                if (! isBass && item.remaining)
                {
                    // Every chord tone from item.n up (until a pitch class repeats, or numNotes)
                    const int f = chordPart.numNotes;
                    int seen = 0, taken = 0;
                    for (int m = 0; m < 99 && m < (int) list.size(); ++m)
                    {
                        const int bit = 1 << (((list[(size_t) m] % 12) + 12) % 12);
                        if (f != 0) { if (taken >= f - item.n) break; }
                        else { if (seen & bit) break; seen |= bit; }
                        if (m < item.n)
                            continue;
                        play (item, noteFor (item, m));
                        ++taken;
                    }
                }
                else
                {
                    play (item, noteFor (item, item.n));
                }
            }
            if (clearFirst && ! step.items.empty())
                firstHit[partIndex] = false;
        }

        // Next tick: move to the next chord at its boundary
        if (i + 1 - E >= chordTicks[(size_t) P])
        {
            E += chordTicks[(size_t) P];
            ++P;
            if (P < (int) prog.size())
                setChord (P, false);
        }
    }
    }

    // Note ends: own release, a retrigger of the same key, the pedal, or the end of the loop.
    // Only the second loop is kept, moved back to 0.
    std::sort (pedalReleases.begin(), pedalReleases.end());
    for (size_t k = 0; k < notes.size(); ++k)
    {
        const auto& n = notes[k];
        const double from = keptLoop * loopEnd, to = from + loopEnd;
        if (n.start < from - 1.0e-9 || n.start >= to - 1.0e-9)
            continue;
        double end = to;
        if (n.scheduledEnd >= 0.0 && ! n.pedal)
            end = std::min (end, n.scheduledEnd);
        for (double p : pedalReleases)
            if (p > n.start) { end = std::min (end, p); break; }
        bool replaced = false;
        for (size_t j = 0; j < notes.size(); ++j)
            if (j != k && notes[j].part == n.part && notes[j].pitch == n.pitch)
            {
                if (notes[j].start > n.start + 1.0e-9)
                    end = std::min (end, notes[j].start);
                else if (std::abs (notes[j].start - n.start) <= 1.0e-9 && j > k)
                    replaced = true;   // pressed again at once: OneMotion's first copy lasts 0
            }
        if (replaced)
            continue;
        end = std::max (end, n.start + 0.01);
        const int vel = juce::jlimit (1, 127, (int) std::lround (127.0 * n.velocity));
        const int channel = n.part + 1;
        out.push_back ({ n.start - from, n.pitch, true, vel, NoteEvent::noteEvent, channel });
        // Ends stay inside the loop, so a note held to the end still gets its note-off
        out.push_back ({ std::min (end, to - 1.0e-3) - from, n.pitch, false, 0, NoteEvent::noteEvent, channel });
    }

    const double from = keptLoop * loopEnd, to = from + loopEnd;
    for (int channel = 1; channel <= 2; ++channel)
    {
        for (const auto& [t, value] : volumeEvents)
            if (t >= from && t < to)
                out.push_back ({ t - from, 0, true, juce::jlimit (0, 127, (int) std::lround (127.0 * value)), NoteEvent::expression, channel });
        for (const auto& [t, value] : pitchEvents)
            if (t >= from && t < to)
                out.push_back ({ t - from, 0, true, juce::jlimit (0, 16383, 8192 + (int) std::lround (value / 2.0 * 8191.0)), NoteEvent::pitchBend, channel });
    }

    std::stable_sort (out.begin(), out.end(), [] (const NoteEvent& x, const NoteEvent& y)
    {
        if (x.ppq != y.ppq)
            return x.ppq < y.ppq;
        return ! x.on && y.on;
    });
    return out;
}

//==============================================================================
// Keeps the rendered loop and hands out the events of any time range (the progression loops forever)
class StyleRenderer
{
public:
    void collect (double start, double end, const Progression& prog, const Voicing& voicing, int style, const ArpSettings& arp,
                  std::vector<NoteEvent>& out)
    {
        out.clear();
        const double loop = loopLength (prog, style);
        if (loop <= 0.0)
            return;

        if (! (prog == cachedProg) || voicing.key != cachedVoicing.key || voicing.scale != cachedVoicing.scale
            || voicing.inversions != cachedVoicing.inversions || style != cachedStyle || arp != cachedArp || ! valid)
        {
            cachedProg = prog;
            cachedVoicing = voicing;
            cachedStyle = style;
            cachedArp = arp;
            events = renderProgression (prog, voicing, style, arp);
            valid = true;
        }

        const auto firstCycle = (long long) std::floor (start / loop);
        const auto lastCycle = (long long) std::floor (end / loop);
        for (auto cycle = firstCycle; cycle <= lastCycle; ++cycle)
        {
            const double base = (double) cycle * loop;
            for (const auto& e : events)
            {
                const double t = base + e.ppq;
                if (t >= start && t < end)
                {
                    auto copy = e;
                    copy.ppq = t;
                    out.push_back (copy);
                }
            }
        }
    }

    void invalidate() { valid = false; }

private:
    Progression cachedProg;
    Voicing cachedVoicing;
    int cachedStyle = -1;
    ArpSettings cachedArp;
    bool valid = false;
    std::vector<NoteEvent> events;
};
} // namespace ChordEngine
