#pragma once

#include "SkewerCore/FieldDocument.h"

#include <QWidget>

#include <set>

class QCheckBox;
class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;

namespace skewer::qt {

class TriangleInspectorWidget final : public QWidget {
    Q_OBJECT

public:
    explicit TriangleInspectorWidget(QWidget* parent = nullptr);

    void showSelection(
        const skewer::core::FieldDocument* document,
        const std::set<skewer::core::TriangleKey,
            skewer::core::TriangleKeyLess>& selection,
        bool writable);
    void setExpertMode(bool enabled);

    [[nodiscard]] int selectorValue() const;
    [[nodiscard]] int jumpSelector() const noexcept;
    [[nodiscard]] bool expertMode() const;

signals:
    void applyRequested();
    void jumpRequested();
    void expertModeChanged(bool enabled);

private:
    QLabel* summaryLabel_ = nullptr;
    QComboBox* selectorEditor_ = nullptr;
    QPushButton* applyButton_ = nullptr;
    QPushButton* jumpButton_ = nullptr;
    QCheckBox* expertCheck_ = nullptr;
    QPlainTextEdit* metadataView_ = nullptr;
    int jumpSelector_ = 0;
};

} // namespace skewer::qt
