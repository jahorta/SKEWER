#pragma once

#include "SceneAdapter.h"
#include "ViewportWidget.h"

#include "SkewerCore/TrianglePicker.h"

#include <QObject>

#include <cstdint>
#include <memory>
#include <set>
#include <span>
#include <vector>

namespace skewer::qt {

class ViewportController final : public QObject {
    Q_OBJECT

public:
    explicit ViewportController(ViewportWidget* viewport, QObject* parent = nullptr);

    void setScene(const skewer::core::SceneModel* scene);
    void refreshScene();
    void setVisibility(std::vector<std::uint8_t> visibility);
    void setContextOpacity(int percent);
    void setVisualSettings(const VisualSettings& settings);
    void setCameraState(const ViewportCameraState& state);
    void frameAll(const skewer::core::SceneBounds& sceneBounds);
    void restoreSelection(std::span<const skewer::core::TriangleKey> selection);

    [[nodiscard]] const std::vector<std::uint8_t>& visibility() const noexcept;
    [[nodiscard]] const std::set<skewer::core::TriangleKey,
        skewer::core::TriangleKeyLess>& selection() const noexcept;
    [[nodiscard]] ViewportCameraState cameraState() const;
    [[nodiscard]] bool isReady() const noexcept;
    [[nodiscard]] const std::vector<skewer::core::Diagnostic>& loadDiagnostics() const noexcept;

    Q_INVOKABLE void handleSceneClick(float nearX, float nearY, float nearZ,
        float farX, float farY, float farZ, int modifiers);
    Q_INVOKABLE void cameraChanged();

signals:
    void selectionChanged();
    void cameraStateChanged();
    void loadDiagnosticsChanged();

private:
    [[nodiscard]] bool containsTriangle(const skewer::core::TriangleKey& key) const;
    void syncScene();
    void syncSelection();

    ViewportWidget* viewport_ = nullptr;
    const skewer::core::SceneModel* scene_ = nullptr;
    SceneAdapter sceneAdapter_{};
    std::unique_ptr<skewer::core::TrianglePicker> picker_{};
    std::set<skewer::core::TriangleKey, skewer::core::TriangleKeyLess> selection_{};
};

} // namespace skewer::qt
