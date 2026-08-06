#include "PluginEditor.h"

namespace sappkeys {

namespace {

juce::Font titleFont(float height)
{
    return juce::Font(juce::FontOptions{"Georgia", height, juce::Font::plain});
}
juce::Font uiFont(float height, bool bold = false)
{
    return juce::Font(juce::FontOptions{height, bold ? juce::Font::bold : juce::Font::plain});
}

} // namespace

// ------------------------------------------------------------ look and feel --

KeysLookAndFeel::KeysLookAndFeel()
{
    setColour(juce::Slider::textBoxTextColourId, palette::dim);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Label::textColourId, palette::ebony);
    setColour(juce::ComboBox::backgroundColourId, palette::cream);
    setColour(juce::ComboBox::textColourId, palette::ebony);
    setColour(juce::ComboBox::outlineColourId, palette::parchment);
    setColour(juce::ComboBox::arrowColourId, palette::brass);
    setColour(juce::PopupMenu::backgroundColourId, palette::cream);
    setColour(juce::PopupMenu::textColourId, palette::ebony);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, palette::felt);
    setColour(juce::PopupMenu::highlightedTextColourId, palette::ivory);
    setColour(juce::TextButton::buttonColourId, palette::cream);
    setColour(juce::TextButton::textColourOffId, palette::ebony);
    setColour(juce::TextButton::textColourOnId, palette::ivory);
    setColour(juce::ToggleButton::textColourId, palette::dim);
    setColour(juce::ToggleButton::tickColourId, palette::felt);
    setColour(juce::ToggleButton::tickDisabledColourId, palette::parchment);
    setColour(juce::BubbleComponent::backgroundColourId, palette::ebony);
    setColour(juce::BubbleComponent::outlineColourId, palette::brass);
}

void KeysLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h,
                                       float sliderPos, float startAngle,
                                       float endAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<float>(float(x), float(y), float(w), float(h)).reduced(4.0f);
    const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float angle = startAngle + sliderPos * (endAngle - startAngle);
    const float arcThickness = juce::jmax(2.4f, radius * 0.085f);
    const float arcRadius = radius - arcThickness * 0.5f;

    // Body: ivory key-top cap with a soft shadow.
    const float capRadius = arcRadius - arcThickness * 1.7f;
    g.setColour(palette::parchment.darker(0.15f).withAlpha(0.5f));
    g.fillEllipse(centre.x - capRadius, centre.y - capRadius + 1.5f, capRadius * 2, capRadius * 2);
    juce::ColourGradient bodyGrad(juce::Colour(0xfffdf8ec),
                                  centre.x - capRadius * 0.4f, centre.y - capRadius * 0.5f,
                                  palette::cream.darker(0.08f), centre.x, centre.y + capRadius, true);
    g.setGradientFill(bodyGrad);
    g.fillEllipse(centre.x - capRadius, centre.y - capRadius, capRadius * 2, capRadius * 2);
    g.setColour(palette::parchment.darker(0.2f));
    g.drawEllipse(centre.x - capRadius, centre.y - capRadius, capRadius * 2, capRadius * 2, 1.0f);

    // Track arc.
    juce::Path track;
    track.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                        startAngle, endAngle, true);
    g.setColour(palette::parchment);
    g.strokePath(track, juce::PathStrokeType(arcThickness, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));

    // Value arc in brass with a warm glow.
    juce::Path value;
    value.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                        startAngle, angle, true);
    g.setColour(palette::brass.withAlpha(0.28f));
    g.strokePath(value, juce::PathStrokeType(arcThickness * 2.2f, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));
    g.setColour(slider.isEnabled() ? palette::brass : palette::dim);
    g.strokePath(value, juce::PathStrokeType(arcThickness, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));

    // Ebony pointer, like a sharp key.
    juce::Path pointer;
    pointer.addRoundedRectangle(-arcThickness * 0.55f, -capRadius + arcThickness,
                                arcThickness * 1.1f, capRadius * 0.45f, arcThickness * 0.4f);
    g.setColour(palette::ebony);
    g.fillPath(pointer, juce::AffineTransform::rotation(angle).translated(centre.x, centre.y));
}

void KeysLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool,
                                   int, int, int, int, juce::ComboBox& box)
{
    const auto bounds = juce::Rectangle<float>(0, 0, float(width), float(height)).reduced(0.5f);
    g.setColour(palette::cream);
    g.fillRoundedRectangle(bounds, 4.0f);
    g.setColour(box.hasKeyboardFocus(true) ? palette::brass : palette::parchment);
    g.drawRoundedRectangle(bounds, 4.0f, 1.0f);
    juce::Path arrow;
    const float ax = float(width) - 14.0f, ay = float(height) * 0.5f;
    arrow.addTriangle(ax - 4, ay - 2.5f, ax + 4, ay - 2.5f, ax, ay + 3.5f);
    g.setColour(palette::brass);
    g.fillPath(arrow);
}

void KeysLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button,
                                           const juce::Colour&, bool highlighted,
                                           bool down)
{
    auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
    const bool on = button.getToggleState();
    juce::Colour fill = on ? palette::felt : palette::cream;
    if (down) fill = fill.darker(0.15f);
    else if (highlighted) fill = fill.brighter(0.05f);
    g.setColour(fill);
    g.fillRoundedRectangle(bounds, 5.0f);
    g.setColour(on ? palette::felt.darker(0.2f) : palette::parchment.darker(0.1f));
    g.drawRoundedRectangle(bounds, 5.0f, 1.0f);
}

juce::Font KeysLookAndFeel::getComboBoxFont(juce::ComboBox&) { return uiFont(13.0f); }
juce::Font KeysLookAndFeel::getPopupMenuFont() { return uiFont(13.5f); }

// -------------------------------------------------------------------- knob ---

Knob::Knob(juce::AudioProcessorValueTreeState&, const juce::String&,
           const juce::String& title, bool big)
    : big_(big)
{
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    slider.setPopupDisplayEnabled(true, true, nullptr);
    addAndMakeVisible(slider);

    label_.setText(title, juce::dontSendNotification);
    label_.setJustificationType(juce::Justification::centred);
    label_.setFont(uiFont(big ? 12.5f : 10.5f, big));
    label_.setColour(juce::Label::textColourId, big ? palette::ebony : palette::dim);
    label_.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(label_);
}

void Knob::resized()
{
    auto bounds = getLocalBounds();
    label_.setBounds(bounds.removeFromBottom(big_ ? 17 : 13));
    slider.setBounds(bounds);
}

// ------------------------------------------------------ velocity curve pad ---

VelocityCurvePad::VelocityCurvePad(SappKeysProcessor& processor)
    : processor_(processor),
      touchParam_(processor.valueTree().getParameter("touch")),
      unaCordaParam_(processor.valueTree().getParameter("unaCorda"))
{
    startTimerHz(30);
}
VelocityCurvePad::~VelocityCurvePad() { stopTimer(); }

void VelocityCurvePad::timerCallback()
{
    // Pull fresh note-on velocity pairs from the engine feed.
    historyCount_ = processor_.engine().velocityHistory(history_);
    for (auto& d : dots_) d.age *= 0.955f;
    dots_.erase(std::remove_if(dots_.begin(), dots_.end(),
                               [](const Dot& d) { return d.age < 0.05f; }),
                dots_.end());
    static_assert(sapp::keys::KeysEngine::kVelHistory <= 16, "history bound");
    // Add dots for pairs we haven't shown yet (approximate: newest only).
    if (historyCount_ > 0) {
        const auto& latest = history_[historyCount_ - 1];
        if (dots_.empty() || dots_.back().in != float(latest.in) ||
            dots_.back().out != float(latest.out) || dots_.back().age < 0.5f) {
            if (dots_.empty() || dots_.back().age < 0.98f)
                dots_.push_back({float(latest.in), float(latest.out), 1.0f});
        }
    }
    repaint();
}

void VelocityCurvePad::mouseDown(const juce::MouseEvent& e)
{
    touchParam_->beginChangeGesture();
    dragStart_ = e.position;
    dragStartValue_ = touchParam_->getValue();
}

void VelocityCurvePad::mouseDrag(const juce::MouseEvent& e)
{
    const float dy = dragStart_.y - e.position.y;  // up = lighter
    touchParam_->setValueNotifyingHost(
        juce::jlimit(0.0f, 1.0f, dragStartValue_ + dy / 220.0f));
}

void VelocityCurvePad::mouseUp(const juce::MouseEvent&) { touchParam_->endChangeGesture(); }

