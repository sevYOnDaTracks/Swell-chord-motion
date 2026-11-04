#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

namespace
{
const Identifier progressionId ("PROGRESSION"), chordId ("CHORD"), rootId ("root"), intervalsId ("intervals"), beatsId ("beats");

ValueTree toTree (const ChordEngine::ChordItem& c)
{
    StringArray iv;
    for (int i : c.intervals)
        iv.add (String (i));
    return ValueTree (chordId, { { rootId, c.root }, { intervalsId, iv.joinIntoString (",") }, { beatsId, c.beats } });
}

ChordEngine::ChordItem fromTree (const ValueTree& t)
{
    ChordEngine::ChordItem c;
    c.root = jlimit (0, 11, (int) t[rootId]);
    c.beats = jlimit (1, 16, (int) t[beatsId]);
    for (const auto& s : StringArray::fromTokens (t[intervalsId].toString(), ",", {}))
        c.intervals.push_back (s.getIntValue());
    return c;
}
} // namespace

SwellChordMotionProcessor::SwellChordMotionProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "SwellChordMotion", createLayout())
{
    volumeParam     = apvts.getRawParameterValue ("vol");
    instrumentParam = apvts.getRawParameterValue ("instr");
    styleParam      = apvts.getRawParameterValue ("style");
    bpmParam        = apvts.getRawParameterValue ("bpm");
    playParam       = apvts.getRawParameterValue ("play");
    effectParam     = apvts.getRawParameterValue ("effect");
    arpParam        = apvts.getRawParameterValue ("arp");
    arpStepParam    = apvts.getRawParameterValue ("arpStep");
    arpMirrorParam  = apvts.getRawParameterValue ("arpMirror");

    for (int i = 0; i < 16; ++i)
        synth.addVoice (new SwellVoice (instrumentParam));
    synth.addSound (new SwellSound());

    events.reserve (4096);

    ensureProgression();
    apvts.state.addListener (this);
    publishProgression();
}

SwellChordMotionProcessor::~SwellChordMotionProcessor()
{
    apvts.state.removeListener (this);
}

AudioProcessorValueTreeState::ParameterLayout SwellChordMotionProcessor::createLayout()
{
    AudioProcessorValueTreeState::ParameterLayout layout;
    auto choice = [&layout] (const String& id, const String& name, const StringArray& items, int def)
    {
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, items, def));
    };

    layout.add (std::make_unique<AudioParameterInt> (ParameterID { "vol", 1 }, "Vol", 0, 100, 80));
    choice ("instr", "Instr", Instruments::names, Instruments::piano);
    choice ("style", "Style", ChordEngine::styleNames(), 5);
    choice ("arp", "Arpeggio", ChordEngine::arpNames(), 0);
    choice ("arpStep", "Arp step", ChordEngine::arpStepNames, 1);
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "arpMirror", 1 }, "Arp mirror", false));
    choice ("key", "Key", ChordEngine::keyNames, 0);
    choice ("scale", "Scale", ChordEngine::scaleNames, 0);
    choice ("effect", "Effect", Instruments::effectNames, 1);
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "bpm", 1 }, "Tempo", NormalisableRange<float> { 40.0f, 240.0f, 1.0f }, 100.0f,
                    AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " BPM"; })));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "play", 1 }, "Play", false));
    return layout;
}

ChordEngine::ArpSettings SwellChordMotionProcessor::getArpSettings() const
{
    return { roundToInt (arpParam->load()), roundToInt (arpStepParam->load()), arpMirrorParam->load() > 0.5f };
}

//==============================================================================
ValueTree SwellChordMotionProcessor::progressionTree()
{
    return apvts.state.getOrCreateChildWithName (progressionId, nullptr);
}

