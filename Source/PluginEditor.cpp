#include "PluginEditor.h"

using namespace juce;
using namespace SwellColours;

namespace
{
String describe (const ChordEngine::ChordItem& c)
{
    StringArray iv;
    for (int i : c.intervals)
        iv.add (String (i));
    return "chord:" + String (c.root) + ":" + iv.joinIntoString (",") + ":" + String (c.beats);
}

std::optional<ChordEngine::ChordItem> parseChord (const String& description)
{
    const auto parts = StringArray::fromTokens (description, ":", {});
    if (parts.size() != 4 || parts[0] != "chord")
        return std::nullopt;

    ChordEngine::ChordItem c;
    c.root = parts[1].getIntValue();
    c.beats = parts[3].getIntValue();
    for (const auto& s : StringArray::fromTokens (parts[2], ",", {}))
        c.intervals.push_back (s.getIntValue());
    return c;
}
} // namespace

//==============================================================================
void ChordPreview::start (const ChordEngine::ChordItem& chord)
{
    stop();
    held = ChordEngine::chordNotes (chord);
    held.push_back (ChordEngine::bassNote (chord));
    for (int n : held)
        state.noteOn (1, n, 0.8f);
}

void ChordPreview::stop()
{
    for (int n : held)
        state.noteOff (1, n, 0.0f);
    held.clear();
}

//==============================================================================
void PaletteCell::setChord (std::optional<ChordEngine::ChordItem> c)
{
    chord = std::move (c);
    setMouseCursor (chord ? MouseCursor::PointingHandCursor : MouseCursor::NormalCursor);
    repaint();
}

void PaletteCell::paint (Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (0.5f);

    if (! chord)
    {
        g.setColour (Colour (0xff18191c));
        g.fillRoundedRectangle (r, 5.0f);
        return;
    }

    g.setColour (pressed ? orange : isMouseOver() ? panelLight.brighter (0.08f) : control);
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (pressed ? orange : border);
    g.drawRoundedRectangle (r, 5.0f, 1.0f);

    g.setColour (pressed ? background : text);
    g.setFont (swellFont (15.0f, bold));
    g.drawText (ChordEngine::chordName (*chord), r, Justification::centred);
}

void PaletteCell::mouseDown (const MouseEvent&)
{
    if (! chord)
        return;

    pressed = true;
    preview.start (*chord);
    repaint();
}

void PaletteCell::mouseUp (const MouseEvent&)
{
    pressed = false;
    preview.stop();
    repaint();
}

void PaletteCell::mouseDrag (const MouseEvent& e)
{
    if (! chord || e.getDistanceFromDragStart() < 6)
        return;

    if (auto* container = DragAndDropContainer::findParentDragContainerFor (this))
    {
        if (! container->isDragAndDropActive())
        {
            preview.stop();
            pressed = false;
            repaint();
            container->startDragging (describe (*chord), this);
        }
    }
}

void PaletteCell::mouseDoubleClick (const MouseEvent&)
{
    if (chord && onAdd)
        onAdd (*chord);
}

//==============================================================================
ProgressionLane::ProgressionLane (SwellChordMotionProcessor& p) : proc (p), preview (p.keyboardState)
{
    refresh();
}

void ProgressionLane::refresh()
{
    items = proc.getProgression();
    if (selected >= (int) items.size())
        select (-1);
    repaint();
}

void ProgressionLane::setPlaying (int index)
{
    if (index != playing)
    {
        playing = index;
        repaint();
    }
}

float ProgressionLane::pixelsPerBeat() const
{
    const float total = (float) jmax (16.0, ChordEngine::totalBeats (items));
    return jmin (30.0f, ((float) getWidth() - 16.0f) / total);
}

Rectangle<float> ProgressionLane::blockBounds (int index) const
{
    const float ppb = pixelsPerBeat();
    float x = 8.0f;
    for (int i = 0; i < index; ++i)
        x += (float) items[(size_t) i].beats * ppb;
    return { x, 8.0f, (float) items[(size_t) index].beats * ppb - 3.0f, (float) getHeight() - 16.0f };
}

int ProgressionLane::blockAt (Point<float> pos) const
{
    for (int i = 0; i < (int) items.size(); ++i)
        if (blockBounds (i).contains (pos))
            return i;
    return -1;
}

