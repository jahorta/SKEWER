#include "ViewportWidget.h"

#include "SceneAdapter.h"

#include <QIcon>
#include <QQuickItem>
#include <QQuickWidget>
#include <QResizeEvent>
#include <QQmlError>
#include <QToolButton>
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

    visualSettingsButton_ = new QToolButton(this);
    visualSettingsButton_->setFixedSize(32, 32);
    visualSettingsButton_->setAutoRaise(true);
    visualSettingsButton_->setToolTip(QStringLiteral("Visual Settings"));
    visualSettingsButton_->setAccessibleName(QStringLiteral("Visual Settings"));
    const auto settingsIcon = QIcon::fromTheme(QStringLiteral("preferences-system"));
    if (settingsIcon.isNull()) {
        visualSettingsButton_->setText(QStringLiteral("\u2699"));
    } else {
        visualSettingsButton_->setIcon(settingsIcon);
        visualSettingsButton_->setIconSize(QSize(20, 20));
    }
    visualSettingsButton_->setStyleSheet(QStringLiteral(
        "QToolButton { background: rgba(32, 36, 43, 190); color: white; "
        "border: 1px solid rgba(255, 255, 255, 90); border-radius: 4px; } "
        "QToolButton:hover { background: rgba(55, 61, 71, 220); } "
        "QToolButton:pressed { background: rgba(20, 23, 28, 230); }"));
    connect(visualSettingsButton_, &QToolButton::clicked,
        this, &ViewportWidget::visualSettingsRequested);
    positionVisualSettingsButton();

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

void ViewportWidget::setVisualSettings(const VisualSettings& settings) {
    visualSettings_ = clampedVisualSettings(settings);
    if (quickView_->rootObject() == nullptr) return;
    auto* root = quickView_->rootObject();
    root->setProperty("encounterBrightness",
        static_cast<double>(visualSettings_.encounter.brightnessPercent) / 100.0);
    root->setProperty("encounterSaturation",
        static_cast<double>(visualSettings_.encounter.saturationPercent) / 100.0);
    root->setProperty("encounterContrast",
        static_cast<double>(visualSettings_.encounter.contrastPercent) / 100.0);
    root->setProperty("contextBrightness",
        static_cast<double>(visualSettings_.fieldContext.brightnessPercent) / 100.0);
    root->setProperty("contextSaturation",
        static_cast<double>(visualSettings_.fieldContext.saturationPercent) / 100.0);
    root->setProperty("contextContrast",
        static_cast<double>(visualSettings_.fieldContext.contrastPercent) / 100.0);
    root->setProperty("encounterEdgesEnabled", visualSettings_.encounterEdgesEnabled);
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

void ViewportWidget::frameAll(const skewer::core::SceneBounds& sceneBounds) {
    auto state = cameraState();
    const auto aspectRatio = quickView_ != nullptr &&
        quickView_->width() > 0 && quickView_->height() > 0
        ? static_cast<float>(quickView_->width()) / static_cast<float>(quickView_->height())
        : 16.0F / 9.0F;
    state.center = QVector3D(0.0F, 0.0F, 0.0F);
    state.distance = skewer::core::frameDistanceForSceneBounds(
        sceneBounds, state.yaw, state.pitch, aspectRatio);
    setCameraState(state);
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

void ViewportWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    positionVisualSettingsButton();
}

void ViewportWidget::positionVisualSettingsButton() {
    if (visualSettingsButton_ == nullptr) return;
    constexpr int margin = 12;
    visualSettingsButton_->move(
        std::max(0, width() - visualSettingsButton_->width() - margin),
        std::max(0, height() - visualSettingsButton_->height() - margin));
    visualSettingsButton_->raise();
}

void ViewportWidget::applyPendingState() {
    if (quickView_->rootObject() != nullptr) {
        quickView_->rootObject()->setProperty(
            "selectorColors", SceneAdapter::selectorColors());
    }
    setBackend(backend_.data());
    setSceneMeshes(sceneMeshes_);
    setSelectionMeshes(selectionMeshes_);
    setContextOpacity(contextOpacityPercent_);
    setVisualSettings(visualSettings_);
    setCameraState(cameraState_);
}

} // namespace skewer::qt