void SwellChordMotionProcessor::ensureProgression()
{
    if (apvts.state.getChildWithName (progressionId).isValid())
        return;

    // Same starting point as a blank song: C - Em7 - Am - F
    auto tree = progressionTree();
    for (auto c : { ChordEngine::ChordItem { 0, { 0, 4, 7 }, 4 }, ChordEngine::ChordItem { 4, { 0, 3, 7, 10 }, 4 },
                    ChordEngine::ChordItem { 9, { 0, 3, 7 }, 4 }, ChordEngine::ChordItem { 5, { 0, 4, 7 }, 4 } })
        tree.appendChild (toTree (c), nullptr);
}

ChordEngine::Progression SwellChordMotionProcessor::getProgression() const
{
    ChordEngine::Progression prog;
    for (const auto& child : apvts.state.getChildWithName (progressionId))
        prog.push_back (fromTree (child));
    return prog;
}

void SwellChordMotionProcessor::insertChord (const ChordEngine::ChordItem& chord, int index)
{
    undoManager.beginNewTransaction();
    progressionTree().addChild (toTree (chord), index, &undoManager);
}

void SwellChordMotionProcessor::removeChord (int index)
{
    undoManager.beginNewTransaction();
    progressionTree().removeChild (index, &undoManager);
}

void SwellChordMotionProcessor::moveChord (int from, int insertIndex)
{
    auto tree = progressionTree();
    const int to = jlimit (0, tree.getNumChildren() - 1, insertIndex > from ? insertIndex - 1 : insertIndex);
    if (to == from)
        return;

    undoManager.beginNewTransaction();
    tree.moveChild (from, to, &undoManager);
}

void SwellChordMotionProcessor::setChordBeats (int index, int beats)
{
    undoManager.beginNewTransaction();
    progressionTree().getChild (index).setProperty (beatsId, jlimit (1, 16, beats), &undoManager);
}

void SwellChordMotionProcessor::clearProgression()
{
    undoManager.beginNewTransaction();
    progressionTree().removeAllChildren (&undoManager);
}

void SwellChordMotionProcessor::startPreview (const ChordEngine::ChordItem& chord)
{
    const SpinLock::ScopedLockType lock (previewLock);
    sharedPreview = chord;
    sharedPreview.beats = 4;   // one bar, so every style plays its whole pattern
    sharedPreviewOn = true;
    ++sharedPreviewVersion;
}

void SwellChordMotionProcessor::stopPreview()
{
    const SpinLock::ScopedLockType lock (previewLock);
    sharedPreviewOn = false;
    ++sharedPreviewVersion;
}

void SwellChordMotionProcessor::valueTreePropertyChanged (ValueTree& tree, const Identifier&)
{
    // Parameter values also live in this tree; only chord edits matter here
    if (tree.hasType (chordId))
        publishProgression();
}

void SwellChordMotionProcessor::publishProgression()
{
    auto prog = getProgression();
    {
        const SpinLock::ScopedLockType lock (progressionLock);
        sharedProgression = std::move (prog);
        ++sharedVersion;
    }
    ++progressionVersion;
}

//==============================================================================
void SwellChordMotionProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    synth.setCurrentPlaybackSampleRate (sampleRate);
    reverb.setSampleRate (sampleRate);
    reverb.reset();
    gain.reset (sampleRate, 0.05);
    generated.ensureSize (4096);
    noteCounts.fill (0);
    keyboardState.reset();
    wasPlaying = internalRunning = false;
}

bool SwellChordMotionProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return out == AudioChannelSet::stereo() || out == AudioChannelSet::mono();
}

void SwellChordMotionProcessor::noteOn (MidiBuffer& buffer, int note, int sample)
{
    if (! isPositiveAndBelow (note, 128))
        return;

    ++noteCounts[(size_t) note];
    buffer.addEvent (MidiMessage::noteOn (1, note, (uint8) 100), sample);
}

void SwellChordMotionProcessor::noteOff (MidiBuffer& buffer, int note, int sample)
{
    if (! isPositiveAndBelow (note, 128) || noteCounts[(size_t) note] == 0)
        return;

    if (--noteCounts[(size_t) note] == 0)
        buffer.addEvent (MidiMessage::noteOff (1, note), sample);
}

