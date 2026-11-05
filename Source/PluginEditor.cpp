#include "PluginEditor.h"
#include "SwellLink.h"

using namespace juce;
using namespace SwellColours;

namespace
{
String describe (const ChordEngine::ChordItem& c)
{
    StringArray iv;
    for (int i : c.intervals)
        iv.add (String (i));
    return "chord:" + String (c.root) + ":" + iv.joinIntoString (",") + ":" + String (c.beats) + ":" + c.symbol;
}

std::optional<ChordEngine::ChordItem> parseChord (const String& description)
{
    const auto parts = StringArray::fromTokens (description, ":", {});
    if ((parts.size() != 4 && parts.size() != 5) || parts[0] != "chord")
        return std::nullopt;

    ChordEngine::ChordItem c;
    c.root = parts[1].getIntValue();
    c.beats = parts[3].getIntValue();
    if (parts.size() == 5) c.symbol = parts[4];
    for (const auto& s : StringArray::fromTokens (parts[2], ",", {}))
        c.intervals.push_back (s.getIntValue());
    return c;
}
} // namespace

//==============================================================================
void ChordPreview::start (const ChordEngine::ChordItem& chord)
{
    proc.startPreview (chord);
    held = true;
}

void ChordPreview::stop()
{
    if (held)
        proc.stopPreview();
    held = false;
}

//==============================================================================
void PaletteCell::setChord (std::optional<ChordEngine::ChordItem> c, const String& chordName)
{
    release();
    chord = std::move (c);
    name = chordName;
    setTitle (name);
    setMouseCursor (chord ? MouseCursor::PointingHandCursor : MouseCursor::NormalCursor);
    repaint();
}

void PaletteCell::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    const auto face = pressed ? activePad : (bold ? triadPad : pad);
    g.setColour (chord ? (isMouseOver() ? face.brighter (0.09f) : face) : control);
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (pressed ? accent : (bold && chord ? border.brighter (0.12f) : border.withAlpha (0.60f)));
    g.drawRoundedRectangle (r, 5.0f, 1.0f);
    g.setColour (pressed ? background : (bold ? text : dim.brighter (0.16f)));
    g.setFont (swellFont (bold ? 19.0f : 16.0f, bold));
    g.drawFittedText (name, getLocalBounds().reduced (2, 0), Justification::centred, 1);
}

void PaletteCell::mouseDown (const MouseEvent&)
{
    if (! chord)
        return;

    if (onPress) onPress (this);
    if (pressed && hold) { release(); return; }
    pressed = true;
    preview.start (*chord);
    repaint();
}

void PaletteCell::mouseUp (const MouseEvent&)
{
    if (! hold) release();
}

void PaletteCell::release()
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
ProgressionLane::ProgressionLane (SwellChordMotionProcessor& p) : proc (p), preview (p)
{
    refresh();
    if (! items.empty()) selected = 0;
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
    return ((float) getWidth() - 8.0f) / (total + 4.0f);
}

