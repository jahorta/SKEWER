#pragma once

#include "SkewerCore/AlxEnrichment.h"
#include "SkewerCore/FieldDiscovery.h"
#include "SkewerCore/FieldDocument.h"
#include "SkewerCore/FieldLoader.h"

#include <QFutureWatcher>
#include <QObject>
#include <QString>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace skewer::qt {

class FieldSessionController final : public QObject {
    Q_OBJECT

public:
    explicit FieldSessionController(QObject* parent = nullptr);

    bool beginDiscovery(const QString& rootPath, QString restoreField = {});
    bool beginFieldLoad(int catalogIndex);
    bool beginAlxLoad(const QString& rootPath, bool interactive);
    void clearAlxData();
    void restoreRememberedAlxRoot(const QString& rootPath);

    void applyTriangleSelectors(std::span<const skewer::core::TriangleKey> keys,
        std::uint8_t selector);
    void applyEctValue(const skewer::core::EctValueKey& key, const QString& text);
    void undo();
    void redo();

    [[nodiscard]] bool discoveryRunning() const noexcept;
    [[nodiscard]] bool fieldLoadRunning() const noexcept;
    [[nodiscard]] bool alxLoadRunning() const noexcept;
    [[nodiscard]] const skewer::core::FieldDiscoveryResult& catalog() const noexcept;
    [[nodiscard]] QString takePendingRestoreField();
    [[nodiscard]] const QString& currentRoot() const noexcept;
    [[nodiscard]] const QString& alxDataRoot() const noexcept;
    [[nodiscard]] const QString& pendingAlxRoot() const noexcept;
    [[nodiscard]] bool completedAlxLoadWasInteractive() const noexcept;
    [[nodiscard]] const std::vector<skewer::core::Diagnostic>& alxLoadDiagnostics() const noexcept;
    [[nodiscard]] bool hasAlxDataset() const noexcept;
    [[nodiscard]] QString alxLocaleName() const;
    [[nodiscard]] QString alxSourceRoot() const;

    [[nodiscard]] skewer::core::FieldDocument* document() noexcept;
    [[nodiscard]] const skewer::core::FieldDocument* document() const noexcept;
    [[nodiscard]] std::optional<std::int32_t> groundTblIdForEntry(
        std::size_t entryTableIndex) const;
    [[nodiscard]] std::optional<skewer::core::FormationResolution> resolveFormation(
        int tableIndex, int rowIndex) const;
    [[nodiscard]] std::vector<skewer::core::Diagnostic> validateActiveFieldAlx() const;
    [[nodiscard]] std::vector<std::uint8_t> modifiedSceneBatches() const;

signals:
    void discoveryStarted();
    void discoveryFinished(bool success);
    void fieldLoadStarted(const QString& stem);
    void fieldLoadFinished(bool success);
    void alxLoadStarted(const QString& rootPath);
    void alxLoadFinished(bool success);
    void alxChanged();
    void documentCleared();
    void documentChanged();
    void ectEditRejected();
    void diagnosticsProduced(
        const std::vector<skewer::core::Diagnostic>& diagnostics,
        bool replaceGeneral);

private:
    void onDiscoveryFinished();
    void onFieldLoadFinished();
    void onAlxLoadFinished();

    QFutureWatcher<skewer::core::FieldDiscoveryResult> discoveryWatcher_{};
    QFutureWatcher<skewer::core::FieldLoadResult> fieldLoadWatcher_{};
    QFutureWatcher<skewer::core::AlxLoadResult> alxLoadWatcher_{};
    skewer::core::FieldDiscoveryResult catalog_{};
    QString pendingRestoreField_{};
    QString currentRoot_{};
    QString alxDataRoot_{};
    QString pendingAlxRoot_{};
    bool alxLoadInteractive_ = false;
    bool completedAlxLoadInteractive_ = false;
    std::vector<skewer::core::Diagnostic> alxLoadDiagnostics_{};
    std::optional<skewer::core::AlxDataset> alxDataset_{};
    std::unique_ptr<skewer::core::FieldDocument> document_{};
};

} // namespace skewer::qt