void SwellChordMotionProcessor::flushNotes (MidiBuffer& buffer, int sample)
{
    for (int note = 0; note < 128; ++note)
    {
        if (noteCounts[(size_t) note] > 0)
        {
            noteCounts[(size_t) note] = 0;
            buffer.addEvent (MidiMessage::noteOff (1, note), sample);
        }
    }
}

void SwellChordMotionProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    buffer.clear();
    generated.clear();

    if (numSamples == 0)
        return;

    // Pick up chord edits; old notes would never get their note-off otherwise
    {
        const SpinLock::ScopedTryLockType lock (progressionLock);
        if (lock.isLocked() && sharedVersion != audioVersion)
        {
            audioProgression = sharedProgression;
            audioVersion = sharedVersion;
            flushNotes (generated, 0);
        }
    }

    {
        const SpinLock::ScopedTryLockType lock (previewLock);
        if (lock.isLocked() && sharedPreviewVersion != audioPreviewVersion)
        {
            audioPreviewVersion = sharedPreviewVersion;
            previewOn = sharedPreviewOn;
            previewProgression = { sharedPreview };
            previewPpq = 0.0;
            flushNotes (generated, 0);
        }
    }

    const int style = roundToInt (styleParam->load());
    const auto arp = getArpSettings();
    if (style != lastStyle || arp != lastArp)
    {
        flushNotes (generated, 0);
        lastStyle = style;
        lastArp = arp;
    }

    double bpm = bpmParam->load();
    double startPpq = 0.0;
    bool playing = false, hostPlaying = false, inHost = false;

    if (wrapperType != wrapperType_Standalone)
    {
        if (auto* ph = getPlayHead())
        {
            if (auto pos = ph->getPosition())
            {
                if (auto hostBpm = pos->getBpm())
                {
                    bpm = *hostBpm;
                    inHost = true;
                }

                if (pos->getIsPlaying())
                {
                    if (auto ppq = pos->getPpqPosition())
                    {
                        startPpq = *ppq;
                        playing = hostPlaying = true;
                    }
                }
            }
        }
    }

    // The play button runs an internal clock whenever the host transport is stopped
    const bool internalPlay = playParam->load() > 0.5f;
    if (! playing && internalPlay)
    {
        if (! internalRunning)
        {
            internalPpq = 0.0;
            internalRunning = true;
        }
        startPpq = internalPpq;
        playing = true;
    }
    if (! internalPlay)
        internalRunning = false;

    const double ppqPerSample = bpm / 60.0 / sr;

    if (previewOn)
    {
        // A clicked chord plays in the chosen style at the current tempo, on the host's beat grid when it is playing
        const double start = hostPlaying ? startPpq : previewPpq;
        const double end = start + numSamples * ppqPerSample;
        ChordEngine::collectEvents (start, end, previewProgression, style, arp, events);

        for (const auto& e : events)
        {
            const int sample = jlimit (0, numSamples - 1, (int) ((e.ppq - start) / ppqPerSample));
            if (e.on)
                noteOn (generated, e.note, sample);
            else
                noteOff (generated, e.note, sample);
        }

        previewPpq = end;
        currentChord = -1;
        playing = false;   // the progression restarts cleanly once the chord is released
    }
    else if (playing)
    {
        const double endPpq = startPpq + numSamples * ppqPerSample;
        const bool continuous = wasPlaying && std::abs (startPpq - lastEndPpq) < 0.05;

        if (! continuous)
            flushNotes (generated, 0);

        ChordEngine::collectEvents (continuous ? lastEndPpq : startPpq, endPpq, audioProgression, style, arp, events);

        for (const auto& e : events)
        {
            const int sample = jlimit (0, numSamples - 1, (int) ((e.ppq - startPpq) / ppqPerSample));
            if (e.on)
                noteOn (generated, e.note, sample);
            else
                noteOff (generated, e.note, sample);
        }

        lastEndPpq = endPpq;
        if (! hostPlaying)
            internalPpq = endPpq;

        currentChord = ChordEngine::chordAt (audioProgression, startPpq);
    }
    else
    {
        if (wasPlaying)
            flushNotes (generated, 0);
        currentChord = -1;
    }

    wasPlaying = playing;
    hostSynced = inHost;
    currentBpm = bpm;

    // MIDI out = incoming MIDI + generated chords + notes clicked in the editor
    midi.addEvents (generated, 0, numSamples, 0);
    keyboardState.processNextMidiBuffer (midi, 0, numSamples, true);
    synth.renderNextBlock (buffer, midi, 0, numSamples);

    if (roundToInt (instrumentParam->load()) == Instruments::midiOut)
    {
        buffer.clear();
        return;
    }

    const int effect = roundToInt (effectParam->load());
    if (effect > 0)
    {
        static constexpr float rooms[] { 0.0f, 0.5f, 0.8f, 0.95f };
        Reverb::Parameters rp;
        rp.roomSize = rooms[effect];
        rp.damping = 0.45f;
        rp.wetLevel = 0.12f + 0.08f * (float) effect;
        rp.dryLevel = 0.9f;
        rp.width = 1.0f;
        reverb.setParameters (rp);

        if (buffer.getNumChannels() >= 2)
            reverb.processStereo (buffer.getWritePointer (0), buffer.getWritePointer (1), numSamples);
        else
            reverb.processMono (buffer.getWritePointer (0), numSamples);
    }

    const float vol = volumeParam->load() / 100.0f;
    gain.setTargetValue (vol * vol * 1.4f);
    gain.applyGain (buffer, numSamples);
}