int ProgressionLane::insertIndexFor (float x) const
{
    for (int i = 0; i < (int) items.size(); ++i)
        if (x < blockBounds (i).getCentreX())
            return i;
    return (int) items.size();
}

void ProgressionLane::select (int index)
{
    if (index != selected)
    {
        selected = index;
        repaint();
        if (onSelectionChanged)
            onSelectionChanged();
    }
}

void ProgressionLane::paint (Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    g.setColour (Colour (0xff111215));
    g.fillRoundedRectangle (b, 8.0f);
    g.setColour (border);
    g.drawRoundedRectangle (b.reduced (0.5f), 8.0f, 1.0f);

    if (items.empty())
    {
        g.setColour (dim);
        g.setFont (swellFont (13.0f));
        g.drawText ("Drag chords here (or double-click a chord)", b, Justification::centred);
    }

    for (int i = 0; i < (int) items.size(); ++i)
    {
        const auto r = blockBounds (i);
        const bool isPlaying = i == playing;

        g.setColour (isPlaying ? Colour (0xffffd84a) : orange);
        g.fillRoundedRectangle (r, 5.0f);

        // One tick per beat along the bottom edge
        g.setColour (background.withAlpha (0.35f));
        const float ppb = pixelsPerBeat();
        for (int beat = 1; beat < items[(size_t) i].beats; ++beat)
            g.fillRect (r.getX() + (float) beat * ppb - 0.5f, r.getBottom() - 7.0f, 1.0f, 5.0f);

        if (i == selected)
        {
            g.setColour (text);
            g.drawRoundedRectangle (r.reduced (1.0f), 5.0f, 2.0f);
        }

        g.setColour (background);
        g.setFont (swellFont (15.0f, true));
        g.drawFittedText (ChordEngine::chordName (items[(size_t) i]), r.toNearestInt().reduced (2, 0), Justification::centred, 1);
    }

    if (insertMarker >= 0)
    {
        const float x = insertMarker < (int) items.size() ? blockBounds (insertMarker).getX() - 2.0f
                                                          : (items.empty() ? 8.0f : blockBounds ((int) items.size() - 1).getRight() + 1.0f);
        g.setColour (text);
        g.fillRoundedRectangle (x - 1.5f, 4.0f, 3.0f, b.getHeight() - 8.0f, 1.5f);
    }
}

void ProgressionLane::mouseDown (const MouseEvent& e)
{
    const int index = blockAt (e.position);
    select (index);

    if (index < 0)
        return;

    if (e.mods.isPopupMenu())
        showMenu (index);
    else
        preview.start (items[(size_t) index]);
}

void ProgressionLane::mouseUp (const MouseEvent&)
{
    preview.stop();
}

void ProgressionLane::mouseDrag (const MouseEvent& e)
{
    if (selected < 0 || e.mods.isPopupMenu() || e.getDistanceFromDragStart() < 6)
        return;

    if (auto* container = DragAndDropContainer::findParentDragContainerFor (this))
    {
        if (! container->isDragAndDropActive())
        {
            preview.stop();
            container->startDragging ("move:" + String (selected), this);
        }
    }
}

void ProgressionLane::showMenu (int index)
{
    PopupMenu durations;
    for (int beats : { 1, 2, 3, 4, 6, 8, 12, 16 })
        durations.addItem (100 + beats, String (beats) + (beats == 1 ? " beat" : " beats"), true, items[(size_t) index].beats == beats);

    PopupMenu menu;
    menu.addSubMenu ("Duration", durations);
    menu.addItem (1, "Duplicate");
    menu.addItem (2, "Delete");

    menu.showMenuAsync (PopupMenu::Options(), [this, index, safe = Component::SafePointer<ProgressionLane> (this)] (int result)
    {
        if (safe == nullptr || index >= (int) items.size())
            return;

        if (result == 1)
            proc.insertChord (items[(size_t) index], index + 1);
        else if (result == 2)
            proc.removeChord (index);
        else if (result > 100)
            proc.setChordBeats (index, result - 100);
    });
}

bool ProgressionLane::isInterestedInDragSource (const SourceDetails& details)
{
    const auto desc = details.description.toString();
    return desc.startsWith ("chord:") || desc.startsWith ("move:");
}

void ProgressionLane::itemDragMove (const SourceDetails& details)
{
    const int marker = insertIndexFor ((float) details.localPosition.x);
    if (marker != insertMarker)
    {
        insertMarker = marker;
        repaint();
    }
}

