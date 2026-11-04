#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "StyleEngine.h"
#include "SwellSynth.h"

class SwellChordMotionProcessor : public juce::AudioProcessor,
                                  private juce::ValueTree::Listener
{
public:
    SwellChordMotionProcessor();
    ~SwellChordMotionProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    int getChoice (const juce::String& paramId) const { return juce::roundToInt (apvts.getRawParameterValue (paramId)->load()); }
    ChordEngine::ArpSettings getArpSettings() const;
    ChordEngine::Voicing getVoicing() const;
    juce::String chordName (const ChordEngine::ChordItem& c) const { const auto v = getVoicing(); return ChordEngine::chordName (c, v.key, v.scale); }

    // Chord progression editing, message thread only (undoable)
    ChordEngine::Progression getProgression() const;
    void insertChord (const ChordEngine::ChordItem&, int index);   // index < 0 appends
    void removeChord (int index);
    void moveChord (int from, int insertIndex);
    void setChordBeats (int index, int beats);
    void clearProgression();

    // Plays one chord in the current style while held (palette / lane clicks), message thread only
    void startPreview (const ChordEngine::ChordItem&);
    void stopPreview();

    // Writes the progression as a .mid file in the export folder and returns it
    juce::File exportMidiFile() const;
    juce::String getExportFileName() const;
    static juce::File getExportFolder();

    juce::AudioProcessorValueTreeState apvts;
    juce::UndoManager undoManager;
    juce::MidiKeyboardState keyboardState;

    std::atomic<int> currentChord { -1 };
    std::atomic<int> progressionVersion { 0 };
    std::atomic<double> currentBpm { 100.0 };
    std::atomic<bool> hostSynced { false };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    juce::ValueTree progressionTree();
    void ensureProgression();
    void publishProgression();

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override { publishProgression(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { publishProgression(); }
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override { publishProgression(); }
    void valueTreeRedirected (juce::ValueTree&) override { publishProgression(); }

    void noteOn (juce::MidiBuffer&, int channel, int note, int velocity, int sample);
    void emit (juce::MidiBuffer&, const ChordEngine::NoteEvent&, int sample);
    void noteOff (juce::MidiBuffer&, int channel, int note, int sample);
    void flushNotes (juce::MidiBuffer&, int sample);

    std::atomic<float> *volumeParam, *instrumentParam, *styleParam, *bpmParam, *playParam, *effectParam;

    SwellSynth synth;
    juce::Reverb reverb;
    juce::SmoothedValue<float> gain;
    juce::MidiBuffer generated;

    // Progression handed from the message thread to the audio thread
    juce::SpinLock progressionLock;
    ChordEngine::Progression sharedProgression;
    int sharedVersion = 0;
    ChordEngine::Progression audioProgression;
    int audioVersion = -1;

    // Previewed chord handed from the message thread to the audio thread
    juce::SpinLock previewLock;
    ChordEngine::ChordItem sharedPreview;
    bool sharedPreviewOn = false;
    int sharedPreviewVersion = 0, audioPreviewVersion = 0;
    ChordEngine::Progression previewProgression;
    bool previewOn = false;
    double previewPpq = 0.0;

    std::array<std::array<int, 128>, 2> noteCounts {};   // chords on channel 1, bass on channel 2
    std::vector<ChordEngine::NoteEvent> events;
    ChordEngine::StyleRenderer previewRenderer, audioRenderer;

    double sr = 44100.0;
    double internalPpq = 0.0, lastEndPpq = 0.0;
    bool wasPlaying = false, internalRunning = false;
    int lastStyle = -1;
    ChordEngine::ArpSettings lastArp { -1, -1, false };
    ChordEngine::Voicing lastVoicing { -1, -1, false };
    std::atomic<float> *keyParam, *scaleParam, *inversionsParam, *arpParam, *arpStepParam, *arpMirrorParam;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SwellChordMotionProcessor)
};