Rectangle<float> ProgressionLane::blockBounds (int index) const
{
    const float ppb = pixelsPerBeat();
    float x = 4.0f;
    for (int i = 0; i < index; ++i)
        x += (float) items[(size_t) i].beats * ppb;
    return { x, 4.0f, jmax (0.5f, (float) items[(size_t) index].beats * ppb - 3.0f), (float) getHeight() - 8.0f };
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
    g.setColour (control);
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
        const bool atCursor = i == (playing >= 0 ? playing : selected);

        g.setColour (atCursor ? SwellColours::cursor : triadPad);
        g.fillRoundedRectangle (r, 5.0f);

        // One tick per beat along the bottom edge
        g.setColour (atCursor ? background.withAlpha (0.42f) : accent.withAlpha (0.5f));
        const float ppb = pixelsPerBeat();
        for (int beat = 1; beat < items[(size_t) i].beats; ++beat)
            g.fillRect (r.getX() + (float) beat * ppb - 0.5f, r.getBottom() - 7.0f, 1.0f, 5.0f);

        g.setColour (atCursor ? background : text);
        g.setFont (swellFont (17.0f, atCursor));
        g.drawFittedText (proc.chordName (items[(size_t) i]), r.toNearestInt().reduced (2, 0), Justification::centred, 1);
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

    const auto v = proc.getVoicing();
    const auto& chord = items[(size_t) index];
    PopupMenu roots;
    for (int pitch = 0; pitch < 12; ++pitch)
        roots.addItem (200 + pitch, ChordEngine::noteName (pitch, v.key, v.scale), true, chord.root == pitch);

    PopupMenu menu;
    menu.addSubMenu ("Root", roots);
    PopupMenu types;
    for (const String family : { "Major", "Minor", "Diminished", "Suspended", "Power" })
    {
        PopupMenu group;
        int id = 300;
        for (const auto& q : ChordEngine::qualities())
        {
            if (q.group != nullptr && family == q.group)
                group.addItem (id, q.menuLabel, true, chord.intervals == q.intervals
                               && (chord.symbol.isEmpty() || chord.symbol == q.suffix));
            ++id;
        }
        types.addSubMenu (family, group);
    }
    menu.addSubMenu ("Chord type", types);
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
        else if (isPositiveAndBelow (result - 300, (int) ChordEngine::qualities().size()))
        {
            auto chordToEdit = items[(size_t) index];
            const auto& quality = ChordEngine::qualities()[(size_t) result - 300];
            chordToEdit.intervals = quality.intervals;
            chordToEdit.symbol = quality.suffix;
            proc.replaceChord (index, chordToEdit);
        }
        else if (isPositiveAndBelow (result - 200, 12))
        {
            auto chordToEdit = items[(size_t) index];
            chordToEdit.root = result - 200;
            proc.replaceChord (index, chordToEdit);
        }
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
    g.setColour (isMouseOverOrDragging() ? orange.brighter (0.12f) : orange);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (accent.withAlpha (0.82f));
    g.drawRoundedRectangle (r, 6.0f, 1.0f);
    g.setColour (background);
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
    else
        AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "MIDI export", "Could not write the MIDI file in Documents/Swell Chord Motion.");
}

void MidiDragButton::mouseUp (const MouseEvent&)
{
    if (! dragging)
    {
        auto file = proc.exportMidiFile();
        if (file.existsAsFile()) file.revealToUser();
        else AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "MIDI export", "Could not write the MIDI file in Documents/Swell Chord Motion.");
    }
    dragging = false;
}

//==============================================================================
void ChordKeyboard::drawWhiteNote (int pitch, Graphics& g, Rectangle<float> area, bool down, bool over,
                                   Colour lineColour, Colour textColour)
{
    if (down)
    {
        g.setColour (notes.isNoteOn (2, pitch) ? bassKey : chordKey);
        g.fillRect (area);
    }
    MidiKeyboardComponent::drawWhiteNote (pitch, g, area, false, over && ! down, lineColour, textColour);
}

void ChordKeyboard::drawBlackNote (int pitch, Graphics& g, Rectangle<float> area, bool down, bool over, Colour)
{
    g.setColour (down ? (notes.isNoteOn (2, pitch) ? bassKey.darker (0.2f) : chordKey.darker (0.2f))
                      : (over ? Colour (0xff222222) : Colours::black));
    g.fillRect (area);
}

