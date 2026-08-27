#include "FieldPatch.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <initializer_list>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <variant>

namespace skewer::core {
namespace {

struct JsonValue;
using JsonObject = std::map<std::string, JsonValue>;
using JsonArray = std::vector<JsonValue>;
struct JsonValue {
    std::variant<std::nullptr_t, std::uint64_t, std::string, JsonObject, JsonArray> value{};
};

class JsonParser final {
public:
    explicit JsonParser(std::string_view input) : input_(input) {}

    JsonValue parse() {
        auto result = parseValue();
        skipSpace();
        if (position_ != input_.size()) fail("Unexpected trailing JSON content");
        return result;
    }

private:
    [[noreturn]] void fail(const char* message) const {
        throw std::runtime_error(std::string(message) + " at byte " + std::to_string(position_));
    }
    void skipSpace() {
        while (position_ < input_.size() && std::isspace(static_cast<unsigned char>(input_[position_])) != 0) ++position_;
    }
    bool consume(char value) {
        skipSpace();
        if (position_ < input_.size() && input_[position_] == value) { ++position_; return true; }
        return false;
    }
    JsonValue parseValue() {
        skipSpace();
        if (position_ >= input_.size()) fail("Expected JSON value");
        if (input_[position_] == '{') return JsonValue{ parseObject() };
        if (input_[position_] == '[') return JsonValue{ parseArray() };
        if (input_[position_] == '"') return JsonValue{ parseString() };
        if (std::isdigit(static_cast<unsigned char>(input_[position_])) != 0) return JsonValue{ parseNumber() };
        if (input_.substr(position_, 4) == "null") { position_ += 4; return JsonValue{ nullptr }; }
        fail("Unsupported JSON value");
    }
    JsonObject parseObject() {
        if (!consume('{')) fail("Expected object");
        JsonObject object{};
        if (consume('}')) return object;
        for (;;) {
            skipSpace();
            if (position_ >= input_.size() || input_[position_] != '"') fail("Expected object member name");
            auto name = parseString();
            if (!consume(':')) fail("Expected colon");
            if (!object.emplace(std::move(name), parseValue()).second) fail("Duplicate object member");
            if (consume('}')) break;
            if (!consume(',')) fail("Expected comma");
        }
        return object;
    }
    JsonArray parseArray() {
        if (!consume('[')) fail("Expected array");
        JsonArray array{};
        if (consume(']')) return array;
        for (;;) {
            array.push_back(parseValue());
            if (consume(']')) break;
            if (!consume(',')) fail("Expected comma");
        }
        return array;
    }
    std::string parseString() {
        if (!consume('"')) fail("Expected string");
        std::string result{};
        while (position_ < input_.size()) {
            const char ch = input_[position_++];
            if (ch == '"') return result;
            if (static_cast<unsigned char>(ch) < 0x20U) fail("Control character in string");
            if (ch != '\\') { result.push_back(ch); continue; }
            if (position_ >= input_.size()) fail("Incomplete escape");
            switch (input_[position_++]) {
            case '"': result.push_back('"'); break;
            case '\\': result.push_back('\\'); break;
            case '/': result.push_back('/'); break;
            case 'b': result.push_back('\b'); break;
            case 'f': result.push_back('\f'); break;
            case 'n': result.push_back('\n'); break;
            case 'r': result.push_back('\r'); break;
            case 't': result.push_back('\t'); break;
            default: fail("Unsupported string escape");
            }
        }
        fail("Unterminated string");
    }
    std::uint64_t parseNumber() {
        const auto begin = position_;
        while (position_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[position_])) != 0) ++position_;
        std::uint64_t result = 0;
        const auto converted = std::from_chars(input_.data() + begin, input_.data() + position_, result);
        if (converted.ec != std::errc{}) fail("Invalid unsigned integer");
        return result;
    }
    std::string_view input_{};
    std::size_t position_ = 0;
};

