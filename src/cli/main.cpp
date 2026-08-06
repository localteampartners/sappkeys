// sappkeys — the SappKeys agent/automation CLI.
//
// This is the machine API for external software (e.g. MIDI-generation
// agents): inspect an instrument's capabilities, validate SFZ, dump the
// parameter schema, list presets, and render MIDI through the full keys
// chain. Every output is a single JSON document on stdout.
//
//   sappkeys inspect  (--sfz <f.sfz> | --diagnostic) [--regions]
//   sappkeys validate --sfz <f.sfz>
//   sappkeys params
//   sappkeys presets
//   sappkeys scan <library-dir> [--all]
//   sappkeys render   (--sfz <f.sfz> | --diagnostic) --midi <f.mid>
//                     --out <f.wav> [--sr N] [--seed N] [--tail S]
//                     [--preset NAME] [--param NAME=VALUE ...]
//
// See docs/agent_api.md for the full contract.

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include <sapp/sounds/DiagnosticInstrument.h>
#include <sapp/sounds/InstrumentLoader.h>
#include <sapp/sounds/MidiFile.h>
#include <sapp/sounds/SfzParser.h>
#include <sapp/sounds/WavIo.h>

#include "../core/KeysInstrument.h"
#include "../core/KeysRender.h"
#include "../core/SappLinkCCMap.h"
#include "Json.h"

using namespace sapp::sounds;
using namespace sapp::keys;
using sapptools::JsonWriter;

