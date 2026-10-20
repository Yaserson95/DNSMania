#pragma once

#include "core/RecordService.h"
#include "ui/dialogs/BaseDialog.h"

class DnsService;
class QComboBox;
class QSpinBox;
class QLineEdit;
class QTextEdit;

// Add a resource record. Type-specific fields are shown/hidden by type.
// Поле «Область» (ZoneScope) есть у всех типов записей: по умолчанию —
// «Область по умолчанию», остальные области зоны подгружаются из кэша.
class RecordDialog : public BaseDialog
{
    Q_OBJECT
public:
    explicit RecordDialog(DnsService* svc, const QString& server, const QString& zoneName,
                          const QString& scope,
                          const QString& defaultType = QStringLiteral("A"),
                          QWidget* parent = nullptr);

    RecordAddParams params() const;
    // Prefills the dialog from an existing record (used for editing).
    void setParams(const RecordAddParams& p);

private slots:
    void onTypeChanged(int index);

private:
    void rebuildForm();

    QString m_zone;
    QComboBox* m_scopeCombo;
    QComboBox* m_type;
    QSpinBox* m_ttl;

    // Type-specific editors (reused across type switches).
    QLineEdit* m_ipv4;
    QLineEdit* m_ipv6;
    QLineEdit* m_cname;
    QLineEdit* m_ns;
    QLineEdit* m_mx;
    QSpinBox* m_mxPref;
    QTextEdit* m_txt;
    QLineEdit* m_ptr;
    QLineEdit* m_srvService;
    QComboBox* m_srvProtocol;
    QLineEdit* m_srvTarget;
    QSpinBox* m_srvPort;
    QSpinBox* m_srvPriority;
    QSpinBox* m_srvWeight;
};
