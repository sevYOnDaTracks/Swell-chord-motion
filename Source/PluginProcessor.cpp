#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

namespace
{
const Identifier progressionId ("PROGRESSION"), chordId ("CHORD"), rootId ("root"), intervalsId ("intervals"), beatsId ("beats"), baseKeyId ("baseKey"), symbolId ("symbol");

ValueTree toTree (const ChordEngine::ChordItem& c)
{
    StringArray iv;
    for (int i : c.intervals)
        iv.add (String (i));
    return ValueTree (chordId, { { rootId, c.root }, { intervalsId, iv.joinIntoString (",") }, { beatsId, c.beats }, { symbolId, c.symbol } });
}

ChordEngine::ChordItem fromTree (const ValueTree& t)
{
    ChordEngine::ChordItem c;
    c.root = jlimit (0, 11, (int) t[rootId]);
    c.symbol = t[symbolId].toString();
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
    keyParam        = apvts.getRawParameterValue ("key");
    scaleParam      = apvts.getRawParameterValue ("scale");
    chordRangeParam = apvts.getRawParameterValue ("chordRange");
    bassRangeParam  = apvts.getRawParameterValue ("bassRange");
    velocityParam   = apvts.getRawParameterValue ("velocity");
    arpParam        = apvts.getRawParameterValue ("arp");
    arpStepParam    = apvts.getRawParameterValue ("arpStep");
    arpMirrorParam  = apvts.getRawParameterValue ("arpMirror");

    for (int i = 0; i < 16; ++i)
        synth.addVoice (new SwellVoice (instrumentParam, synth.channelState));
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
    choice ("style", "Style", ChordEngine::styleNames(), ChordEngine::styleNames().indexOf ("Dance 1"));
    choice ("arp", "Arpeggio", ChordEngine::arpNames(), 0);
    choice ("arpStep", "Arp step", ChordEngine::arpStepNames, 1);
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "arpMirror", 1 }, "Arp mirror", false));
    choice ("key", "Key", ChordEngine::keyNames, 0);
    choice ("scale", "Scale", ChordEngine::scaleNames, 0);
    choice ("effect", "Effect", Instruments::effectNames, 1);
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "bpm", 1 }, "Tempo", NormalisableRange<float> { 40.0f, 240.0f, 1.0f }, 100.0f,
                    AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " BPM"; })));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "play", 1 }, "Play", false));
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { "velocity", 2 }, "Velocity", 1, 127, ChordEngine::defaultVelocity));
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { "chordRange", 3 }, "Chord range shift", -12, 12, ChordEngine::referenceRangeShift));
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { "bassRange", 3 }, "Bass range shift", -12, 12, ChordEngine::referenceRangeShift));
    return layout;
}

ChordEngine::Voicing SwellChordMotionProcessor::getVoicing() const
{
    return { roundToInt (keyParam->load()), roundToInt (scaleParam->load()),
             roundToInt (chordRangeParam->load()), roundToInt (bassRangeParam->load()) };
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
    if (auto saved = apvts.state.getChildWithName (progressionId); saved.isValid())
    {
        // Roots in earlier sessions were absolute. Anchor them at the saved tonic.
        if (! saved.hasProperty (baseKeyId)) saved.setProperty (baseKeyId, ChordEngine::keyPitch (getChoice ("key")), nullptr);
        return;
    }

    // Same starting point as a blank song: C - Em7 - Am - F
    auto tree = progressionTree();
    tree.setProperty (baseKeyId, 0, nullptr);
    for (auto c : { ChordEngine::ChordItem { 0, { 0, 4, 7 }, 4, {} }, ChordEngine::ChordItem { 4, { 0, 3, 7, 10 }, 4, "m7" },
                    ChordEngine::ChordItem { 9, { 0, 3, 7 }, 4, "m" }, ChordEngine::ChordItem { 5, { 0, 4, 7 }, 4, {} } })
        tree.appendChild (toTree (c), nullptr);
}

ChordEngine::Progression SwellChordMotionProcessor::getProgression() const
{
    return progressionInKey (ChordEngine::keyPitch (getChoice ("key")));
}

ChordEngine::Progression SwellChordMotionProcessor::progressionInKey (int tonic) const
{
    ChordEngine::Progression prog;
    auto tree = apvts.state.getChildWithName (progressionId);
    const int offset = tonic - (int) tree.getProperty (baseKeyId, tonic);
    for (const auto& child : tree)
    {
        auto chord = fromTree (child);
        chord.root = (chord.root + offset + 12) % 12;
        prog.push_back (std::move (chord));
    }
    return prog;
}