void VelocityCurvePad::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    // Ebony display well.
    g.setColour(palette::ebony);
    g.fillRoundedRectangle(bounds, 8.0f);
    juce::ColourGradient sheen(juce::Colour(0x14ffffff), bounds.getX(), bounds.getY(),
                               juce::Colour(0x00000000), bounds.getX(), bounds.getBottom(),
                               false);
    g.setGradientFill(sheen);
    g.fillRoundedRectangle(bounds, 8.0f);
    g.setColour(palette::brass.withAlpha(0.5f));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 8.0f, 1.0f);

    const auto area = bounds.reduced(16.0f, 14.0f);

    // Grid.
    g.setColour(palette::ebonySoft.withAlpha(0.9f));
    for (int i = 1; i < 4; ++i) {
        const float fx = area.getX() + area.getWidth() * float(i) / 4.0f;
        const float fy = area.getY() + area.getHeight() * float(i) / 4.0f;
        g.drawVerticalLine(int(fx), area.getY(), area.getBottom());
        g.drawHorizontalLine(int(fy), area.getX(), area.getRight());
    }
    g.drawRect(area, 1.0f);

    // Reference diagonal (as-played).
    g.setColour(palette::ebonySoft.brighter(0.25f));
    const float dash[2] = {3.0f, 4.0f};
    g.drawDashedLine(juce::Line<float>(area.getX(), area.getBottom(),
                                       area.getRight(), area.getY()), dash, 2, 1.0f);

    const float touch = touchParam_->convertFrom0to1(touchParam_->getValue());
    const float unaCorda = unaCordaParam_->convertFrom0to1(unaCordaParam_->getValue());

    // The actual curve the engine applies.
    juce::Path curve;
    for (int i = 0; i <= 64; ++i) {
        const float vin = 1.0f + 126.0f * float(i) / 64.0f;
        const float vout = sapp::keys::shapeVelocity(vin, touch, unaCorda);
        const float px = area.getX() + area.getWidth() * (vin - 1.0f) / 126.0f;
        const float py = area.getBottom() - area.getHeight() * (vout - 1.0f) / 126.0f;
        if (i == 0) curve.startNewSubPath(px, py);
        else curve.lineTo(px, py);
    }
    g.setColour(palette::brassBright.withAlpha(0.35f));
    g.strokePath(curve, juce::PathStrokeType(5.0f, juce::PathStrokeType::curved));
    g.setColour(palette::brassBright);
    g.strokePath(curve, juce::PathStrokeType(2.2f, juce::PathStrokeType::curved));

    // Recently played notes: felt-red dots fading out.
    for (const auto& d : dots_) {
        const float px = area.getX() + area.getWidth() * (d.in - 1.0f) / 126.0f;
        const float py = area.getBottom() - area.getHeight() * (d.out - 1.0f) / 126.0f;
        g.setColour(palette::felt.brighter(0.35f).withAlpha(0.85f * d.age));
        const float r = 3.0f + 3.0f * d.age;
        g.fillEllipse(px - r, py - r, r * 2, r * 2);
    }

    // Labels.
    g.setColour(palette::dim);
    g.setFont(uiFont(9.5f));
    g.drawText("PLAYED", area.toNearestInt().removeFromBottom(12),
               juce::Justification::centredBottom);
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText(uiFont(9.5f), "SOUNDED", 0.0f, 0.0f);
    juce::Path vertical;
    glyphs.createPath(vertical);
    vertical.applyTransform(juce::AffineTransform::rotation(-juce::MathConstants<float>::halfPi)
                                .translated(bounds.getX() + 11.0f, area.getCentreY() + 24.0f));
    g.fillPath(vertical);

    g.setColour(palette::ivory.withAlpha(0.9f));
    g.setFont(uiFont(10.5f, true));
    const juce::String touchText =
        touch < 0.35f ? "HEAVY" : touch > 0.65f ? "LIGHT" : "NATURAL";
    g.drawText("TOUCH: " + touchText,
               bounds.toNearestInt().reduced(14, 8), juce::Justification::topRight);
}

// ------------------------------------------------------------- pedal lamps ---

PedalLamps::PedalLamps(SappKeysProcessor& processor) : processor_(processor)
{
    startTimerHz(30);
}
PedalLamps::~PedalLamps() { stopTimer(); }

void PedalLamps::timerCallback()
{
    const float sustainTarget = processor_.engine().sustainPedalDown() ? 1.0f : 0.0f;
    const float unaTarget =
        processor_.valueTree().getRawParameterValue("unaCorda")->load() > 0.2f ? 1.0f : 0.0f;
    sustainGlow_ += 0.4f * (sustainTarget - sustainGlow_);
    unaGlow_ += 0.4f * (unaTarget - unaGlow_);
    repaint();
}

