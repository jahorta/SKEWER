#pragma once

#include "SkewerCore/AlxEnrichment.h"

#include <QWidget>

class QLabel;
class QTableWidget;

namespace skewer::qt {

class FormationInspectorWidget final : public QWidget {
public:
    explicit FormationInspectorWidget(QWidget* parent = nullptr);

    void showLoading(const QString& rootPath);
    void showUnavailable(bool rememberedRoot);
    void showLoadedSource(const QString& locale, const QString& rootPath);
    void showSelectionPrompt();
    void showInvalidSelection();
    void showFormation(const skewer::core::FormationResolution& formation);

private:
    void clearRows();

    QLabel* statusLabel_ = nullptr;
    QLabel* headerLabel_ = nullptr;
    QTableWidget* formationTable_ = nullptr;
};

} // namespace skewer::qt