void ResultRoll::refresh()
{
    String stamp = String (proc.progressionVersion.load());
    for (const auto* id : { "key", "scale", "style", "arp", "arpStep", "arpMirror", "velocity", "chordRange", "bassRange" })
        stamp += "/" + String (proc.getChoice (id));
    if (stamp == signature) return;
    signature = stamp;
    notes.clear();
    low = 127;
    high = 0;
    length = 0;
    MemoryOutputStream bytes;
    if (proc.writeMidiTo (bytes))
    {
        MemoryInputStream stream (bytes.getData(), bytes.getDataSize(), false);
        MidiFile midi;
        if (midi.readFrom (stream) && midi.getNumTracks() > 0)
        {
            auto* track = midi.getTrack (0);
            const double ppq = midi.getTimeFormat();
            length = track->getEndTime() / ppq;
            for (auto* evt : *track)
            {
                const auto& msg = evt->message;
                if (! msg.isNoteOn() || evt->noteOffObject == nullptr) continue;
                const int pitch = msg.getNoteNumber();
                notes.push_back ({ msg.getTimeStamp() / ppq, evt->noteOffObject->message.getTimeStamp() / ppq,
                                   pitch, msg.getVelocity(), msg.getChannel() });
                low = jmin (low, pitch);
                high = jmax (high, pitch);
            }
        }
    }
    low = jlimit (0, 125, low - 1);
    high = jlimit (low + 1, 127, high + 1);
    offset = jlimit (0.0, jmax (0.0, length - 4.0), offset);
    selection = "Scroll for later beats  -  Point to a note for velocity";
    if (getParentComponent()) getParentComponent()->repaint();
    repaint();
}

Rectangle<float> ResultRoll::boundsFor (const Note& note) const
{
    const float w = (float) (getWidth() - 38) / (float) (high - low + 1);
    const float h = (float) getHeight() / 4.0f;
    return { 38.0f + (float) (note.pitch - low) * w, (float) (note.start - offset) * h,
             jmax (1.0f, w - 1.0f), jmax (2.0f, (float) (note.end - note.start) * h - 1.0f) };
}

void ResultRoll::paint (Graphics& g)
{
    g.fillAll (control);
    g.setColour (border);
    const float w = (float) (getWidth() - 38) / (float) (high - low + 1);
    for (int p = low; p <= high; ++p)
        g.drawVerticalLine (roundToInt (38.0f + (float) (p - low) * w), 0, (float) getHeight());
    g.setFont (swellFont (12.0f));
    for (int beat = (int) std::ceil (offset); beat < offset + 4; ++beat)
    {
        const int y = roundToInt ((beat - offset) * getHeight() / 4.0);
        g.setColour (border);
        g.drawHorizontalLine (y, 0, (float) getWidth());
        g.setColour (dim);
        g.drawText (String (beat + 1), 4, y + 2, 28, 16, Justification::centredLeft);
    }
    g.setFont (swellFont (11.0f, true));
    const auto voicing = proc.getVoicing();
    for (const auto& note : notes)
    {
        auto r = boundsFor (note);
        if (r.getBottom() <= 0 || r.getY() >= (float) getHeight()) continue;
        const Colour ink = note.channel == 1 ? chordKey : bassKey;
        g.setColour (ink.withAlpha (0.35f + 0.6f * (float) note.velocity / 127.0f));
        g.fillRect (r);
        g.setColour (Colours::black);
        g.drawFittedText (ChordEngine::noteName (note.pitch, voicing.key, voicing.scale),
                          r.toNearestInt().withHeight (jmin (14, (int) r.getHeight())), Justification::centred, 1);
    }
}

void ResultRoll::mouseMove (const MouseEvent& e)
{
    for (const auto& note : notes)
        if (boundsFor (note).contains (e.position))
        {
            const auto v = proc.getVoicing();
            selection = String (note.channel == 1 ? "Chord" : "Bass") + "   "
                        + ChordEngine::noteName (note.pitch, v.key, v.scale) + String (note.pitch / 12 - 1)
                        + "   -   Velocity " + String (note.velocity);
            getParentComponent()->repaint();
            return;
        }
    mouseExit (e);
}

void ResultRoll::mouseExit (const MouseEvent&)
{
    selection = "Scroll for later beats  -  Point to a note for velocity";
    if (getParentComponent()) getParentComponent()->repaint();
}

void ResultRoll::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& wheel)
{
    offset = jlimit (0.0, jmax (0.0, length - 4.0), offset - 3.0 * wheel.deltaY);
    mouseMove (e);
    repaint();
}

