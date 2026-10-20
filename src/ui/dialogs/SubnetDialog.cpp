#include "SubnetDialog.h"

#include "ui/controls/LineListEdit.h"

#include <QFormLayout>

SubnetDialog::SubnetDialog(QWidget* parent)
    : BaseDialog(tr("Подсет клиентов"), parent)
{
    resize(420, 320);
    setNamePlaceholder(tr("Имя подсети, напр. BranchOffice"));
    addNameRow(tr("Имя:"));

    m_ipv4 = new LineListEdit(this);
    m_ipv4->setPlaceholderText(tr("192.168.1.0/24\n10.0.0.0/8"));
    m_ipv4->setMaximumHeight(90);
    m_ipv6 = new LineListEdit(this);
    m_ipv6->setPlaceholderText(tr("fd00::/64"));
    m_ipv6->setMaximumHeight(70);
    form()->addRow(tr("IPv4-подсети:"), m_ipv4);
    form()->addRow(tr("IPv6-подсети:"), m_ipv6);
}

void SubnetDialog::setSubnets(const QStringList& ipv4, const QStringList& ipv6)
{
    m_ipv4->setLines(ipv4);
    m_ipv6->setLines(ipv6);
}
QStringList SubnetDialog::ipv4() const { return m_ipv4->lines(); }
QStringList SubnetDialog::ipv6() const { return m_ipv6->lines(); }