namespace {

const char* noteName(int note)
{
    static const char* names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    static char buf[8];
    std::snprintf(buf, sizeof(buf), "%s%d", names[((note % 12) + 12) % 12], note / 12 - 1);
    return buf;
}

struct ParamSpec {
    const char* name;      // CLI --param name (snake_case)
    const char* apvtsId;   // stable plugin parameter ID (= SappLink manifest id)
    float KeysParams::* field;
    float lo, hi, def;
    int nativeCc;          // engine-native CC (1/11), or -1
    const char* doc;
};

// Single source of truth for the float parameters an agent may set.
// MIDI CC reachability comes from the SappLink table (core/SappLinkCCMap.cpp)
// except dynamics/expression, which are engine-native on CC 1 / CC 11.
const ParamSpec kParams[] = {
    {"touch", "touch", &KeysParams::touch, 0.0f, 1.0f, 0.5f, -1,
     "Velocity response curve: 0 heavy action (mids play quieter), 0.5 as played, 1 light."},
    {"dynamics", "dynamics", &KeysParams::dynamics, 0.0f, 1.0f, 1.0f, 1,
     "Gentle dynamics trim (follows MIDI CC1): level and brightness together. 1 = neutral."},
    {"expression", "expression", &KeysParams::expression, 0.0f, 1.0f, 1.0f, 11,
     "Phrase volume (follows MIDI CC11). Level only; timbre unchanged."},
    {"una_corda", "unaCorda", &KeysParams::unaCorda, 0.0f, 1.0f, 0.0f, -1,
     "Soft pedal: softer hammer strike (velocity curve) plus a darker felt tilt."},
    {"lid", "lid", &KeysParams::lid, 0.0f, 1.0f, 0.85f, -1,
     "Piano lid: 0 closed (dark, narrow) to 1 full stick (open, wide)."},
    {"resonance", "resonance", &KeysParams::resonance, 0.0f, 1.0f, 0.5f, -1,
     "Sympathetic string resonance while the sustain pedal is down."},
    {"mech_noise", "mechNoise", &KeysParams::mechNoise, 0.0f, 1.0f, 1.0f, -1,
     "Mechanical noises mix (key-release samples): 1 as recorded, 0 off."},
    {"width", "width", &KeysParams::width, 0.0f, 2.0f, 1.0f, -1,
     "Stereo width: 0 mono, 1 natural, 2 wide."},
    {"vintage", "vintage", &KeysParams::vintage, 0.0f, 1.0f, 0.0f, -1,
     "Tape/vintage character: per-note random tuning, wow & flutter, softened highs."},
    {"drive", "drive", &KeysParams::drive, 0.0f, 1.0f, 0.0f, -1,
     "Gentle saturation. Electric pianos love a little of this."},
    {"room_level", "roomLevel", &KeysParams::roomLevel, 0.0f, 1.0f, 0.30f, -1,
     "Small-room ambience level (early reflections + short tail)."},
    {"room_size", "roomSize", &KeysParams::roomSize, 0.6f, 1.4f, 1.0f, -1,
     "Room size scaling."},
    {"room_decay", "roomDecay", &KeysParams::roomDecay, 0.2f, 2.5f, 0.9f, -1,
     "Room decay time in seconds (T60). This is a room, not a hall."},
    {"master_gain_db", "masterGain", &KeysParams::masterGainDb, -24.0f, 12.0f, 0.0f, -1,
     "Master output gain in dB."},
};

// Sound presets: named starting points an agent (or player) picks by intent.
struct PresetValue {
    const char* param;
    float value;
};
struct Preset {
    const char* name;
    const char* doc;
    PresetValue values[8];
    int count;
};
const Preset kPresets[] = {
    {"concert-grand", "Open lid, natural touch, present room. The default voice.",
     {{"lid", 1.0f}, {"resonance", 0.55f}, {"room_level", 0.32f}, {"room_size", 1.15f},
      {"room_decay", 1.3f}}, 5},
    {"intimate", "Close, half-lid, small dry room. Late-night piano.",
     {{"lid", 0.4f}, {"room_level", 0.18f}, {"room_size", 0.7f}, {"room_decay", 0.5f},
      {"width", 0.85f}, {"vintage", 0.12f}}, 6},
    {"felt", "Una corda + heavy mechanical noises. Felt-piano aesthetic.",
     {{"una_corda", 0.85f}, {"mech_noise", 1.0f}, {"touch", 0.4f}, {"room_level", 0.28f},
      {"room_decay", 1.1f}, {"vintage", 0.3f}, {"width", 0.8f}}, 7},
    {"pop-bright", "Light touch, open lid, tight bright room for a mix.",
     {{"touch", 0.62f}, {"lid", 1.0f}, {"room_level", 0.22f}, {"room_decay", 0.55f},
      {"width", 1.2f}}, 5},
    {"ep-tine", "Vintage tine EP: tape character and a little drive.",
     {{"vintage", 0.5f}, {"drive", 0.35f}, {"room_level", 0.18f}, {"room_decay", 0.45f},
      {"width", 1.1f}, {"resonance", 0.0f}, {"mech_noise", 0.7f}}, 7},
    {"ep-crunch", "Driven EP through a small room. Break-up territory.",
     {{"vintage", 0.6f}, {"drive", 0.7f}, {"room_level", 0.15f}, {"room_decay", 0.4f},
      {"width", 1.0f}, {"resonance", 0.0f}}, 6},
};

const Preset* findPreset(const std::string& name)
{
    for (const auto& p : kPresets)
        if (name == p.name) return &p;
    return nullptr;
}

bool setParamByName(KeysParams& params, const std::string& name, float value)
{
    for (const auto& p : kParams) {
        if (name == p.name) {
            params.*(p.field) = std::clamp(value, p.lo, p.hi);
            return true;
        }
    }
    if (name == "quality") { params.quality = int(value); return true; }
    return false;
}

InstrumentPtr loadInstrument(const std::string& sfzPath, bool useDiagnostic,
                             std::vector<Diagnostic>& diags,
                             std::vector<std::string>& missing)
{
    if (useDiagnostic) return makeDiagnosticInstrument();
    auto result = loadKeysSfz(sfzPath);
    diags = result.diagnostics;
    missing = result.missingSamples;
    return result.ok ? result.instrument : nullptr;
}

void writeDiagnostics(JsonWriter& w, const std::vector<Diagnostic>& diags)
{
    w.key("diagnostics");
    w.beginArray();
    for (const auto& d : diags) {
        w.beginObject();
        w.field("severity", d.severity == Severity::Error ? "error"
                          : d.severity == Severity::Warning ? "warning" : "info");
        w.field("file", d.file);
        w.field("line", d.line);
        w.field("message", d.message);
        w.endObject();
    }
    w.endArray();
}

int cmdInspect(const std::string& sfzPath, bool useDiagnostic, bool dumpRegions)
{
    std::vector<Diagnostic> diags;
    std::vector<std::string> missing;
    auto inst = loadInstrument(sfzPath, useDiagnostic, diags, missing);

    JsonWriter w;
    w.beginObject();
    if (!inst) {
        w.field("ok", false);
        writeDiagnostics(w, diags);
        w.endObject();
        std::printf("%s\n", w.str().c_str());
        return 2;
    }
    const auto& def = inst->definition;

    std::set<int> velocitySplits;
    uint16_t maxRoundRobins = 1;
    bool hasReleaseSamples = false;
    for (const auto& r : def.regions) {
        velocitySplits.insert(r.loVel);
        maxRoundRobins = std::max(maxRoundRobins, r.seqLength);
        if (r.trigger == TriggerMode::Release || r.trigger == TriggerMode::ReleaseKey)
            hasReleaseSamples = true;
    }

    w.field("ok", true);
    w.field("name", def.name);
    w.field("source", def.sourcePath.empty() ? std::string("(generated)") : def.sourcePath);
    w.field("regions", uint64_t(def.regions.size()));
    w.field("missingSamples", uint64_t(missing.size()));
    w.field("estimatedRamBytes", inst->sampleBytes());

    w.key("playableRange");
    w.beginObject();
    w.field("low", int(def.loKeyUsed));
    w.field("high", int(def.hiKeyUsed));
    w.field("lowName", noteName(def.loKeyUsed));
    w.field("highName", noteName(def.hiKeyUsed));
    w.endObject();

    w.key("capabilities");
    w.beginObject();
    w.field("velocityLayers", uint64_t(velocitySplits.size()));
    w.field("roundRobins", int(maxRoundRobins));
    w.field("releaseSamples", hasReleaseSamples);
    w.field("mechNoiseControl", hasReleaseSamples);  // mech_noise scales them
    w.endObject();

    // Controller conventions the keys engine responds to.
    w.key("controllers");
    w.beginArray();
    {
        w.beginObject();
        w.field("cc", 1);
        w.field("role", "dynamics");
        w.field("doc", "Gentle level + brightness trim. Neutral at 127; ride down for hazy verses.");
        w.endObject();
        w.beginObject();
        w.field("cc", 11);
        w.field("role", "expression");
        w.field("doc", "Phrase volume on top of dynamics.");
        w.endObject();
        w.beginObject();
        w.field("cc", 64);
        w.field("role", "sustain");
        w.field("doc", "Sustain pedal: holds notes, defers release samples, "
                       "and wakes the sympathetic-resonance layer.");
        w.endObject();
        w.beginObject();
        w.field("cc", 67);
        w.field("role", "unaCorda");
        w.field("doc", "Soft pedal via SappLink: softer strike, darker tilt.");
        w.endObject();
    }
    w.endArray();

    if (dumpRegions) {
        w.key("regionDetails");
        w.beginArray();
        for (const auto& r : def.regions) {
            w.beginObject();
            w.field("sample", r.samplePath);
            w.field("loKey", int(r.loKey));
            w.field("hiKey", int(r.hiKey));
            w.field("rootKey", int(r.rootKey));
            w.field("loVel", int(r.loVel));
            w.field("hiVel", int(r.hiVel));
            w.field("release", r.trigger == TriggerMode::Release ||
                               r.trigger == TriggerMode::ReleaseKey);
            w.field("seqPosition", int(r.seqPosition));
            w.field("seqLength", int(r.seqLength));
            w.field("missing", r.sample == kInvalidSample);
            w.endObject();
        }
        w.endArray();
    }

    writeDiagnostics(w, diags);
    w.endObject();
    std::printf("%s\n", w.str().c_str());
    return missing.empty() ? 0 : 1;
}

int cmdValidate(const std::string& sfzPath)
{
    std::vector<Diagnostic> diags;
    std::vector<std::string> missing;
    auto inst = loadInstrument(sfzPath, false, diags, missing);

    int errors = 0, warnings = 0;
    for (const auto& d : diags) {
        if (d.severity == Severity::Error) ++errors;
        else if (d.severity == Severity::Warning) ++warnings;
    }

    JsonWriter w;
    w.beginObject();
    w.field("ok", inst != nullptr);
    w.field("file", sfzPath);
    w.field("errors", errors);
    w.field("warnings", warnings);
    w.field("missingSamples", uint64_t(missing.size()));
    if (inst) {
        w.field("regions", uint64_t(inst->definition.regions.size()));
        w.key("unsupportedOpcodes");
        w.beginArray();
        for (const auto& o : inst->definition.unsupportedOpcodes) w.value(o);
        w.endArray();
    }
    writeDiagnostics(w, diags);
    w.endObject();
    std::printf("%s\n", w.str().c_str());
    return inst == nullptr ? 2 : (warnings > 0 || !missing.empty() ? 1 : 0);
}

int cmdParams()
{
    JsonWriter w;
    w.beginObject();
    w.field("ok", true);
    w.field("product", "SappKeys");
    w.key("params");
    w.beginArray();
    for (const auto& p : kParams) {
        w.beginObject();
        w.field("name", p.name);
        w.field("id", p.apvtsId);
        w.field("min", double(p.lo));
        w.field("max", double(p.hi));
        w.field("default", double(p.def));
        // MIDI reachability (SappLink): mapped CC, or the engine-native CC.
        if (p.nativeCc >= 0) {
            w.field("cc", p.nativeCc);
            w.field("ccNative", true);
        } else {
            for (const auto& m : sapplink::mappings()) {
                if (std::string(m.paramId) == p.apvtsId) {
                    w.field("cc", m.cc);
                    w.field("ccCurve", m.curve == sapplink::Curve::Log ? "log" : "linear");
                    break;
                }
            }
        }
        w.field("doc", p.doc);
        w.endObject();
    }
    w.endArray();
    w.key("enums");
    w.beginObject();
    w.key("quality");
    w.beginArray();
    w.value("draft");
    w.value("normal");
    w.endArray();
    w.endObject();
    w.endObject();
    std::printf("%s\n", w.str().c_str());
    return 0;
}

int cmdPresets()
{
    JsonWriter w;
    w.beginObject();
    w.field("ok", true);
    w.key("presets");
    w.beginArray();
    for (const auto& p : kPresets) {
        w.beginObject();
        w.field("name", p.name);
        w.field("doc", p.doc);
        w.key("params");
        w.beginObject();
        for (int i = 0; i < p.count; ++i)
            w.field(p.values[i].param, double(p.values[i].value));
        w.endObject();
        w.endObject();
    }
    w.endArray();
    w.field("doc", "Use with render --preset NAME; explicit --param after "
                   "--preset overrides individual values.");
    w.endObject();
    std::printf("%s\n", w.str().c_str());
    return 0;
}

int cmdScan(int argc, char** argv)
{
    std::string dir;
    bool includePartials = false;
    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--all") includePartials = true;
        else dir = arg;
    }
    if (dir.empty()) {
        std::fprintf(stderr, "usage: sappkeys scan <library-dir> [--all]\n");
        return 2;
    }

    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        std::fprintf(stderr, "error: not a directory: %s\n", dir.c_str());
        return 2;
    }

    std::vector<fs::path> files;
    for (auto it = fs::recursive_directory_iterator(
             dir, fs::directory_options::skip_permission_denied, ec);
         !ec && it != fs::recursive_directory_iterator(); ++it) {
        if (!it->is_regular_file(ec)) continue;
        auto ext = it->path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return char(std::tolower(c)); });
        if (ext != ".sfz") continue;
        // Skip include-partials (conventionally kept in "includes/" folders)
        // unless --all: they are fragments, not playable instruments.
        if (!includePartials) {
            bool partial = false;
            for (const auto& part : it->path().parent_path())
                if (part == "includes") partial = true;
            if (partial) continue;
        }
        files.push_back(it->path());
    }
    std::sort(files.begin(), files.end());

    SfzParser parser;
    JsonWriter w;
    w.beginObject();
    w.field("ok", true);
    w.field("root", dir);
    w.key("instruments");
    w.beginArray();
    size_t playable = 0;
    for (const auto& file : files) {
        auto parsed = parser.parseFile(file);
        const auto& def = parsed.instrument;
        if (def.regions.empty()) continue;
        ++playable;
        bool releases = false;
        for (const auto& r : def.regions)
            if (r.trigger == TriggerMode::Release || r.trigger == TriggerMode::ReleaseKey)
                releases = true;
        w.beginObject();
        w.field("path", file.string());
        w.field("name", def.name);
        w.field("category", fs::relative(file.parent_path(), dir, ec).string());
        w.field("regions", uint64_t(def.regions.size()));
        w.field("releaseSamples", releases);
        w.field("lowKey", int(def.loKeyUsed));
        w.field("highKey", int(def.hiKeyUsed));
        w.endObject();
    }
    w.endArray();
    w.field("count", uint64_t(playable));
    w.endObject();
    std::printf("%s\n", w.str().c_str());
    return 0;
}