void PedalLamps::paint(juce::Graphics& g)
{
    auto lamp = [&](juce::Rectangle<float> r, const juce::String& text, float glow) {
        const float d = juce::jmin(r.getHeight() - 16.0f, 16.0f);
        const juce::Point<float> c(r.getCentreX(), r.getY() + d * 0.5f + 2.0f);
        // Brass ring.
        g.setColour(palette::brass);
        g.drawEllipse(c.x - d * 0.5f, c.y - d * 0.5f, d, d, 1.4f);
        // Felt-red glow when engaged.
        if (glow > 0.02f) {
            g.setColour(palette::felt.withAlpha(0.25f * glow));
            g.fillEllipse(c.x - d * 0.95f, c.y - d * 0.95f, d * 1.9f, d * 1.9f);
        }
        g.setColour(palette::felt.brighter(0.2f).withAlpha(0.15f + 0.85f * glow));
        g.fillEllipse(c.x - d * 0.34f, c.y - d * 0.34f, d * 0.68f, d * 0.68f);
        g.setColour(palette::dim);
        g.setFont(uiFont(9.0f, true));
        g.drawText(text, r.toNearestInt().removeFromBottom(13), juce::Justification::centred);
    };
    auto bounds = getLocalBounds().toFloat();
    // Stack vertically: sustain above, una corda below.
    lamp(bounds.removeFromTop(bounds.getHeight() * 0.5f), "SUSTAIN", sustainGlow_);
    lamp(bounds, "UNA CORDA", unaGlow_);
}

// ---------------------------------------------------------------- keyboard ---

KeysKeyboard::KeysKeyboard(juce::MidiKeyboardState& state)
    : juce::MidiKeyboardComponent(state, juce::MidiKeyboardComponent::horizontalKeyboard)
{
    setColour(juce::MidiKeyboardComponent::whiteNoteColourId, juce::Colour(0xfffbf6ea));
    setColour(juce::MidiKeyboardComponent::blackNoteColourId, palette::ebony);
    setColour(juce::MidiKeyboardComponent::keySeparatorLineColourId, juce::Colour(0xffb9ac92));
    setColour(juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId,
              palette::brass.withAlpha(0.35f));
    setColour(juce::MidiKeyboardComponent::keyDownOverlayColourId,
              palette::felt.withAlpha(0.55f));
    setColour(juce::MidiKeyboardComponent::shadowColourId, palette::ebony.withAlpha(0.4f));
    setColour(juce::MidiKeyboardComponent::upDownButtonBackgroundColourId, palette::cream);
    setColour(juce::MidiKeyboardComponent::upDownButtonArrowColourId, palette::brass);
    setAvailableRange(21, 108);   // the 88
    setKeyWidth(12.0f);
    setScrollButtonsVisible(false);
}

// ------------------------------------------------------------------- editor --

