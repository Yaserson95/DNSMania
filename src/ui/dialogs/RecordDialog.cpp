#include "RecordDialog.h"

#include "core/DnsService.h"
#include "ui/controls/LabeledLineEdit.h"

#include <QFormLayout>
#include <QComboBox>
#include <QLineEdit>
#include <QPointer>
#include <QSpinBox>
#include <QTextEdit>

RecordDialog::RecordDialog(DnsService* svc, const QString& server, const QString& zoneName,
                           const QString& scope, const QString& defaultType, QWidget* parent)
    : BaseDialog(tr("Добавить запись в %1%2")
                     .arg(zoneName, scope.isEmpty() ? QString() : QStringLiteral(" / ") + scope),
                 parent)
    , m_zone(zoneName)
{
    resize(460, 420);

    m_type = new QComboBox(this);
    const QStringList types = { "A", "AAAA", "CNAME", "NS", "MX", "TXT", "PTR", "SRV" };
    m_type->addItems(types);
    nameField()->setPlaceholderText(tr("Имя хоста или @ для корня зоны"));
    m_ttl = new QSpinBox(this);
    m_ttl->setRange(0, 2147483647);
    m_ttl->setValue(3600);
    m_ttl->setSuffix(tr(" сек"));

    form()->addRow(tr("Тип:"), m_type);
    nameField()->setLabelText(tr("Имя:"));
    nameField()->addTo(form());
    form()->addRow(tr("TTL:"), m_ttl);

    // Область (ZoneScope) — есть у всех типов записей, как в dnsmgmt, но
    // с дополнительным полем. Область по умолчанию — пустое значение
    // (запись создаётся без -ZoneScope).
    m_scopeCombo = new QComboBox(this);
    m_scopeCombo->addItem(tr("Область по умолчанию"), QString());
    if (!scope.isEmpty() && scope.compare(zoneName, Qt::CaseInsensitive) != 0)
        m_scopeCombo->addItem(scope, scope);
    form()->addRow(tr("Область:"), m_scopeCombo);

    // Остальные области зоны — из кэша/сервера, асинхронно.
    if (svc && !server.isEmpty()) {
        const QPointer<QComboBox> combo(m_scopeCombo);
        svc->getScopeNames(server, zoneName,
            [combo, zoneName, scope](bool ok, const QStringList& names, const QString&) {
                if (!ok || !combo)
                    return;
                for (const QString& n : names) {
                    if (n.compare(zoneName, Qt::CaseInsensitive) == 0)
                        continue; // это область по умолчанию — уже в списке
                    if (combo->findData(n) >= 0)
                        continue;
                    combo->addItem(n, n);
                }
                const int idx = combo->findData(scope);
                if (idx >= 0)
                    combo->setCurrentIndex(idx);
            });
    }

    // Build type-specific editors once.
    m_ipv4 = new QLineEdit(this);
    m_ipv6 = new QLineEdit(this);
    m_cname = new QLineEdit(this);
    m_ns = new QLineEdit(this);
    m_mx = new QLineEdit(this);
    m_mxPref = new QSpinBox(this);
    m_mxPref->setRange(0, 65535);
    m_txt = new QTextEdit(this);
    m_txt->setMaximumHeight(70);
    m_ptr = new QLineEdit(this);
    m_srvService = new QLineEdit(this);
    m_srvProtocol = new QComboBox(this);
    m_srvProtocol->addItems({ "_tcp", "_udp" });
    m_srvTarget = new QLineEdit(this);
    m_srvPort = new QSpinBox(this);
    m_srvPort->setRange(0, 65535);
    m_srvPriority = new QSpinBox(this);
    m_srvPriority->setRange(0, 65535);
    m_srvWeight = new QSpinBox(this);
    m_srvWeight->setRange(0, 65535);
    m_srvWeight->setValue(100);

    int idx = m_type->findText(defaultType, Qt::MatchFixedString);
    if (idx >= 0)
        m_type->setCurrentIndex(idx);

    connect(m_type, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &RecordDialog::onTypeChanged);
    rebuildForm();
}