void ProgressionLane::itemDragExit (const SourceDetails&)
{
    insertMarker = -1;
    repaint();
}

void ProgressionLane::itemDropped (const SourceDetails& details)
{
    const int index = insertIndexFor ((float) details.localPosition.x);
    const auto desc = details.description.toString();
    insertMarker = -1;

    if (auto chord = parseChord (desc))
    {
        proc.insertChord (*chord, index);
        refresh();
        select (index);
    }
    else if (desc.startsWith ("move:"))
    {
        const int from = desc.fromFirstOccurrenceOf (":", false, false).getIntValue();
        proc.moveChord (from, index);
        refresh();
        select (index > from ? index - 1 : index);
    }
    repaint();
}

//==============================================================================
void MidiDragButton::paint (Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (isMouseOverOrDragging() ? panelLight : control);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (orange);
    g.drawRoundedRectangle (r, 6.0f, 1.0f);
    g.setFont (swellFont (13.0f, true));
    g.drawText (String (CharPointer_UTF8 ("DRAG MIDI \xe2\x86\x93")), r, Justification::centred);
}

void MidiDragButton::mouseDrag (const MouseEvent& e)
{
    if (dragging || e.getDistanceFromDragStart() < 5)
        return;

    dragging = true;
    const auto file = proc.exportMidiFile();
    if (file.existsAsFile())
        DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this);
}

void MidiDragButton::mouseUp (const MouseEvent&)
{
    if (! dragging)
        proc.exportMidiFile().revealToUser();
    dragging = false;
}

//==============================================================================
SwellChordMotionEditor::SwellChordMotionEditor (SwellChordMotionProcessor& p)
    : AudioProcessorEditor (p), proc (p),
      keyboard (p.keyboardState, MidiKeyboardComponent::horizontalKeyboard),
      lane (p), midiButton (p)
{
    setLookAndFeel (&lnf);

    playButton.setClickingTogglesState (true);
    addAndMakeVisible (playButton);
    playAttachment = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "play", playButton);

    undoButton.onClick = [this] { proc.undoManager.undo(); };
    redoButton.onClick = [this] { proc.undoManager.redo(); };
    addAndMakeVisible (undoButton);
    addAndMakeVisible (redoButton);

    setupCaption (volLabel, "VOL");
    setupCaption (instrLabel, "INSTR");
    setupCaption (styleLabel, "STYLE");
    setupCaption (scaleLabel, "SCALE");
    setupCaption (effectLabel, "FX");
    setupCaption (bpmLabel, "TEMPO");

    setupBar (volSlider, "vol");
    setupBar (bpmSlider, "bpm");
    setupCombo (instrBox, Instruments::names, "instr");
    setupCombo (styleBox, ChordEngine::styleNames(), "style");
    setupCombo (keyBox, ChordEngine::keyNames, "key");
    setupCombo (scaleBox, ChordEngine::scaleNames, "scale");
    setupCombo (effectBox, Instruments::effectNames, "effect");

    keyboard.setAvailableRange (24, 96);
    keyboard.setScrollButtonsVisible (false);
    keyboard.setOctaveForMiddleC (4);
    keyboard.setColour (MidiKeyboardComponent::whiteNoteColourId, Colour (0xffe9e9ec));
    keyboard.setColour (MidiKeyboardComponent::blackNoteColourId, Colour (0xff1a1b1e));
    keyboard.setColour (MidiKeyboardComponent::keySeparatorLineColourId, Colour (0xff8c8d94));
    keyboard.setColour (MidiKeyboardComponent::keyDownOverlayColourId, orange);
    keyboard.setColour (MidiKeyboardComponent::mouseOverKeyOverlayColourId, orange.withAlpha (0.3f));
    keyboard.setColour (MidiKeyboardComponent::shadowColourId, Colours::transparentBlack);
    keyboard.setColour (MidiKeyboardComponent::textLabelColourId, Colour (0xff6b6c72));
    addAndMakeVisible (keyboard);

    for (int row = 0; row < 4; ++row)
    {
        for (int degree = 0; degree < 7; ++degree)
        {
            auto cell = std::make_unique<PaletteCell> (proc.keyboardState, row == 1);
            cell->onAdd = [this] (const ChordEngine::ChordItem& c) { proc.insertChord (c, -1); };
            addAndMakeVisible (*cell);
            cells.push_back (std::move (cell));
        }
    }

    shorterButton.onClick = [this]
    {
        const auto prog = proc.getProgression();
        if (isPositiveAndBelow (lane.getSelected(), (int) prog.size()))
            proc.setChordBeats (lane.getSelected(), prog[(size_t) lane.getSelected()].beats - 1);
    };
    longerButton.onClick = [this]
    {
        const auto prog = proc.getProgression();
        if (isPositiveAndBelow (lane.getSelected(), (int) prog.size()))
            proc.setChordBeats (lane.getSelected(), prog[(size_t) lane.getSelected()].beats + 1);
    };
    deleteButton.onClick = [this] { if (lane.getSelected() >= 0) proc.removeChord (lane.getSelected()); };
    clearButton.onClick = [this] { proc.clearProgression(); };

    for (auto* b : { &shorterButton, &longerButton, &deleteButton, &clearButton })
        addAndMakeVisible (b);

    lane.onSelectionChanged = [this] { updateEditButtons(); };
    addAndMakeVisible (lane);
    addAndMakeVisible (midiButton);

    setSize (900, 540);
    refreshPalette();
    updateEditButtons();
    startTimerHz (30);
}