//==============================================================================
SwellChordMotionEditor::SwellChordMotionEditor (SwellChordMotionProcessor& p)
    : AudioProcessorEditor (p), proc (p),
      keyboard (p.keyboardState),
      resultRoll (p), lane (p), midiButton (p)
{
    setLookAndFeel (&lnf);

    playButton.setColour (TextButton::buttonColourId, triadPad);
    playButton.setColour (TextButton::buttonOnColourId, orange);
    playButton.setColour (TextButton::textColourOnId, background);
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
    setupCaption (arpLabel, "ARP");
    setupCaption (arpStepLabel, "STEP");

    setupCaption (velocityLabel, "VEL");
    setupBar (velocitySlider, "velocity");
    velocitySlider.setTooltip ("MIDI note velocity (1-127), used for playback and drag-and-drop");
    setupBar (volSlider, "vol");
    setupBar (bpmSlider, "bpm");
    hostBpmDisplay.setFont (swellFont (15.0f, true));
    hostBpmDisplay.setColour (Label::textColourId, background);
    hostBpmDisplay.setColour (Label::backgroundColourId, orange);
    hostBpmDisplay.setJustificationType (Justification::centred);
    hostBpmDisplay.setTooltip ("Tempo synced to the DAW");
    addChildComponent (hostBpmDisplay);
    setupCombo (instrBox, Instruments::names, "instr");
    setupCombo (styleBox, ChordEngine::styleNames(), "style");
    styleBox.onChange = [this]
    {
        // Like OneMotion, a style can come with its own tempo (when the DAW does not drive it)
        const int tempo = ChordEngine::styleDef (styleBox.getSelectedItemIndex()).tempo;
        if (tempo > 0 && ! proc.hostSynced.load())
            if (auto* p = proc.apvts.getParameter ("bpm"))
                p->setValueNotifyingHost (p->convertTo0to1 ((float) tempo));
    };
    setupCombo (keyBox, ChordEngine::keyNames, "key");
    setupCombo (scaleBox, ChordEngine::scaleNames, "scale");
    setupCombo (effectBox, Instruments::effectNames, "effect");
    setupCombo (arpBox, ChordEngine::arpNames(), "arp");
    setupCombo (arpStepBox, ChordEngine::arpStepNames, "arpStep");
    arpBox.setTooltip ("Arpeggio pattern: 1 = lowest chord note, x = rest. Replaces the chord part of the style, keeps its bass");
    arpStepBox.setTooltip ("Length of each arpeggio step");

    arpMirrorButton.setClickingTogglesState (true);
    arpMirrorButton.setTooltip ("Count the notes from the top of the chord down");
    addAndMakeVisible (arpMirrorButton);
    arpMirrorAttachment = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "arpMirror", arpMirrorButton);

    rangesButton.onClick = [this] { showRanges(); };
    rangesButton.setTooltip ("Set the starting notes of the bass and chord ranges");
    for (auto* button : { &chordsTab, &resultTab, &holdButton, &rangesButton })
        addAndMakeVisible (button);
    chordsTab.onClick = [this] { showResult (false); };
    resultTab.onClick = [this] { showResult (true); };
    chordsTab.setToggleState (true, dontSendNotification);
    holdButton.setClickingTogglesState (true);
    holdButton.setTooltip ("Keep the palette chord sounding after releasing the mouse");
    holdButton.onClick = [this]
    {
        for (auto& cell : cells)
        {
            cell->release();
            cell->hold = holdButton.getToggleState();
        }
    };
    addChildComponent (resultRoll);

    swellKeyButton.onClick = [this] { useSwellKey(); };
    addAndMakeVisible (swellKeyButton);

    keyboard.setAvailableRange (24, 96);
    keyboard.setScrollButtonsVisible (false);
    keyboard.setOctaveForMiddleC (4);
    keyboard.setColour (MidiKeyboardComponent::whiteNoteColourId, Colour (0xffeeedef));
    keyboard.setColour (MidiKeyboardComponent::blackNoteColourId, background);
    keyboard.setColour (MidiKeyboardComponent::keySeparatorLineColourId, Colour (0xff8c8d94));
    keyboard.setColour (MidiKeyboardComponent::mouseOverKeyOverlayColourId, accent.withAlpha (0.3f));
    keyboard.setColour (MidiKeyboardComponent::shadowColourId, Colours::transparentBlack);
    keyboard.setColour (MidiKeyboardComponent::textLabelColourId, Colour (0xff6b6c72));
    addAndMakeVisible (keyboard);

    for (int row = 0; row < 4; ++row)
    {
        for (int degree = 0; degree < 7; ++degree)
        {
            auto cell = std::make_unique<PaletteCell> (proc, row == 1);
            cell->onPress = [this] (PaletteCell* pressedCell)
            {
                for (auto& other : cells) if (other.get() != pressedCell) other->release();
            };
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
    editButton.onClick = [this] { lane.editSelected(); };
    deleteButton.onClick = [this] { if (lane.getSelected() >= 0) proc.removeChord (lane.getSelected()); };
    clearButton.onClick = [this] { proc.clearProgression(); };

    for (auto* b : { &editButton, &shorterButton, &longerButton, &deleteButton, &clearButton })
        addAndMakeVisible (b);

    lane.onSelectionChanged = [this] { updateEditButtons(); };
    addAndMakeVisible (lane);
    addAndMakeVisible (midiButton);

    setSize (1000, 610);
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

// Sets KEY and SCALE to the last key found by Swell Key
void SwellChordMotionEditor::useSwellKey()
{
    const auto k = SwellLink::read();
    if (! k.isValid())
    {
        swellKeyButton.setButtonText ("NO KEY YET");
        swellKeyFlashUntil = Time::getMillisecondCounter() + 2500;
        return;
    }

    auto set = [this] (const String& id, int index)
    {
        if (auto* p = proc.apvts.getParameter (id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 ((float) index));
            p->endChangeGesture();
        }
    };
    set ("key", k.tonic);
    set ("scale", k.minor ? 1 : 0);   // scaleNames: 0 = Major, 1 = Minor

    swellKeyButton.setButtonText (ChordEngine::keyNames[k.tonic] + (k.minor ? " MIN" : " MAJ"));
    swellKeyFlashUntil = Time::getMillisecondCounter() + 2500;
}

void SwellChordMotionEditor::setupBar (Slider& slider, const String& paramId)
{
    slider.setSliderStyle (Slider::LinearBar);
    slider.setColour (Slider::textBoxTextColourId, text);
    slider.setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
    slider.setColour (Slider::trackColourId, accent);
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
        {
            const auto c = ChordEngine::paletteChord (key, scale, degree, row);
            cells[(size_t) (row * 7 + degree)]->setChord (c, c ? ChordEngine::chordName (*c, key, scale) : String());
        }

    paletteTitle = "Chords in " + ChordEngine::keyNames[key] + " " + ChordEngine::scaleNames[scale];
    repaint (paletteTitleArea);
    lane.refresh();
}

void SwellChordMotionEditor::showResult (bool show)
{
    resultVisible = show;
    chordsTab.setToggleState (! show, dontSendNotification);
    resultTab.setToggleState (show, dontSendNotification);
    for (auto& cell : cells) { cell->release(); cell->setVisible (! show); }
    holdButton.setVisible (! show);
    resultRoll.setVisible (show);
    if (show) resultRoll.refresh();
    repaint();
}

void SwellChordMotionEditor::showRanges()
{
    const auto v = proc.getVoicing();
    const auto& style = ChordEngine::styleDef (proc.getChoice ("style"));
    const auto arp = proc.getArpSettings();
    const auto chordPart = arp.pattern > 0 ? ChordEngine::arpPart (arp) : style.chord;

    auto choices = [&v] (const ChordEngine::Part& part, int selected, int firstId)
    {
        PopupMenu menu;
        const int base = part.octave * 12 + part.octaveOffset;
        for (int shift = -12; shift <= 12; ++shift)
        {
            const int pitch = ChordEngine::rangeStart (base, v.key, shift);
            // Several lower settings may resolve to the keyboard's bottom C.
            if (pitch == 24)
            {
                const int boundary = 24 - base - ChordEngine::keyPitch (v.key);
                const int representative = selected <= boundary ? selected : boundary;
                if (shift != representative) continue;
            }
            const auto label = MidiMessage::getMidiNoteName (pitch, true, true, 4);
            menu.addItem (firstId + shift + 12, label, true, shift == selected);
        }
        return menu;
    };
    PopupMenu ranges;
    ranges.addSubMenu ("Chord range starts at", choices (chordPart, v.chordShift, 100));
    ranges.addSubMenu ("Bass range starts at", choices (style.bass, v.bassShift, 200));
    ranges.addSeparator();
    ranges.addItem (1, "Reset to reference");
    ranges.showMenuAsync (PopupMenu::Options().withTargetComponent (&rangesButton),
                          [safe = Component::SafePointer<SwellChordMotionEditor> (this)] (int choice)
    {
        if (safe == nullptr || choice == 0) return;
        auto set = [&safe] (const char* id, int shift)
        {
            auto* param = safe->proc.apvts.getParameter (id);
            param->beginChangeGesture();
            param->setValueNotifyingHost (param->convertTo0to1 ((float) shift));
            param->endChangeGesture();
        };
        if (choice == 1)
        {
            set ("chordRange", ChordEngine::referenceRangeShift);
            set ("bassRange", ChordEngine::referenceRangeShift);
        }
        else if (choice >= 200) set ("bassRange", choice - 212);
        else set ("chordRange", choice - 112);
    });
}

void SwellChordMotionEditor::updateEditButtons()
{
    midiButton.setEnabled (! proc.getProgression().empty());
    const bool hasSelection = lane.getSelected() >= 0;
    editButton.setEnabled (hasSelection);
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

    if (resultVisible) resultRoll.refresh();
    lane.setPlaying (proc.currentChord.load());
    undoButton.setEnabled (proc.undoManager.canUndo());
    redoButton.setEnabled (proc.undoManager.canRedo());
    const bool synced = proc.hostSynced.load();
    bpmSlider.setVisible (! synced);
    hostBpmDisplay.setVisible (synced);
    bpmLabel.setText (synced ? "SYNC" : "TEMPO", dontSendNotification);
    bpmLabel.setColour (Label::textColourId, synced ? orange : dim);
    if (synced)
    {
        const double bpm = proc.currentBpm.load();
        const bool whole = std::abs (bpm - std::round (bpm)) < 0.005;
        hostBpmDisplay.setText (String (bpm, whole ? 0 : 2) + " BPM", dontSendNotification);
    }

    const auto playText = playButton.getToggleState() ? "STOP" : "PLAY";
    if (playButton.getButtonText() != playText)
        playButton.setButtonText (playText);

    if (swellKeyFlashUntil != 0 && Time::getMillisecondCounter() > swellKeyFlashUntil)
    {
        swellKeyFlashUntil = 0;
        swellKeyButton.setButtonText ("SWELL KEY");
    }
}

void SwellChordMotionEditor::paint (Graphics& g)
{
    g.fillAll (background);
    auto head = headerArea.toFloat().expanded (22.0f, 13.0f);
    g.setColour (panel);
    g.fillRect (head);
    g.setColour (border.withAlpha (0.8f));
    g.drawHorizontalLine (headerArea.getBottom() + 6, 0, (float) getWidth());

    drawSwellLogo (g, headerArea.toFloat().withWidth (40.0f).withSizeKeepingCentre (40.0f, 40.0f));

    g.setColour (text);
    g.setFont (swellFont (26.0f, true));
    g.drawText ("SWELL", headerArea.getX() + 50, headerArea.getY() + 2, 170, 26, Justification::centredLeft);

    auto subtitle = swellFont (11.0f, true);
    subtitle.setExtraKerningFactor (0.22f);
    g.setFont (subtitle);
    g.setColour (orange);
    g.drawText ("CHORD MOTION", headerArea.getX() + 51, headerArea.getY() + 28, 170, 14, Justification::centredLeft);

    g.setColour (panel);
    g.fillRoundedRectangle (paletteArea.expanded (5).toFloat(), 8.0f);

    g.setFont (swellFont (15.0f, true));
    g.setColour (dim);
    g.drawText (resultVisible ? "Result" : "Chord palette", paletteTitleArea, Justification::centredLeft);
    g.drawText (resultVisible ? resultRoll.selection : paletteTitle,
                paletteTitleArea.withTrimmedRight (resultVisible ? 0 : 68), Justification::centredRight);
    g.drawText ("Chord progression", progressionTitleArea, Justification::centredLeft);

    auto footer = getLocalBounds().removeFromBottom (24).reduced (22, 0);
    g.setFont (swellFont (11.5f));
    g.drawText ("v" + String (JucePlugin_VersionString) + "   |   by @sevyondatracks", footer, Justification::centredRight);
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
        hostBpmDisplay.setBounds (bpmSlider.getBounds());
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
        place (instrLabel, 52, instrBox, 120);
        place (styleLabel, 52, styleBox, 120);
        scaleLabel.setBounds (row.removeFromLeft (54).withTrimmedRight (8));
        keyBox.setBounds (row.removeFromLeft (64));
        row.removeFromLeft (6);
        scaleBox.setBounds (row.removeFromLeft (170));
        row.removeFromLeft (8);
        swellKeyButton.setBounds (row);
    }

    area.removeFromTop (10);
    {
        auto row = area.removeFromTop (30);
        arpLabel.setBounds (row.removeFromLeft (36).withTrimmedRight (8));
        arpBox.setBounds (row.removeFromLeft (220));
        row.removeFromLeft (14);
        arpStepLabel.setBounds (row.removeFromLeft (52).withTrimmedRight (8));
        arpStepBox.setBounds (row.removeFromLeft (80));
        row.removeFromLeft (14);
        arpMirrorButton.setBounds (row.removeFromLeft (90));
        velocitySlider.setBounds (row.removeFromRight (96));
        velocityLabel.setBounds (row.removeFromRight (44).withTrimmedRight (8));
    }

    area.removeFromTop (10);
    auto tabs = area.removeFromTop (28);
    chordsTab.setBounds (tabs.removeFromLeft (94));
    tabs.removeFromLeft (4);
    resultTab.setBounds (tabs.removeFromLeft (94));
    rangesButton.setBounds (tabs.removeFromRight (78));
    area.removeFromTop (8);
    {
        auto keys = area.removeFromTop (40);
        keyboard.setBounds (keys);
        keyboard.setKeyWidth ((float) keys.getWidth() / 43.0f);   // C1..C7 = 43 white keys
    }

    area.removeFromTop (12);
    paletteTitleArea = area.removeFromTop (24);
    holdButton.setBounds (paletteTitleArea.withWidth (60).withX (paletteTitleArea.getRight() - 60));
    area.removeFromTop (8);
    constexpr int rowHeight = 44, gap = 4;
    paletteArea = area.removeFromTop (4 * rowHeight + 3 * gap);
    resultRoll.setBounds (paletteArea);
    const int width = paletteArea.getWidth() - 6 * gap;
    for (int row = 0; row < 4; ++row)
        for (int degree = 0; degree < 7; ++degree)
        {
            int x = degree * width / 7;
            int next = (degree + 1) * width / 7;
            cells[(size_t) (row * 7 + degree)]->setBounds (paletteArea.getX() + x + degree * gap,
                              paletteArea.getY() + row * (rowHeight + gap), next - x, rowHeight);
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
        row.removeFromRight (6);
        editButton.setBounds (row.removeFromRight (64));
    }

    area.removeFromTop (6);
    lane.setBounds (area.removeFromTop (64));
}