void RecordDialog::rebuildForm()
{
    // Статические строки: тип / имя / TTL / область (4 шт.). Дальше — поля
    // типа записи. Важно: takeRow, а НЕ removeRow: начиная с Qt 6.9 removeRow
    // УДАЛЯЕТ виджеты строки, а наши поля — члены класса, переиспользуемые
    // при смене типа (их удаление = висячий указатель и падение при следующей
    // смене типа или при чтении params()). После takeRow виджеты строки
    // (включая сгенерированный QLabel метки) остаются видимыми — прячем
    // метку явно; item'ы удаляем (обёртки, не виджеты).
    while (form()->rowCount() > 4) {
        const QFormLayout::TakeRowResult r = form()->takeRow(4);
        if (r.labelItem && r.labelItem->widget())
            r.labelItem->widget()->hide();
        delete r.labelItem;
        delete r.fieldItem;
    }

    // Поля всех типов создаются сразу (с parent=диалог) и иначе остаются
    // видимыми в углу диалога поверх формы: прячем все, показываем только
    // добавленные в строку ниже.
    for (QWidget* w : { static_cast<QWidget*>(m_ipv4), static_cast<QWidget*>(m_ipv6),
                        static_cast<QWidget*>(m_cname), static_cast<QWidget*>(m_ns),
                        static_cast<QWidget*>(m_mx), static_cast<QWidget*>(m_mxPref),
                        static_cast<QWidget*>(m_txt), static_cast<QWidget*>(m_ptr),
                        static_cast<QWidget*>(m_srvService), static_cast<QWidget*>(m_srvProtocol),
                        static_cast<QWidget*>(m_srvTarget), static_cast<QWidget*>(m_srvPort),
                        static_cast<QWidget*>(m_srvPriority), static_cast<QWidget*>(m_srvWeight) })
        w->hide();

    const QString t = m_type->currentText();
    if (t == QLatin1String("A")) {
        form()->addRow(tr("IPv4-адрес:"), m_ipv4);
        m_ipv4->show();
        m_ipv4->setFocus();
    } else if (t == QLatin1String("AAAA")) {
        form()->addRow(tr("IPv6-адрес:"), m_ipv6);
        m_ipv6->show();
        m_ipv6->setFocus();
    } else if (t == QLatin1String("CNAME")) {
        form()->addRow(tr("Целевое имя:"), m_cname);
        m_cname->show();
        m_cname->setFocus();
    } else if (t == QLatin1String("NS")) {
        form()->addRow(tr("NS-сервер:"), m_ns);
        m_ns->show();
        m_ns->setFocus();
    } else if (t == QLatin1String("MX")) {
        form()->addRow(tr("Mail-сервер:"), m_mx);
        form()->addRow(tr("Приоритет:"), m_mxPref);
        m_mx->show();
        m_mxPref->show();
        m_mx->setFocus();
    } else if (t == QLatin1String("TXT")) {
        form()->addRow(tr("Текст:"), m_txt);
        m_txt->show();
        m_txt->setFocus();
    } else if (t == QLatin1String("PTR")) {
        form()->addRow(tr("PTR-имя:"), m_ptr);
        m_ptr->show();
        m_ptr->setFocus();
    } else if (t == QLatin1String("SRV")) {
        form()->addRow(tr("Сервис:"), m_srvService);
        form()->addRow(tr("Протокол:"), m_srvProtocol);
        form()->addRow(tr("Целевой хост:"), m_srvTarget);
        form()->addRow(tr("Порт:"), m_srvPort);
        form()->addRow(tr("Приоритет:"), m_srvPriority);
        form()->addRow(tr("Вес:"), m_srvWeight);
        m_srvService->show();
        m_srvProtocol->show();
        m_srvTarget->show();
        m_srvPort->show();
        m_srvPriority->show();
        m_srvWeight->show();
        m_srvService->setFocus();
    }
}

void RecordDialog::onTypeChanged(int) { rebuildForm(); }

RecordAddParams RecordDialog::params() const
{
    RecordAddParams p;
    p.zone = m_zone;
    p.scope = m_scopeCombo->currentData().toString();
    p.type = m_type->currentText();
    p.name = name();
    p.ttlSeconds = m_ttl->value();
    p.ipv4 = m_ipv4->text().trimmed();
    p.ipv6 = m_ipv6->text().trimmed();
    p.cnameTarget = m_cname->text().trimmed();
    p.nsServer = m_ns->text().trimmed();
    p.mxServer = m_mx->text().trimmed();
    p.mxPreference = m_mxPref->value();
    p.txtText = m_txt->toPlainText().trimmed();
    p.ptrTarget = m_ptr->text().trimmed();
    p.srvService = m_srvService->text().trimmed();
    p.srvProtocol = m_srvProtocol->currentText();
    p.srvTarget = m_srvTarget->text().trimmed();
    p.srvPort = m_srvPort->value();
    p.srvPriority = m_srvPriority->value();
    p.srvWeight = m_srvWeight->value();
    return p;
}

void RecordDialog::setParams(const RecordAddParams& p)
{
    setWindowTitle(tr("Запись %1 (%2)").arg(p.name, p.type));
    setName(p.name);
    m_ttl->setValue(p.ttlSeconds);
    const int sidx = m_scopeCombo->findData(p.scope);
    if (sidx >= 0)
        m_scopeCombo->setCurrentIndex(sidx);
    const int idx = m_type->findText(p.type, Qt::MatchFixedString);
    if (idx >= 0 && m_type->currentIndex() != idx)
        m_type->setCurrentIndex(idx); // triggers rebuildForm()

    if (p.type.compare(QLatin1String("A"), Qt::CaseInsensitive) == 0)
        m_ipv4->setText(p.ipv4);
    else if (p.type.compare(QLatin1String("AAAA"), Qt::CaseInsensitive) == 0)
        m_ipv6->setText(p.ipv6);
    else if (p.type.compare(QLatin1String("CNAME"), Qt::CaseInsensitive) == 0)
        m_cname->setText(p.cnameTarget);
    else if (p.type.compare(QLatin1String("NS"), Qt::CaseInsensitive) == 0)
        m_ns->setText(p.nsServer);
    else if (p.type.compare(QLatin1String("MX"), Qt::CaseInsensitive) == 0) {
        m_mx->setText(p.mxServer);
        m_mxPref->setValue(p.mxPreference);
    } else if (p.type.compare(QLatin1String("TXT"), Qt::CaseInsensitive) == 0)
        m_txt->setPlainText(p.txtText);
    else if (p.type.compare(QLatin1String("PTR"), Qt::CaseInsensitive) == 0)
        m_ptr->setText(p.ptrTarget);
    else if (p.type.compare(QLatin1String("SRV"), Qt::CaseInsensitive) == 0) {
        m_srvService->setText(p.srvService);
        const int pi = m_srvProtocol->findText(p.srvProtocol, Qt::MatchFixedString);
        if (pi >= 0)
            m_srvProtocol->setCurrentIndex(pi);
        m_srvTarget->setText(p.srvTarget);
        m_srvPort->setValue(p.srvPort);
        m_srvPriority->setValue(p.srvPriority);
        m_srvWeight->setValue(p.srvWeight);
    }
}