SwellChordMotionEditor::~SwellChordMotionEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void SwellChordMotionEditor::setupCombo (ComboBox& box, const StringArray& items, const String& paramId)
{
    box.addItemList (items, 1);
    addAndMakeVisible (box);
    comboAttachments.push_back (std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, paramId, box));
}

void SwellChordMotionEditor::setupBar (Slider& slider, const String& paramId)
{
    slider.setSliderStyle (Slider::LinearBar);
    slider.setColour (Slider::textBoxTextColourId, text);
    slider.setColour (Slider::trackColourId, orange.withAlpha (0.55f));
    addAndMakeVisible (slider);
    sliderAttachments.push_back (std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, paramId, slider));
}

void SwellChordMotionEditor::setupCaption (Label& label, const String& textToShow)
{
    label.setText (textToShow, dontSendNotification);
    label.setFont (swellFont (11.5f, true));
    label.setColour (Label::textColourId, dim);
    label.setJustificationType (Justification::centredRight);
    addAndMakeVisible (label);
}

void SwellChordMotionEditor::refreshPalette()
{
    const int key = proc.getChoice ("key");
    const int scale = proc.getChoice ("scale");

    for (int row = 0; row < 4; ++row)
        for (int degree = 0; degree < 7; ++degree)
            cells[(size_t) (row * 7 + degree)]->setChord (ChordEngine::paletteChord (key, scale, degree, row));

    paletteTitle = "CHORDS IN " + ChordEngine::keyNames[key] + " " + ChordEngine::scaleNames[scale].toUpperCase();
    repaint (paletteTitleArea);
}

void SwellChordMotionEditor::updateEditButtons()
{
    const bool hasSelection = lane.getSelected() >= 0;
    shorterButton.setEnabled (hasSelection);
    longerButton.setEnabled (hasSelection);
    deleteButton.setEnabled (hasSelection);
}

void SwellChordMotionEditor::timerCallback()
{
    const auto sig = String (proc.getChoice ("key")) + "/" + String (proc.getChoice ("scale"));
    if (sig != paletteSignature)
    {
        paletteSignature = sig;
        refreshPalette();
    }

    if (proc.progressionVersion != lastProgressionVersion)
    {
        lastProgressionVersion = proc.progressionVersion;
        lane.refresh();
        updateEditButtons();
    }

    lane.setPlaying (proc.currentChord.load());
    undoButton.setEnabled (proc.undoManager.canUndo());
    redoButton.setEnabled (proc.undoManager.canRedo());
    bpmSlider.setEnabled (! proc.hostSynced.load());

    const auto playText = playButton.getToggleState() ? "STOP" : "PLAY";
    if (playButton.getButtonText() != playText)
        playButton.setButtonText (playText);
}

