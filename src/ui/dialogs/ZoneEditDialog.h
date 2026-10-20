#pragma once

#include "ui/dialogs/BaseDialog.h"

#include <QJsonObject>
#include <QStringList>

class QComboBox;
class QLineEdit;

// Свойства существующей зоны (правка через Set-DnsServer*Zone).
// Primary: динамическое обновление и область репликации (AD-зоны);
// Secondary/Stub: список мастер-серверов; Forwarder — только просмотр.
// Имя и тип зоны не редактируются (для этого зона удаляется и создаётся).
class ZoneEditDialog : public BaseDialog
{
    Q_OBJECT
public:
    explicit ZoneEditDialog(const QJsonObject& zone, QWidget* parent = nullptr);

    // Значения для Set-DnsServerPrimaryZone.
    QString dynamicUpdate() const;
    QString replicationScope() const; // "None" — файловая зона (параметр не передаётся)
    // Мастер-серверы (Secondary/Stub).
    QStringList masters() const;

private:
    QString m_type;
    bool m_ds = false;
    QComboBox* m_scope = nullptr;
    QComboBox* m_dynUpdate = nullptr;
    QLineEdit* m_masters = nullptr;
};
