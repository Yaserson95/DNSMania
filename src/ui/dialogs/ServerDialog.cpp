#include "ServerDialog.h"

#include <QCheckBox>
#include <QFormLayout>
#include <QLineEdit>

ServerDialog::ServerDialog(QWidget* parent)
    : BaseDialog(tr("DNS-сервер"), parent)
{
    setNamePlaceholder(tr("dns01.contoso.com или 10.0.0.10"));
    addNameRow(tr("Имя / адрес:"));

    m_desc = new QLineEdit(this);
    form()->addRow(tr("Описание:"), m_desc);

    m_local = new QCheckBox(tr("Локальный PowerShell с -ComputerName (RSAT DNS)"), this);
    m_local->setToolTip(
        tr("Команды выполняются локальным PowerShell с параметром -ComputerName, "
           "как в оснастке DNS (dnsmgmt.msc), вместо WinRM-ремоутинга.\n"
           "Полезно, если WinRM запрещён настройками или требует запуска от "
           "администратора. На этой машине должен быть модуль DnsServer "
           "(компонент «Средства удалённого администрирования DNS-сервера»)."));
    form()->addRow(QString(), m_local);

    m_user = new QLineEdit(this);
    m_user->setPlaceholderText(tr("DOMAIN\\user или user@domain.local"));
    m_user->setToolTip(
        tr("Учётные данные WinRM. По умолчанию (поле пустое) — текущий пользователь "
           "Windows. Для цели, указанной IP-адресом, WinRM использует NTLM и требует "
           "И явные учётные данные (это поле), И одно из: HTTPS либо наличие сервера "
           "в TrustedHosts на этой рабочей машине (точную команду см. в подсказке "
           "при ошибке подключения). Для FQDN обычно ничего заполнять не нужно."));
    form()->addRow(tr("Пользователь WinRM:"), m_user);

    m_pass = new QLineEdit(this);
    m_pass->setEchoMode(QLineEdit::Password);
    m_pass->setPlaceholderText(tr("(оставьте пустым = без пароля)"));
    m_pass->setToolTip(
        tr("Пароль WinRM. Хранится в хранилище учётных данных Windows (DPAPI), "
           "привязан к вашему пользователю, и не передаётся в командную строку "
           "PowerShell."));
    form()->addRow(tr("Пароль WinRM:"), m_pass);
}

void ServerDialog::setDescription(const QString& description) { m_desc->setText(description); }
QString ServerDialog::description() const { return m_desc->text().trimmed(); }

void ServerDialog::setLocalPs(bool local) { m_local->setChecked(local); }
bool ServerDialog::localPs() const { return m_local->isChecked(); }

void ServerDialog::setUserName(const QString& userName) { m_user->setText(userName); }
QString ServerDialog::userName() const { return m_user->text().trimmed(); }

void ServerDialog::setPassword(const QString& password) { m_pass->setText(password); }
QString ServerDialog::password() const { return m_pass->text(); }