SappKeysEditor::SappKeysEditor(SappKeysProcessor& processor)
    : juce::AudioProcessorEditor(&processor), processor_(processor)
{
    setLookAndFeel(&lookAndFeel_);

    auto& state = processor_.valueTree();

    title_.setText("SappKeys", juce::dontSendNotification);
    title_.setFont(titleFont(30.0f));
    title_.setColour(juce::Label::textColourId, palette::ebony);
    addAndMakeVisible(title_);

    subtitle_.setText("PIANO & ELECTRIC KEYS", juce::dontSendNotification);
    subtitle_.setFont(uiFont(10.0f));
    subtitle_.setColour(juce::Label::textColourId, palette::brass);
    addAndMakeVisible(subtitle_);

    instrumentName_.setFont(uiFont(15.0f, true));
    instrumentName_.setColour(juce::Label::textColourId, palette::ebony);
    instrumentName_.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(instrumentName_);

    status_.setFont(uiFont(11.0f));
    status_.setColour(juce::Label::textColourId, palette::dim);
    status_.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(status_);

    loadButton_.onClick = [this] { chooseSfz(); };
    addAndMakeVisible(loadButton_);
    diagButton_.onClick = [this] { processor_.loadDiagnosticInstrument(); };
    addAndMakeVisible(diagButton_);

    auto header = [&](juce::Label& label, const juce::String& text) {
        label.setText(text, juce::dontSendNotification);
        label.setFont(uiFont(10.5f, true));
        label.setColour(juce::Label::textColourId, palette::dim);
        addAndMakeVisible(label);
    };
    header(touchHeader_, "TOUCH & PEDALS");
    header(bodyHeader_, "INSTRUMENT");
    header(characterHeader_, "CHARACTER");
    header(roomHeader_, "ROOM & OUTPUT");

    auto knob = [&](const juce::String& id, const juce::String& text, bool big = false) {
        auto k = std::make_unique<Knob>(state, id, text, big);
        sliderAttachments_.push_back(
            std::make_unique<SliderAttachment>(state, id, k->slider));
        addAndMakeVisible(*k);
        return k;
    };
    touch_ = knob("touch", "TOUCH");
    dynamics_ = knob("dynamics", "DYNAMICS");
    expression_ = knob("expression", "EXPRESS");
    unaCorda_ = knob("unaCorda", "UNA CORDA", true);
    lid_ = knob("lid", "LID", true);
    resonance_ = knob("resonance", "RESONANCE", true);
    mechNoise_ = knob("mechNoise", "MECHANICS");
    width_ = knob("width", "WIDTH");
    vintage_ = knob("vintage", "VINTAGE", true);
    drive_ = knob("drive", "DRIVE", true);
    roomLevel_ = knob("roomLevel", "ROOM");
    roomSize_ = knob("roomSize", "SIZE");
    roomDecay_ = knob("roomDecay", "DECAY");
    master_ = knob("masterGain", "MASTER");

    quality_.addItemList({"Draft", "Normal"}, 1);
    qualityAttachment_ = std::make_unique<ComboAttachment>(state, "quality", quality_);
    addAndMakeVisible(quality_);

    limiterAttachment_ = std::make_unique<ButtonAttachment>(state, "limiter", limiter_);
    addAndMakeVisible(limiter_);

    velocityPad_ = std::make_unique<VelocityCurvePad>(processor_);
    addAndMakeVisible(*velocityPad_);

    pedalLamps_ = std::make_unique<PedalLamps>(processor_);
    addAndMakeVisible(*pedalLamps_);

    keyboard_ = std::make_unique<KeysKeyboard>(processor_.keyboardState);
    addAndMakeVisible(*keyboard_);

    voicesLabel_.setFont(uiFont(11.0f));
    voicesLabel_.setColour(juce::Label::textColourId, palette::dim);
    addAndMakeVisible(voicesLabel_);

    processor_.onInstrumentChanged = [this] {
        instrumentName_.setText(processor_.currentInstrumentName(),
                                juce::dontSendNotification);
    };

    startTimerHz(24);
    setResizable(true, true);
    setResizeLimits(820, 540, 1640, 1080);
    getConstrainer()->setFixedAspectRatio(960.0 / 630.0);
    setSize(960, 630);
}

SappKeysEditor::~SappKeysEditor()
{
    processor_.onInstrumentChanged = nullptr;
    setLookAndFeel(nullptr);
}

void SappKeysEditor::chooseSfz()
{
    fileChooser_ = std::make_unique<juce::FileChooser>(
        "Load SFZ instrument", juce::File(), "*.sfz");
    fileChooser_->launchAsync(juce::FileBrowserComponent::openMode |
                                  juce::FileBrowserComponent::canSelectFiles,
                              [this](const juce::FileChooser& chooser) {
                                  const auto file = chooser.getResult();
                                  if (file.existsAsFile())
                                      processor_.loadSfzInstrument(file);
                              });
}

void SappKeysEditor::timerCallback()
{
    status_.setText(processor_.loadStatus(), juce::dontSendNotification);
    instrumentName_.setText(processor_.currentInstrumentName(), juce::dontSendNotification);

    sapp::sounds::DiagnosticSnapshot snap;
    if (processor_.engine().sampler().diagnostics().read(snap)) {
        voicesLabel_.setText(juce::String(snap.activeVoices) + " voices",
                             juce::dontSendNotification);
        meterL_ = juce::jmax(snap.lastPeakL, meterL_ * 0.86f);
        meterR_ = juce::jmax(snap.lastPeakR, meterR_ * 0.86f);
    }
    repaint(meterArea_);
}

