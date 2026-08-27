#pragma once

#include "SkewerCore/TriangleKeys.h"

#include <QString>
#include <QStringList>
#include <QVector3D>

#include <optional>
#include <vector>

namespace skewer::qt {

struct WorkspaceState {
    QString gameDataRoot{};
    QString fieldDirectory{};
    QString alxDataRoot{};
    QString activeField{};
    int encounterTable = 0;
    QVector3D orbitCenter{};
    float orbitDistance = 500.0F;
    float orbitYaw = 0.0F;
    float orbitPitch = -20.0F;
    bool expertMetadata = false;
    QStringList hiddenBatches{};
    std::vector<skewer::core::TriangleKey> selection{};
};

class WorkspaceStateStore final {
public:
    explicit WorkspaceStateStore(QString executableDirectory);

    [[nodiscard]] bool isWritable() const noexcept;
    [[nodiscard]] QString workspaceDirectory() const;
    [[nodiscard]] QString statePath() const;
    [[nodiscard]] QString errorString() const;
    [[nodiscard]] std::optional<WorkspaceState> load();
    [[nodiscard]] bool save(const WorkspaceState& state);

private:
    void probe();

    QString executableDirectory_{};
    QString workspaceDirectory_{};
    QString error_{};
    bool writable_ = false;
};

} // namespace skewer::qt