const JsonObject& object(const JsonValue& value, const char* name) {
    const auto* result = std::get_if<JsonObject>(&value.value);
    if (result == nullptr) throw std::runtime_error(std::string(name) + " must be an object");
    return *result;
}
const JsonArray& array(const JsonValue& value, const char* name) {
    const auto* result = std::get_if<JsonArray>(&value.value);
    if (result == nullptr) throw std::runtime_error(std::string(name) + " must be an array");
    return *result;
}
const JsonValue& member(const JsonObject& value, const char* name) {
    const auto found = value.find(name);
    if (found == value.end()) throw std::runtime_error(std::string("Missing member: ") + name);
    return found->second;
}
std::string string(const JsonValue& value, const char* name) {
    const auto* result = std::get_if<std::string>(&value.value);
    if (result == nullptr) throw std::runtime_error(std::string(name) + " must be a string");
    return *result;
}
std::uint64_t number(const JsonValue& value, const char* name) {
    const auto* result = std::get_if<std::uint64_t>(&value.value);
    if (result == nullptr) throw std::runtime_error(std::string(name) + " must be an unsigned integer");
    return *result;
}
std::optional<std::reference_wrapper<const JsonValue>> optionalMember(const JsonObject& value, const char* name) {
    const auto found = value.find(name);
    if (found == value.end()) return std::nullopt;
    return std::cref(found->second);
}

void requireKeys(const JsonObject& value, std::initializer_list<std::string_view> allowed) {
    for (const auto& [name, ignored] : value) {
        (void)ignored;
        if (std::find(allowed.begin(), allowed.end(), name) == allowed.end()) {
            throw std::runtime_error("Unexpected object member: " + name);
        }
    }
}

std::string escaped(std::string_view value) {
    std::string result{};
    for (const char ch : value) {
        switch (ch) {
        case '"': result += "\\\""; break;
        case '\\': result += "\\\\"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default: result.push_back(ch); break;
        }
    }
    return result;
}

std::string addressText(std::uint32_t address) {
    std::ostringstream output{};
    output << "0x" << std::hex << std::nouppercase << std::setfill('0') << std::setw(8) << address;
    return output.str();
}

std::uint32_t parseAddress(const JsonValue& value) {
    const auto text = string(value, "resourceAddress");
    if (text.size() != 10U || text.substr(0, 2) != "0x") throw std::runtime_error("resourceAddress must be 0x plus eight lowercase hex digits");
    if (!std::all_of(text.begin() + 2, text.end(), [](unsigned char ch) {
        return std::isdigit(ch) != 0 || (ch >= 'a' && ch <= 'f');
    })) throw std::runtime_error("resourceAddress must use lowercase hexadecimal digits");
    std::uint32_t result = 0;
    const auto converted = std::from_chars(text.data() + 2, text.data() + text.size(), result, 16);
    if (converted.ec != std::errc{} || converted.ptr != text.data() + text.size()) throw std::runtime_error("Invalid resourceAddress");
    return result;
}

const char* kindName(EctValueKind kind) {
    switch (kind) {
    case EctValueKind::Stage: return "stage";
    case EctValueKind::OverallEncounterRate: return "overallEncounterRate";
    case EctValueKind::EncounterId: return "encounterId";
    case EctValueKind::Weight: return "weight";
    }
    return "stage";
}

EctValueKind parseKind(const std::string& kind) {
    if (kind == "stage") return EctValueKind::Stage;
    if (kind == "overallEncounterRate") return EctValueKind::OverallEncounterRate;
    if (kind == "encounterId") return EctValueKind::EncounterId;
    if (kind == "weight") return EctValueKind::Weight;
    throw std::runtime_error("Unknown ECT value kind");
}

bool validStem(std::string_view stem) {
    return !stem.empty() && std::all_of(stem.begin(), stem.end(), [](unsigned char ch) {
        return std::islower(ch) != 0 || std::isdigit(ch) != 0 || ch == '_';
    });
}

bool sameKey(const TriangleKey& lhs, const TriangleKey& rhs) {
    const TriangleKeyLess less{};
    return !less(lhs, rhs) && !less(rhs, lhs);
}

} // namespace

bool FieldPatch::empty() const noexcept { return triangleSelectorEdits.empty() && ectValueEdits.empty(); }

