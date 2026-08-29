#include "VisualSettingsDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

namespace skewer::qt {
namespace {

void configureSlider(QSlider* slider) {
    slider->setRange(kVisualAdjustmentMinimumPercent, kVisualAdjustmentMaximumPercent);
    slider->setValue(kVisualAdjustmentNeutralPercent);
    slider->setSingleStep(1);
    slider->setPageStep(10);
}

} // namespace

VisualSettingsDialog::VisualSettingsDialog(QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(QStringLiteral("Visual Settings"));
    setModal(false);
    setAttribute(Qt::WA_DeleteOnClose, false);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(createLayerGroup(
        QStringLiteral("Encounter Surfaces"), encounterControls_, true));
    layout->addWidget(createLayerGroup(
        QStringLiteral("Field Context"), contextControls_, false));

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto* resetAll = buttons->addButton(
        QStringLiteral("Reset All"), QDialogButtonBox::ResetRole);
    connect(resetAll, &QPushButton::clicked, this, [this]() {
        updating_ = true;
        setLayerSettings(encounterControls_, {});
        setLayerSettings(contextControls_, {});
        encounterEdges_->setChecked(false);
        updating_ = false;
        emit settingsChanged();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::hide);
    layout->addWidget(buttons);

    resize(480, sizeHint().height());
}

QWidget* VisualSettingsDialog::createLayerGroup(
    const QString& title,
    LayerControls& controls,
    const bool includeEdges) {
    auto* group = new QGroupBox(title, this);
    auto* grid = new QGridLayout(group);

    const auto addSlider = [this, grid](
        const int row,
        const QString& label,
        QSlider*& slider,
        QLabel*& valueLabel) {
        grid->addWidget(new QLabel(label, this), row, 0);
        slider = new QSlider(Qt::Horizontal, this);
        configureSlider(slider);
        grid->addWidget(slider, row, 1);
        valueLabel = new QLabel(QStringLiteral("100%"), this);
        valueLabel->setMinimumWidth(42);
        grid->addWidget(valueLabel, row, 2);
    };

    addSlider(0, QStringLiteral("Brightness"),
        controls.brightness, controls.brightnessValue);
    addSlider(1, QStringLiteral("Saturation"),
        controls.saturation, controls.saturationValue);
    addSlider(2, QStringLiteral("Contrast"),
        controls.contrast, controls.contrastValue);

    int nextRow = 3;
    if (includeEdges) {
        encounterEdges_ = new QCheckBox(QStringLiteral("Show triangle edges"), this);
        grid->addWidget(encounterEdges_, nextRow++, 0, 1, 3);
        connect(encounterEdges_, &QCheckBox::toggled,
            this, &VisualSettingsDialog::notifyChanged);
    }

    controls.reset = new QPushButton(QStringLiteral("Reset"), this);
    grid->addWidget(controls.reset, nextRow, 2);
    connect(controls.reset, &QPushButton::clicked, this,
        [this, &controls]() { resetLayer(controls); });

    for (auto* slider : { controls.brightness, controls.saturation, controls.contrast }) {
        connect(slider, &QSlider::valueChanged, this, [this, &controls](const int) {
            updateLabels(controls);
            notifyChanged();
        });
    }
    return group;
}

void VisualSettingsDialog::setSettings(const VisualSettings& settings) {
    const auto clamped = clampedVisualSettings(settings);
    updating_ = true;
    setLayerSettings(encounterControls_, clamped.encounter);
    setLayerSettings(contextControls_, clamped.fieldContext);
    encounterEdges_->setChecked(clamped.encounterEdgesEnabled);
    updating_ = false;
}

VisualSettings VisualSettingsDialog::settings() const {
    return clampedVisualSettings({
        layerSettings(encounterControls_),
        layerSettings(contextControls_),
        encounterEdges_->isChecked()
    });
}

void VisualSettingsDialog::setLayerSettings(
    LayerControls& controls,
    const LayerVisualSettings& settings) {
    const auto clamped = clampedLayerVisualSettings(settings);
    controls.brightness->setValue(clamped.brightnessPercent);
    controls.saturation->setValue(clamped.saturationPercent);
    controls.contrast->setValue(clamped.contrastPercent);
    updateLabels(controls);
}

LayerVisualSettings VisualSettingsDialog::layerSettings(const LayerControls& controls) const {
    return {
        controls.brightness->value(),
        controls.saturation->value(),
        controls.contrast->value()
    };
}

void VisualSettingsDialog::updateLabels(LayerControls& controls) {
    controls.brightnessValue->setText(
        QStringLiteral("%1%").arg(controls.brightness->value()));
    controls.saturationValue->setText(
        QStringLiteral("%1%").arg(controls.saturation->value()));
    controls.contrastValue->setText(
        QStringLiteral("%1%").arg(controls.contrast->value()));
}

void VisualSettingsDialog::resetLayer(LayerControls& controls) {
    updating_ = true;
    setLayerSettings(controls, {});
    updating_ = false;
    emit settingsChanged();
}

void VisualSettingsDialog::notifyChanged() {
    if (!updating_) emit settingsChanged();
}

} // namespace skewer::qt
