#include "cli/Args.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace phys::cli {

namespace {

constexpr float kDeg = 3.14159265358979f / 180.0f;

bool ToFloat(const std::string& s, float& out)
{
    char* end = nullptr;
    out = std::strtof(s.c_str(), &end);
    return end && end != s.c_str() && *end == '\0' && std::isfinite(out);
}

bool ToUint(const std::string& s, uint32_t& out)
{
    char* end = nullptr;
    const unsigned long long v = std::strtoull(s.c_str(), &end, 10);
    if (!end || end == s.c_str() || *end != '\0' || v > 0xFFFFFFFFull) return false;
    out = static_cast<uint32_t>(v);
    return true;
}

std::string Lower(std::string s)
{
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Accepts either an index or one of the names.
bool ToEnum(const std::string& s, std::initializer_list<const char*> names, uint32_t& out)
{
    uint32_t i = 0;
    const std::string l = Lower(s);
    for (const char* n : names) {
        if (l == n) {
            out = i;
            return true;
        }
        ++i;
    }
    return ToUint(s, out) && out < names.size();
}

bool ToColour(const std::string& s, float out[4])
{
    std::stringstream ss(s);
    std::string part;
    float v[3];
    for (int i = 0; i < 3; ++i) {
        if (!std::getline(ss, part, ',') || !ToFloat(part, v[i])) return false;
    }
    out[0] = v[0];
    out[1] = v[1];
    out[2] = v[2];
    out[3] = 1.0f;
    return true;
}

bool ToBool(const std::string& s, uint32_t& out)
{
    const std::string l = Lower(s);
    if (l == "true" || l == "1" || l == "on") { out = 1; return true; }
    if (l == "false" || l == "0" || l == "off") { out = 0; return true; }
    return false;
}

} // namespace

void PrintUsage()
{
    std::puts(
        "physarum_cli - runs the Physarum simulation without After Effects and writes PNG frames.\n"
        "\n"
        "Options:\n"
        "  --width N --height N       simulation size (default 1024 x 1024)\n"
        "  --out-width N --out-height N  output size; differs from the sim to test resampling\n"
        "  --frames N                 frames to simulate (default 60)\n"
        "  --every N                  write every Nth frame (default: last frame only)\n"
        "  --bits 8|16|32             output pixel format, as After Effects would request\n"
        "  --out DIR                  output directory (default physarum-out)\n"
        "  --no-images                do not write PNGs\n"
        "  --json FILE                flat JSON object of settings, e.g. {\"decay\": 0.05}\n"
        "  --set KEY=VALUE            one setting (repeatable, applied after --json)\n"
        "  --selfcheck                run twice and verify identical output (determinism)\n"
        "  --snapshot-check K         verify snapshot restore against a straight run, snapshots every K frames\n"
        "  --cache-mb N               snapshot budget in MB (default 512)\n"
        "  --warp                     use the software WARP adapter\n"
        "  --debug-layer              enable the D3D12 debug layer\n"
        "\n"
        "Settings (angles in degrees, fractions 0..1, colours \"r,g,b\"):\n"
        "  species seed agents stepsPerFrame preRoll boundary(wrap|bounce|respawn)\n"
        "  diffusionRadius diffusionStrength decay trailClamp exposure gamma\n"
        "  palette(species|gradient|fire|ice|neon|mono|bio) colA colB colC\n"
        "  background(transparent|black|colour) bgColour alphaFromIntensity\n"
        "  drawAgents agentSize agentOpacity\n"
        "  species.N.<field> for N = 0..3: sensorAngle sensorDist turnAngle stepSize deposit jitter\n"
        "    other(-1..1) share colour spawn(random|centre|ring|box|edge) heading(random|inward|outward)\n"
        "    spawnX spawnY (0..1 of the frame) spawnSize (0..2)");
}

bool ParseArgs(int argc, char** argv, Args& a, std::string& error)
{
    auto need = [&](int& i) -> const char* {
        if (i + 1 >= argc) {
            error = std::string("missing value for ") + argv[i];
            return nullptr;
        }
        return argv[++i];
    };
    auto uintArg = [&](int& i, uint32_t& out) -> bool {
        const char* v = need(i);
        if (!v) return false;
        if (!ToUint(v, out)) {
            error = std::string("bad number for ") + argv[i - 1] + ": " + v;
            return false;
        }
        return true;
    };

    std::vector<std::pair<std::string, std::string>> jsonSettings, cliSettings;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") a.help = true;
        else if (arg == "--width") { if (!uintArg(i, a.width)) return false; }
        else if (arg == "--height") { if (!uintArg(i, a.height)) return false; }
        else if (arg == "--out-width") { if (!uintArg(i, a.outWidth)) return false; }
        else if (arg == "--out-height") { if (!uintArg(i, a.outHeight)) return false; }
        else if (arg == "--frames") { if (!uintArg(i, a.frames)) return false; }
        else if (arg == "--every") { if (!uintArg(i, a.every)) return false; }
        else if (arg == "--bits") { if (!uintArg(i, a.bits)) return false; }
        else if (arg == "--snapshot-check") { if (!uintArg(i, a.snapshotCheck)) return false; }
        else if (arg == "--cache-mb") { if (!uintArg(i, a.cacheMb)) return false; }
        else if (arg == "--out") { const char* v = need(i); if (!v) return false; a.outDir = v; }
        else if (arg == "--no-images") a.images = false;
        else if (arg == "--selfcheck") a.selfcheck = true;
        else if (arg == "--warp") a.warp = true;
        else if (arg == "--debug-layer") a.debugLayer = true;
        else if (arg == "--json") {
            const char* v = need(i);
            if (!v || !LoadJsonSettings(v, jsonSettings, error)) return false;
        } else if (arg == "--set") {
            const char* v = need(i);
            if (!v) return false;
            const std::string kv = v;
            const size_t eq = kv.find('=');
            if (eq == std::string::npos) {
                error = "--set expects KEY=VALUE, got " + kv;
                return false;
            }
            cliSettings.emplace_back(kv.substr(0, eq), kv.substr(eq + 1));
        } else {
            error = "unknown option " + arg;
            return false;
        }
    }
    if (a.bits != 8 && a.bits != 16 && a.bits != 32) {
        error = "--bits must be 8, 16 or 32";
        return false;
    }
    if (a.frames == 0) a.frames = 1;
    a.settings = jsonSettings;
    a.settings.insert(a.settings.end(), cliSettings.begin(), cliSettings.end());
    return true;
}

