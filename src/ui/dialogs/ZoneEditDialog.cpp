#include "ZoneEditDialog.h"

#include "core/JsonUtil.h"
#include "ui/controls/LabeledLineEdit.h"

#include <QComboBox>
#include <QFormLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>

namespace {

QStringList jsonToStringList(const QJsonValue& v)
{
    QStringList out;
    const QJsonArray arr = v.toArray();
    for (const QJsonValue& x : arr) {
        const QString s = x.toString().trimmed();
        if (!s.isEmpty())
            out << s;
    }
    return out;
}

} // namespace

ZoneEditDialog::ZoneEditDialog(const QJsonObject& zone, QWidget* parent)
    : BaseDialog(tr("Свойства зоны"), parent)
    , m_type(Json::propStr(zone, { QStringLiteral("ZoneType") }))
    , m_ds(Json::propBool(zone, { QStringLiteral("IsDsIntegrated") }))
{
    const QString name = Json::propStr(zone, { QStringLiteral("ZoneName") });
    setWindowTitle(tr("Свойства зоны %1").arg(name));
    resize(420, 260);

    nameField()->setLabelText(tr("Имя зоны:"));
    nameField()->setText(name);
    nameField()->edit()->setReadOnly(true);
    nameField()->addTo(form());

    form()->addRow(tr("Тип:"), new QLabel(m_type, this));

    const bool isPrimary = (m_type == QLatin1String("Primary"));
    const bool isSecondary = (m_type == QLatin1String("Secondary"));
    const bool isStub = (m_type == QLatin1String("Stub"));

    if (isPrimary) {
        m_dynUpdate = new QComboBox(this);
        m_dynUpdate->addItems({ tr("None"), tr("NonsecureAndSecure"), tr("Secure") });
        m_dynUpdate->setItemData(0, QStringLiteral("None"), Qt::UserRole);
        m_dynUpdate->setItemData(1, QStringLiteral("NonsecureAndSecure"), Qt::UserRole);
        m_dynUpdate->setItemData(2, QStringLiteral("Secure"), Qt::UserRole);
        const QString cur = Json::propStr(zone, { QStringLiteral("DynamicUpdate") });
        int idx = m_dynUpdate->findData(cur, Qt::UserRole);
        if (idx < 0)
            idx = m_dynUpdate->findData(QStringLiteral("Secure"), Qt::UserRole);
        m_dynUpdate->setCurrentIndex(idx);
        form()->addRow(tr("Динамическое обновление:"), m_dynUpdate);

        // Область репликации — только у AD-интегрированных зон.
        const QString repl = Json::propStr(zone, { QStringLiteral("ReplicationScope") });
        if (m_ds && !repl.isEmpty()) {
            m_scope = new QComboBox(this);
            m_scope->addItems({ "Domain", "Forest", "Custom" });
            m_scope->setItemData(0, QStringLiteral("Domain"), Qt::UserRole);
            m_scope->setItemData(1, QStringLiteral("Forest"), Qt::UserRole);
            m_scope->setItemData(2, QStringLiteral("Custom"), Qt::UserRole);
            int sidx = m_scope->findData(repl, Qt::UserRole);
            if (sidx < 0)
                sidx = 0;
            m_scope->setCurrentIndex(sidx);
            form()->addRow(tr("Область репликации:"), m_scope);
        }
    } else if (isSecondary || isStub) {
        m_masters = new QLineEdit(this);
        m_masters->setPlaceholderText(tr("8.8.8.8, 8.8.4.4"));
        m_masters->setText(
            jsonToStringList(zone.value(QStringLiteral("MasterServers"))).join(QLatin1String(", ")));
        form()->addRow(tr("Master-серверы:"), m_masters);
    } else {
        // Forwarder и прочие типы: только просмотр (правка через dnsmgmt).
        const QString masters =
            jsonToStringList(zone.value(QStringLiteral("MasterServers"))).join(QLatin1String(", "));
        auto* info = new QLabel(masters.isEmpty() ? tr("—") : masters, this);
        info->setWordWrap(true);
        form()->addRow(tr("Master-серверы:"), info);
    }
}

QString ZoneEditDialog::dynamicUpdate() const
{
    return m_dynUpdate ? m_dynUpdate->currentData(Qt::UserRole).toString() : QString();
}

QString ZoneEditDialog::replicationScope() const
{
    return m_scope ? m_scope->currentData(Qt::UserRole).toString() : QStringLiteral("None");
}

QStringList ZoneEditDialog::masters() const
{
    QStringList out;
    if (!m_masters)
        return out;
    const QStringList parts = m_masters->text().split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const QString& p : parts) {
        const QString t = p.trimmed();
        if (!t.isEmpty())
            out << t;
    }
    return out;
}
