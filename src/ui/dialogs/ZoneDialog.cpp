#include "ZoneDialog.h"

#include "ui/controls/LabeledLineEdit.h"

#include <QFormLayout>
#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>

ZoneDialog::ZoneDialog(QWidget* parent)
    : BaseDialog(tr("Добавить зону"), parent)
{
    m_type = new QComboBox(this);
    m_type->addItems({ tr("Primary"), tr("Secondary"), tr("Stub") });
    // Store English identifiers for command building.
    m_type->setItemData(0, QStringLiteral("Primary"), Qt::UserRole);
    m_type->setItemData(1, QStringLiteral("Secondary"), Qt::UserRole);
    m_type->setItemData(2, QStringLiteral("Stub"), Qt::UserRole);

    m_reverse = new QCheckBox(tr("Обратная зона (по сети)"), this);

    // Name field: placed into the form after the type row.
    nameField()->setLabelText(tr("Имя зоны:"));
    nameField()->setPlaceholderText(tr("example.com"));

    m_networkId = new LabeledLineEdit(tr("Сеть (CIDR):"), this);
    m_networkId->setPlaceholderText(tr("192.168.1.0/24"));
    m_networkId->setVisible(false);

    m_scope = new QComboBox(this);
    m_scope->addItems({ tr("None (файловая)"), "Custom", "Domain", "Forest" });
    m_scope->setItemData(0, QStringLiteral("None"), Qt::UserRole);
    m_scope->setItemData(1, QStringLiteral("Custom"), Qt::UserRole);
    m_scope->setItemData(2, QStringLiteral("Domain"), Qt::UserRole);
    m_scope->setItemData(3, QStringLiteral("Forest"), Qt::UserRole);
    m_scope->setCurrentIndex(3);

    m_dynUpdate = new QComboBox(this);
    m_dynUpdate->addItems({ tr("None"), tr("NonsecureAndSecure"), tr("Secure") });
    m_dynUpdate->setItemData(0, QStringLiteral("None"), Qt::UserRole);
    m_dynUpdate->setItemData(1, QStringLiteral("NonsecureAndSecure"), Qt::UserRole);
    m_dynUpdate->setItemData(2, QStringLiteral("Secure"), Qt::UserRole);
    m_dynUpdate->setCurrentIndex(2);

    m_masters = new QLineEdit(this);
    m_masters->setPlaceholderText(tr("8.8.8.8, 8.8.4.4"));
    m_masters->setVisible(false);

    form()->addRow(tr("Тип:"), m_type);
    form()->addRow(QString(), m_reverse);
    nameField()->addTo(form());
    m_networkId->addTo(form());
    form()->addRow(tr("Область репликации:"), m_scope);
    form()->addRow(tr("Динамическое обновление:"), m_dynUpdate);
    form()->addRow(tr("Master-серверы:"), m_masters);

    connect(m_type, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ZoneDialog::onTypeChanged);
    connect(m_reverse, &QCheckBox::toggled, this, &ZoneDialog::onReverseChanged);
    onTypeChanged(m_type->currentIndex());
}

void ZoneDialog::onTypeChanged(int)
{
    const QString t = zoneType();
    const bool isPrimary = (t == QLatin1String("Primary"));
    m_masters->setVisible(!isPrimary);
    m_dynUpdate->setVisible(isPrimary);
    m_reverse->setVisible(isPrimary);
    if (!isPrimary)
        m_reverse->setChecked(false);
    onReverseChanged(m_reverse->isChecked());
}

void ZoneDialog::onReverseChanged(bool checked)
{
    nameField()->setVisible(!checked);
    m_networkId->setVisible(checked);
}

QString ZoneDialog::zoneType() const { return m_type->currentData(Qt::UserRole).toString(); }
bool ZoneDialog::isReverse() const { return m_reverse->isChecked(); }
QString ZoneDialog::networkId() const { return m_networkId->text(); }
QString ZoneDialog::replicationScope() const { return m_scope->currentData(Qt::UserRole).toString(); }
QString ZoneDialog::dynamicUpdate() const { return m_dynUpdate->currentData(Qt::UserRole).toString(); }
QStringList ZoneDialog::masters() const
{
    QStringList out;
    const QStringList parts = m_masters->text().split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const QString& p : parts) {
        const QString t = p.trimmed();
        if (!t.isEmpty())
            out << t;
    }
    return out;
}