bool LoadJsonSettings(const std::string& path, std::vector<std::pair<std::string, std::string>>& settings,
                      std::string& error)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        error = "cannot open " + path;
        return false;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string s = ss.str();

    size_t i = 0;
    auto skipWs = [&] { while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i; };
    auto readString = [&](std::string& out) -> bool {
        if (i >= s.size() || s[i] != '"') return false;
        ++i;
        out.clear();
        while (i < s.size() && s[i] != '"') {
            if (s[i] == '\\' && i + 1 < s.size()) ++i;
            out += s[i++];
        }
        if (i >= s.size()) return false;
        ++i;
        return true;
    };

    skipWs();
    if (i >= s.size() || s[i] != '{') { error = path + ": expected '{'"; return false; }
    ++i;
    for (;;) {
        skipWs();
        if (i < s.size() && s[i] == '}') break;
        std::string key, value;
        if (!readString(key)) { error = path + ": expected a string key"; return false; }
        skipWs();
        if (i >= s.size() || s[i] != ':') { error = path + ": expected ':' after " + key; return false; }
        ++i;
        skipWs();
        if (i < s.size() && s[i] == '"') {
            if (!readString(value)) { error = path + ": bad string for " + key; return false; }
        } else {
            while (i < s.size() && s[i] != ',' && s[i] != '}' && !std::isspace(static_cast<unsigned char>(s[i])))
                value += s[i++];
        }
        settings.emplace_back(key, value);
        skipWs();
        if (i < s.size() && s[i] == ',') { ++i; continue; }
        if (i < s.size() && s[i] == '}') break;
        error = path + ": expected ',' or '}' after " + key;
        return false;
    }
    return true;
}

