#include "PluginProcessor.h"

#include <sapp/sounds/DiagnosticInstrument.h>

#include "../core/KeysInstrument.h"
#include "../core/SappLinkCCMap.h"
#include "FactoryPresets.h"
#include "PluginEditor.h"
#include "SoundsPanel.h"

namespace sappkeys {

using namespace sapp::keys;
using sapp::sounds::MidiEvent;

juce::AudioProcessorValueTreeState::ParameterLayout SappKeysProcessor::makeLayout()
{
    using P = juce::AudioParameterFloat;
    using Range = juce::NormalisableRange<float>;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // Parameter IDs are compatibility contracts — never reuse or renumber.
    layout.add(std::make_unique<P>(juce::ParameterID{"touch", 1}, "Touch",
                                   Range{0.0f, 1.0f, 0.001f}, 0.5f));
    layout.add(std::make_unique<P>(juce::ParameterID{"dynamics", 1}, "Dynamics",
                                   Range{0.0f, 1.0f, 0.001f}, 1.0f));
    layout.add(std::make_unique<P>(juce::ParameterID{"expression", 1}, "Expression",
                                   Range{0.0f, 1.0f, 0.001f}, 1.0f));
    layout.add(std::make_unique<P>(juce::ParameterID{"unaCorda", 1}, "Una Corda",
                                   Range{0.0f, 1.0f, 0.001f}, 0.0f));
    layout.add(std::make_unique<P>(juce::ParameterID{"lid", 1}, "Lid",
                                   Range{0.0f, 1.0f, 0.001f}, 0.85f));
    layout.add(std::make_unique<P>(juce::ParameterID{"resonance", 1}, "Resonance",
                                   Range{0.0f, 1.0f, 0.001f}, 0.5f));
    layout.add(std::make_unique<P>(juce::ParameterID{"mechNoise", 1}, "Mechanics",
                                   Range{0.0f, 1.0f, 0.001f}, 1.0f));
    layout.add(std::make_unique<P>(juce::ParameterID{"width", 1}, "Width",
                                   Range{0.0f, 2.0f, 0.001f}, 1.0f));
    layout.add(std::make_unique<P>(juce::ParameterID{"vintage", 1}, "Vintage",
                                   Range{0.0f, 1.0f, 0.001f}, 0.0f));
    layout.add(std::make_unique<P>(juce::ParameterID{"drive", 1}, "Drive",
                                   Range{0.0f, 1.0f, 0.001f}, 0.0f));
    layout.add(std::make_unique<P>(juce::ParameterID{"roomLevel", 1}, "Room Level",
                                   Range{0.0f, 1.0f, 0.001f}, 0.30f));
    layout.add(std::make_unique<P>(juce::ParameterID{"roomSize", 1}, "Room Size",
                                   Range{0.6f, 1.4f, 0.001f}, 1.0f));
    layout.add(std::make_unique<P>(juce::ParameterID{"roomDecay", 1}, "Room Decay",
                                   Range{0.2f, 2.5f, 0.01f, 0.5f}, 0.9f));
    layout.add(std::make_unique<P>(juce::ParameterID{"masterGain", 1}, "Master Gain",
                                   Range{-24.0f, 12.0f, 0.1f}, 0.0f));
    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"limiter", 1}, "Safety Limiter", true));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"quality", 1}, "Quality",
        juce::StringArray{"Draft", "Normal"}, 1));

    // Host-automatable sound selection (sapptune issue #13). ADDED LAST so no
    // existing parameter's index moves — automation lanes are a contract.
    // The factory bank in program order, then the user presets that exist
    // right now; the list is fixed for this instance's lifetime because a
    // choice parameter cannot change its choices without breaking lanes.
    juce::StringArray presetChoices;
    for (const auto& preset : presets::all())
        presetChoices.add(preset.name);
    presetChoices.addArray(sapp::userpresets::choiceLabels(SappKeysProcessor::kInstrument));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{sapp::userpresets::kPresetParamId, 1}, "Preset",
        presetChoices, 0));
    return layout;
}

