#include "ExportService.h"

#include "FieldLoader.h"
#include "SceneModel.h"

#include "SPICE/SpiceEct/EctFileWriter.h"
#include "SPICE/SpiceEct/EctParser.h"
#include "SPICE/SpiceMLD/Parsing/MldParser.h"
#include "SPICE/SpiceMLD/Patching/DreamcastTrianglePatcher.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#include <variant>

namespace skewer::core {
namespace {

using spice::mld::patching::DreamcastTriangleSelectorEdit;
using spice::mld::patching::TriangleResourceKind;

DiagnosticSeverity mapMld(spice::mld::model::MldDiagnostic::Severity severity) {
    using Source = spice::mld::model::MldDiagnostic::Severity;
    if (severity == Source::Error) return DiagnosticSeverity::Error;
    if (severity == Source::Warning) return DiagnosticSeverity::Warning;
    return DiagnosticSeverity::Info;
}

DiagnosticSeverity mapEct(spice::ect::DiagnosticSeverity severity) {
    if (severity == spice::ect::DiagnosticSeverity::Error) return DiagnosticSeverity::Error;
    if (severity == spice::ect::DiagnosticSeverity::Warning) return DiagnosticSeverity::Warning;
    return DiagnosticSeverity::Info;
}

std::string keyDescription(const TriangleKey& key) {
    std::ostringstream out{};
    if (const auto* grnd = std::get_if<GrndTriangleKey>(&key)) {
        out << "GRND 0x" << std::hex << grnd->resourceAddress << std::dec << " triangle " << grnd->triangleIndex;
    } else {
        const auto& gobj = std::get<GobjTriangleKey>(key);
        out << "GOBJ 0x" << std::hex << gobj.resourceAddress << std::dec << " node " << gobj.nodeIndex << " triangle " << gobj.triangleIndex;
    }
    return out.str();
}

std::string ectDescription(const EctValueKey& key) {
    const char* kind = "stage";
    if (key.kind == EctValueKind::OverallEncounterRate) kind = "overall encounter rate";
    else if (key.kind == EctValueKind::EncounterId) kind = "encounter ID";
    else if (key.kind == EctValueKind::Weight) kind = "weight";
    std::string result = "table " + std::to_string(key.tableIndex + 1U) + " " + kind;
    if (key.kind == EctValueKind::EncounterId || key.kind == EctValueKind::Weight) result += " row " + std::to_string(key.rowIndex);
    return result;
}

DreamcastTriangleSelectorEdit toSpiceEdit(const TriangleKey& key, std::uint8_t selector) {
    if (const auto* grnd = std::get_if<GrndTriangleKey>(&key)) {
        return { TriangleResourceKind::Grnd, grnd->resourceAddress, std::nullopt, grnd->triangleIndex, selector };
    }
    const auto& gobj = std::get<GobjTriangleKey>(key);
    return { TriangleResourceKind::Gobj, gobj.resourceAddress, gobj.nodeIndex, gobj.triangleIndex, selector };
}

std::string jsonEscape(std::string_view value) {
    std::string result{};
    for (char ch : value) {
        if (ch == '"' || ch == '\\') result.push_back('\\');
        if (ch == '\n') result += "\\n"; else result.push_back(ch);
    }
    return result;
}

std::string timestamp() {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_s(&tm, &time);
    std::ostringstream out{};
    out << std::put_time(&tm, "%Y%m%d-%H%M%S");
    return out.str();
}

bool writeBytes(const std::filesystem::path& path, std::span<const std::uint8_t> bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(output);
}

std::vector<std::uint8_t> readBytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return { std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
}

std::string sha256(std::span<const std::uint8_t> input) {
    constexpr std::array<std::uint32_t, 64> constants{
        0x428a2f98U,0x71374491U,0xb5c0fbcfU,0xe9b5dba5U,0x3956c25bU,0x59f111f1U,0x923f82a4U,0xab1c5ed5U,
        0xd807aa98U,0x12835b01U,0x243185beU,0x550c7dc3U,0x72be5d74U,0x80deb1feU,0x9bdc06a7U,0xc19bf174U,
        0xe49b69c1U,0xefbe4786U,0x0fc19dc6U,0x240ca1ccU,0x2de92c6fU,0x4a7484aaU,0x5cb0a9dcU,0x76f988daU,
        0x983e5152U,0xa831c66dU,0xb00327c8U,0xbf597fc7U,0xc6e00bf3U,0xd5a79147U,0x06ca6351U,0x14292967U,
        0x27b70a85U,0x2e1b2138U,0x4d2c6dfcU,0x53380d13U,0x650a7354U,0x766a0abbU,0x81c2c92eU,0x92722c85U,
        0xa2bfe8a1U,0xa81a664bU,0xc24b8b70U,0xc76c51a3U,0xd192e819U,0xd6990624U,0xf40e3585U,0x106aa070U,
        0x19a4c116U,0x1e376c08U,0x2748774cU,0x34b0bcb5U,0x391c0cb3U,0x4ed8aa4aU,0x5b9cca4fU,0x682e6ff3U,
        0x748f82eeU,0x78a5636fU,0x84c87814U,0x8cc70208U,0x90befffaU,0xa4506cebU,0xbef9a3f7U,0xc67178f2U,
    };
    auto rotate = [](std::uint32_t value, unsigned amount) { return (value >> amount) | (value << (32U - amount)); };
    std::vector<std::uint8_t> data(input.begin(), input.end());
    const auto bitLength = static_cast<std::uint64_t>(data.size()) * 8U;
    data.push_back(0x80U);
    while ((data.size() % 64U) != 56U) data.push_back(0U);
    for (int shift = 56; shift >= 0; shift -= 8) data.push_back(static_cast<std::uint8_t>(bitLength >> shift));
    std::array<std::uint32_t, 8> hash{ 0x6a09e667U,0xbb67ae85U,0x3c6ef372U,0xa54ff53aU,0x510e527fU,0x9b05688cU,0x1f83d9abU,0x5be0cd19U };
    for (std::size_t offset = 0; offset < data.size(); offset += 64U) {
        std::array<std::uint32_t, 64> words{};
        for (std::size_t index = 0; index < 16U; ++index) {
            const auto at = offset + index * 4U;
            words[index] = (static_cast<std::uint32_t>(data[at]) << 24U) |
                (static_cast<std::uint32_t>(data[at + 1U]) << 16U) |
                (static_cast<std::uint32_t>(data[at + 2U]) << 8U) | data[at + 3U];
        }
        for (std::size_t index = 16U; index < 64U; ++index) {
            const auto s0 = rotate(words[index - 15U], 7U) ^ rotate(words[index - 15U], 18U) ^ (words[index - 15U] >> 3U);
            const auto s1 = rotate(words[index - 2U], 17U) ^ rotate(words[index - 2U], 19U) ^ (words[index - 2U] >> 10U);
            words[index] = words[index - 16U] + s0 + words[index - 7U] + s1;
        }
        auto a=hash[0]; auto b=hash[1]; auto c=hash[2]; auto d=hash[3]; auto e=hash[4]; auto f=hash[5]; auto g=hash[6]; auto h=hash[7];
        for (std::size_t index = 0; index < 64U; ++index) {
            const auto s1 = rotate(e, 6U) ^ rotate(e, 11U) ^ rotate(e, 25U);
            const auto choose = (e & f) ^ (~e & g);
            const auto temp1 = h + s1 + choose + constants[index] + words[index];
            const auto s0 = rotate(a, 2U) ^ rotate(a, 13U) ^ rotate(a, 22U);
            const auto majority = (a & b) ^ (a & c) ^ (b & c);
            const auto temp2 = s0 + majority;
            h=g; g=f; f=e; e=d+temp1; d=c; c=b; b=a; a=temp1+temp2;
        }
        hash[0]+=a; hash[1]+=b; hash[2]+=c; hash[3]+=d; hash[4]+=e; hash[5]+=f; hash[6]+=g; hash[7]+=h;
    }
    std::ostringstream out{};
    out << std::hex << std::setfill('0');
    for (const auto value : hash) out << std::setw(8) << value;
    return out.str();
}

} // namespace

ExportPreflightResult ExportService::preflight(const std::filesystem::path& fieldDirectory,
    std::span<const FieldPatch> patches) {
    ExportPreflightResult result{};
    if (patches.empty()) {
        result.diagnostics.push_back({ DiagnosticSeverity::Error, "No workspace patches were selected for export." });
        return result;
    }
    std::set<std::string> basenames{};
    for (const auto& patch : patches) {
        const auto validation = validateFieldPatch(patch);
        result.diagnostics.insert(result.diagnostics.end(), validation.begin(), validation.end());
        if (hasErrors(validation)) continue;
        FieldAssetPair assets{ patch.stem, fieldDirectory / patch.ectFile, fieldDirectory / patch.mldFile };
        auto loaded = FieldLoader::load(assets);
        result.diagnostics.insert(result.diagnostics.end(), loaded.diagnostics.begin(), loaded.diagnostics.end());
        if (!loaded.ok()) continue;
        auto document = std::move(*loaded.document);
        if (document.readOnly) {
            result.diagnostics.push_back({ DiagnosticSeverity::Error, document.readOnlyReason, assets.mldPath });
            continue;
        }
        const auto restored = restoreFieldPatch(document, patch);
        result.diagnostics.insert(result.diagnostics.end(), restored.diagnostics.begin(), restored.diagnostics.end());
        for (const auto& conflict : restored.conflicts) {
            result.diagnostics.push_back({ DiagnosticSeverity::Error,
                patch.stem + ": " + conflict.message + " Current source remains active until the patch is explicitly rebased." });
        }
        if (!restored.conflicts.empty() || hasErrors(restored.diagnostics)) continue;
        for (const auto& edit : restored.preservedTriangleEdits) {
            result.alreadyAppliedEntries.push_back(patch.stem + ": " + keyDescription(edit.key));
        }
        for (const auto& edit : restored.preservedEctEdits) {
            result.alreadyAppliedEntries.push_back(patch.stem + ": " + ectDescription(edit.key));
        }

        if (!document.ectEdits().empty()) {
            spice::ect::EctFileWriter writer{};
            auto written = writer.write(document.workingEct, spice::ect::EctTargetPlatform::Dreamcast);
            for (const auto& diagnostic : written.diagnostics) result.diagnostics.push_back({ mapEct(diagnostic.severity), patch.stem + ": " + diagnostic.message, assets.ectPath });
            if (!written.ok()) continue;
            const auto reparsed = spice::ect::EctParser::parse(written.bytes, spice::ect::EctLayout::Flat);
            for (const auto& diagnostic : reparsed.diagnostics) result.diagnostics.push_back({ mapEct(diagnostic.severity), patch.stem + ": candidate ECT: " + diagnostic.message });
            if (!reparsed.ok() || *reparsed.file != document.workingEct) {
                result.diagnostics.push_back({ DiagnosticSeverity::Error, patch.stem + ": candidate ECT did not round-trip to the working model." });
            } else if (!basenames.insert(patch.ectFile).second) {
                result.diagnostics.push_back({ DiagnosticSeverity::Error, "Selected patches produce duplicate output basenames." });
            } else {
                result.assets.push_back({ patch.stem, assets.ectPath, patch.ectFile,
                    std::move(written.bytes), sha256(readBytes(assets.ectPath)) });
            }
        }

        if (!document.selectorEdits().empty()) {
            std::vector<DreamcastTriangleSelectorEdit> edits{};
            for (const auto& [key, selector] : document.selectorEdits()) edits.push_back(toSpiceEdit(key, selector));
            const auto plan = spice::mld::patching::planDreamcastTriangleSelectorPatches(document.mld, edits);
            for (const auto& diagnostic : plan.diagnostics) result.diagnostics.push_back({ mapMld(diagnostic.severity), patch.stem + ": " + diagnostic.message, assets.mldPath });
            if (!plan.ok()) continue;
            auto bytes = document.mld.sourceBytes;
            const auto applied = spice::mld::patching::applyMldPatchPlan(bytes, plan);
            for (const auto& diagnostic : applied.diagnostics) result.diagnostics.push_back({ mapMld(diagnostic.severity), patch.stem + ": " + diagnostic.message, assets.mldPath });
            if (!applied.ok()) continue;
            spice::mld::parsing::MldParser parser{};
            const auto candidate = parser.parseBytes(bytes);
            std::vector<Diagnostic> candidateDiagnostics{};
            auto scene = buildSceneModel(candidate, candidateDiagnostics);
            result.diagnostics.insert(result.diagnostics.end(), candidateDiagnostics.begin(), candidateDiagnostics.end());
            bool matches = candidate.sourcePlatform == spice::mld::model::TargetPlatform::Dreamcast;
            for (const auto& [key, selector] : document.selectorEdits()) {
                const auto found = std::find_if(scene.triangles.begin(), scene.triangles.end(), [&](const SceneTriangle& triangle) {
                    const TriangleKeyLess less{}; return !less(triangle.key, key) && !less(key, triangle.key);
                });
                matches = matches && found != scene.triangles.end() && found->selector == selector;
            }
            if (!matches) {
                result.diagnostics.push_back({ DiagnosticSeverity::Error, patch.stem + ": candidate MLD did not reparse with all requested selectors." });
            } else if (!basenames.insert(patch.mldFile).second) {
                result.diagnostics.push_back({ DiagnosticSeverity::Error, "Selected patches produce duplicate output basenames." });
            } else {
                result.assets.push_back({ patch.stem, assets.mldPath, patch.mldFile, std::move(bytes), sha256(document.mld.sourceBytes) });
            }
        }
    }
    if (hasErrors(result.diagnostics)) result.assets.clear();
    return result;
}

ExportPublicationResult ExportService::publish(const ExportPreflightResult& plan,
    const std::filesystem::path& destinationDirectory) {
    ExportPublicationResult result{};
    if (!plan.ok() || plan.assets.empty()) {
        result.diagnostics.push_back({ DiagnosticSeverity::Error, "Cannot publish an invalid or empty export preflight plan." });
        return result;
    }
    std::error_code error{};
    std::filesystem::create_directories(destinationDirectory, error);
    if (error) { result.diagnostics.push_back({ DiagnosticSeverity::Error, "Could not create the export directory.", destinationDirectory }); return result; }
    const auto stamp = timestamp();
    const auto staging = destinationDirectory / (".skewer-staging-" + stamp);
    const auto backup = staging / "backup";
    std::filesystem::create_directories(backup, error);
    if (error) { result.diagnostics.push_back({ DiagnosticSeverity::Error, "Could not create export staging.", staging }); return result; }

    std::vector<std::filesystem::path> movedBackups{};
    std::vector<std::filesystem::path> published{};
    auto rollback = [&]() {
        std::error_code ignored{};
        for (const auto& path : published) std::filesystem::remove(path, ignored);
        for (const auto& name : movedBackups) std::filesystem::rename(backup / name, destinationDirectory / name, ignored);
        std::filesystem::remove_all(staging, ignored);
    };
    for (const auto& asset : plan.assets) {
        if (!writeBytes(staging / asset.basename, asset.bytes)) {
            result.diagnostics.push_back({ DiagnosticSeverity::Error, "Could not write a staged export file.", staging / asset.basename });
            rollback(); return result;
        }
    }
    for (const auto& asset : plan.assets) {
        const auto destination = destinationDirectory / asset.basename;
        if (std::filesystem::exists(destination, error)) {
            std::filesystem::rename(destination, backup / asset.basename, error);
            if (error) { result.diagnostics.push_back({ DiagnosticSeverity::Error, "Could not back up an existing destination file.", destination }); rollback(); return result; }
            movedBackups.push_back(asset.basename);
        }
        std::filesystem::rename(staging / asset.basename, destination, error);
        if (error) { result.diagnostics.push_back({ DiagnosticSeverity::Error, "Could not publish a staged export file.", destination }); rollback(); return result; }
        published.push_back(destination);
    }

    const auto receiptName = std::filesystem::path("skewer-export-" + stamp + ".json");
    std::ostringstream receipt{};
    receipt << "{\n  \"format\": \"skewer-export-receipt\",\n  \"version\": 1,\n  \"timestamp\": \"" << stamp
        << "\",\n  \"destination\": \"" << jsonEscape(destinationDirectory.generic_string()) << "\",\n  \"files\": [";
    for (std::size_t index = 0; index < plan.assets.size(); ++index) {
        const auto& asset = plan.assets[index];
        receipt << (index == 0 ? "\n" : ",\n") << "    { \"field\": \"" << jsonEscape(asset.fieldStem)
            << "\", \"file\": \"" << jsonEscape(asset.basename.generic_string()) << "\", \"bytes\": " << asset.bytes.size()
            << ", \"sourceSha256\": \"" << asset.sourceSha256 << "\", \"outputSha256\": \"" << sha256(asset.bytes) << "\" }";
    }
    receipt << "\n  ],\n  \"alreadyApplied\": [";
    for (std::size_t index = 0; index < plan.alreadyAppliedEntries.size(); ++index) {
        receipt << (index == 0 ? "\n" : ",\n") << "    \"" << jsonEscape(plan.alreadyAppliedEntries[index]) << "\"";
    }
    if (!plan.alreadyAppliedEntries.empty()) receipt << '\n';
    receipt << "  ],\n  \"warnings\": [";
    bool firstWarning = true;
    for (const auto& diagnostic : plan.diagnostics) {
        if (diagnostic.severity != DiagnosticSeverity::Warning) continue;
        receipt << (firstWarning ? "\n" : ",\n") << "    \"" << jsonEscape(diagnostic.message) << "\"";
        firstWarning = false;
    }
    if (!firstWarning) receipt << '\n';
    receipt << "  ],\n  \"publicationResult\": \"success\"\n}\n";
    const auto receiptText = receipt.str();
    if (!writeBytes(destinationDirectory / receiptName,
        std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(receiptText.data()), receiptText.size()))) {
        result.diagnostics.push_back({ DiagnosticSeverity::Warning, "Export succeeded, but the receipt could not be written.", destinationDirectory / receiptName });
    } else {
        result.receiptPath = destinationDirectory / receiptName;
    }
    std::filesystem::remove_all(staging, error);
    result.publishedFiles = std::move(published);
    return result;
}

} // namespace skewer::core
