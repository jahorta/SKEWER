#include "FieldLoader.h"

#include "SPICE/Compression/Aklz.h"
#include "SPICE/SpiceEct/EctParser.h"
#include "SPICE/SpiceMLD/Parsing/MldParser.h"

#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <utility>
#include <variant>

namespace skewer::core {
namespace {

[[nodiscard]] std::optional<std::vector<std::uint8_t>> readBinary(
    const std::filesystem::path& path,
    std::vector<Diagnostic>& diagnostics) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        diagnostics.push_back({ DiagnosticSeverity::Error, "Could not open the file for reading.", path });
        return std::nullopt;
    }
    input.seekg(0, std::ios::end);
    const auto length = input.tellg();
    if (length < 0) {
        diagnostics.push_back({ DiagnosticSeverity::Error, "Could not determine the file size.", path });
        return std::nullopt;
    }
    input.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    if (!bytes.empty()) {
        input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    if (!input && !bytes.empty()) {
        diagnostics.push_back({ DiagnosticSeverity::Error, "The file could not be read completely.", path });
        return std::nullopt;
    }
    return bytes;
}

[[nodiscard]] DiagnosticSeverity mapMldSeverity(const spice::mld::model::MldDiagnostic::Severity severity) {
    using Source = spice::mld::model::MldDiagnostic::Severity;
    switch (severity) {
    case Source::Error: return DiagnosticSeverity::Error;
    case Source::Warning: return DiagnosticSeverity::Warning;
    case Source::Info: return DiagnosticSeverity::Info;
    }
    return DiagnosticSeverity::Info;
}

[[nodiscard]] DiagnosticSeverity mapEctSeverity(const spice::ect::DiagnosticSeverity severity) {
    using Source = spice::ect::DiagnosticSeverity;
    switch (severity) {
    case Source::Error: return DiagnosticSeverity::Error;
    case Source::Warning: return DiagnosticSeverity::Warning;
    case Source::Info: return DiagnosticSeverity::Info;
    }
    return DiagnosticSeverity::Info;
}

} // namespace

FieldLoadResult FieldLoader::load(const FieldAssetPair& assets) {
    FieldLoadResult result{};

    const auto ectBytes = readBinary(assets.ectPath, result.diagnostics);
    const auto mldBytes = readBinary(assets.mldPath, result.diagnostics);
    if (!ectBytes.has_value() || !mldBytes.has_value()) {
        return result;
    }
    if (spice::compression::aklz::isAklz(*ectBytes)) {
        result.diagnostics.push_back({ DiagnosticSeverity::Error, "GameCube is not yet supported.", assets.ectPath });
        return result;
    }

    auto ectResult = spice::ect::EctParser::parseFile(assets.ectPath);
    for (const auto& diagnostic : ectResult.diagnostics) {
        result.diagnostics.push_back({ mapEctSeverity(diagnostic.severity), diagnostic.message, assets.ectPath });
    }
    if (!ectResult.file.has_value()) {
        if (!hasErrors(result.diagnostics)) {
            result.diagnostics.push_back({ DiagnosticSeverity::Error, "ECT parsing failed.", assets.ectPath });
        }
        return result;
    }
    const auto* flat = std::get_if<spice::ect::EctFlatContent>(&ectResult.file->content);
    if (flat == nullptr) {
        result.diagnostics.push_back({ DiagnosticSeverity::Error,
            "Area 99 uses the indexed overworld ECT layout and is deferred.", assets.ectPath });
        return result;
    }
    if (flat->tables.size() != spice::ect::kOverworldTablesPerEntry) {
        result.diagnostics.push_back({ DiagnosticSeverity::Error,
            "The ECT file must contain exactly 8 encounter tables.", assets.ectPath });
        return result;
    }

    spice::mld::parsing::MldParser mldParser{};
    auto mld = mldParser.parseBytes(std::span<const std::uint8_t>(*mldBytes));
    for (const auto& diagnostic : mld.parseDiagnostics) {
        result.diagnostics.push_back({ mapMldSeverity(diagnostic.severity), diagnostic.message, assets.mldPath });
    }
    if (mld.sourceWasCompressedAklz || mld.sourcePlatform == spice::mld::model::TargetPlatform::GameCube) {
        result.diagnostics.push_back({ DiagnosticSeverity::Error, "GameCube is not yet supported.", assets.mldPath });
        return result;
    }
    if (mld.parseStatus == spice::mld::model::MldParseStatus::Failed ||
        mld.parseStatus == spice::mld::model::MldParseStatus::Empty) {
        if (!hasErrors(result.diagnostics)) {
            result.diagnostics.push_back({ DiagnosticSeverity::Error, "MLD parsing failed.", assets.mldPath });
        }
        return result;
    }
    if (mld.sourcePlatform != spice::mld::model::TargetPlatform::Dreamcast) {
        result.diagnostics.push_back({ DiagnosticSeverity::Error,
            "The MLD platform could not be confirmed as Dreamcast.", assets.mldPath });
        return result;
    }

    auto scene = buildSceneModel(mld, result.diagnostics);
    if (hasErrors(result.diagnostics)) {
        return result;
    }

    FieldDocument document{};
    document.assets = assets;
    document.mld = std::move(mld);
    document.ect = std::move(*ectResult.file);
    document.scene = std::move(scene);
    document.diagnostics = result.diagnostics;
    result.document = std::move(document);
    return result;
}

} // namespace skewer::core