int cmdRender(int argc, char** argv)
{
    std::string sfzPath, midiPath, outPath;
    bool useDiagnostic = false;
    KeysRenderOptions options;

    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (arg == "--sfz") sfzPath = next();
        else if (arg == "--midi") midiPath = next();
        else if (arg == "--out") outPath = next();
        else if (arg == "--diagnostic") useDiagnostic = true;
        else if (arg == "--sr") options.sampleRate = std::atof(next().c_str());
        else if (arg == "--seed") options.seed = uint32_t(std::strtoul(next().c_str(), nullptr, 10));
        else if (arg == "--tail") options.tailSeconds = std::atof(next().c_str());
        else if (arg == "--preset") {
            const std::string name = next();
            const auto* preset = findPreset(name);
            if (preset == nullptr) {
                std::fprintf(stderr, "error: unknown preset '%s' (see: sappkeys presets)\n",
                             name.c_str());
                return 2;
            }
            for (int v = 0; v < preset->count; ++v)
                setParamByName(options.params, preset->values[v].param,
                               preset->values[v].value);
        }
        else if (arg == "--param") {
            const std::string kv = next();
            const size_t eq = kv.find('=');
            if (eq == std::string::npos) {
                std::fprintf(stderr, "error: --param expects NAME=VALUE, got '%s'\n", kv.c_str());
                return 2;
            }
            const std::string name = kv.substr(0, eq);
            const float value = float(std::atof(kv.c_str() + eq + 1));
            if (!setParamByName(options.params, name, value)) {
                std::fprintf(stderr, "error: unknown param '%s' (see: sappkeys params)\n",
                             name.c_str());
                return 2;
            }
        }
    }

    if ((sfzPath.empty() && !useDiagnostic) || midiPath.empty() || outPath.empty()) {
        std::fprintf(stderr, "usage: sappkeys render (--sfz <f.sfz> | --diagnostic) "
                             "--midi <f.mid> --out <f.wav> [--sr N] [--seed N] [--tail S] "
                             "[--preset NAME] [--param NAME=VALUE ...]\n");
        return 2;
    }

    std::vector<Diagnostic> diags;
    std::vector<std::string> missing;
    auto inst = loadInstrument(sfzPath, useDiagnostic, diags, missing);
    if (!inst) {
        JsonWriter w;
        w.beginObject();
        w.field("ok", false);
        w.field("error", "failed to load instrument");
        writeDiagnostics(w, diags);
        w.endObject();
        std::printf("%s\n", w.str().c_str());
        return 2;
    }

    auto midi = readMidiFile(midiPath);
    if (!midi.ok) {
        std::fprintf(stderr, "error: %s: %s\n", midiPath.c_str(), midi.error.c_str());
        return 2;
    }

    auto rendered = renderKeys(inst, midi.events, options);
    if (rendered.left.empty() ||
        !writeWavFile(outPath, rendered.left.data(), rendered.right.data(),
                      rendered.left.size(), uint32_t(options.sampleRate), true)) {
        std::fprintf(stderr, "error: render/write failed\n");
        return 2;
    }

    JsonWriter w;
    w.beginObject();
    w.field("ok", true);
    w.field("out", outPath);
    w.field("sampleRate", options.sampleRate);
    w.field("frames", uint64_t(rendered.left.size()));
    w.field("durationSeconds", double(rendered.left.size()) / options.sampleRate);
    w.field("peak", double(rendered.peak));
    w.field("rms", double(rendered.rms));
    w.field("midiEvents", uint64_t(midi.events.size()));
    w.field("seed", uint64_t(options.seed));
    w.endObject();
    std::printf("%s\n", w.str().c_str());
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::fprintf(stderr,
                     "sappkeys — SappKeys agent CLI\n"
                     "  sappkeys inspect  (--sfz <f.sfz> | --diagnostic) [--regions]\n"
                     "  sappkeys validate --sfz <f.sfz>\n"
                     "  sappkeys params\n"
                     "  sappkeys presets\n"
                     "  sappkeys scan <library-dir> [--all]\n"
                     "  sappkeys render   (--sfz | --diagnostic) --midi <f.mid> --out <f.wav>\n"
                     "                    [--preset NAME] [--param NAME=VALUE ...]\n");
        return 2;
    }
    const std::string cmd = argv[1];
    std::string sfzPath;
    bool useDiagnostic = false, dumpRegions = false;
    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--sfz" && i + 1 < argc) sfzPath = argv[++i];
        else if (arg == "--diagnostic") useDiagnostic = true;
        else if (arg == "--regions") dumpRegions = true;
    }

    if (cmd == "inspect") return cmdInspect(sfzPath, useDiagnostic, dumpRegions);
    if (cmd == "validate") {
        if (sfzPath.empty()) { std::fprintf(stderr, "validate requires --sfz\n"); return 2; }
        return cmdValidate(sfzPath);
    }
    if (cmd == "params") return cmdParams();
    if (cmd == "presets") return cmdPresets();
    if (cmd == "scan") return cmdScan(argc, argv);
    if (cmd == "render") return cmdRender(argc, argv);

    std::fprintf(stderr, "unknown command '%s'\n", cmd.c_str());
    return 2;
}