SappKeysProcessor::SappKeysProcessor()
    : juce::AudioProcessor(BusesProperties().withOutput(
          "Output", juce::AudioChannelSet::stereo(), true)),
      apvts_(*this, nullptr, "SappKeys", makeLayout())
{
    auto raw = [this](const char* id) { return apvts_.getRawParameterValue(id); };
    pTouch_ = raw("touch");
    pDynamics_ = raw("dynamics");
    pExpression_ = raw("expression");
    pUnaCorda_ = raw("unaCorda");
    pLid_ = raw("lid");
    pResonance_ = raw("resonance");
    pMechNoise_ = raw("mechNoise");
    pWidth_ = raw("width");
    pVintage_ = raw("vintage");
    pDrive_ = raw("drive");
    pRoomLevel_ = raw("roomLevel");
    pRoomSize_ = raw("roomSize");
    pRoomDecay_ = raw("roomDecay");
    pMaster_ = raw("masterGain");
    pLimiter_ = raw("limiter");
    pQuality_ = raw("quality");

    // Sized so a MIDI flood can never make processBlock() allocate. Events past
    // the cap are dropped here and the engine panics its voices (KeysEngine).
    eventScratch_.reserve(size_t(sapp::keys::kMaxBlockEvents) + 2);

    const auto& table = sapplink::mappings();
    static_assert(std::tuple_size<decltype(ccSlews_)>::value == size_t(sapplink::kNumMappings),
                  "ccSlews_ must match the SappLink mapping table size");
    for (size_t i = 0; i < table.size(); ++i)
        ccSlews_[i].parameter = apvts_.getParameter(table[i].paramId);

    // Host-automatable sound selection (sapptune issue #13). The callback can
    // arrive on the audio thread; it only stores an index.
    apvts_.addParameterListener(sapp::userpresets::kPresetParamId, this);

    loadDiagnosticInstrument();
    startTimerHz(30);   // deferred program-change apply (message thread)
}

// ------------------------------------------------------- factory programs --

int SappKeysProcessor::getNumPrograms()
{
    return int(presets::all().size());
}

const juce::String SappKeysProcessor::getProgramName(int index)
{
    const auto& bank = presets::all();
    if (index < 0 || index >= int(bank.size()))
        return {};
    return bank[size_t(index)].name;
}

void SappKeysProcessor::setCurrentProgram(int index)
{
    // Hosts may call this from any thread; defer to the timer like a MIDI
    // program change. currentProgram_ updates immediately so hosts that read
    // it straight back see the new value.
    if (index < 0 || index >= getNumPrograms() || index == currentProgram_.load())
        return;
    currentProgram_.store(index);
    pendingProgram_.store(index);
}

void SappKeysProcessor::applyFactoryPreset(int index)
{
    const auto& bank = presets::all();
    if (index < 0 || index >= int(bank.size()))
        return;
    const auto& preset = bank[size_t(index)];

    for (auto* parameter : getParameters())
        if (auto* withId = dynamic_cast<juce::RangedAudioParameter*>(parameter))
            // `preset` is the chooser, not part of the sound: resetting it
            // here would snap the selection back to program 0 on every load.
            if (withId->paramID != sapp::userpresets::kPresetParamId)
                withId->setValueNotifyingHost(withId->getDefaultValue());

    for (const auto& [id, value] : preset.values)
        if (auto* parameter = apvts_.getParameter(id))
            parameter->setValueNotifyingHost(parameter->convertTo0to1(value));

    // Instrument hint: swap libraries when the preferred one is installed
    // and not already loaded. Otherwise keep the current instrument — the
    // parameter starting points still apply.
    const auto sfz = presets::resolveInstrument(preset, SoundsPanel::samplesRoot());
    if (sfz.existsAsFile() && sfz.getFullPathName() != sfzPath_)
        loadSfzInstrument(sfz);

    currentProgram_.store(index);
    syncPresetParameter(index);
    updateHostDisplay(ChangeDetails{}.withProgramChanged(true));
}

// ----------------------------------------------------------- user presets --

int SappKeysProcessor::factoryPresetCount() const
{
    return int(presets::all().size());
}

std::vector<sapp::userpresets::UserPreset> SappKeysProcessor::userPresets() const
{
    return sapp::userpresets::scan(kInstrument);
}

bool SappKeysProcessor::saveUserPreset(const juce::String& name, const juce::String& notes,
                                       juce::String& error)
{
    auto preset = sapp::userpresets::capture(*this, name.trim(), notes);
    // capture() is instrument-agnostic and never fills `sfz`: record which
    // sample library this sound was captured with, so loading it elsewhere can
    // put the same instrument back (PRESETS.md section 1).
    preset.sfz = sfzPath_;
    juce::File written;
    return sapp::userpresets::save(preset, kInstrument, written, error);
}

