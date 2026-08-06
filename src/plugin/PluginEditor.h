#pragma once
// SappKeys editor — "ivory & ebony".
// Warm ivory panels, ebony display wells, felt-red and brass accents.

#include <juce_audio_utils/juce_audio_utils.h>

#include "PluginProcessor.h"

namespace sappkeys {

// ------------------------------------------------------------------ palette --
namespace palette {
const juce::Colour ivory{0xfff3ecdd};        // warm ivory background
const juce::Colour cream{0xffe9e0cc};        // panel fill
const juce::Colour parchment{0xffd9cdb2};    // panel edge
const juce::Colour ebony{0xff231d17};        // text / display wells
const juce::Colour ebonySoft{0xff3d342a};
const juce::Colour felt{0xff8e3b3f};         // hammer-felt red
const juce::Colour brass{0xffa8823f};        // hinge brass
const juce::Colour brassBright{0xffcaa55b};
const juce::Colour dim{0xff8a7e69};          // secondary text
} // namespace palette

// ------------------------------------------------------------ look and feel --
class KeysLookAndFeel : public juce::LookAndFeel_V4
{
public:
    KeysLookAndFeel();
    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h,
                          float sliderPos, float rotaryStartAngle,
                          float rotaryEndAngle, juce::Slider&) override;
    void drawComboBox(juce::Graphics&, int width, int height, bool isButtonDown,
                      int buttonX, int buttonY, int buttonW, int buttonH,
                      juce::ComboBox&) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&,
                              const juce::Colour& backgroundColour,
                              bool highlighted, bool down) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
};

// ------------------------------------------------------------- labeled knob --
class Knob : public juce::Component
{
public:
    Knob(juce::AudioProcessorValueTreeState& state, const juce::String& paramId,
         const juce::String& title, bool big = false);
    void resized() override;
    juce::Slider slider;

private:
    juce::Label label_;
    bool big_;
};

// ------------------------------------------------------ velocity curve pad ---
// The signature control: draws the touch/una-corda velocity mapping in an
// ebony display well, with recently played notes as fading felt-red dots.
// Vertical drag bends the curve (the `touch` parameter).
class VelocityCurvePad : public juce::Component, private juce::Timer
{
public:
    explicit VelocityCurvePad(SappKeysProcessor& processor);
    ~VelocityCurvePad() override;
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    SappKeysProcessor& processor_;
    juce::RangedAudioParameter* touchParam_;
    juce::RangedAudioParameter* unaCordaParam_;
    float dragStartValue_ = 0.0f;
    juce::Point<float> dragStart_;
    struct Dot {
        float in = 0, out = 0, age = 1.0f;
    };
    std::vector<Dot> dots_;
    uint32_t lastVelWrite_ = 0;
    sapp::keys::KeysEngine::VelSample history_[sapp::keys::KeysEngine::kVelHistory];
    int historyCount_ = 0;
};

// ------------------------------------------------------------- pedal lamps ---
// Sustain follows the live CC64 state; una corda follows the parameter.
class PedalLamps : public juce::Component, private juce::Timer
{
public:
    explicit PedalLamps(SappKeysProcessor& processor);
    ~PedalLamps() override;
    void paint(juce::Graphics&) override;

private:
    void timerCallback() override;
    SappKeysProcessor& processor_;
    float sustainGlow_ = 0.0f, unaGlow_ = 0.0f;
};

// ---------------------------------------------------------------- keyboard ---
class KeysKeyboard : public juce::MidiKeyboardComponent
{
public:
    explicit KeysKeyboard(juce::MidiKeyboardState& state);
};

// ------------------------------------------------------------------- editor --
class SappKeysEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SappKeysEditor(SappKeysProcessor&);
    ~SappKeysEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void chooseSfz();

    SappKeysProcessor& processor_;
    KeysLookAndFeel lookAndFeel_;

    juce::Label title_, subtitle_, instrumentName_, status_;
    juce::TextButton loadButton_{"LOAD SFZ"};
    juce::TextButton diagButton_{"BUILT-IN"};

    juce::Label touchHeader_, bodyHeader_, characterHeader_, roomHeader_;

    std::unique_ptr<VelocityCurvePad> velocityPad_;
    std::unique_ptr<PedalLamps> pedalLamps_;
    std::unique_ptr<Knob> touch_, dynamics_, expression_;
    std::unique_ptr<Knob> unaCorda_, lid_, resonance_, mechNoise_, width_;
    std::unique_ptr<Knob> vintage_, drive_;
    std::unique_ptr<Knob> roomLevel_, roomSize_, roomDecay_, master_;
    juce::ComboBox quality_;
    juce::ToggleButton limiter_{"limiter"};
    std::unique_ptr<KeysKeyboard> keyboard_;

    juce::Label voicesLabel_;
    float meterL_ = 0.0f, meterR_ = 0.0f;
    juce::Rectangle<int> meterArea_;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::vector<std::unique_ptr<SliderAttachment>> sliderAttachments_;
    std::unique_ptr<ComboAttachment> qualityAttachment_;
    std::unique_ptr<ButtonAttachment> limiterAttachment_;

    std::unique_ptr<juce::FileChooser> fileChooser_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SappKeysEditor)
};

} // namespace sappkeys