FieldPatch makeFieldPatch(const FieldDocument& document,
    std::span<const TriangleSelectorPatchEdit> preservedTriangles,
    std::span<const EctValuePatchEdit> preservedEct) {
    FieldPatch result{};
    result.stem = document.assets.stem;
    std::transform(result.stem.begin(), result.stem.end(), result.stem.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    result.ectFile = result.stem + ".ect";
    result.mldFile = result.stem + ".mld";
    result.triangleSelectorEdits.assign(preservedTriangles.begin(), preservedTriangles.end());
    result.ectValueEdits.assign(preservedEct.begin(), preservedEct.end());
    for (const auto& [key, selector] : document.selectorEdits()) {
        const auto expected = document.baselineSelector(key);
        if (expected.has_value() && *expected <= 8U && *expected != selector) {
            const auto found = std::find_if(result.triangleSelectorEdits.begin(), result.triangleSelectorEdits.end(),
                [&](const auto& edit) { return sameKey(edit.key, key); });
            const TriangleSelectorPatchEdit edit{ key, *expected, selector };
            if (found == result.triangleSelectorEdits.end()) result.triangleSelectorEdits.push_back(edit); else *found = edit;
        }
    }
    for (const auto& [key, value] : document.ectEdits()) {
        const auto expected = document.baselineEctValue(key);
        if (expected.has_value() && *expected != value) {
            const auto found = std::find_if(result.ectValueEdits.begin(), result.ectValueEdits.end(), [&](const auto& edit) { return edit.key == key; });
            const EctValuePatchEdit edit{ key, *expected, value };
            if (found == result.ectValueEdits.end()) result.ectValueEdits.push_back(edit); else *found = edit;
        }
    }
    std::sort(result.triangleSelectorEdits.begin(), result.triangleSelectorEdits.end(), [](const auto& a, const auto& b) { return TriangleKeyLess{}(a.key, b.key); });
    std::sort(result.ectValueEdits.begin(), result.ectValueEdits.end(), [](const auto& a, const auto& b) { return a.key < b.key; });
    return result;
}

std::vector<Diagnostic> validateFieldPatch(const FieldPatch& patch) {
    std::vector<Diagnostic> result{};
    auto error = [&](std::string message) { result.push_back({ DiagnosticSeverity::Error, std::move(message) }); };
    if (!validStem(patch.stem)) error("Patch field stem must be lowercase and contain only letters, digits, or underscore.");
    if (patch.ectFile != patch.stem + ".ect" || patch.mldFile != patch.stem + ".mld") error("Patch source basenames must match the field stem.");
    if (patch.empty()) error("A field patch must contain at least one semantic edit.");
    std::set<TriangleKey, TriangleKeyLess> triangleKeys{};
    for (const auto& edit : patch.triangleSelectorEdits) {
        if (edit.expectedSelector > 8U || edit.selector > 8U) error("Patch selectors must be between 0 and 8; selector 9 is malformed data.");
        if (edit.expectedSelector == edit.selector) error("Patch selector expected and replacement values must differ.");
        if (!triangleKeys.insert(edit.key).second) error("Patch contains a duplicate triangle key.");
    }
    std::set<EctValueKey> ectKeys{};
    for (const auto& edit : patch.ectValueEdits) {
        if (edit.key.tableIndex >= 8U) error("Patch ECT table number must be between 1 and 8.");
        if ((edit.key.kind == EctValueKind::EncounterId || edit.key.kind == EctValueKind::Weight) && edit.key.rowIndex >= 32U) error("Patch ECT row must be between 0 and 31.");
        if (edit.expected == edit.value) error("Patch ECT expected and replacement values must differ.");
        if (!ectKeys.insert(edit.key).second) error("Patch contains a duplicate ECT value key.");
    }
    return result;
}

PatchRestoreResult restoreFieldPatch(FieldDocument& document, const FieldPatch& patch) {
    PatchRestoreResult result{};
    result.diagnostics = validateFieldPatch(patch);
    if (patch.stem != document.assets.stem) result.diagnostics.push_back({ DiagnosticSeverity::Error, "Patch field stem does not match the loaded field." });
    if (hasErrors(result.diagnostics)) return result;
    for (const auto& edit : patch.triangleSelectorEdits) {
        const auto current = document.baselineSelector(edit.key);
        if (!current.has_value()) {
            result.preservedTriangleEdits.push_back(edit);
            result.conflicts.push_back({ edit, std::nullopt, 0, PatchEntryState::Unresolved, "Triangle key no longer resolves." });
        } else if (*current == edit.selector) {
            result.preservedTriangleEdits.push_back(edit);
            ++result.alreadyAppliedCount;
        } else if (*current != edit.expectedSelector) {
            result.preservedTriangleEdits.push_back(edit);
            result.conflicts.push_back({ edit, std::nullopt, *current, PatchEntryState::Conflict, "Triangle expected selector differs from current source." });
        } else {
            const std::array<TriangleKey, 1> keys{ edit.key };
            const auto applied = document.setTriangleSelectors(keys, edit.selector, "Restore selector patch");
            result.diagnostics.insert(result.diagnostics.end(), applied.diagnostics.begin(), applied.diagnostics.end());
            if (applied.changed) ++result.appliedCount;
        }
    }
    for (const auto& edit : patch.ectValueEdits) {
        const auto current = document.baselineEctValue(edit.key);
        if (!current.has_value()) {
            result.preservedEctEdits.push_back(edit);
            result.conflicts.push_back({ std::nullopt, edit, 0, PatchEntryState::Unresolved, "ECT key no longer resolves." });
        } else if (*current == edit.value) {
            result.preservedEctEdits.push_back(edit);
            ++result.alreadyAppliedCount;
        } else if (*current != edit.expected) {
            result.preservedEctEdits.push_back(edit);
            result.conflicts.push_back({ std::nullopt, edit, *current, PatchEntryState::Conflict, "ECT expected value differs from current source." });
        } else {
            const auto applied = document.setEctValue(edit.key, edit.value, "Restore ECT patch");
            result.diagnostics.insert(result.diagnostics.end(), applied.diagnostics.begin(), applied.diagnostics.end());
            if (applied.changed) ++result.appliedCount;
        }
    }
    document.clearHistory();
    return result;
}

std::string serializeFieldPatch(const FieldPatch& source) {
    auto patch = source;
    std::sort(patch.triangleSelectorEdits.begin(), patch.triangleSelectorEdits.end(), [](const auto& a, const auto& b) { return TriangleKeyLess{}(a.key, b.key); });
    std::sort(patch.ectValueEdits.begin(), patch.ectValueEdits.end(), [](const auto& a, const auto& b) { return a.key < b.key; });
    std::ostringstream out{};
    out << "{\n  \"format\": \"skewer-field-patch\",\n  \"version\": 1,\n  \"field\": {\n"
        << "    \"stem\": \"" << escaped(patch.stem) << "\",\n"
        << "    \"platform\": \"dreamcast\",\n"
        << "    \"ectFile\": \"" << escaped(patch.ectFile) << "\",\n"
        << "    \"mldFile\": \"" << escaped(patch.mldFile) << "\"\n  },\n  \"mld\": {\n    \"triangleSelectorEdits\": [";
    for (std::size_t index = 0; index < patch.triangleSelectorEdits.size(); ++index) {
        const auto& edit = patch.triangleSelectorEdits[index];
        out << (index == 0 ? "\n" : ",\n") << "      {\n        \"key\": {\n";
        if (const auto* grnd = std::get_if<GrndTriangleKey>(&edit.key)) {
            out << "          \"kind\": \"grnd\",\n          \"resourceAddress\": \"" << addressText(grnd->resourceAddress)
                << "\",\n          \"triangleIndex\": " << grnd->triangleIndex << "\n";
        } else {
            const auto& gobj = std::get<GobjTriangleKey>(edit.key);
            out << "          \"kind\": \"gobj\",\n          \"resourceAddress\": \"" << addressText(gobj.resourceAddress)
                << "\",\n          \"nodeIndex\": " << gobj.nodeIndex << ",\n          \"triangleIndex\": " << gobj.triangleIndex << "\n";
        }
        out << "        },\n        \"expectedSelector\": " << static_cast<unsigned>(edit.expectedSelector)
            << ",\n        \"selector\": " << static_cast<unsigned>(edit.selector) << "\n      }";
    }
    if (!patch.triangleSelectorEdits.empty()) out << '\n';
    out << "    ]\n  },\n  \"ect\": {\n    \"valueEdits\": [";
    for (std::size_t index = 0; index < patch.ectValueEdits.size(); ++index) {
        const auto& edit = patch.ectValueEdits[index];
        out << (index == 0 ? "\n" : ",\n") << "      {\n        \"table\": " << edit.key.tableIndex + 1U
            << ",\n        \"kind\": \"" << kindName(edit.key.kind) << "\"";
        if (edit.key.kind == EctValueKind::EncounterId || edit.key.kind == EctValueKind::Weight) out << ",\n        \"row\": " << edit.key.rowIndex;
        out << ",\n        \"expected\": " << edit.expected << ",\n        \"value\": " << edit.value << "\n      }";
    }
    if (!patch.ectValueEdits.empty()) out << '\n';
    out << "    ]\n  }\n}\n";
    return out.str();
}

FieldPatchReadResult parseFieldPatch(std::string_view json, const std::filesystem::path& sourcePath) {
    FieldPatchReadResult result{};
    try {
        const auto root = object(JsonParser(json).parse(), "root");
        requireKeys(root, { "format", "version", "field", "mld", "ect" });
        if (string(member(root, "format"), "format") != "skewer-field-patch" || number(member(root, "version"), "version") != 1U) throw std::runtime_error("Unsupported field patch format or version");
        const auto& field = object(member(root, "field"), "field");
        requireKeys(field, { "stem", "platform", "ectFile", "mldFile" });
        if (string(member(field, "platform"), "platform") != "dreamcast") throw std::runtime_error("Only Dreamcast field patches are supported");
        FieldPatch patch{};
        patch.stem = string(member(field, "stem"), "stem");
        patch.ectFile = string(member(field, "ectFile"), "ectFile");
        patch.mldFile = string(member(field, "mldFile"), "mldFile");
        const auto& mld = object(member(root, "mld"), "mld");
        requireKeys(mld, { "triangleSelectorEdits" });
        for (const auto& value : array(member(mld, "triangleSelectorEdits"), "triangleSelectorEdits")) {
            const auto& editObject = object(value, "triangle edit");
            requireKeys(editObject, { "key", "expectedSelector", "selector" });
            const auto& keyObject = object(member(editObject, "key"), "key");
            const auto kind = string(member(keyObject, "kind"), "kind");
            if (kind == "grnd") requireKeys(keyObject, { "kind", "resourceAddress", "triangleIndex" });
            else if (kind == "gobj") requireKeys(keyObject, { "kind", "resourceAddress", "nodeIndex", "triangleIndex" });
            const auto address = parseAddress(member(keyObject, "resourceAddress"));
            const auto triangle = static_cast<std::size_t>(number(member(keyObject, "triangleIndex"), "triangleIndex"));
            TriangleKey key{};
            if (kind == "grnd") key = GrndTriangleKey{ address, triangle };
            else if (kind == "gobj") key = GobjTriangleKey{ address, static_cast<std::size_t>(number(member(keyObject, "nodeIndex"), "nodeIndex")), triangle };
            else throw std::runtime_error("Triangle key kind must be grnd or gobj");
            const auto expected = number(member(editObject, "expectedSelector"), "expectedSelector");
            const auto selector = number(member(editObject, "selector"), "selector");
            if (expected > 8U || selector > 8U) throw std::runtime_error("Patch selectors must be between 0 and 8; selector 9 is malformed data");
            patch.triangleSelectorEdits.push_back({ key, static_cast<std::uint8_t>(expected), static_cast<std::uint8_t>(selector) });
        }
        const auto& ect = object(member(root, "ect"), "ect");
        requireKeys(ect, { "valueEdits" });
        for (const auto& value : array(member(ect, "valueEdits"), "valueEdits")) {
            const auto& editObject = object(value, "ECT edit");
            requireKeys(editObject, { "table", "kind", "row", "expected", "value" });
            EctValueKey key{};
            const auto table = number(member(editObject, "table"), "table");
            if (table == 0U || table > 8U) throw std::runtime_error("ECT table numbers must be between 1 and 8");
            key.tableIndex = static_cast<std::size_t>(table - 1U);
            key.kind = parseKind(string(member(editObject, "kind"), "kind"));
            const auto row = optionalMember(editObject, "row");
            if (row) key.rowIndex = static_cast<std::size_t>(number(row->get(), "row"));
            const bool rowKind = key.kind == EctValueKind::EncounterId || key.kind == EctValueKind::Weight;
            if (rowKind != row.has_value()) throw std::runtime_error("ECT row is required only for encounterId and weight edits");
            if ((key.kind == EctValueKind::EncounterId || key.kind == EctValueKind::Weight) && key.rowIndex >= 32U) throw std::runtime_error("ECT row must be between 0 and 31");
            const auto expected = number(member(editObject, "expected"), "expected");
            const auto replacement = number(member(editObject, "value"), "value");
            if (expected > 65535U || replacement > 65535U) throw std::runtime_error("ECT values must be between 0 and 65535");
            patch.ectValueEdits.push_back({ key, static_cast<std::uint16_t>(expected), static_cast<std::uint16_t>(replacement) });
        }
        result.diagnostics = validateFieldPatch(patch);
        if (!hasErrors(result.diagnostics)) result.patch = std::move(patch);
    } catch (const std::exception& exception) {
        result.diagnostics.push_back({ DiagnosticSeverity::Error, std::string("Invalid field patch JSON: ") + exception.what(), sourcePath });
    }
    return result;
}

FieldPatchStore::FieldPatchStore(std::filesystem::path workspaceDirectory) : workspaceDirectory_(std::move(workspaceDirectory)) {}
std::filesystem::path FieldPatchStore::patchesDirectory() const { return workspaceDirectory_ / "patches"; }
std::filesystem::path FieldPatchStore::patchPath(std::string_view stem) const { return patchesDirectory() / (std::string(stem) + ".skewer.patch.json"); }

FieldPatchReadResult FieldPatchStore::load(std::string_view stem) const {
    const auto path = patchPath(stem);
    std::ifstream input(path, std::ios::binary);
    if (!input) return { std::nullopt, { { DiagnosticSeverity::Error, "Could not open field patch.", path } } };
    std::string json((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    return parseFieldPatch(json, path);
}

std::vector<std::string> FieldPatchStore::listPatchStems(std::vector<Diagnostic>& diagnostics) const {
    std::vector<std::string> result{};
    std::error_code error{};
    if (!std::filesystem::exists(patchesDirectory(), error)) return result;
    for (std::filesystem::directory_iterator it(patchesDirectory(), error), end; !error && it != end; it.increment(error)) {
        if (!it->is_regular_file()) continue;
        const auto filename = it->path().filename().string();
        constexpr std::string_view suffix = ".skewer.patch.json";
        if (filename.size() > suffix.size() && filename.ends_with(suffix)) result.push_back(filename.substr(0, filename.size() - suffix.size()));
    }
    if (error) diagnostics.push_back({ DiagnosticSeverity::Error, "Could not enumerate workspace patches.", patchesDirectory() });
    std::sort(result.begin(), result.end());
    return result;
}

bool FieldPatchStore::save(const FieldPatch& patch, std::vector<Diagnostic>& diagnostics) const {
    const auto validation = validateFieldPatch(patch);
    diagnostics.insert(diagnostics.end(), validation.begin(), validation.end());
    if (hasErrors(validation)) return false;
    std::error_code error{};
    std::filesystem::create_directories(patchesDirectory(), error);
    if (error) { diagnostics.push_back({ DiagnosticSeverity::Error, "Could not create the patches directory.", patchesDirectory() }); return false; }
    const auto destination = patchPath(patch.stem);
    const auto temporary = destination.string() + ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        const auto json = serializeFieldPatch(patch);
        output.write(json.data(), static_cast<std::streamsize>(json.size()));
        if (!output) { diagnostics.push_back({ DiagnosticSeverity::Error, "Could not write the temporary patch file.", temporary }); return false; }
    }
    const auto backup = destination.string() + ".bak";
    const bool existed = std::filesystem::exists(destination, error);
    error.clear();
    if (existed) {
        std::filesystem::remove(backup, error);
        error.clear();
        std::filesystem::rename(destination, backup, error);
        if (error) { diagnostics.push_back({ DiagnosticSeverity::Error, "Could not stage the previous patch for replacement.", destination }); std::filesystem::remove(temporary); return false; }
    }
    std::filesystem::rename(temporary, destination, error);
    if (error) {
        diagnostics.push_back({ DiagnosticSeverity::Error, "Could not publish the field patch atomically.", destination });
        if (existed) { std::error_code restoreError{}; std::filesystem::rename(backup, destination, restoreError); }
        std::filesystem::remove(temporary); return false;
    }
    if (existed) std::filesystem::remove(backup, error);
    return true;
}

bool FieldPatchStore::remove(std::string_view stem, std::vector<Diagnostic>& diagnostics) const {
    std::error_code error{};
    std::filesystem::remove(patchPath(stem), error);
    if (error) { diagnostics.push_back({ DiagnosticSeverity::Error, "Could not remove the empty field patch.", patchPath(stem) }); return false; }
    return true;
}

} // namespace skewer::core