bool SappKeysProcessor::loadUserPreset(const juce::String& name, juce::String& error)
{
    const auto preset = sapp::userpresets::findByName(kInstrument, name);
    if (!preset.has_value()) {
        error = "no user preset named \"" + name + "\" in "
                + sapp::userpresets::presetDir(kInstrument).getFullPathName();
        return false;
    }
    sapp::userpresets::apply(*preset, apvts_);
    // Optional resource hint: swap libraries when the captured one is still
    // where it was. A missing path is not an error — the parameters still
    // apply on top of whatever instrument is loaded.
    const juce::File sfz(preset->sfz);
    if (preset->sfz.isNotEmpty() && sfz.existsAsFile()
        && sfz.getFullPathName() != sfzPath_)
        loadSfzInstrument(sfz);
    return true;
}

void SappKeysProcessor::applyPresetChoice(int index)
{
    if (index < 0)
        return;
    if (index < factoryPresetCount()) {
        applyFactoryPreset(index);
        return;
    }
    // Beyond the factory bank: resolve the choice label back to a name and
    // load from disk, so the file is the source of truth even if it changed
    // since this instance was constructed.
    auto* choice = dynamic_cast<juce::AudioParameterChoice*>(
        apvts_.getParameter(sapp::userpresets::kPresetParamId));
    if (choice == nullptr || index >= choice->choices.size())
        return;
    juce::String error;
    loadUserPreset(sapp::userpresets::nameFromChoiceLabel(choice->choices[index]), error);
    syncPresetParameter(index);
}

void SappKeysProcessor::syncPresetParameter(int choiceIndex)
{
    auto* choice = dynamic_cast<juce::AudioParameterChoice*>(
        apvts_.getParameter(sapp::userpresets::kPresetParamId));
    if (choice == nullptr || choiceIndex < 0 || choiceIndex >= choice->choices.size())
        return;
    if (choice->getIndex() == choiceIndex)
        return;
    const juce::ScopedValueSetter<bool> guard(applyingPreset_, true);
    choice->setValueNotifyingHost(choice->convertTo0to1(float(choiceIndex)));
}

void SappKeysProcessor::parameterChanged(const juce::String& parameterId, float newValue)
{
    if (applyingPreset_ || parameterId != sapp::userpresets::kPresetParamId)
        return;
    pendingPresetChoice_.store(int(newValue));
}

void SappKeysProcessor::timerCallback()
{
    const int program = pendingProgram_.exchange(-1);
    if (program >= 0)
        applyFactoryPreset(program);

    const int choice = pendingPresetChoice_.exchange(-1);
    if (choice >= 0)
        applyPresetChoice(choice);
}

void SappKeysProcessor::handleSappLinkCc(int ccNumber, int ccValue)
{
    const auto* mapping = sapplink::findMapping(ccNumber);
    if (mapping == nullptr)
        return;
    const auto index = size_t(mapping - sapplink::mappings().data());
    auto& slew = ccSlews_[index];
    if (slew.parameter == nullptr)
        return;
    slew.target = slew.parameter->convertTo0to1(sapplink::ccToEngineering(*mapping, ccValue));
    if (!slew.active)
        slew.current = slew.parameter->getValue();
    slew.active = true;
}

void SappKeysProcessor::advanceCcSlews(int numSamples)
{
    // ~15 ms approach per step, applied through the same normalized-value
    // path host automation uses — never straight into the DSP.
    const float coefficient =
        1.0f - std::exp(-float(numSamples) / (0.015f * float(getSampleRate() > 0 ? getSampleRate() : 48000.0)));
    for (auto& slew : ccSlews_) {
        if (!slew.active || slew.parameter == nullptr)
            continue;
        slew.current += (slew.target - slew.current) * coefficient;
        if (std::abs(slew.target - slew.current) < 1.0e-4f) {
            slew.current = slew.target;
            slew.active = false;
        }
        slew.parameter->setValueNotifyingHost(slew.current);
    }
}

SappKeysProcessor::~SappKeysProcessor()
{
    apvts_.removeParameterListener(sapp::userpresets::kPresetParamId, this);
}

void SappKeysProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    engine_.prepare(sampleRate, juce::jmax(64, samplesPerBlock));
    pushParamsToEngine();
}

void SappKeysProcessor::releaseResources() {}