bool ApplySetting(SimParams& p, const std::string& key, const std::string& value, std::string& error)
{
    float f = 0.0f;
    uint32_t u = 0;
    auto bad = [&] {
        error = "bad value for " + key + ": " + value;
        return false;
    };

    if (key.rfind("species.", 0) == 0) {
        const size_t dot = key.find('.', 8);
        if (dot == std::string::npos) return bad();
        uint32_t s = 0;
        if (!ToUint(key.substr(8, dot - 8), s) || s >= kMaxSpecies) return bad();
        const std::string field = key.substr(dot + 1);
        SpeciesParams& sp = p.species[s];
        if (field == "sensorAngle") { if (!ToFloat(value, f)) return bad(); sp.sensorAngle = f * kDeg; }
        else if (field == "sensorDist") { if (!ToFloat(value, f)) return bad(); sp.sensorDist = f; }
        else if (field == "turnAngle") { if (!ToFloat(value, f)) return bad(); sp.turnAngle = f * kDeg; }
        else if (field == "stepSize") { if (!ToFloat(value, f)) return bad(); sp.stepSize = f; }
        else if (field == "deposit") { if (!ToFloat(value, f)) return bad(); sp.deposit = f; }
        else if (field == "jitter") { if (!ToFloat(value, f)) return bad(); sp.jitter = f * kDeg; }
        else if (field == "other") { if (!ToFloat(value, f)) return bad(); sp.otherWeight = f; }
        else if (field == "share") { if (!ToFloat(value, f)) return bad(); sp.share = f; }
        else if (field == "colour") { if (!ToColour(value, sp.colour)) return bad(); }
        else if (field == "spawn") { if (!ToEnum(value, {"random", "centre", "ring", "box", "edge"}, sp.spawnPattern)) return bad(); }
        else if (field == "heading") { if (!ToEnum(value, {"random", "inward", "outward"}, sp.headingMode)) return bad(); }
        else if (field == "spawnX") { if (!ToFloat(value, f)) return bad(); sp.spawnCx = f * float(p.simW); }
        else if (field == "spawnY") { if (!ToFloat(value, f)) return bad(); sp.spawnCy = f * float(p.simH); }
        else if (field == "spawnSize") { if (!ToFloat(value, f)) return bad(); sp.spawnSize = f; }
        else { error = "unknown setting " + key; return false; }
        return true;
    }

    if (key == "species") { if (!ToUint(value, u)) return bad(); p.numSpecies = u; }
    else if (key == "seed") { if (!ToUint(value, u)) return bad(); p.seed = u; }
    else if (key == "agents") { if (!ToUint(value, u)) return bad(); p.agentCount = u; }
    else if (key == "stepsPerFrame") { if (!ToUint(value, u)) return bad(); p.stepsPerFrame = u; }
    else if (key == "preRoll") { if (!ToUint(value, u)) return bad(); p.preRoll = u; }
    else if (key == "boundary") { if (!ToEnum(value, {"wrap", "bounce", "respawn"}, p.boundary)) return bad(); }
    else if (key == "diffusionRadius") { if (!ToUint(value, u)) return bad(); p.diffusionRadius = u; }
    else if (key == "diffusionStrength") { if (!ToFloat(value, f)) return bad(); p.diffusionStrength = f; }
    else if (key == "decay") { if (!ToFloat(value, f)) return bad(); p.decay = f; }
    else if (key == "trailClamp") { if (!ToFloat(value, f)) return bad(); p.trailClamp = f; }
    else if (key == "exposure") { if (!ToFloat(value, f)) return bad(); p.exposure = f; }
    else if (key == "gamma") { if (!ToFloat(value, f)) return bad(); p.gamma = f; }
    else if (key == "palette") { if (!ToEnum(value, {"species", "gradient", "fire", "ice", "neon", "mono", "bio"}, p.palette)) return bad(); }
    else if (key == "colA") { if (!ToColour(value, p.colA)) return bad(); }
    else if (key == "colB") { if (!ToColour(value, p.colB)) return bad(); }
    else if (key == "colC") { if (!ToColour(value, p.colC)) return bad(); }
    else if (key == "background") { if (!ToEnum(value, {"transparent", "black", "colour"}, p.background)) return bad(); }
    else if (key == "bgColour") { if (!ToColour(value, p.bgColour)) return bad(); }
    else if (key == "alphaFromIntensity") { if (!ToBool(value, p.alphaFromIntensity)) return bad(); }
    else if (key == "drawAgents") { if (!ToBool(value, p.drawAgents)) return bad(); }
    else if (key == "agentSize") { if (!ToUint(value, u)) return bad(); p.agentSize = u; }
    else if (key == "agentOpacity") { if (!ToFloat(value, f)) return bad(); p.agentOpacity = f; }
    else { error = "unknown setting " + key; return false; }
    return true;
}

void PrintParams(const SimParams& p)
{
    std::printf("  species %u, agents %u, seed %u, steps/frame %u, pre-roll %u, boundary %u\n", p.numSpecies,
                p.agentCount, p.seed, p.stepsPerFrame, p.preRoll, p.boundary);
    std::printf("  decay %.3f, diffusion r%u x %.2f, clamp %.2f, exposure %.2f, gamma %.2f, palette %u, background %u\n",
                p.decay, p.diffusionRadius, p.diffusionStrength, p.trailClamp, p.exposure, p.gamma, p.palette,
                p.background);
    for (uint32_t s = 0; s < p.numSpecies; ++s) {
        const SpeciesParams& sp = p.species[s];
        std::printf("  species %u: agents [%u, %u) SA %.1f SD %.1f TA %.1f step %.2f dep %.2f jit %.1f other %.2f spawn %u\n",
                    s, sp.agentBegin, sp.agentEnd, sp.sensorAngle / kDeg, sp.sensorDist, sp.turnAngle / kDeg,
                    sp.stepSize, sp.deposit, sp.jitter / kDeg, sp.otherWeight, sp.spawnPattern);
    }
}

} // namespace phys::cli
