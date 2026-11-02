#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "SwellLookAndFeel.h"

// Plays a chord on the shared keyboard state while the mouse is held
class ChordPreview
{
public:
    explicit ChordPreview (juce::MidiKeyboardState& s) : state (s) {}
    ~ChordPreview() { stop(); }

    void start (const ChordEngine::ChordItem&);
    void stop();

private:
    juce::MidiKeyboardState& state;
    std::vector<int> held;
};

// One cell of the "Chords in <scale>" palette: click to hear, drag (or double-click) to add
class PaletteCell : public juce::Component
{
public:
    PaletteCell (juce::MidiKeyboardState& s, bool isTriadRow) : preview (s), bold (isTriadRow) {}

    void setChord (std::optional<ChordEngine::ChordItem>);
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
    bool bold, pressed = false;
};

// The chord progression lane: one block per chord, width = duration
class ProgressionLane : public juce::Component, public juce::DragAndDropTarget
{
public:
    explicit ProgressionLane (SwellChordMotionProcessor&);

    void refresh();
    void setPlaying (int index);
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
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    SwellChordMotionProcessor& proc;
    bool dragging = false;
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

    void setupCombo (juce::ComboBox&, const juce::StringArray& items, const juce::String& paramId);
    void setupBar (juce::Slider&, const juce::String& paramId);
    void setupCaption (juce::Label&, const juce::String& text);

    SwellChordMotionProcessor& proc;
    SwellLookAndFeel lnf;

    juce::TextButton playButton { "PLAY" }, undoButton { "UNDO" }, redoButton { "REDO" };
    juce::Slider volSlider, bpmSlider;
    juce::ComboBox instrBox, styleBox, keyBox, scaleBox, effectBox;
    juce::Label volLabel, instrLabel, styleLabel, scaleLabel, effectLabel, bpmLabel;

    juce::MidiKeyboardComponent keyboard;

    juce::String paletteTitle;
    std::vector<std::unique_ptr<PaletteCell>> cells;   // 4 rows x 7 degrees

    juce::TextButton shorterButton { "-" }, longerButton { "+" }, deleteButton { "DELETE" }, clearButton { "CLEAR" };
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
