#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace skewer::core {

enum class DiagnosticSeverity {
    Info,
    Warning,
    Error,
};

struct Diagnostic {
    DiagnosticSeverity severity = DiagnosticSeverity::Info;
    std::string message{};
    std::filesystem::path path{};
};

[[nodiscard]] inline bool hasErrors(const std::vector<Diagnostic>& diagnostics) {
    for (const auto& diagnostic : diagnostics) {
        if (diagnostic.severity == DiagnosticSeverity::Error) {
            return true;
        }
    }
    return false;
}

} // namespace skewer::core
