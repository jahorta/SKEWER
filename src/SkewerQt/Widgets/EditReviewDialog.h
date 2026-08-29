#pragma once

#include "../Session/WorkspaceController.h"

#include <QDialog>

namespace skewer::core { struct FieldDocument; }

namespace skewer::qt {

class EditReviewDialog final : public QDialog {
public:
    EditReviewDialog(
        const CurrentFieldPatchSnapshot& snapshot,
        const skewer::core::FieldDocument& document,
        QWidget* parent = nullptr);
};

} // namespace skewer::qt
