#include "TriangleInspectorWidget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <algorithm>
#include <optional>
#include <variant>

namespace skewer::qt {
namespace {

[[nodiscard]] QString keyText(const skewer::core::TriangleKey& key) {
    if (const auto* grnd = std::get_if<skewer::core::GrndTriangleKey>(&key)) {
        return QStringLiteral("GRND 0x%1 triangle %2")
            .arg(grnd->resourceAddress, 8, 16, QLatin1Char('0'))
            .arg(grnd->triangleIndex);
    }
    const auto& gobj = std::get<skewer::core::GobjTriangleKey>(key);
    return QStringLiteral("GOBJ 0x%1 node %2 triangle %3")
        .arg(gobj.resourceAddress, 8, 16, QLatin1Char('0'))
        .arg(gobj.nodeIndex)
        .arg(gobj.triangleIndex);
}

[[nodiscard]] const skewer::core::SceneTriangle* findTriangle(
    const skewer::core::FieldDocument* document,
    const skewer::core::TriangleKey& key) {
    if (document == nullptr) return nullptr;
    const skewer::core::TriangleKeyLess less{};
    const auto found = std::find_if(
        document->scene.triangles.begin(), document->scene.triangles.end(),
        [&](const auto& triangle) {
            return !less(triangle.key, key) && !less(key, triangle.key);
        });
    return found == document->scene.triangles.end() ? nullptr : &*found;
}

} // namespace

TriangleInspectorWidget::TriangleInspectorWidget(QWidget* parent)
    : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);

    summaryLabel_ = new QLabel(QStringLiteral("No selection"), this);
    summaryLabel_->setWordWrap(true);
    layout->addWidget(summaryLabel_);

    auto* selectorRow = new QHBoxLayout();
    selectorEditor_ = new QComboBox(this);
    selectorEditor_->addItem(QStringLiteral("0 - No encounters"), 0);
    for (int selector = 1; selector <= 8; ++selector) {
        selectorEditor_->addItem(QStringLiteral("%1 - Table %1").arg(selector), selector);
    }
    applyButton_ = new QPushButton(QStringLiteral("Apply selector"), this);
    selectorRow->addWidget(selectorEditor_, 1);
    selectorRow->addWidget(applyButton_);
    layout->addLayout(selectorRow);

    jumpButton_ = new QPushButton(QStringLiteral("Jump to encounter table"), this);
    jumpButton_->setEnabled(false);
    layout->addWidget(jumpButton_);

    expertCheck_ = new QCheckBox(QStringLiteral("Show full triangle metadata"), this);
    layout->addWidget(expertCheck_);

    metadataView_ = new QPlainTextEdit(this);
    metadataView_->setReadOnly(true);
    metadataView_->setMaximumBlockCount(500);
    metadataView_->setVisible(false);
    layout->addWidget(metadataView_, 1);

    connect(applyButton_, &QPushButton::clicked, this, &TriangleInspectorWidget::applyRequested);
    connect(jumpButton_, &QPushButton::clicked, this, &TriangleInspectorWidget::jumpRequested);
    connect(expertCheck_, &QCheckBox::toggled, this, [this](const bool enabled) {
        metadataView_->setVisible(enabled);
        emit expertModeChanged(enabled);
    });
}

void TriangleInspectorWidget::showSelection(
    const skewer::core::FieldDocument* document,
    const std::set<skewer::core::TriangleKey,
        skewer::core::TriangleKeyLess>& selection,
    const bool writable) {
    selectorEditor_->setEnabled(writable);
    applyButton_->setEnabled(writable && !selection.empty());
    if (selection.empty()) {
        summaryLabel_->setText(QStringLiteral("No selection"));
        metadataView_->clear();
        jumpButton_->setEnabled(false);
        jumpSelector_ = 0;
        return;
    }

    std::optional<std::uint8_t> commonSelector{};
    bool mixed = false;
    QStringList details{};
    for (const auto& key : selection) {
        const auto* triangle = findTriangle(document, key);
        if (triangle == nullptr) continue;
        const auto baseline = document == nullptr
            ? std::optional<std::uint8_t>{}
            : document->baselineSelector(key);
        if (!commonSelector.has_value()) commonSelector = triangle->selector;
        else if (*commonSelector != triangle->selector) mixed = true;
        details.push_back(QStringLiteral(
            "%1\n  raw metadata: 0x%2 0x%3 0x%4\n  selector: %5%6")
            .arg(keyText(key))
            .arg(triangle->rawMetadata[0], 4, 16, QLatin1Char('0'))
            .arg(triangle->rawMetadata[1], 4, 16, QLatin1Char('0'))
            .arg(triangle->rawMetadata[2], 4, 16, QLatin1Char('0'))
            .arg(triangle->selector <= 8U
                ? QString::number(triangle->selector)
                : QStringLiteral("invalid"))
            .arg(baseline.has_value() && *baseline != triangle->selector
                ? QStringLiteral(" (baseline %1, modified)").arg(*baseline)
                : QString{}));
    }

    if (mixed || !commonSelector.has_value()) {
        summaryLabel_->setText(QStringLiteral(
            "%1 triangles selected; encounter selector is mixed.")
            .arg(selection.size()));
        jumpButton_->setEnabled(false);
        jumpSelector_ = 0;
    } else {
        summaryLabel_->setText(QStringLiteral(
            "%1 triangle(s); encounter selector %2%3")
            .arg(selection.size())
            .arg(*commonSelector)
            .arg(*commonSelector == 0U
                ? QStringLiteral(" (no encounters)") : QString{}));
        jumpSelector_ = static_cast<int>(*commonSelector);
        jumpButton_->setEnabled(jumpSelector_ >= 1 && jumpSelector_ <= 8);
        const auto index = selectorEditor_->findData(jumpSelector_);
        if (index >= 0) selectorEditor_->setCurrentIndex(index);
    }
    metadataView_->setPlainText(details.join(QStringLiteral("\n\n")));
}

void TriangleInspectorWidget::setExpertMode(const bool enabled) {
    const QSignalBlocker blocker(expertCheck_);
    expertCheck_->setChecked(enabled);
    metadataView_->setVisible(enabled);
}

int TriangleInspectorWidget::selectorValue() const {
    return selectorEditor_->currentData().toInt();
}

int TriangleInspectorWidget::jumpSelector() const noexcept {
    return jumpSelector_;
}

bool TriangleInspectorWidget::expertMode() const {
    return expertCheck_->isChecked();
}

} // namespace skewer::qt
