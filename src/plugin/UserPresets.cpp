#include "UserPresets.h"

namespace sapp::userpresets {

namespace {

/** 9 decimals round-trips an IEEE-754 float exactly for the 0..1 normalised
    values we store, and keeps the file readable. */
juce::String number(float value)
{
    return juce::String(static_cast<double>(value), 9);
}

juce::String quoted(const juce::String& text)
{
    return juce::JSON::toString(juce::var(text));
}

} // namespace

juce::File presetsRoot()
{
    // Tests point SAPPSOUNDS_PRESETS somewhere disposable; everything else
    // uses the one documented location (sapplink/PRESETS.md section 4).
    const auto override_ = juce::SystemStats::getEnvironmentVariable("SAPPSOUNDS_PRESETS", {});
    if (override_.isNotEmpty())
        return juce::File(override_);
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
        .getChildFile("SappSounds")
        .getChildFile("presets");
}

juce::File presetDir(const juce::String& instrument)
{
    return presetsRoot().getChildFile(instrument);
}

juce::String sanitiseFileName(const juce::String& name)
{
    juce::String out;
    for (auto c : name)
    {
        // charToString, not operator+=(juce_wchar): appending the raw
        // character type would append its NUMBER, not the character.
        const bool illegal = c < 32 || juce::String("/\\:*?\"<>|").containsChar(c);
        out += illegal ? juce::String("-") : juce::String::charToString(c);
    }
    out = out.trim();
    return out.isEmpty() ? juce::String("preset") : out;
}

bool parse(const juce::File& file, const juce::String& instrument,
           UserPreset& out, juce::String& error)
{
    const auto v = juce::JSON::parse(file.loadFileAsString());
    auto* obj = v.getDynamicObject();
    if (obj == nullptr)
    {
        error = file.getFileName() + ": not a JSON object";
        return false;
    }
    const int version = static_cast<int>(v.getProperty("sapplink", 0));
    if (version != kFormatVersion)
    {
        // Refuse rather than guess — an unknown version may mean anything.
        error = file.getFileName() + ": unsupported sapplink preset version " + juce::String(version);
        return false;
    }
    const auto owner = v.getProperty("instrument", "").toString();
    if (owner != instrument)
    {
        error = file.getFileName() + ": preset is for \"" + owner + "\", not \"" + instrument + "\"";
        return false;
    }

    out = {};
    out.file = file;
    out.name = v.getProperty("name", file.getFileNameWithoutExtension()).toString();
    out.author = v.getProperty("author", "").toString();
    out.created = v.getProperty("created", "").toString();
    out.notes = v.getProperty("notes", "").toString();
    out.sfz = v.getProperty("sfz", "").toString();
    out.realUnits = v.getProperty("encoding", "normalised").toString() == "real";

    const auto params = v.getProperty("params", {});
    if (auto* paramObj = params.getDynamicObject())
        for (const auto& p : paramObj->getProperties())
            if (p.name.toString() != kPresetParamId)
                out.params.push_back({ p.name.toString(), static_cast<float>(static_cast<double>(p.value)) });

    if (out.params.empty())
    {
        error = file.getFileName() + ": no usable params";
        return false;
    }
    return true;
}

std::vector<UserPreset> scan(const juce::String& instrument)
{
    std::vector<UserPreset> found;
    const auto dir = presetDir(instrument);
    if (!dir.isDirectory())
        return found;

    for (const auto& entry : juce::RangedDirectoryIterator(dir, false, "*.json", juce::File::findFiles))
    {
        UserPreset preset;
        juce::String error;
        if (parse(entry.getFile(), instrument, preset, error))
            found.push_back(std::move(preset));
    }
    // Alphabetical, case-insensitive: the same order the `preset` parameter's
    // choice list uses, so an index means the same thing on both sides.
    std::sort(found.begin(), found.end(), [](const UserPreset& a, const UserPreset& b)
              { return a.name.compareIgnoreCase(b.name) < 0; });
    return found;
}

std::optional<UserPreset> findByName(const juce::String& instrument, const juce::String& name)
{
    // The file name is sanitised, the `name` field is authoritative — try the
    // fast path, then fall back to a scan so a renamed file still resolves.
    const auto direct = presetDir(instrument).getChildFile(sanitiseFileName(name) + ".json");
    UserPreset preset;
    juce::String error;
    if (direct.existsAsFile() && parse(direct, instrument, preset, error))
        return preset;
    for (auto& candidate : scan(instrument))
        if (candidate.name.equalsIgnoreCase(name))
            return candidate;
    return std::nullopt;
}

UserPreset capture(juce::AudioProcessor& processor, const juce::String& name,
                   const juce::String& notes)
{
    UserPreset preset;
    preset.name = name;
    preset.notes = notes;
    preset.author = juce::SystemStats::getFullUserName();
    if (preset.author.isEmpty())
        preset.author = juce::SystemStats::getLogonName();
    preset.created = juce::Time::getCurrentTime().toISO8601(true);
    preset.realUnits = false;

    for (auto* parameter : processor.getParameters())
    {
        auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(parameter);
        if (withId == nullptr || withId->paramID == kPresetParamId)
            continue;
        // getValue() is the normalised number the host's automation lane
        // stores; re-applying it lands on the identical plain value.
        preset.params.push_back({ withId->paramID, withId->getValue() });
    }
    return preset;
}

juce::String toJson(const UserPreset& preset, const juce::String& instrument)
{
    juce::StringArray lines;
    lines.add("{");
    lines.add("  \"sapplink\": " + juce::String(kFormatVersion) + ",");
    lines.add("  \"instrument\": " + quoted(instrument) + ",");
    lines.add("  \"name\": " + quoted(preset.name) + ",");
    lines.add("  \"author\": " + quoted(preset.author) + ",");
    lines.add("  \"created\": " + quoted(preset.created) + ",");
    lines.add("  \"encoding\": " + quoted(preset.realUnits ? "real" : "normalised") + ",");
    if (preset.sfz.isNotEmpty())
        lines.add("  \"sfz\": " + quoted(preset.sfz) + ",");
    lines.add("  \"params\": {");
    for (std::size_t i = 0; i < preset.params.size(); ++i)
        lines.add("    " + quoted(preset.params[i].first) + ": " + number(preset.params[i].second)
                  + (i + 1 < preset.params.size() ? "," : ""));
    lines.add("  },");
    lines.add("  \"notes\": " + quoted(preset.notes));
    lines.add("}");
    return lines.joinIntoString("\n") + "\n";
}

bool save(const UserPreset& preset, const juce::String& instrument,
          juce::File& outFile, juce::String& error)
{
    if (preset.name.trim().isEmpty())
    {
        error = "a preset needs a name";
        return false;
    }
    const auto dir = presetDir(instrument);
    if (!dir.isDirectory() && !dir.createDirectory().wasOk())
    {
        error = "could not create " + dir.getFullPathName();
        return false;
    }
    outFile = dir.getChildFile(sanitiseFileName(preset.name) + ".json");
    if (!outFile.replaceWithText(toJson(preset, instrument)))
    {
        error = "could not write " + outFile.getFullPathName();
        return false;
    }
    return true;
}

int apply(const UserPreset& preset, juce::AudioProcessorValueTreeState& apvts)
{
    // Only the listed parameters move (PRESETS.md section 1): a captured
    // preset lists them all, an authored one is a deliberate partial tweak.
    int applied = 0;
    for (const auto& [id, value] : preset.params)
    {
        if (id == kPresetParamId)
            continue;
        auto* parameter = apvts.getParameter(id);
        if (parameter == nullptr)
            continue;
        const float normalised = preset.realUnits ? parameter->convertTo0to1(value) : value;
        parameter->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, normalised));
        ++applied;
    }
    return applied;
}

juce::StringArray choiceLabels(const juce::String& instrument)
{
    juce::StringArray labels;
    for (const auto& preset : scan(instrument))
        labels.add(preset.name + kUserSuffix);
    return labels;
}

juce::String nameFromChoiceLabel(const juce::String& label)
{
    return label.endsWith(kUserSuffix)
               ? label.dropLastCharacters(static_cast<int>(juce::String(kUserSuffix).length()))
               : label;
}

} // namespace sapp::userpresets
