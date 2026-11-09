#include "PluginProcessor.h"
#include <iostream>
#include <stdexcept>

using namespace juce;
using namespace ChordEngine;

namespace
{
void check (bool ok, const String& message)
{
    if (! ok) throw std::runtime_error (message.toStdString());
}

void choose (SwellChordMotionProcessor& proc, const char* id, int value)
{
    auto* param = proc.apvts.getParameter (id);
    param->setValueNotifyingHost (param->convertTo0to1 ((float) value));
}

void checkPalette()
{
    // In this scale a stack of diatonic thirds gives the wrong triads and sevenths.
    const char* reference[] { "|||Fsus2|||", "C|Db|E|Fm|G(b5)|Abaug|",
                              "Csus4||||||", "Cmaj7|Db7||Fmmaj7|G7b5|Abmaj7#5|" };
    for (int row = 0; row < 4; ++row)
    {
        StringArray cells;
        for (int degree = 0; degree < 7; ++degree)
        {
            auto c = paletteChord (0, 5, degree, row);
            cells.add (c ? chordName (*c, 0, 5) : String());
        }
        check (cells.joinIntoString ("|") == reference[row], "Double harmonic major palette");
    }
    check (noteName (7, 13, 0) == "F##", "D# major spelling");
    check (noteName (5, 14, 0) == "F", "Gb major spelling");
}

MidiFile exportAndRead (SwellChordMotionProcessor& proc)
{
    MemoryOutputStream bytes;
    check (proc.writeMidiTo (bytes), "MIDI export");
    MemoryInputStream input (bytes.getData(), bytes.getDataSize(), false);
    MidiFile midi;
    check (midi.readFrom (input) && midi.getNumTracks() == 1, "MIDI track");
    return midi;
}

void checkEditedVoicings (SwellChordMotionProcessor& proc, const File& reference)
{
    // First chord attack in A# major, as shown on the OneMotion keyboard in Dance 1.
    const auto expected = StringArray::fromLines (reference.loadFileAsString());
    // This older fixture used the site's other project range (G3 in C).
    choose (proc, "chordRange", 0);
    choose (proc, "bassRange", 0);
    choose (proc, "key", 16);
    int count = 0;
    for (const auto& q : qualities())
    {
        if (q.group == nullptr) continue;
        auto c = proc.getProgression().front();
        c.intervals = q.intervals;
        c.symbol = q.suffix;
        proc.replaceChord (0, c);
        check (proc.chordName (proc.getProgression().front()) == "A#" + String (q.suffix), "Edited symbol");
        auto midi = exportAndRead (proc);
        StringArray notes;
        for (auto* e : *midi.getTrack (0))
            if (e->message.isNoteOn() && e->message.getChannel() == 1 && e->message.getTimeStamp() == 0.0)
                notes.add (String (e->message.getNoteNumber()));
        String label = q.menuLabel;
        if (label == "Major") label = "_";
        else if (label == "Minor") label = "m";
        const auto actual = label + "|" + notes.joinIntoString (",");
        check (expected.contains (actual), "Unexpected first attack: " + actual);
        ++count;
    }
    check (count == 82, "Chord catalog size");
}

void checkDynamics (SwellChordMotionProcessor& proc)
{
    auto loud = exportAndRead (proc);
    choose (proc, "velocity", 44);
    auto soft = exportAndRead (proc);
    auto* original = loud.getTrack (0);
    auto* changed = soft.getTrack (0);
    check (original->getNumEvents() == changed->getNumEvents(), "VEL changed the rhythm");
    int chords = 0, bass = 0;
    bool hasSofterVoice = false;
    for (int i = 0; i < original->getNumEvents(); ++i)
    {
        const auto& note = original->getEventPointer (i)->message;
        if (! note.isNoteOn()) continue;
        check (original->getEventPointer (i)->noteOffObject != nullptr, "Missing note off");
        const int v = note.getVelocity();
        check (changed->getEventPointer (i)->message.getVelocity() == scaledVelocity (v, 44), "Wrong exported velocity");
        if (note.getChannel() == 1) { ++chords; hasSofterVoice |= v < defaultVelocity; }
        else { ++bass; check (v == defaultVelocity, "Dance 1 bass strength"); }
    }
    check (chords == 78 && bass == 32 && hasSofterVoice, "Dance 1 default performance");
}
// Ordered as on the reference keyboard: bass first, then the chord from low to high.
std::vector<int> onceAttack (int key, int mode, int degree, int row)
{
    auto chord = paletteChord (key, mode, degree, row);
    if (! chord) return {};
    auto events = renderProgression ({*chord}, {key, mode}, styleNames().indexOf ("Once"), {});
    std::vector<int> chordNotes, bass;
    for (const auto& e : events)
        if (e.on && e.kind == NoteEvent::noteEvent && e.ppq == 0.0)
            (e.channel == 1 ? chordNotes : bass).push_back (e.note);
    check (bass.size() == 1 && ! chordNotes.empty(), "Missing Once attack");
    std::sort (chordNotes.begin(), chordNotes.end());
    bass.insert (bass.end(), chordNotes.begin(), chordNotes.end());
    return bass;
}

String readoutDigest (const StringArray& cells)
{
    uint32_t hash = 2166136261u;
    for (char ch : cells.joinIntoString ("|").toStdString())
        hash = (hash ^ (uint8_t) ch) * 16777619u;
    return String::toHexString ((int) hash);
}

void checkReferenceRanges (const File& reference)
{
    const auto lines = StringArray::fromLines (reference.loadFileAsString());
    int modesChecked = 0;
    for (const auto& line : lines)
    {
        if (line.isEmpty()) continue;
        const String name = line.upToFirstOccurrenceOf ("|", false, false);
        const int mode = scaleNames.indexOf (name);
        check (mode >= 0, "Unknown reference mode: " + name);
        StringArray cells;
        for (int row = 0; row < 4; ++row)
            for (int degree = 0; degree < 7; ++degree)
            {
                const auto anchor = onceAttack (6, mode, degree, row);
                StringArray pitches;
                for (int n : anchor) pitches.add (String (n));
                cells.add (pitches.joinIntoString (","));
                for (int key = 0; key < keyNames.size(); ++key)
                {
                    auto expected = anchor;
                    for (int& note : expected) note += keyPitch (key) - 6;
                    if (! expected.empty() && expected.front() < 24) expected.front() += 12;
                    check (onceAttack (key, mode, degree, row) == expected,
                           "Transposed Once voicing: " + keyNames[key] + " " + name);
                }
            }
        // Each digest covers all 28 keyboard readouts, including blank cells.
        check (readoutDigest (cells) == line.fromFirstOccurrenceOf ("|", false, false),
               "Reference mismatch: " + name + "  " + cells.joinIntoString ("|"));
        ++modesChecked;
    }
    check (modesChecked == 49, "Missing reference mode grids: " + String (modesChecked));
    // Independent keyboard captures in every key spelling, including the low bass boundary.
    const auto major = StringArray::fromTokens (
        "147ef98f a2fdc472 4934a5eb 3bb88e89 4598d97c b4e99d09 cff56e74 b0427828 "
        "1ff1f16a b5226c30 2647b37b 31f84baf a2fdc472 3bb88e89 cff56e74 1ff1f16a 2647b37b", " ", "");
    for (int key = 0; key < keyNames.size(); ++key)
    {
        StringArray cells;
        for (int degree = 0; degree < 7; ++degree)
        {
            StringArray notes;
            for (int n : onceAttack (key, 0, degree, 1)) notes.add (String (n));
            cells.add (notes.joinIntoString (","));
        }
        check (readoutDigest (cells) == major[key], "Reference triads in " + keyNames[key] + " major");
    }
}

void checkBMajorPreviewAndFile()
{
    SwellChordMotionProcessor proc;
    choose (proc, "key", 6);
    choose (proc, "style", styleNames().indexOf ("Once"));
    const auto chord = *paletteChord (6, 0, 3, 1);
    const std::vector<int> reference {35, 59, 63, 66};
    proc.clearProgression();
    proc.insertChord (chord, -1);
    auto file = exportAndRead (proc);
    std::vector<int> exported;
    for (auto* e : *file.getTrack (0))
        if (e->message.isNoteOn())
        {
            check (e->message.getTimeStamp() == 0.0, "File attack is late");
            check (e->noteOffObject != nullptr && e->noteOffObject->message.getTimeStamp() == 4 * file.getTimeFormat(),
                   "Once chord did not release on the final MIDI tick");
            exported.push_back (e->message.getNoteNumber());
        }
    std::sort (exported.begin(), exported.end());
    check (exported == reference, "Exported B in F# major");
    struct RunningHost : AudioPlayHead
    {
        Optional<PositionInfo> getPosition() const override
        {
            PositionInfo position;
            position.setIsPlaying (true);
            position.setPpqPosition (3.5);
            position.setBpm (100.0);
            return position;
        }
    } host;
    proc.setPlayHead (&host);
    proc.prepareToPlay (48000, 256);
    proc.startPreview (chord);
    AudioBuffer<float> audio (2, 256);
    MidiBuffer midi;
    proc.processBlock (audio, midi);
    std::vector<int> live;
    for (const auto event : midi)
        if (auto m = event.getMessage(); m.isNoteOn())
        {
            check (event.samplePosition == 0, "Palette attack is late");
            live.push_back (m.getNoteNumber());
        }
    std::sort (live.begin(), live.end());
    check (live == reference, "Palette B in F# major");
    proc.setPlayHead (nullptr);

}

void checkRangeRestore()
{
    SwellChordMotionProcessor proc;
    choose (proc, "chordRange", 1);
    choose (proc, "bassRange", 4);
    MemoryBlock modern;
    proc.getStateInformation (modern);
    auto old = proc.apvts.copyState();
    old.removeChild (old.getChildWithProperty ("id", "chordRange"), nullptr);
    old.removeChild (old.getChildWithProperty ("id", "bassRange"), nullptr);
    MemoryBlock legacy;
    AudioProcessor::copyXmlToBinary (*old.createXml(), legacy);
    proc.setStateInformation (legacy.getData(), (int) legacy.getSize());
    check (proc.getVoicing().chordShift == referenceRangeShift && proc.getVoicing().bassShift == referenceRangeShift,
           "Missing legacy ranges");
    proc.setStateInformation (modern.getData(), (int) modern.getSize());
    check (proc.getVoicing().chordShift == 1 && proc.getVoicing().bassShift == 4, "Saved independent ranges");
}

}

int main (int argc, char** argv)
{
    swell::Licence::unlockForTesting(); // rendu hors ligne, sans fichier de licence
    ScopedJuceInitialiser_GUI gui;
    try
    {
        check (argc == 3, "Expected reference chord and range fixtures");
        checkBMajorPreviewAndFile();
        checkRangeRestore();
        checkReferenceRanges (File (argv[2]));
        checkPalette();
        SwellChordMotionProcessor proc;
        checkDynamics (proc);
        choose (proc, "velocity", defaultVelocity);
        checkEditedVoicings (proc, File (argv[1]));
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
    std::cout << "49 mode grids, 17 key spellings, preview, exported MIDI, saved ranges and 82 chord types passed.\n";
}
