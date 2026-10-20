#pragma once

#include "ui/dialogs/BaseDialog.h"

#include <QStringList>

class LineListEdit;

// Add / edit a client subnet. Address lists are LineListEdit controls
// (one value per line).
class SubnetDialog : public BaseDialog
{
    Q_OBJECT
public:
    explicit SubnetDialog(QWidget* parent = nullptr);
    void setSubnets(const QStringList& ipv4, const QStringList& ipv6);
    QStringList ipv4() const;
    QStringList ipv6() const;

private:
    LineListEdit* m_ipv4;
    LineListEdit* m_ipv6;
};
