#include "FieldDiscovery.h"

#include "SPICE/Compression/Aklz.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <map>
#include <set>
#include <span>

namespace skewer::core {
namespace {

[[nodiscard]] std::string fold(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

[[nodiscard]] bool equalsFolded(const std::string& lhs, const std::string& rhs) {
    return fold(lhs) == fold(rhs);
}

[[nodiscard]] bool naturalLess(const std::string& lhs, const std::string& rhs) {
    const auto a = fold(lhs);
    const auto b = fold(rhs);
    std::size_t ai = 0;
    std::size_t bi = 0;
    while (ai < a.size() && bi < b.size()) {
        if (std::isdigit(static_cast<unsigned char>(a[ai])) != 0 &&
            std::isdigit(static_cast<unsigned char>(b[bi])) != 0) {
            std::size_t ae = ai;
            std::size_t be = bi;
            while (ae < a.size() && std::isdigit(static_cast<unsigned char>(a[ae])) != 0) ++ae;
            while (be < b.size() && std::isdigit(static_cast<unsigned char>(b[be])) != 0) ++be;
            const auto an = a.substr(ai, ae - ai);
            const auto bn = b.substr(bi, be - bi);
            const auto az = an.find_first_not_of('0');
            const auto bz = bn.find_first_not_of('0');
            const auto av = az == std::string::npos ? std::string("0") : an.substr(az);
            const auto bv = bz == std::string::npos ? std::string("0") : bn.substr(bz);
            if (av.size() != bv.size()) return av.size() < bv.size();
            if (av != bv) return av < bv;
            if (an.size() != bn.size()) return an.size() < bn.size();
            ai = ae;
            bi = be;
            continue;
        }
        if (a[ai] != b[bi]) return a[ai] < b[bi];
        ++ai;
        ++bi;
    }
    return a.size() < b.size();
}

[[nodiscard]] bool isAklzFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return false;
    }
    std::array<std::uint8_t, spice::compression::aklz::kHeaderSize> header{};
    input.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size()));
    const auto count = static_cast<std::size_t>(std::max<std::streamsize>(input.gcount(), std::streamsize{ 0 }));
    return spice::compression::aklz::isAklz(std::span<const std::uint8_t>(header.data(), count));
}

void addError(FieldDiscoveryResult& result, std::string message, std::filesystem::path path = {}) {
    result.diagnostics.push_back({ DiagnosticSeverity::Error, std::move(message), std::move(path) });
}

} // namespace

FieldDiscoveryResult FieldDiscovery::discover(const std::filesystem::path& selectedRoot) {
    FieldDiscoveryResult result{};
    result.selectedRoot = selectedRoot;

    std::error_code ec{};
    if (selectedRoot.empty() || !std::filesystem::is_directory(selectedRoot, ec) || ec) {
        addError(result, "The selected game-data root is not a readable directory.", selectedRoot);
        return result;
    }

    if (equalsFolded(selectedRoot.filename().string(), "field")) {
        result.fieldDirectoryCandidates.push_back(selectedRoot);
    }
    const auto options = std::filesystem::directory_options::skip_permission_denied;
    std::filesystem::recursive_directory_iterator it(selectedRoot, options, ec);
    const std::filesystem::recursive_directory_iterator end{};
    while (!ec && it != end) {
        const auto entry = *it;
        std::error_code typeError{};
        if (entry.is_symlink(typeError) && entry.is_directory(typeError)) {
            it.disable_recursion_pending();
        } else if (entry.is_directory(typeError) &&
            equalsFolded(entry.path().filename().string(), "field")) {
            result.fieldDirectoryCandidates.push_back(entry.path());
            it.disable_recursion_pending();
        }
        it.increment(ec);
    }
    if (ec) {
        result.diagnostics.push_back({ DiagnosticSeverity::Warning,
            "Some directories could not be inspected while locating FIELD.", selectedRoot });
    }

    std::sort(result.fieldDirectoryCandidates.begin(), result.fieldDirectoryCandidates.end());
    result.fieldDirectoryCandidates.erase(
        std::unique(result.fieldDirectoryCandidates.begin(), result.fieldDirectoryCandidates.end()),
        result.fieldDirectoryCandidates.end());
    if (result.fieldDirectoryCandidates.empty()) {
        addError(result, "No FIELD directory was found under the selected game-data root.", selectedRoot);
        return result;
    }
    if (result.fieldDirectoryCandidates.size() != 1U) {
        addError(result, "More than one FIELD directory was found; SKEWER will not guess which dataset to use.", selectedRoot);
        for (const auto& candidate : result.fieldDirectoryCandidates) {
            result.diagnostics.push_back({ DiagnosticSeverity::Info, "FIELD candidate", candidate });
        }
        return result;
    }
    result.fieldDirectory = result.fieldDirectoryCandidates.front();

    std::map<std::string, std::vector<std::filesystem::path>> ectByStem{};
    std::map<std::string, std::vector<std::filesystem::path>> mldByStem{};
    std::filesystem::directory_iterator childIt(*result.fieldDirectory, options, ec);
    while (!ec && childIt != std::filesystem::directory_iterator{}) {
        const auto entry = *childIt;
        std::error_code fileError{};
        if (entry.is_regular_file(fileError)) {
            const auto extension = fold(entry.path().extension().string());
            const auto stem = fold(entry.path().stem().string());
            if (extension == ".ect") {
                ectByStem[stem].push_back(entry.path());
                if (isAklzFile(entry.path())) {
                    addError(result, "GameCube is not yet supported.", entry.path());
                }
            } else if (extension == ".mld") {
                mldByStem[stem].push_back(entry.path());
            }
        }
        childIt.increment(ec);
    }
    if (ec) {
        addError(result, "The FIELD directory could not be enumerated completely.", *result.fieldDirectory);
    }
    if (ectByStem.empty()) {
        addError(result, "The FIELD directory contains no ECT files.", *result.fieldDirectory);
    }
    if (hasErrors(result.diagnostics)) {
        return result;
    }

    for (const auto& [stem, ectPaths] : ectByStem) {
        FieldCatalogEntry field{};
        field.stem = stem;
        field.ectPath = ectPaths.front();
        const auto mldFound = mldByStem.find(stem);
        if (ectPaths.size() != 1U || (mldFound != mldByStem.end() && mldFound->second.size() > 1U)) {
            field.availability = FieldAvailability::Ambiguous;
            field.unavailableReason = "Ambiguous duplicate case-insensitive field stem.";
        } else if (mldFound == mldByStem.end()) {
            field.availability = FieldAvailability::MissingMld;
            field.unavailableReason = "No matching MLD file.";
        } else {
            field.mldPath = mldFound->second.front();
            if (stem == "a099a") {
                field.availability = FieldAvailability::Area99Deferred;
                field.unavailableReason = "Area 99 support is deferred.";
            } else {
                field.availability = FieldAvailability::Available;
            }
        }
        result.fields.push_back(std::move(field));
    }
    std::sort(result.fields.begin(), result.fields.end(), [](const auto& lhs, const auto& rhs) {
        return naturalLess(lhs.stem, rhs.stem);
    });
    return result;
}

} // namespace skewer::core
