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

constexpr int kDefaultContextOpacityPercent = 40;

void configureSlider(QSlider* slider, const int minimum) {
    slider->setRange(minimum, kVisualAdjustmentMaximumPercent);
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
        QStringLiteral("Encounter Surfaces"), encounterControls_, true, false));
    layout->addWidget(createLayerGroup(
        QStringLiteral("Field Context"), contextControls_, false, true));

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto* resetAll = buttons->addButton(
        QStringLiteral("Reset All"), QDialogButtonBox::ResetRole);
    connect(resetAll, &QPushButton::clicked, this, [this]() {
        updating_ = true;
        setLayerSettings(encounterControls_, {});
        setLayerSettings(contextControls_, {});
        contextControls_.opacity->setValue(kDefaultContextOpacityPercent);
        encounterEdges_->setChecked(false);
        traversalBarriers_->setChecked(false);
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
    const bool includeEdges,
    const bool includeOpacity) {
    auto* group = new QGroupBox(title, this);
    auto* grid = new QGridLayout(group);

    const auto addSlider = [this, grid](
        const int row,
        const QString& label,
        QSlider*& slider,
        QLabel*& valueLabel,
        const int minimum = kVisualAdjustmentMinimumPercent) {
        grid->addWidget(new QLabel(label, this), row, 0);
        slider = new QSlider(Qt::Horizontal, this);
        configureSlider(slider, minimum);
        grid->addWidget(slider, row, 1);
        valueLabel = new QLabel(QStringLiteral("100%"), this);
        valueLabel->setMinimumWidth(42);
        grid->addWidget(valueLabel, row, 2);
    };

    addSlider(0, QStringLiteral("Brightness"),
        controls.brightness, controls.brightnessValue);
    addSlider(1, QStringLiteral("Saturation"),
        controls.saturation, controls.saturationValue,
        kVisualSaturationMinimumPercent);
    addSlider(2, QStringLiteral("Contrast"),
        controls.contrast, controls.contrastValue);

    int nextRow = 3;
    if (includeOpacity) {
        grid->addWidget(new QLabel(QStringLiteral("Opacity"), this), nextRow, 0);
        controls.opacity = new QSlider(Qt::Horizontal, this);
        controls.opacity->setRange(0, 100);
        controls.opacity->setValue(kDefaultContextOpacityPercent);
        controls.opacity->setSingleStep(1);
        controls.opacity->setPageStep(10);
        controls.opacity->setToolTip(QStringLiteral(
            "Adjust the opacity of the non-editable field context layer."));
        grid->addWidget(controls.opacity, nextRow, 1);
        controls.opacityValue = new QLabel(QStringLiteral("40%"), this);
        controls.opacityValue->setMinimumWidth(42);
        grid->addWidget(controls.opacityValue, nextRow++, 2);
        connect(controls.opacity, &QSlider::valueChanged, this,
            [this, &controls](const int) {
                updateLabels(controls);
                notifyChanged();
            });
    }
    if (includeEdges) {
        encounterEdges_ = new QCheckBox(QStringLiteral("Show triangle edges"), this);
        grid->addWidget(encounterEdges_, nextRow++, 0, 1, 3);
        connect(encounterEdges_, &QCheckBox::toggled,
            this, &VisualSettingsDialog::notifyChanged);

        traversalBarriers_ = new QCheckBox(
            QStringLiteral("Highlight traversal barriers"), this);
        traversalBarriers_->setToolTip(QStringLiteral(
            "Hatch collision-ground triangles carrying the decoded "
            "0x4800 traversal-barrier mask."));
        grid->addWidget(traversalBarriers_, nextRow++, 0, 1, 3);
        connect(traversalBarriers_, &QCheckBox::toggled,
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
    traversalBarriers_->setChecked(clamped.traversalBarriersEnabled);
    updating_ = false;
}

VisualSettings VisualSettingsDialog::settings() const {
    return clampedVisualSettings({
        layerSettings(encounterControls_),
        layerSettings(contextControls_),
        encounterEdges_->isChecked(),
        traversalBarriers_->isChecked()
    });
}

void VisualSettingsDialog::setContextOpacityPercent(const int percent) {
    updating_ = true;
    contextControls_.opacity->setValue(std::clamp(percent, 0, 100));
    updateLabels(contextControls_);
    updating_ = false;
}

int VisualSettingsDialog::contextOpacityPercent() const {
    return contextControls_.opacity->value();
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
    if (controls.opacity != nullptr) {
        controls.opacityValue->setText(
            QStringLiteral("%1%").arg(controls.opacity->value()));
    }
}

void VisualSettingsDialog::resetLayer(LayerControls& controls) {
    updating_ = true;
    setLayerSettings(controls, {});
    if (controls.opacity != nullptr) {
        controls.opacity->setValue(kDefaultContextOpacityPercent);
    }
    if (&controls == &encounterControls_) {
        encounterEdges_->setChecked(false);
        traversalBarriers_->setChecked(false);
    }
    updating_ = false;
    emit settingsChanged();
}

void VisualSettingsDialog::notifyChanged() {
    if (!updating_) emit settingsChanged();
}

} // namespace skewer::qt
