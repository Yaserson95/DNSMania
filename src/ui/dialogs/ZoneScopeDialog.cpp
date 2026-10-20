#include "ZoneScopeDialog.h"

ZoneScopeDialog::ZoneScopeDialog(const QString& zoneName, QWidget* parent)
    : BaseDialog(tr("Добавить область (zone scope) в %1").arg(zoneName), parent)
{
    setNamePlaceholder(tr("Имя области, напр. Subnet1Scope"));
    addNameRow(tr("Имя области:"));
}
