#pragma once

#include "core/DnsService.h"

#include <QDialog>

class QLineEdit;
class QComboBox;
class QCheckBox;
class QTextEdit;
class QLabel;
class QFormLayout;
class QVBoxLayout;
class QGroupBox;
class QRadioButton;
class QPushButton;
class QTableWidget;

// Add / edit a query resolution policy. The form is dynamic: sections
// enable/disable according to the chosen level (server/zone) and the checked
// criteria; the resulting PowerShell cmdlet is previewed live.
class PolicyDialog : public QDialog
{
    Q_OBJECT
public:
    PolicyDialog(DnsService* service, const QString& server,
                 const QStringList& zones, QWidget* parent = nullptr);
    void setParams(const DNSPolicy& p);
    DNSPolicy params() const;
    void setZoneLevel(const QString& zone);
    void accept() override;

private slots:
    void onLevelChanged();
    void onZoneTextChanged(const QString& zone);
    void updatePreview();
    void addWeightRow(const QString& scope = QString(), int weight = 1);

private:
    void loadScopesForZone(const QString& zone);
    void loadSubnetNames();
    QStringList weightRows() const;

    DnsService* m_service = nullptr;
    QString m_server;

    // Основные параметры.
    QLineEdit* m_name;
    QCheckBox* m_levelZone; // вкл. = политика на уровне зоны
    QComboBox* m_zone;
    QComboBox* m_action; // ALLOW | DENY | IGNORE (ALLOW — только для зон)

    // Область зоны (веса балансировки).
    QGroupBox* m_scopeGroup;
    QTableWidget* m_weights;

    // Критерии фильтрации.
    QRadioButton* m_condAnd;
    QRadioButton* m_condOr;
    QCheckBox* m_useSubnet;
    QComboBox* m_subnetOp;
    QComboBox* m_subnetValue;
    QCheckBox* m_useFqdn;
    QComboBox* m_fqdnOp;
    QLineEdit* m_fqdnValue;
    QCheckBox* m_useQtype;
    QComboBox* m_qtypeOp;
    QLineEdit* m_qtypeValue;
    QCheckBox* m_useTime;
    QComboBox* m_timeOp;
    QLineEdit* m_timeValue;
    QRadioButton* m_trUdp;
    QRadioButton* m_trTcp;
    QRadioButton* m_trAny;

    // Предпросмотр команды.
    QTextEdit* m_preview;

    QStringList m_scopeNames; // загруженные области выбранной зоны

    // The policy as it was loaded (or a default one for a new policy).
    // params() starts from it, so fields the dialog does not show
    // (enabled, processingOrder, otherCriteria, the loaded snapshot used to
    // pick Set- vs Remove+Add) survive an edit.
    DNSPolicy m_loaded;
};
