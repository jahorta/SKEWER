#include "ViewportWidget.h"

#include <QQuickItem>
#include <QQuickWidget>
#include <QQmlError>
#include <QVBoxLayout>

#include <algorithm>

namespace skewer::qt {

ViewportWidget::ViewportWidget(QWidget* parent)
    : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    quickView_ = new QQuickWidget(this);
    quickView_->setResizeMode(QQuickWidget::SizeRootObjectToView);
    layout->addWidget(quickView_);

    connect(quickView_, &QQuickWidget::statusChanged, this,
        [this](const QQuickWidget::Status status) {
            if (status == QQuickWidget::Ready) {
                loadDiagnostics_.clear();
                applyPendingState();
                emit ready();
                emit loadDiagnosticsChanged();
            } else if (status == QQuickWidget::Error) {
                loadDiagnostics_ = {
                    { skewer::core::DiagnosticSeverity::Error,
                        "The Qt Quick 3D viewer failed to load." }
                };
                for (const auto& error : quickView_->errors()) {
                    loadDiagnostics_.push_back({
                        skewer::core::DiagnosticSeverity::Error,
                        error.toString().toStdString() });
                }
                emit loadDiagnosticsChanged();
            }
        });

    quickView_->setSource(QUrl(QStringLiteral("qrc:/qml/Qml/ViewerScene.qml")));
}

void ViewportWidget::setBackend(QObject* backend) {
    backend_ = backend;
    if (quickView_->rootObject() != nullptr) {
        quickView_->rootObject()->setProperty(
            "backend", QVariant::fromValue<QObject*>(backend_.data()));
    }
}

void ViewportWidget::setSceneMeshes(QVariantList meshes) {
    sceneMeshes_ = std::move(meshes);
    if (quickView_->rootObject() != nullptr) {
        quickView_->rootObject()->setProperty("sceneMeshes", sceneMeshes_);
    }
}

void ViewportWidget::setSelectionMeshes(QVariantList meshes) {
    selectionMeshes_ = std::move(meshes);
    if (quickView_->rootObject() != nullptr) {
        quickView_->rootObject()->setProperty("selectionMeshes", selectionMeshes_);
    }
}

void ViewportWidget::setContextOpacity(const int percent) {
    contextOpacityPercent_ = std::clamp(percent, 0, 100);
    if (quickView_->rootObject() != nullptr) {
        quickView_->rootObject()->setProperty(
            "contextOpacity", static_cast<double>(contextOpacityPercent_) / 100.0);
    }
}

void ViewportWidget::setCameraState(const ViewportCameraState& state) {
    cameraState_ = state;
    cameraState_.distance = std::max(20.0F, cameraState_.distance);
    if (quickView_->rootObject() == nullptr) return;
    auto* root = quickView_->rootObject();
    root->setProperty("orbitCenter", cameraState_.center);
    root->setProperty("orbitDistance", cameraState_.distance);
    root->setProperty("orbitYaw", cameraState_.yaw);
    root->setProperty("orbitPitch", cameraState_.pitch);
}

void ViewportWidget::frameAll(const float sceneExtent) {
    setCameraState({
        QVector3D(0.0F, 0.0F, 0.0F),
        std::max(20.0F, sceneExtent * 1.35F),
        0.0F,
        -25.0F });
}

ViewportCameraState ViewportWidget::cameraState() const {
    if (quickView_->rootObject() == nullptr) return cameraState_;
    auto* root = quickView_->rootObject();
    return {
        root->property("orbitCenter").value<QVector3D>(),
        root->property("orbitDistance").toFloat(),
        root->property("orbitYaw").toFloat(),
        root->property("orbitPitch").toFloat()
    };
}

bool ViewportWidget::isReady() const noexcept {
    return quickView_ != nullptr && quickView_->status() == QQuickWidget::Ready &&
        quickView_->rootObject() != nullptr;
}

const std::vector<skewer::core::Diagnostic>& ViewportWidget::loadDiagnostics() const noexcept {
    return loadDiagnostics_;
}

void ViewportWidget::applyPendingState() {
    setBackend(backend_.data());
    setSceneMeshes(sceneMeshes_);
    setSelectionMeshes(selectionMeshes_);
    setContextOpacity(contextOpacityPercent_);
    setCameraState(cameraState_);
}

} // namespace skewer::qt
