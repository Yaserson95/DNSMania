#pragma once

#include "ui/dialogs/BaseDialog.h"

#include <QStringList>

class QComboBox;
class QCheckBox;
class QLineEdit;
class LabeledLineEdit;

// Add a DNS zone (forward/reverse, primary/secondary/stub).
// The name field and the CIDR network field swap places when the reverse
// option is checked — both are LabeledLineEdit controls, toggled as units.
class ZoneDialog : public BaseDialog
{
    Q_OBJECT
public:
    explicit ZoneDialog(QWidget* parent = nullptr);
    QString zoneType() const;
    bool isReverse() const;
    QString networkId() const;
    QString replicationScope() const;
    QString dynamicUpdate() const;
    QStringList masters() const;

private slots:
    void onTypeChanged(int index);
    void onReverseChanged(bool checked);

private:
    QComboBox* m_type;
    QCheckBox* m_reverse;
    LabeledLineEdit* m_networkId;
    QComboBox* m_scope;
    QComboBox* m_dynUpdate;
    QLineEdit* m_masters;
};
