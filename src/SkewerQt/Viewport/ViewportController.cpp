#include "ViewportController.h"

#include <Qt>

#include <algorithm>

namespace skewer::qt {

ViewportController::ViewportController(ViewportWidget* viewport, QObject* parent)
    : QObject(parent), viewport_(viewport) {
    viewport_->setBackend(this);
    connect(viewport_, &ViewportWidget::ready, this, [this]() {
        syncScene();
        syncSelection();
    });
    connect(viewport_, &ViewportWidget::loadDiagnosticsChanged,
        this, &ViewportController::loadDiagnosticsChanged);
}

void ViewportController::setScene(const skewer::core::SceneModel* scene) {
    scene_ = scene;
    picker_ = scene_ == nullptr
        ? nullptr
        : std::make_unique<skewer::core::TrianglePicker>(*scene_);
    selection_.clear();
    sceneAdapter_.setScene(scene_);
    syncScene();
    syncSelection();
}

void ViewportController::refreshScene() {
    sceneAdapter_.refreshScene();
    syncScene();
    syncSelection();
}

void ViewportController::setVisibility(std::vector<std::uint8_t> visibility) {
    sceneAdapter_.setVisibility(std::move(visibility));
    syncScene();
}

void ViewportController::setContextOpacity(const int percent) {
    viewport_->setContextOpacity(percent);
}

void ViewportController::setVisualSettings(const VisualSettings& settings) {
    viewport_->setVisualSettings(settings);
}

void ViewportController::setCameraState(const ViewportCameraState& state) {
    viewport_->setCameraState(state);
}

void ViewportController::frameAll(const skewer::core::SceneBounds& sceneBounds) {
    viewport_->frameAll(sceneBounds);
}

void ViewportController::restoreSelection(
    const std::span<const skewer::core::TriangleKey> selection) {
    selection_.clear();
    for (const auto& key : selection) {
        if (containsTriangle(key)) selection_.insert(key);
    }
    syncSelection();
    emit selectionChanged();
}

const std::vector<std::uint8_t>& ViewportController::visibility() const noexcept {
    return sceneAdapter_.visibility();
}

const std::set<skewer::core::TriangleKey,
    skewer::core::TriangleKeyLess>& ViewportController::selection() const noexcept {
    return selection_;
}

ViewportCameraState ViewportController::cameraState() const {
    return viewport_->cameraState();
}

bool ViewportController::isReady() const noexcept {
    return viewport_->isReady();
}

const std::vector<skewer::core::Diagnostic>& ViewportController::loadDiagnostics() const noexcept {
    return viewport_->loadDiagnostics();
}

void ViewportController::handleSceneClick(
    const float nearX, const float nearY, const float nearZ,
    const float farX, const float farY, const float farZ,
    const int modifiers) {
    if (picker_ == nullptr) return;
    const skewer::core::SceneRay ray{
        { nearX, nearY, nearZ },
        { farX - nearX, farY - nearY, farZ - nearZ }
    };
    const auto hit = picker_->pick(ray, sceneAdapter_.visibility());
    const bool control = (modifiers & static_cast<int>(Qt::ControlModifier)) != 0;
    const bool shift = (modifiers & static_cast<int>(Qt::ShiftModifier)) != 0;
    if (!hit.has_value()) {
        if (!control && !shift) selection_.clear();
    } else if (control) {
        const auto found = selection_.find(hit->key);
        if (found == selection_.end()) selection_.insert(hit->key);
        else selection_.erase(found);
    } else if (shift) {
        selection_.insert(hit->key);
    } else {
        selection_.clear();
        selection_.insert(hit->key);
    }
    syncSelection();
    emit selectionChanged();
}

void ViewportController::cameraChanged() {
    emit cameraStateChanged();
}

bool ViewportController::containsTriangle(const skewer::core::TriangleKey& key) const {
    if (scene_ == nullptr) return false;
    const skewer::core::TriangleKeyLess less{};
    return std::any_of(scene_->triangles.begin(), scene_->triangles.end(),
        [&](const auto& triangle) {
            return !less(triangle.key, key) && !less(key, triangle.key);
        });
}

void ViewportController::syncScene() {
    viewport_->setSceneMeshes(sceneAdapter_.sceneMeshes());
}

void ViewportController::syncSelection() {
    sceneAdapter_.setSelection(selection_);
    viewport_->setSelectionMeshes(sceneAdapter_.selectionMeshes());
}

} // namespace skewer::qt