void SwellChordMotionProcessor::insertChord (const ChordEngine::ChordItem& chord, int index)
{
    undoManager.beginNewTransaction();
    auto tree = progressionTree();
    auto stored = chord;
    stored.root = (chord.root + (int) tree[baseKeyId] - ChordEngine::keyPitch (getChoice ("key")) + 12) % 12;
    tree.addChild (toTree (stored), index, &undoManager);
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

void SwellChordMotionProcessor::replaceChord (int index, const ChordEngine::ChordItem& chord)
{
    auto tree = progressionTree();
    if (! isPositiveAndBelow (index, tree.getNumChildren())) return;
    undoManager.beginNewTransaction();
    auto stored = chord;
    stored.root = (chord.root + (int) tree[baseKeyId] - ChordEngine::keyPitch (getChoice ("key")) + 12) % 12;
    auto target = tree.getChild (index);
    target.copyPropertiesFrom (toTree (stored), &undoManager);
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
    sharedPreview.beats = 16;   // long enough for the 4-bar patterns
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
    auto prog = progressionInKey (0);
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
    for (auto& counts : noteCounts)
        counts.fill (0);
    keyboardState.reset();
    wasPlaying = internalRunning = false;
}

bool SwellChordMotionProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return out == AudioChannelSet::stereo() || out == AudioChannelSet::mono();
}

void SwellChordMotionProcessor::noteOn (MidiBuffer& buffer, int channel, int note, int velocity, int sample)
{
    if (! isPositiveAndBelow (note, 128))
        return;

    channel = jlimit (1, 2, channel);
    ++noteCounts[(size_t) channel - 1][(size_t) note];
    buffer.addEvent (MidiMessage::noteOn (channel, note, (uint8) jlimit (1, 127, velocity)), sample);
}

void SwellChordMotionProcessor::emit (MidiBuffer& buffer, const ChordEngine::NoteEvent& e, int sample)
{
    using Kind = ChordEngine::NoteEvent::Kind;
    if (e.kind == Kind::expression)
        buffer.addEvent (MidiMessage::controllerEvent (e.channel, 11, e.value), sample);
    else if (e.kind == Kind::pitchBend)
        buffer.addEvent (MidiMessage::pitchWheel (e.channel, e.value), sample);
    else if (e.on)
        noteOn (buffer, e.channel, e.note, ChordEngine::scaledVelocity (e.value, roundToInt (velocityParam->load())), sample);
    else
        noteOff (buffer, e.channel, e.note, sample);
}

void SwellChordMotionProcessor::noteOff (MidiBuffer& buffer, int channel, int note, int sample)
{
    channel = jlimit (1, 2, channel);
    auto& counts = noteCounts[(size_t) channel - 1];
    if (! isPositiveAndBelow (note, 128) || counts[(size_t) note] == 0)
        return;

    if (--counts[(size_t) note] == 0)
        buffer.addEvent (MidiMessage::noteOff (channel, note), sample);
}

