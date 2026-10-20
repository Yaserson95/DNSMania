#pragma once

#include "ui/dialogs/BaseDialog.h"

class QCheckBox;
class QLineEdit;

// Add / edit a managed DNS server.
class ServerDialog : public BaseDialog
{
    Q_OBJECT
public:
    explicit ServerDialog(QWidget* parent = nullptr);
    void setDescription(const QString& description);
    QString description() const;

    // true — управлять сервером локальным PowerShell с -ComputerName
    // (в обход WinRM; нужен RSAT DNS на рабочей машине).
    void setLocalPs(bool local);
    bool localPs() const;

    // Учётные данные WinRM: "DOMAIN\user" или "user@domain"; пусто — текущий
    // пользователь Windows. Пароль вводится под маской и хранится в SecureStore
    // (DPAPI), в реестре открытым текстом не лежит.
    void setUserName(const QString& userName);
    QString userName() const;
    void setPassword(const QString& password);
    QString password() const;

private:
    QLineEdit* m_desc;
    QCheckBox* m_local;
    QLineEdit* m_user;
    QLineEdit* m_pass;
};