void SappKeysEditor::paint(juce::Graphics& g)
{
    // Warm ivory with a soft top light.
    juce::ColourGradient grad(palette::ivory.brighter(0.03f), 0.0f, 0.0f,
                              palette::ivory.darker(0.06f), 0.0f, float(getHeight()),
                              false);
    g.setGradientFill(grad);
    g.fillAll();

    // Panels.
    auto panel = [&](juce::Rectangle<int> r) {
        g.setColour(palette::ebony.withAlpha(0.06f));
        g.fillRoundedRectangle(r.toFloat().translated(0.0f, 1.5f), 8.0f);
        g.setColour(palette::cream.withAlpha(0.85f));
        g.fillRoundedRectangle(r.toFloat(), 8.0f);
        g.setColour(palette::parchment);
        g.drawRoundedRectangle(r.toFloat(), 8.0f, 1.0f);
    };
    const float scale = float(getWidth()) / 960.0f;
    auto s = [scale](int v) { return int(float(v) * scale); };

    panel({s(14), s(74), s(360), s(376)});    // touch & pedals
    panel({s(384), s(74), s(286), s(376)});   // instrument
    panel({s(680), s(74), s(266), s(186)});   // character
    panel({s(680), s(268), s(266), s(182)});  // room & output

    // Brass hairline under the header.
    g.setColour(palette::brass.withAlpha(0.5f));
    g.fillRect(s(14), s(66), getWidth() - s(28), 1);

    // Peak meter (ebony well, brass bars).
    if (!meterArea_.isEmpty()) {
        g.setColour(palette::ebony);
        g.fillRoundedRectangle(meterArea_.toFloat(), 3.0f);
        auto bar = [&](float level, juce::Rectangle<int> r) {
            const float db = juce::Decibels::gainToDecibels(level, -60.0f);
            const float norm = juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f);
            auto fill = r.toFloat();
            fill = fill.removeFromLeft(fill.getWidth() * norm);
            g.setColour(db > -3.0f ? palette::felt.brighter(0.3f) : palette::brassBright);
            g.fillRoundedRectangle(fill, 2.0f);
        };
        auto inner = meterArea_.reduced(2);
        bar(meterL_, inner.removeFromTop(inner.getHeight() / 2).reduced(0, 1));
        bar(meterR_, inner.reduced(0, 1));
    }
}

void SappKeysEditor::resized()
{
    const float scale = float(getWidth()) / 960.0f;
    auto s = [scale](int v) { return int(float(v) * scale); };

    title_.setBounds(s(18), s(10), s(220), s(34));
    subtitle_.setBounds(s(21), s(42), s(240), s(16));
    loadButton_.setBounds(s(268), s(20), s(92), s(28));
    diagButton_.setBounds(s(366), s(20), s(84), s(28));
    instrumentName_.setBounds(getWidth() - s(400), s(12), s(384), s(24));
    status_.setBounds(getWidth() - s(400), s(36), s(384), s(18));

    // Touch & pedals panel: the velocity curve is the hero.
    touchHeader_.setBounds(s(26), s(82), s(200), s(16));
    velocityPad_->setBounds(s(26), s(102), s(336), s(226));
    touch_->setBounds(s(28), s(336), s(80), s(104));
    dynamics_->setBounds(s(114), s(336), s(80), s(104));
    expression_->setBounds(s(200), s(336), s(80), s(104));
    pedalLamps_->setBounds(s(288), s(338), s(80), s(102));

    // Instrument panel.
    bodyHeader_.setBounds(s(396), s(82), s(200), s(16));
    unaCorda_->setBounds(s(398), s(104), s(126), s(140));
    lid_->setBounds(s(532), s(104), s(126), s(140));
    resonance_->setBounds(s(398), s(248), s(126), s(140));
    mechNoise_->setBounds(s(544), s(258), s(100), s(120));
    width_->setBounds(s(544), s(370), s(100), s(74));

    // Character panel.
    characterHeader_.setBounds(s(692), s(82), s(200), s(16));
    vintage_->setBounds(s(694), s(102), s(118), s(146));
    drive_->setBounds(s(818), s(102), s(118), s(146));

    // Room & output panel.
    roomHeader_.setBounds(s(692), s(276), s(200), s(16));
    const int knobW = s(60), knobH = s(86);
    roomLevel_->setBounds(s(688), s(296), knobW, knobH);
    roomSize_->setBounds(s(752), s(296), knobW, knobH);
    roomDecay_->setBounds(s(816), s(296), knobW, knobH);
    master_->setBounds(s(880), s(296), knobW, knobH);
    quality_.setBounds(s(692), s(394), s(110), s(24));
    limiter_.setBounds(s(830), s(394), s(100), s(24));

    // Footer strip: the 88.
    const int keyboardY = s(466);
    keyboard_->setBounds(s(14), keyboardY, getWidth() - s(28), s(112));
    keyboard_->setKeyWidth(float(keyboard_->getWidth()) / 52.3f);

    voicesLabel_.setBounds(s(14), s(588), s(90), s(20));
    meterArea_ = {s(110), s(592), s(150), s(14)};
}

} // namespace sappkeys
