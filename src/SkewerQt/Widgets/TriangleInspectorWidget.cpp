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

[[nodiscard]] skewer::core::SceneReferenceRole referenceRole(
    const skewer::core::FieldDocument* document,
    const skewer::core::SceneTriangle& triangle) {
    if (document == nullptr || triangle.batchIndex >= document->scene.batches.size()) {
        return skewer::core::SceneReferenceRole::Unreferenced;
    }
    return document->scene.batches[triangle.batchIndex].instance.referenceRole;
}

[[nodiscard]] QString traversalText(
    const skewer::core::TraversalClassification classification) {
    switch (classification) {
    case skewer::core::TraversalClassification::NotApplicable:
        return QStringLiteral("not applicable");
    case skewer::core::TraversalClassification::NoKnownBarrierMask:
        return QStringLiteral("no known barrier mask");
    case skewer::core::TraversalClassification::BarrierMaskPresent:
        return QStringLiteral("barrier mask present");
    }
    return QStringLiteral("not applicable");
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
    std::size_t barrierCount = 0U;
    QStringList details{};
    for (const auto& key : selection) {
        const auto* triangle = findTriangle(document, key);
        if (triangle == nullptr) continue;
        const auto baseline = document == nullptr
            ? std::optional<std::uint8_t>{}
            : document->baselineSelector(key);
        if (!commonSelector.has_value()) commonSelector = triangle->selector;
        else if (*commonSelector != triangle->selector) mixed = true;
        const auto metadata = skewer::core::interpretTriangleMetadata(
            triangle->rawMetadata[2], referenceRole(document, *triangle));
        if (metadata.traversal ==
            skewer::core::TraversalClassification::BarrierMaskPresent) {
            ++barrierCount;
        }
        auto detail = QStringLiteral(
            "%1\n  raw metadata: 0x%2 0x%3 0x%4"
            "\n  source word: 0x%5; decoded word: 0x%6"
            "\n  digits: ones %7, tens %8, hundreds %9")
            .arg(keyText(key))
            .arg(triangle->rawMetadata[0], 4, 16, QLatin1Char('0'))
            .arg(triangle->rawMetadata[1], 4, 16, QLatin1Char('0'))
            .arg(triangle->rawMetadata[2], 4, 16, QLatin1Char('0'))
            .arg(metadata.sourceWord, 4, 16, QLatin1Char('0'))
            .arg(metadata.decodedWord, 4, 16, QLatin1Char('0'))
            .arg(static_cast<unsigned>(metadata.onesDigit))
            .arg(static_cast<unsigned>(metadata.tensDigit))
            .arg(static_cast<unsigned>(metadata.hundredsDigit));
        detail += QStringLiteral(
            "\n  digits: thousands %1, ignored ten-thousands %2"
            "\n  stream winding high bit: %3"
            "\n  traversal: %4"
            "\n  selector: %5%6")
            .arg(static_cast<unsigned>(metadata.thousandsDigit))
            .arg(static_cast<unsigned>(metadata.ignoredTenThousandsDigit))
            .arg(metadata.streamWindingHighBit
                ? QStringLiteral("set") : QStringLiteral("clear"))
            .arg(traversalText(metadata.traversal))
            .arg(triangle->selector <= 8U
                ? QString::number(triangle->selector)
                : QStringLiteral("invalid"))
            .arg(baseline.has_value() && *baseline != triangle->selector
                ? QStringLiteral(" (baseline %1, modified)").arg(*baseline)
                : QString{});
        details.push_back(std::move(detail));
    }

    QString summary{};
    if (mixed || !commonSelector.has_value()) {
        summary = QStringLiteral(
            "%1 triangles selected; encounter selector is mixed")
            .arg(selection.size());
        jumpButton_->setEnabled(false);
        jumpSelector_ = 0;
    } else {
        summary = QStringLiteral(
            "%1 triangle(s); encounter selector %2%3")
            .arg(selection.size())
            .arg(*commonSelector)
            .arg(*commonSelector == 0U
                ? QStringLiteral(" (no encounters)") : QString{});
        jumpSelector_ = static_cast<int>(*commonSelector);
        jumpButton_->setEnabled(jumpSelector_ >= 1 && jumpSelector_ <= 8);
        const auto index = selectorEditor_->findData(jumpSelector_);
        if (index >= 0) selectorEditor_->setCurrentIndex(index);
    }
    if (barrierCount == 1U && selection.size() == 1U) {
        summary += QStringLiteral("; traversal barrier");
    } else if (barrierCount > 0U) {
        summary += QStringLiteral("; %1 of %2 traversal barriers")
            .arg(barrierCount).arg(selection.size());
    }
    summaryLabel_->setText(summary + QLatin1Char('.'));
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