//==============================================================================
File SwellChordMotionProcessor::getExportFolder()
{
    auto folder = File::getSpecialLocation (File::userDocumentsDirectory).getChildFile ("Swell Chord Motion");
    folder.createDirectory();
    return folder;
}

String SwellChordMotionProcessor::getExportFileName() const
{
    StringArray names;
    for (const auto& c : getProgression())
        names.add (ChordEngine::chordName (c).replace ("#", "s"));

    auto name = names.joinIntoString ("-").substring (0, 60);
    return "SwellChordMotion_" + (name.isEmpty() ? String ("empty") : name) + "_" + String (roundToInt (currentBpm.load())) + "bpm.mid";
}

File SwellChordMotionProcessor::exportMidiFile() const
{
    const auto prog = getProgression();
    std::vector<ChordEngine::NoteEvent> evs;
    const double length = ChordEngine::totalBeats (prog);
    ChordEngine::collectEvents (0.0, length, prog, getChoice ("style"), getArpSettings(), evs);

    constexpr int ticksPerBeat = 960;
    MidiMessageSequence seq;
    seq.addEvent (MidiMessage::tempoMetaEvent (roundToInt (60000000.0 / currentBpm.load())), 0.0);
    seq.addEvent (MidiMessage::timeSignatureMetaEvent (4, 4), 0.0);

    for (const auto& e : evs)
        seq.addEvent (e.on ? MidiMessage::noteOn (1, e.note, (uint8) 100) : MidiMessage::noteOff (1, e.note), e.ppq * ticksPerBeat);

    seq.addEvent (MidiMessage::endOfTrack(), length * ticksPerBeat);
    seq.updateMatchedPairs();

    MidiFile midiFile;
    midiFile.setTicksPerQuarterNote (ticksPerBeat);
    midiFile.addTrack (seq);

    auto file = getExportFolder().getChildFile (getExportFileName());
    file.deleteFile();

    FileOutputStream stream (file);
    if (stream.openedOk())
    {
        midiFile.writeTo (stream);
        stream.flush();
    }
    return file;
}

void SwellChordMotionProcessor::getStateInformation (MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void SwellChordMotionProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (ValueTree::fromXml (*xml));
            ensureProgression();
            undoManager.clearUndoHistory();
            publishProgression();
        }
    }
}

AudioProcessorEditor* SwellChordMotionProcessor::createEditor()
{
    return new SwellChordMotionEditor (*this);
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SwellChordMotionProcessor();
}
