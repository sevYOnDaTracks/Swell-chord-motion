#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "SwellLookAndFeel.h"

// Plays a chord in the current style while the mouse is held
class ChordPreview
{
public:
    explicit ChordPreview (SwellChordMotionProcessor& p) : proc (p) {}
    ~ChordPreview() { stop(); }

    void start (const ChordEngine::ChordItem&);
    void stop();

private:
    SwellChordMotionProcessor& proc;
    bool held = false;
};

// One cell of the "Chords in <scale>" palette: click to hear, drag (or double-click) to add
class PaletteCell : public juce::Component
{
public:
    PaletteCell (SwellChordMotionProcessor& p, bool isTriadRow) : preview (p), bold (isTriadRow) {}

    void setChord (std::optional<ChordEngine::ChordItem>, const juce::String& name);
    std::function<void (const ChordEngine::ChordItem&)> onAdd;

    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    ChordPreview preview;
    std::optional<ChordEngine::ChordItem> chord;
    juce::String name;
public:
    void release();
    std::function<void (PaletteCell*)> onPress;
    bool hold = false;
private:
    bool bold, pressed = false;
};

// The chord progression lane: one block per chord, width = duration
class ProgressionLane : public juce::Component, public juce::DragAndDropTarget
{
public:
    explicit ProgressionLane (SwellChordMotionProcessor&);

    void refresh();
    void setPlaying (int index);
    void editSelected() { if (selected >= 0) showMenu (selected); }
    int getSelected() const { return selected; }
    std::function<void()> onSelectionChanged;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragMove (const SourceDetails&) override;
    void itemDragExit (const SourceDetails&) override;
    void itemDropped (const SourceDetails&) override;

private:
    juce::Rectangle<float> blockBounds (int index) const;
    float pixelsPerBeat() const;
    int blockAt (juce::Point<float>) const;
    int insertIndexFor (float x) const;
    void select (int index);
    void showMenu (int index);

    SwellChordMotionProcessor& proc;
    ChordPreview preview;
    ChordEngine::Progression items;
    int selected = -1, playing = -1, insertMarker = -1;
};

// Drag this into the DAW to drop the progression as a .mid file; click to show the file
class MidiDragButton : public juce::Component
{
public:
    explicit MidiDragButton (SwellChordMotionProcessor& p) : proc (p) {}

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { dragging = false; }
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    SwellChordMotionProcessor& proc;
    bool dragging = false;
};

class ChordKeyboard : public juce::MidiKeyboardComponent
{
public:
    explicit ChordKeyboard (juce::MidiKeyboardState& stateToUse)
        : juce::MidiKeyboardComponent (stateToUse, horizontalKeyboard), notes (stateToUse) {}
    void drawWhiteNote (int, juce::Graphics&, juce::Rectangle<float>, bool, bool, juce::Colour, juce::Colour) override;
    void drawBlackNote (int, juce::Graphics&, juce::Rectangle<float>, bool, bool, juce::Colour) override;
private:
    juce::MidiKeyboardState& notes;
};

// A vertical time view of the MIDI that will be dropped into the host.
class ResultRoll : public juce::Component
{
public:
    explicit ResultRoll (SwellChordMotionProcessor& p) : proc (p) {}
    void refresh();
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    juce::String selection;
private:
    struct Note { double start, end; int pitch, velocity, channel; };
    juce::Rectangle<float> boundsFor (const Note&) const;
    SwellChordMotionProcessor& proc;
    std::vector<Note> notes;
    int low = 24, high = 72;
    double offset = 0, length = 0;
    juce::String signature;
};

class SwellChordMotionEditor : public juce::AudioProcessorEditor,
                               public juce::DragAndDropContainer,
                               private juce::Timer
{
public:
    explicit SwellChordMotionEditor (SwellChordMotionProcessor&);
    ~SwellChordMotionEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshPalette();
    void updateEditButtons();
    void showRanges();

    void setupCombo (juce::ComboBox&, const juce::StringArray& items, const juce::String& paramId);
    void setupBar (juce::Slider&, const juce::String& paramId);
    void setupCaption (juce::Label&, const juce::String& text);

    SwellChordMotionProcessor& proc;
    SwellLookAndFeel lnf;

    juce::TextButton playButton { "PLAY" }, undoButton { "UNDO" }, redoButton { "REDO" };
    juce::Slider volSlider, bpmSlider, velocitySlider;
    juce::ComboBox instrBox, styleBox, keyBox, scaleBox, effectBox;
    juce::Label volLabel, instrLabel, styleLabel, scaleLabel, effectLabel, bpmLabel, velocityLabel;
    juce::Label hostBpmDisplay;

    // Arpeggio (replaces the chord part of the style, the style keeps its bass)
    juce::ComboBox arpBox, arpStepBox;
    juce::Label arpLabel, arpStepLabel;
    juce::TextButton arpMirrorButton { "MIRROR" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> arpMirrorAttachment;

    // Takes the key found by Swell Key (see SwellLink.h)
    juce::TextButton swellKeyButton { "SWELL KEY" };
    juce::uint32 swellKeyFlashUntil = 0;
    void useSwellKey();

    ChordKeyboard keyboard;

    juce::TextButton chordsTab { "Chords" }, resultTab { "Result" }, holdButton { "Hold" }, rangesButton { "Ranges" };
    juce::String paletteTitle;
    void showResult (bool);
    bool resultVisible = false;
    ResultRoll resultRoll;
    std::vector<std::unique_ptr<PaletteCell>> cells;   // 4 rows x 7 degrees

    juce::TextButton editButton { "Edit" }, shorterButton { "-" }, longerButton { "+" }, deleteButton { "DELETE" }, clearButton { "CLEAR" };
    ProgressionLane lane;
    MidiDragButton midiButton;

    juce::Rectangle<int> headerArea, paletteArea, paletteTitleArea, progressionTitleArea;
    juce::String paletteSignature;
    int lastProgressionVersion = -1;

    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>> comboAttachments;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> sliderAttachments;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> playAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SwellChordMotionEditor)
};