bool SappKeysProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void SappKeysProcessor::pushParamsToEngine()
{
    KeysParams p;
    p.touch = pTouch_->load();
    p.dynamics = pDynamics_->load();
    p.expression = pExpression_->load();
    p.unaCorda = pUnaCorda_->load();
    p.lid = pLid_->load();
    p.resonance = pResonance_->load();
    p.mechNoise = pMechNoise_->load();
    p.width = pWidth_->load();
    p.vintage = pVintage_->load();
    p.drive = pDrive_->load();
    p.roomLevel = pRoomLevel_->load();
    p.roomSize = pRoomSize_->load();
    p.roomDecay = pRoomDecay_->load();
    p.masterGainDb = pMaster_->load();
    p.limiter = pLimiter_->load() > 0.5f;
    p.quality = int(pQuality_->load());
    engine_.setParams(p);
}

void SappKeysProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                     juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    advanceCcSlews(buffer.getNumSamples());
    pushParamsToEngine();

    keyboardState.processNextMidiBuffer(midi, 0, buffer.getNumSamples(), true);

    eventScratch_.clear();

    // Knob→CC bridging: moving Dynamics/Expression behaves like riding CC1/11.
    const float dynParam = pDynamics_->load();
    if (lastDynParam_ >= 0.0f && std::abs(dynParam - lastDynParam_) > 0.004f) {
        MidiEvent e;
        e.type = MidiEvent::Type::Controller;
        e.frame = 0;
        e.note = 1;
        e.value = uint8_t(juce::jlimit(0, 127, int(dynParam * 127.0f + 0.5f)));
        eventScratch_.push_back(e);
    }
    lastDynParam_ = dynParam;
    const float exprParam = pExpression_->load();
    if (lastExprParam_ >= 0.0f && std::abs(exprParam - lastExprParam_) > 0.004f) {
        MidiEvent e;
        e.type = MidiEvent::Type::Controller;
        e.frame = 0;
        e.note = 11;
        e.value = uint8_t(juce::jlimit(0, 127, int(exprParam * 127.0f + 0.5f)));
        eventScratch_.push_back(e);
    }
    lastExprParam_ = exprParam;

    int floodedEvents = 0;
    for (const auto metadata : midi) {
        // One slot short of the cap: the last is kept for the flood panic below.
        if (eventScratch_.size() + 1 >= size_t(sapp::keys::kMaxBlockEvents)) {
            ++floodedEvents;   // never grow the vector on the audio thread
            continue;
        }
        const auto msg = metadata.getMessage();
        MidiEvent e;
        e.frame = uint32_t(juce::jmax(0, metadata.samplePosition));
        if (msg.isNoteOn()) {
            e.type = MidiEvent::Type::NoteOn;
            e.note = uint8_t(msg.getNoteNumber());
            e.value = uint8_t(msg.getVelocity());
        } else if (msg.isNoteOff()) {
            e.type = MidiEvent::Type::NoteOff;
            e.note = uint8_t(msg.getNoteNumber());
        } else if (msg.isController()) {
            e.type = MidiEvent::Type::Controller;
            e.note = uint8_t(msg.getControllerNumber());
            e.value = uint8_t(msg.getControllerValue());
            // SappLink CC-in (any channel): mapped CCs also steer parameters.
            // The event still reaches the engine below (SFZ CC conditions,
            // native CC1/CC11/CC64 behavior stay untouched).
            handleSappLinkCc(msg.getControllerNumber(), msg.getControllerValue());
        } else if (msg.isPitchWheel()) {
            e.type = MidiEvent::Type::PitchBend;
            e.bend14 = int16_t(msg.getPitchWheelValue() - 8192);
        } else if (msg.isAllNotesOff()) {
            e.type = MidiEvent::Type::AllNotesOff;
        } else if (msg.isAllSoundOff()) {
            e.type = MidiEvent::Type::AllSoundOff;
        } else if (msg.isProgramChange()) {
            // Factory-preset select; applied on the message thread (timer).
            pendingProgram_.store(msg.getProgramChangeNumber());
            continue;
        } else {
            continue;
        }
        eventScratch_.push_back(e);
    }
    // A MidiBuffer is already ordered by sample position and the injected CC
    // events sit at frame 0 ahead of it, so this is a check, not a sort — and
    // it keeps std::stable_sort's temporary allocation off the audio thread in
    // every real case.
    const auto byFrame = [](const MidiEvent& a, const MidiEvent& b) { return a.frame < b.frame; };
    if (!std::is_sorted(eventScratch_.begin(), eventScratch_.end(), byFrame))
        std::stable_sort(eventScratch_.begin(), eventScratch_.end(), byFrame);

    if (floodedEvents > 0) {
        // Dropped events sort after what we kept, so the losses are note-OFFs:
        // end every voice rather than leave notes stuck on. Fits in the two
        // slots reserved past the cap — still no audio-thread allocation.
        MidiEvent panic;
        panic.type = MidiEvent::Type::AllSoundOff;
        panic.frame = uint32_t(juce::jmax(0, buffer.getNumSamples() - 1));
        eventScratch_.push_back(panic);
    }

    buffer.clear();
    if (buffer.getNumChannels() >= 2) {
        engine_.process(eventScratch_.data(), int(eventScratch_.size()),
                        buffer.getWritePointer(0), buffer.getWritePointer(1),
                        buffer.getNumSamples());
    }
    midi.clear();
}

