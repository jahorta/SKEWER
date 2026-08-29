#pragma once

#include "../VisualSettings.h"

#include <QDialog>

class QCheckBox;
class QLabel;
class QPushButton;
class QSlider;
class QVBoxLayout;

namespace skewer::qt {

class VisualSettingsDialog final : public QDialog {
    Q_OBJECT

public:
    explicit VisualSettingsDialog(QWidget* parent = nullptr);

    void setSettings(const VisualSettings& settings);
    [[nodiscard]] VisualSettings settings() const;

signals:
    void settingsChanged();

private:
    struct LayerControls {
        QSlider* brightness = nullptr;
        QLabel* brightnessValue = nullptr;
        QSlider* saturation = nullptr;
        QLabel* saturationValue = nullptr;
        QSlider* contrast = nullptr;
        QLabel* contrastValue = nullptr;
        QPushButton* reset = nullptr;
    };

    [[nodiscard]] QWidget* createLayerGroup(
        const QString& title,
        LayerControls& controls,
        bool includeEdges);
    void setLayerSettings(LayerControls& controls, const LayerVisualSettings& settings);
    [[nodiscard]] LayerVisualSettings layerSettings(const LayerControls& controls) const;
    void updateLabels(LayerControls& controls);
    void resetLayer(LayerControls& controls);
    void notifyChanged();

    LayerControls encounterControls_{};
    LayerControls contextControls_{};
    QCheckBox* encounterEdges_ = nullptr;
    bool updating_ = false;
};

} // namespace skewer::qt
