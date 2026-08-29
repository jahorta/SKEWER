#pragma once

#include "../VisualSettings.h"

#include "SkewerCore/Diagnostics.h"
#include "SkewerCore/SceneModel.h"

#include <QPointer>
#include <QVariantList>
#include <QVector3D>
#include <QWidget>

#include <vector>

class QObject;
class QQuickWidget;
class QResizeEvent;
class QToolButton;

namespace skewer::qt {

struct ViewportCameraState {
    QVector3D center{};
    float distance = 500.0F;
    float yaw = 0.0F;
    float pitch = -20.0F;
};

class ViewportWidget final : public QWidget {
    Q_OBJECT

public:
    explicit ViewportWidget(QWidget* parent = nullptr);

    void setBackend(QObject* backend);
    void setSceneMeshes(QVariantList meshes);
    void setSelectionMeshes(QVariantList meshes);
    void setContextOpacity(int percent);
    void setVisualSettings(const VisualSettings& settings);
    void setCameraState(const ViewportCameraState& state);
    void frameAll(const skewer::core::SceneBounds& sceneBounds);

    [[nodiscard]] ViewportCameraState cameraState() const;
    [[nodiscard]] bool isReady() const noexcept;
    [[nodiscard]] const std::vector<skewer::core::Diagnostic>& loadDiagnostics() const noexcept;

signals:
    void ready();
    void loadDiagnosticsChanged();
    void visualSettingsRequested();

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void applyPendingState();
    void positionVisualSettingsButton();

    QQuickWidget* quickView_ = nullptr;
    QToolButton* visualSettingsButton_ = nullptr;
    QPointer<QObject> backend_{};
    QVariantList sceneMeshes_{};
    QVariantList selectionMeshes_{};
    ViewportCameraState cameraState_{};
    int contextOpacityPercent_ = 40;
    VisualSettings visualSettings_{};
    std::vector<skewer::core::Diagnostic> loadDiagnostics_{};
};

} // namespace skewer::qt
