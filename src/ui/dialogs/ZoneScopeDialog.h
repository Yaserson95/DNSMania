#pragma once

#include "ui/dialogs/BaseDialog.h"

// Add a zone scope within a zone.
class ZoneScopeDialog : public BaseDialog
{
    Q_OBJECT
public:
    explicit ZoneScopeDialog(const QString& zoneName, QWidget* parent = nullptr);
};
