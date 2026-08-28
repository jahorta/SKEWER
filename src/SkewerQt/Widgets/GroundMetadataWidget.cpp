#include "GroundMetadataWidget.h"

#include <QLabel>
#include <QVBoxLayout>

namespace skewer::qt {

GroundMetadataWidget::GroundMetadataWidget(QWidget* parent)
    : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(QStringLiteral("tblId"), this));
    valueLabel_ = new QLabel(this);
    valueLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(valueLabel_);
    layout->addStretch(1);
    clear();
}

void GroundMetadataWidget::setTblId(const std::int32_t tblId) {
    valueLabel_->setText(QStringLiteral("0x%1 (%2)")
        .arg(static_cast<std::uint32_t>(tblId), 8, 16, QLatin1Char('0'))
        .arg(tblId)
        .toUpper());
}

void GroundMetadataWidget::clear() {
    valueLabel_->setText(QStringLiteral("No GRND selected"));
}

} // namespace skewer::qt
