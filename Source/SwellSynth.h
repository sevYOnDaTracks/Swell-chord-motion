#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <cmath>

// Built-in instruments so the progression can be heard without another plugin
namespace Instruments
{
enum Type { piano, ePiano, organ, pad, pluck, strings, midiOut };
inline const juce::StringArray names { "Piano", "E-Piano", "Organ", "Pad", "Pluck", "Strings", "MIDI Out" };
inline const juce::StringArray effectNames { "None", "Room", "Hall", "Church" };
} // namespace Instruments

class SwellSound : public juce::SynthesiserSound
{
public:
    bool appliesToNote (int) override { return true; }
    bool appliesToChannel (int) override { return true; }
};

// Channel state shared by every voice: expression (CC11) and pitch bend, moved by the Mod styles
struct SwellChannelState
{
    float expression = 1.0f;   // 0..1
    float bend = 1.0f;         // frequency ratio
};

class SwellVoice : public juce::SynthesiserVoice
{
public:
    SwellVoice (std::atomic<float>* instrumentParam, const SwellChannelState& channelState)
        : instrument (instrumentParam), channel (channelState) {}

    bool canPlaySound (juce::SynthesiserSound* sound) override { return dynamic_cast<SwellSound*> (sound) != nullptr; }

    void startNote (int midiNote, float velocity, juce::SynthesiserSound*, int) override
    {
        using namespace Instruments;
        type = juce::roundToInt (instrument->load());
        sr = getSampleRate();
        inc = juce::MidiMessage::getMidiNoteInHertz (midiNote) / sr;
        phase = 0.0;
        phase2 = 0.37;
        phase3 = 0.71;
        lowpass = 0.0f;
        level = velocity * 0.14f;
        expression = channel.expression;
        bend = channel.bend;
        mod = 1.0f;
        tine = 1.0f;
        modDecay = (float) std::exp (-3.0 / sr);
        tineDecay = (float) std::exp (-25.0 / sr);

        for (size_t k = 0; k < harmonics.size(); ++k)
        {
            const double h = (double) k + 1.0;
            harmonics[k] = (float) (1.0 / std::pow (h, 1.3));
            harmonicDecay[k] = (float) std::exp (-(0.6 + 0.9 * h) / sr);
        }

        juce::ADSR::Parameters env;
        double cutoff = 20000.0;
        switch (type)
        {
            case piano:  env = { 0.002f, 2.5f, 0.0f, 0.35f }; break;
            case ePiano: env = { 0.002f, 3.5f, 0.25f, 0.4f }; break;
            case organ:  env = { 0.005f, 0.1f, 1.0f, 0.08f }; break;
            case pad:    env = { 0.45f, 0.5f, 0.85f, 1.2f }; cutoff = 1800.0; break;
            case pluck:  env = { 0.001f, 0.6f, 0.0f, 0.25f }; break;
            default:     env = { 0.25f, 0.4f, 0.85f, 0.6f }; cutoff = 3500.0; break;
        }
        lpCoef = (float) (1.0 - std::exp (-juce::MathConstants<double>::twoPi * std::min (cutoff, sr * 0.45) / sr));

        adsr.setSampleRate (sr);
        adsr.setParameters (env);
        adsr.noteOn();
    }

    void stopNote (float, bool allowTailOff) override
    {
        if (allowTailOff)
            adsr.noteOff();
        else
        {
            adsr.reset();
            clearCurrentNote();
        }
    }

    void pitchWheelMoved (int) override {}
    void controllerMoved (int, int) override {}

    void renderNextBlock (juce::AudioBuffer<float>& out, int start, int num) override
    {
        if (! isVoiceActive())
            return;

        const float expressionCoef = (float) (1.0 - std::exp (-1.0 / (0.004 * sr)));   // ~4 ms glide, no clicks
        for (int i = 0; i < num; ++i)
        {
            expression += expressionCoef * (channel.expression - expression);
            bend += expressionCoef * (channel.bend - bend);
            const float v = nextSample() * adsr.getNextSample() * level * expression;

            for (int ch = 0; ch < out.getNumChannels(); ++ch)
                out.addSample (ch, start + i, v);

            if (! adsr.isActive())
            {
                clearCurrentNote();
                break;
            }
        }
    }

private:
    static float sine (double ph) { return (float) std::sin (juce::MathConstants<double>::twoPi * ph); }
    static float saw (double ph) { return (float) (2.0 * ph - 1.0); }

    float nextSample()
    {
        using namespace Instruments;
        float x = 0.0f;

        switch (type)
        {
            case piano:
                for (size_t k = 0; k < harmonics.size(); ++k)
                {
                    x += sine (std::fmod (phase * (double) (k + 1), 1.0)) * harmonics[k];
                    harmonics[k] *= harmonicDecay[k];
                }
                break;

            case ePiano:   // two-operator FM with a short metallic "tine" on the attack
                x = (float) std::sin (juce::MathConstants<double>::twoPi * phase + 2.2 * mod * std::sin (juce::MathConstants<double>::twoPi * phase))
                  + 0.08f * tine * sine (std::fmod (phase * 14.0, 1.0));
                mod *= modDecay;
                tine *= tineDecay;
                break;

            case organ:
                x = 0.5f * sine (phase) + 0.35f * sine (std::fmod (phase * 2.0, 1.0))
                  + 0.2f * sine (std::fmod (phase * 3.0, 1.0)) + 0.12f * sine (std::fmod (phase * 4.0, 1.0));
                break;

            case pad:
                x = lowpassed (0.5f * (saw (phase) + saw (phase2)));
                break;

            case pluck:   // saw whose brightness fades with the note
                lpCoef = 0.02f + 0.5f * mod;
                mod *= modDecay;
                x = lowpassed (saw (phase));
                break;

            default:
                x = lowpassed ((saw (phase) + saw (phase2) + saw (phase3)) / 3.0f);
                break;
        }

        auto advance = [] (double& p, double step) { p += step; if (p >= 1.0) p -= 1.0; };
        const double b = (double) bend;
        advance (phase, inc * b);
        advance (phase2, inc * b * 1.006);
        advance (phase3, inc * b * 0.995);
        return x;
    }

    float lowpassed (float x)
    {
        lowpass += lpCoef * (x - lowpass);
        return lowpass;
    }

    std::atomic<float>* instrument;
    const SwellChannelState& channel;
    float expression = 1.0f, bend = 1.0f;
    juce::ADSR adsr;
    std::array<float, 6> harmonics {}, harmonicDecay {};
    double sr = 44100.0, inc = 0.0, phase = 0.0, phase2 = 0.0, phase3 = 0.0;
    float level = 0.0f, lowpass = 0.0f, lpCoef = 1.0f, mod = 1.0f, modDecay = 1.0f, tine = 1.0f, tineDecay = 1.0f;
    int type = 0;
};

// Synthesiser that keeps the channel expression and pitch bend for its voices
class SwellSynth : public juce::Synthesiser
{
public:
    SwellChannelState channelState;

    void handleController (int midiChannel, int controllerNumber, int controllerValue) override
    {
        if (controllerNumber == 11)
            channelState.expression = (float) controllerValue / 127.0f;
        juce::Synthesiser::handleController (midiChannel, controllerNumber, controllerValue);
    }

    void handlePitchWheel (int midiChannel, int wheelValue) override
    {
        channelState.bend = (float) std::pow (2.0, ((wheelValue - 8192) / 8192.0) * 2.0 / 12.0);   // +/- 2 semitones
        juce::Synthesiser::handlePitchWheel (midiChannel, wheelValue);
    }
};
