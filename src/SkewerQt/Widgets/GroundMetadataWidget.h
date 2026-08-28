#pragma once

#include <QWidget>

#include <cstdint>

class QLabel;

namespace skewer::qt {

class GroundMetadataWidget final : public QWidget {
public:
    explicit GroundMetadataWidget(QWidget* parent = nullptr);

    void setTblId(std::int32_t tblId);
    void clear();

private:
    QLabel* valueLabel_ = nullptr;
};

} // namespace skewer::qt