void SwellChordMotionProcessor::flushNotes (MidiBuffer& buffer, int sample)
{
    for (int channel = 1; channel <= 2; ++channel)
    {
        auto& counts = noteCounts[(size_t) channel - 1];
        for (int note = 0; note < 128; ++note)
        {
            if (counts[(size_t) note] > 0)
            {
                counts[(size_t) note] = 0;
                buffer.addEvent (MidiMessage::noteOff (channel, note), sample);
            }
        }

        // Mod styles move the expression and the pitch: put them back
        buffer.addEvent (MidiMessage::controllerEvent (channel, 11, 127), sample);
        buffer.addEvent (MidiMessage::pitchWheel (channel, 8192), sample);
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

    const auto voicing = getVoicing();
    const int tonic = ChordEngine::keyPitch (voicing.key);

    // Pick up chord edits; old notes would never get their note-off otherwise
    {
        const SpinLock::ScopedTryLockType lock (progressionLock);
        if (lock.isLocked() && (sharedVersion != audioVersion || audioTonic != tonic))
        {
            audioProgression = sharedProgression;
            for (auto& chord : audioProgression) chord.root = (chord.root + tonic) % 12;
            audioTonic = tonic;
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
    const bool voicingChanged = voicing != lastVoicing;
    if (style != lastStyle || arp != lastArp || voicingChanged)
    {
        flushNotes (generated, 0);
        lastStyle = style;
        lastArp = arp;
        lastVoicing = voicing;
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
        // Once is a single attack on pressing the pad, even mid-bar in the DAW.
        // Rhythmic patterns continue to follow the running host's grid.
        const bool rhythmicPreview = arp.pattern != 0 || ChordEngine::styleDef (style).name != "Once";
        const double start = hostPlaying && rhythmicPreview ? startPpq : previewPpq;
        const double end = start + numSamples * ppqPerSample;
        previewRenderer.collect (start, end, previewProgression, voicing, style, arp, events);

        for (const auto& e : events)
            emit (generated, e, jlimit (0, numSamples - 1, (int) ((e.ppq - start) / ppqPerSample)));

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

        audioRenderer.collect (continuous ? lastEndPpq : startPpq, endPpq, audioProgression, voicing, style, arp, events);

        for (const auto& e : events)
            emit (generated, e, jlimit (0, numSamples - 1, (int) ((e.ppq - startPpq) / ppqPerSample)));

        lastEndPpq = endPpq;
        if (! hostPlaying)
            internalPpq = endPpq;

        currentChord = ChordEngine::chordAtTime (audioProgression, style, startPpq);
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
        names.add (chordName (c).replace ("#", "s"));

    auto name = names.joinIntoString ("-").substring (0, 60);
    const double bpm = hostSynced.load() ? currentBpm.load() : bpmParam->load();
    return "SwellChordMotion_" + (name.isEmpty() ? String ("empty") : name) + "_" + String (roundToInt (bpm)) + "bpm.mid";
}

bool SwellChordMotionProcessor::writeMidiTo (OutputStream& dest) const
{
    const int style = getChoice ("style");
    const int strength = getChoice ("velocity");
    const auto voicing = getVoicing();
    const auto prog = progressionInKey (ChordEngine::keyPitch (voicing.key));
    if (prog.empty()) return false;
    std::vector<ChordEngine::NoteEvent> evs;
    const double length = ChordEngine::loopLength (prog, style);
    ChordEngine::StyleRenderer renderer;
    renderer.collect (0.0, length, prog, voicing, style, getArpSettings(), evs);

    constexpr int ticksPerBeat = ChordEngine::midiTicksPerQuarter;
    const auto& ts = ChordEngine::styleDef (style).time;
    MidiMessageSequence seq;
    seq.addEvent (MidiMessage::tempoMetaEvent (roundToInt (60000000.0 / (hostSynced.load() ? currentBpm.load() : bpmParam->load()))), 0.0);
    seq.addEvent (MidiMessage::timeSignatureMetaEvent (ts.numerator, ts.denominator), 0.0);

    using Kind = ChordEngine::NoteEvent::Kind;
    for (const auto& e : evs)
    {
        const double t = e.ppq * ticksPerBeat;
        if (e.kind == Kind::expression)     seq.addEvent (MidiMessage::controllerEvent (e.channel, 11, e.value), t);
        else if (e.kind == Kind::pitchBend) seq.addEvent (MidiMessage::pitchWheel (e.channel, e.value), t);
        else if (e.on)                      seq.addEvent (MidiMessage::noteOn (e.channel, e.note, (uint8) ChordEngine::scaledVelocity (e.value, strength)), t);
        else                                seq.addEvent (MidiMessage::noteOff (e.channel, e.note), t);
    }

    seq.addEvent (MidiMessage::endOfTrack(), length * ticksPerBeat);
    seq.updateMatchedPairs();

    MidiFile midiFile;
    midiFile.setTicksPerQuarterNote (ticksPerBeat);
    midiFile.addTrack (seq);

    return midiFile.writeTo (dest, 0);
}

File SwellChordMotionProcessor::exportMidiFile (const File& folder) const
{
    // Each drag gets an immutable file. Hosts can continue reading a previous drop.
    const auto name = File::createLegalFileName (getExportFileName()).upToLastOccurrenceOf (".mid", false, true);
    const auto file = folder.getChildFile (name + "_" + Uuid().toString().substring (0, 12) + ".mid");
    TemporaryFile temp (file);
    {
        FileOutputStream stream (temp.getFile());
        if (! stream.openedOk() || ! writeMidiTo (stream)) return {};
        stream.flush();
        if (stream.getStatus().failed()) return {};
    }
    return temp.overwriteTargetFileWithTemporary() ? file : File();
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
            auto state = ValueTree::fromXml (*xml);
            // A 1.4.0 project has no stored ranges. Always use the reference defaults
            // when opening it, even if the previously loaded project had custom ranges.
            for (const auto* id : { "chordRange", "bassRange" })
                if (! state.getChildWithProperty ("id", id).isValid())
                    state.appendChild (ValueTree ("PARAM", {{ "id", id }, { "value", ChordEngine::referenceRangeShift }}), nullptr);
            apvts.replaceState (state);
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