// ------------------------------------------------------------- instruments --

void SappKeysProcessor::loadDiagnosticInstrument()
{
    const uint64_t generation = ++loadGeneration_;
    loading_ = true;
    {
        const juce::ScopedLock sl(loadLock_);
        loadStatus_ = "Generating diagnostic keys...";
    }
    std::thread([this, generation] {
        auto inst = sapp::sounds::makeDiagnosticInstrument();
        sapp::sounds::LoadResult result;
        result.instrument = inst;
        result.ok = true;
        juce::MessageManager::callAsync([this, result = std::move(result), generation]() mutable {
            finishLoad(std::move(result), {}, generation);
        });
    }).detach();
}

void SappKeysProcessor::loadSfzInstrument(const juce::File& sfzFile)
{
    const uint64_t generation = ++loadGeneration_;
    loading_ = true;
    {
        const juce::ScopedLock sl(loadLock_);
        loadStatus_ = "Loading " + sfzFile.getFileName() + "...";
    }
    const juce::String path = sfzFile.getFullPathName();
    std::thread([this, path, generation] {
        auto result = sapp::keys::loadKeysSfz(path.toStdString());
        juce::MessageManager::callAsync([this, result = std::move(result), path, generation]() mutable {
            finishLoad(std::move(result), path, generation);
        });
    }).detach();
}

void SappKeysProcessor::finishLoad(sapp::sounds::LoadResult result,
                                   const juce::String& path, uint64_t generation)
{
    if (generation != loadGeneration_.load()) return;  // superseded
    loading_ = false;

    const juce::ScopedLock sl(loadLock_);
    if (!result.ok || result.instrument == nullptr) {
        loadStatus_ = "Load failed";
        for (const auto& d : result.diagnostics)
            if (d.severity == sapp::sounds::Severity::Error) {
                loadStatus_ = "Load failed: " + juce::String(d.message);
                break;
            }
    } else {
        engine_.setInstrument(result.instrument);
        engine_.collectRetired();
        sfzPath_ = path;
        instrumentName_ = juce::String(result.instrument->definition.name);
        loadStatus_ = result.missingSamples.empty()
                          ? "Ready"
                          : juce::String(result.missingSamples.size()) + " samples missing";
    }
    if (onInstrumentChanged) onInstrumentChanged();
}

juce::String SappKeysProcessor::currentInstrumentName() const
{
    const juce::ScopedLock sl(loadLock_);
    return instrumentName_;
}

juce::String SappKeysProcessor::loadStatus() const
{
    const juce::ScopedLock sl(loadLock_);
    return loadStatus_;
}

// -------------------------------------------------------------------- state --

void SappKeysProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts_.copyState();
    state.setProperty("sfzPath", sfzPath_, nullptr);
    state.setProperty("stateVersion", 1, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void SappKeysProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes)) {
        auto state = juce::ValueTree::fromXml(*xml);
        if (!state.isValid()) return;
        apvts_.replaceState(state);
        const juce::String path = state.getProperty("sfzPath", "").toString();
        if (path.isNotEmpty() && juce::File(path).existsAsFile())
            loadSfzInstrument(juce::File(path));
        else
            loadDiagnosticInstrument();
    }
}

juce::AudioProcessorEditor* SappKeysProcessor::createEditor()
{
    return new SappKeysEditor(*this);
}

} // namespace sappkeys

// JUCE plugin entry point.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new sappkeys::SappKeysProcessor();
}