void SwellChordMotionEditor::paint (Graphics& g)
{
    g.fillAll (background);

    drawSwellLogo (g, headerArea.toFloat().withWidth (38.0f).withSizeKeepingCentre (38.0f, 38.0f), text);

    g.setColour (text);
    g.setFont (swellFont (26.0f, true));
    g.drawText ("SWELL", headerArea.getX() + 50, headerArea.getY() + 2, 170, 26, Justification::centredLeft);

    auto subtitle = swellFont (11.0f, true);
    subtitle.setExtraKerningFactor (0.22f);
    g.setFont (subtitle);
    g.setColour (orange);
    g.drawText ("CHORD MOTION", headerArea.getX() + 51, headerArea.getY() + 28, 170, 14, Justification::centredLeft);

    g.setColour (panel);
    g.fillRoundedRectangle (paletteArea.expanded (6).toFloat(), 8.0f);

    g.setFont (swellFont (12.0f, true));
    g.setColour (dim);
    g.drawText (paletteTitle, paletteTitleArea, Justification::centredLeft);
    g.drawText ("CHORD PROGRESSION", progressionTitleArea, Justification::centredLeft);

    g.setFont (swellFont (11.0f));
    g.drawText ("Click = listen   |   drag or double-click = add   |   right-click a block = duration",
                paletteTitleArea, Justification::centredRight);

    auto footer = getLocalBounds().removeFromBottom (24).reduced (22, 0);
    g.setFont (swellFont (11.5f));
    g.drawText ("v1.1.0   |   by @sevyondatracks", footer, Justification::centredRight);
}

void SwellChordMotionEditor::resized()
{
    auto area = getLocalBounds().reduced (20, 14);

    headerArea = area.removeFromTop (44);
    {
        auto row = headerArea.withTrimmedLeft (230).reduced (0, 6);
        playButton.setBounds (row.removeFromRight (90));
        row.removeFromRight (8);
        redoButton.setBounds (row.removeFromRight (64));
        row.removeFromRight (6);
        undoButton.setBounds (row.removeFromRight (64));
        row.removeFromRight (18);
        bpmSlider.setBounds (row.removeFromRight (100));
        bpmLabel.setBounds (row.removeFromRight (52));
        row.removeFromRight (12);
        effectBox.setBounds (row.removeFromRight (100));
        effectLabel.setBounds (row.removeFromRight (28));
    }

    area.removeFromTop (10);
    {
        auto row = area.removeFromTop (30);
        auto place = [&row] (Label& label, int labelW, Component& comp, int compW)
        {
            label.setBounds (row.removeFromLeft (labelW).withTrimmedRight (8));
            comp.setBounds (row.removeFromLeft (compW));
            row.removeFromLeft (14);
        };
        place (volLabel, 36, volSlider, 70);
        place (instrLabel, 52, instrBox, 130);
        place (styleLabel, 52, styleBox, 140);
        scaleLabel.setBounds (row.removeFromLeft (54).withTrimmedRight (8));
        keyBox.setBounds (row.removeFromLeft (64));
        row.removeFromLeft (6);
        scaleBox.setBounds (row.removeFromLeft (190));
    }

    area.removeFromTop (12);
    {
        auto keys = area.removeFromTop (66);
        keyboard.setBounds (keys);
        keyboard.setKeyWidth ((float) keys.getWidth() / 43.0f);   // C1..C7 = 43 white keys
    }

    area.removeFromTop (12);
    paletteTitleArea = area.removeFromTop (20);
    area.removeFromTop (8);
    paletteArea = area.removeFromTop (4 * 36 + 3 * 4).reduced (6, 0);
    {
        constexpr int gap = 4;
        const int cellW = (paletteArea.getWidth() - gap * 6) / 7;
        for (int row = 0; row < 4; ++row)
            for (int degree = 0; degree < 7; ++degree)
                cells[(size_t) (row * 7 + degree)]->setBounds (paletteArea.getX() + degree * (cellW + gap),
                                                               paletteArea.getY() + row * (36 + gap), cellW, 36);
    }

    area.removeFromTop (16);
    {
        auto row = area.removeFromTop (28);
        progressionTitleArea = row.removeFromLeft (200);
        midiButton.setBounds (row.removeFromRight (120));
        row.removeFromRight (10);
        clearButton.setBounds (row.removeFromRight (70));
        row.removeFromRight (6);
        deleteButton.setBounds (row.removeFromRight (76));
        row.removeFromRight (6);
        longerButton.setBounds (row.removeFromRight (30));
        row.removeFromRight (4);
        shorterButton.setBounds (row.removeFromRight (30));
    }

    area.removeFromTop (6);
    lane.setBounds (area.removeFromTop (64));
}
