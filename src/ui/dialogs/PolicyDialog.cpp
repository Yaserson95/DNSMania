#include "PolicyDialog.h"

#include "core/Log.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFont>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QTableWidget>
#include <QTextEdit>
#include <QVBoxLayout>

PolicyDialog::PolicyDialog(DnsService* service, const QString& server,
                           const QStringList& zones, QWidget* parent)
    : QDialog(parent)
    , m_service(service)
    , m_server(server)
{
    setWindowTitle(tr("Политика разрешения DNS-запросов"));
    resize(680, 800);
    Log::info(QStringLiteral("PolicyDialog: opened (add)"));

    auto* main = new QVBoxLayout(this);

    // ----- Основные параметры.
    auto* basicBox = new QGroupBox(tr("Основные параметры"), this);
    auto* basic = new QFormLayout(basicBox);

    m_name = new QLineEdit(basicBox);
    m_name->setPlaceholderText(tr("Имя политики"));
    basic->addRow(tr("Имя (-Name):"), m_name);

    auto* levelRow = new QHBoxLayout;
    m_levelZone = new QCheckBox(tr("На зону"), basicBox);
    m_zone = new QComboBox(basicBox);
    m_zone->setEditable(true);
    m_zone->addItems(zones);
    m_zone->setPlaceholderText(tr("Выберите зону..."));
    m_zone->setEnabled(false);
    levelRow->addWidget(m_levelZone);
    levelRow->addWidget(m_zone, 1);
    basic->addRow(tr("Уровень (-Level):"), levelRow);

    m_action = new QComboBox(basicBox);
    m_action->addItem(tr("ALLOW (разрешить)"), QStringLiteral("ALLOW"));
    m_action->addItem(tr("DENY (отклонить)"), QStringLiteral("DENY"));
    m_action->addItem(tr("IGNORE (игнорировать)"), QStringLiteral("IGNORE"));
    m_action->setCurrentIndex(1); // DENY по умолчанию
    basic->addRow(tr("Действие (-Action):"), m_action);

    auto* warn = new QLabel(tr("ALLOW доступен только для политик на уровне зоны."), basicBox);
    warn->setStyleSheet(QStringLiteral("color: #a05a00;"));
    basic->addRow(QString(), warn);
    main->addWidget(basicBox);

    // ----- Область зоны (веса балансировки Geo-DNS).
    m_scopeGroup = new QGroupBox(tr("Область зоны (-ZoneScope) — веса балансировки"), this);
    auto* scopeL = new QVBoxLayout(m_scopeGroup);
    m_weights = new QTableWidget(0, 2, m_scopeGroup);
    m_weights->setHorizontalHeaderLabels({ tr("Область (scope)"), tr("Вес") });
    m_weights->horizontalHeader()->setStretchLastSection(true);
    m_weights->verticalHeader()->setVisible(false);
    m_weights->setMaximumHeight(150);
    scopeL->addWidget(m_weights);
    auto* scopeBtns = new QHBoxLayout;
    auto* addW = new QPushButton(tr("Добавить строку"), m_scopeGroup);
    auto* delW = new QPushButton(tr("Удалить строку"), m_scopeGroup);
    scopeBtns->addWidget(addW);
    scopeBtns->addWidget(delW);
    scopeBtns->addStretch();
    scopeL->addLayout(scopeBtns);
    m_scopeGroup->setEnabled(false); // активна только для уровня «На зону»
    main->addWidget(m_scopeGroup);

    // ----- Критерии фильтрации.
    auto* critBox = new QGroupBox(tr("Критерии фильтрации"), this);
    auto* crit = new QVBoxLayout(critBox);

    auto* condRow = new QHBoxLayout;
    condRow->addWidget(new QLabel(tr("Условие (-Condition):"), critBox));
    m_condAnd = new QRadioButton(tr("И (AND)"), critBox);
    m_condOr = new QRadioButton(tr("ИЛИ (OR)"), critBox);
    condRow->addWidget(m_condAnd);
    condRow->addWidget(m_condOr);
    condRow->addStretch();
    crit->addLayout(condRow);
    // Условие и Транспорт лежат в одном QGroupBox: радиокнопки с общим
    // parent образуют одну авто-эксклюзивную группу, и выбор транспорта
    // сбрасывал выбранное условие. Каждому набору — своя QButtonGroup.
    auto* condGroup = new QButtonGroup(this);
    condGroup->addButton(m_condAnd);
    condGroup->addButton(m_condOr);
    m_condAnd->setChecked(true); // условие И по умолчанию

    // Чекбокс + оператор (eq/ne) + значение; оператор включается с чекбоксом.
    auto criteriaRow = [this, critBox, crit](const QString& title, QCheckBox** box,
                                             QComboBox** op, QWidget* value) {
        auto* use = new QCheckBox(title, critBox);
        crit->addWidget(use);
        auto* opCombo = new QComboBox(critBox);
        opCombo->addItem(tr("Равно (eq)"), QStringLiteral("eq"));
        opCombo->addItem(tr("Не равно (ne)"), QStringLiteral("ne"));
        opCombo->setEnabled(false);
        auto* row = new QHBoxLayout;
        row->addSpacing(24);
        row->addWidget(new QLabel(tr("Оператор:"), critBox));
        row->addWidget(opCombo);
        row->addWidget(new QLabel(tr("Значение:"), critBox));
        row->addWidget(value, 1);
        crit->addLayout(row);
        connect(use, &QCheckBox::toggled, opCombo, &QWidget::setEnabled);
        *box = use;
        *op = opCombo;
    };

    m_subnetValue = new QComboBox(critBox);
    m_subnetValue->setEditable(true);
    m_subnetValue->setPlaceholderText(tr("Выберите подсеть из списка..."));
    criteriaRow(tr("Клиентская подсеть (-ClientSubnet)"), &m_useSubnet, &m_subnetOp, m_subnetValue);

    m_fqdnValue = new QLineEdit(critBox);
    m_fqdnValue->setPlaceholderText(tr("*.malicious-site.com"));
    criteriaRow(tr("Имя домена (-Fqdn)"), &m_useFqdn, &m_fqdnOp, m_fqdnValue);

    m_qtypeValue = new QLineEdit(critBox);
    m_qtypeValue->setPlaceholderText(tr("A, AAAA, TXT"));
    criteriaRow(tr("Тип записи (-QType)"), &m_useQtype, &m_qtypeOp, m_qtypeValue);

    m_timeValue = new QLineEdit(critBox);
    m_timeValue->setPlaceholderText(tr("22:00-06:00"));
    criteriaRow(tr("Время суток (-TimeOfDay)"), &m_useTime, &m_timeOp, m_timeValue);

    auto* trRow = new QHBoxLayout;
    trRow->addWidget(new QLabel(tr("Транспорт (-TransportProtocol):"), critBox));
    m_trUdp = new QRadioButton(QStringLiteral("UDP"), critBox);
    m_trTcp = new QRadioButton(QStringLiteral("TCP"), critBox);
    m_trAny = new QRadioButton(tr("Оба"), critBox);
    trRow->addWidget(m_trUdp);
    trRow->addWidget(m_trTcp);
    trRow->addWidget(m_trAny);
    trRow->addStretch();
    crit->addLayout(trRow);
    auto* trGroup = new QButtonGroup(this); // см. комментарий про condGroup
    trGroup->addButton(m_trUdp);
    trGroup->addButton(m_trTcp);
    trGroup->addButton(m_trAny);
    m_trAny->setChecked(true); // «Оба» по умолчанию
    main->addWidget(critBox);

    // ----- Предпросмотр команды PowerShell.
    auto* prevBox = new QGroupBox(tr("Предпросмотр команды PowerShell"), this);
    auto* prevL = new QVBoxLayout(prevBox);
    m_preview = new QTextEdit(prevBox);
    m_preview->setReadOnly(true);
    m_preview->setMaximumHeight(140); // recreate preview shows two commands
    QFont mono(QStringLiteral("Consolas"));
    mono.setStyleHint(QFont::Monospace);
    m_preview->setFont(mono);
    prevL->addWidget(m_preview);
    main->addWidget(prevBox);

    main->addStretch();

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Save)->setText(tr("Сохранить"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Отмена"));
    main->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // ----- Live preview: обновляем при любом изменении формы.
    auto update = [this] { updatePreview(); };
    connect(m_name, &QLineEdit::textChanged, this, update);
    connect(m_zone, &QComboBox::currentTextChanged, this, update);
    for (QRadioButton* b : { m_condAnd, m_condOr, m_trUdp, m_trTcp, m_trAny })
        connect(b, &QRadioButton::toggled, this, update);
    for (QCheckBox* c : { m_levelZone, m_useSubnet, m_useFqdn, m_useQtype, m_useTime })
        connect(c, &QCheckBox::toggled, this, update);
    for (QComboBox* c : { m_action, m_subnetOp, m_fqdnOp, m_qtypeOp, m_timeOp })
        connect(c, &QComboBox::currentIndexChanged, this, update);
    connect(m_subnetValue, &QComboBox::currentTextChanged, this, update);
    for (QLineEdit* e : { m_fqdnValue, m_qtypeValue, m_timeValue })
        connect(e, &QLineEdit::textChanged, this, update);

    // ----- Логика уровней, зоны и весов.
    connect(m_levelZone, &QCheckBox::toggled, this, &PolicyDialog::onLevelChanged);
    connect(m_zone, &QComboBox::currentTextChanged, this, &PolicyDialog::onZoneTextChanged);
    connect(addW, &QPushButton::clicked, this, [this] { addWeightRow(); });
    connect(delW, &QPushButton::clicked, this, [this] {
        const int r = m_weights->currentRow();
        if (r >= 0)
            m_weights->removeRow(r);
    });

    onLevelChanged();
    if (m_service)
        loadSubnetNames();
    updatePreview();
}

void PolicyDialog::onLevelChanged()
{
    const bool zone = m_levelZone->isChecked();
    m_zone->setEnabled(zone);
    m_scopeGroup->setEnabled(zone);
    // ALLOW — только для зон: гасим пункт списка на серверном уровне.
    // QStandardItem::setEnabled надёжно и гасит пункт, и блокирует клик
    // (в отличие от setItemData(..., Qt::UserRole - 1)).
    if (auto* model = qobject_cast<QStandardItemModel*>(m_action->model())) {
        if (QStandardItem* allow = model->item(0))
            allow->setEnabled(zone);
    }
    if (!zone && m_action->currentData().toString() == QLatin1String("ALLOW"))
        m_action->setCurrentIndex(m_action->findData(QStringLiteral("DENY")));
    updatePreview();
}

void PolicyDialog::onZoneTextChanged(const QString& zone)
{
    loadScopesForZone(zone);
    updatePreview();
}

void PolicyDialog::loadScopesForZone(const QString& zone)
{
    if (!m_service || zone.trimmed().isEmpty())
        return;
    Log::info(QStringLiteral("PolicyDialog: loading scope names for '%1'").arg(zone.trimmed()));
    // QPointer: диалог может быть закрыт до прихода ответа.
    const QPointer<PolicyDialog> guard(this);
    m_service->getScopeNames(m_server, zone.trimmed(),
        [guard](bool ok, const QStringList& names, const QString& error) {
            if (!guard)
                return;
            Log::info(QStringLiteral("PolicyDialog: scope names ok=%1 count=%2")
                          .arg(ok ? 1 : 0).arg(names.size()));
            if (!ok) {
                Log::error(QStringLiteral("PolicyDialog: scope names error: %1")
                               .arg(error.left(300)));
                return;
            }
            guard->m_scopeNames = names;
            // Обновить выпадающие списки уже добавленных строк весов.
            for (int r = 0; r < guard->m_weights->rowCount(); ++r) {
                if (auto* cb = qobject_cast<QComboBox*>(guard->m_weights->cellWidget(r, 0))) {
                    const QString cur = cb->currentText();
                    cb->clear();
                    cb->addItems(guard->m_scopeNames);
                    cb->setCurrentText(cur);
                }
            }
        });
}

void PolicyDialog::loadSubnetNames()
{
    if (!m_service)
        return;
    Log::info(QStringLiteral("PolicyDialog: loading subnet names"));
    const QPointer<PolicyDialog> guard(this);
    m_service->getSubnetNames(m_server,
        [guard](bool ok, const QStringList& names, const QString& error) {
            if (!guard)
                return;
            Log::info(QStringLiteral("PolicyDialog: subnet names ok=%1 count=%2")
                          .arg(ok ? 1 : 0).arg(names.size()));
            if (ok) {
                guard->m_subnetValue->addItems(names);
            } else {
                Log::error(QStringLiteral("PolicyDialog: subnet names error: %1")
                               .arg(error.left(300)));
            }
        });
}

void PolicyDialog::addWeightRow(const QString& scope, int weight)
{
    const int r = m_weights->rowCount();
    m_weights->insertRow(r);
    auto* cb = new QComboBox(m_weights);
    cb->setEditable(true);
    cb->addItems(m_scopeNames);
    cb->setCurrentText(scope);
    m_weights->setCellWidget(r, 0, cb);
    auto* spin = new QSpinBox(m_weights);
    spin->setRange(0, 100000);
    spin->setValue(weight);
    m_weights->setCellWidget(r, 1, spin);
    connect(cb, &QComboBox::currentTextChanged, this, &PolicyDialog::updatePreview);
    connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), this, &PolicyDialog::updatePreview);
}

QStringList PolicyDialog::weightRows() const
{
    QStringList out;
    for (int r = 0; r < m_weights->rowCount(); ++r) {
        auto* cb = qobject_cast<QComboBox*>(m_weights->cellWidget(r, 0));
        auto* spin = qobject_cast<QSpinBox*>(m_weights->cellWidget(r, 1));
        const QString scope = cb ? cb->currentText().trimmed() : QString();
        if (scope.isEmpty())
            continue;
        out << scope + QLatin1Char(',') + QString::number(spin ? spin->value() : 1);
    }
    return out;
}

void PolicyDialog::setZoneLevel(const QString& zone)
{
    m_levelZone->setChecked(true);
    m_zone->setCurrentText(zone);
    // Явный запуск: currentTextChanged не шлётся, если текст уже совпадал.
    loadScopesForZone(zone);
    updatePreview();
}

void PolicyDialog::setParams(const DNSPolicy& p)
{
    m_loaded = p;
    setWindowTitle(tr("Изменение политики %1").arg(p.name));
    Log::info(QStringLiteral("PolicyDialog: editing '%1' (zone='%2', level=%3)")
                  .arg(p.name, p.zoneName, p.level));
    m_name->setText(p.name);
    m_levelZone->setChecked(p.level == QLatin1String("Zone"));
    m_zone->setCurrentText(p.zoneName);
    // Текущий текст зоны мог не измениться (например, это первый элемент
    // списка) — currentTextChanged не сработает, грузим области явно.
    loadScopesForZone(m_zone->currentText());

    const int actionIdx = m_action->findData(p.action.toUpper());
    m_action->setCurrentIndex(actionIdx >= 0 ? actionIdx : 1); // fallback — DENY
    // ALLOW на серверном уровне невозможен — откатываем на DENY.
    if (!m_levelZone->isChecked()
        && m_action->currentData().toString() == QLatin1String("ALLOW"))
        m_action->setCurrentIndex(m_action->findData(QStringLiteral("DENY")));
    (p.condition.compare(QLatin1String("Or"), Qt::CaseInsensitive) == 0 ? m_condOr : m_condAnd)
        ->setChecked(true);

    auto setOp = [](QComboBox* cb, const QString& value) {
        const QString op = value.section(QLatin1Char(','), 0, 0).trimmed().toLower();
        const int i = cb->findData(op);
        if (i >= 0)
            cb->setCurrentIndex(i);
    };
    auto valueAfterOp = [](const QString& value) {
        return value.section(QLatin1Char(','), 1).trimmed();
    };
    if (!p.clientSubnet.isEmpty()) {
        m_useSubnet->setChecked(true);
        setOp(m_subnetOp, p.clientSubnet);
        m_subnetValue->setCurrentText(valueAfterOp(p.clientSubnet));
    }
    if (!p.fqdn.isEmpty()) {
        m_useFqdn->setChecked(true);
        setOp(m_fqdnOp, p.fqdn);
        m_fqdnValue->setText(valueAfterOp(p.fqdn));
    }
    if (!p.qtype.isEmpty()) {
        m_useQtype->setChecked(true);
        setOp(m_qtypeOp, p.qtype);
        m_qtypeValue->setText(valueAfterOp(p.qtype));
    }
    if (!p.timeOfDay.isEmpty()) {
        m_useTime->setChecked(true);
        setOp(m_timeOp, p.timeOfDay);
        m_timeValue->setText(valueAfterOp(p.timeOfDay));
    }
    if (p.transportProtocol.contains(QLatin1String("UDP"), Qt::CaseInsensitive))
        m_trUdp->setChecked(true);
    else if (p.transportProtocol.contains(QLatin1String("TCP"), Qt::CaseInsensitive))
        m_trTcp->setChecked(true);
    else
        m_trAny->setChecked(true);

    for (const QString& entry : p.zoneScope.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
        const QString scope = entry.section(QLatin1Char(','), 0, 0).trimmed();
        const int weight = entry.section(QLatin1Char(','), 1, 1).trimmed().toInt();
        if (!scope.isEmpty())
            addWeightRow(scope, weight > 0 ? weight : 1);
    }
    updatePreview();
}

DNSPolicy PolicyDialog::params() const
{
    // Start from the loaded policy: keeps is_new, processingOrder, enabled,
    // otherCriteria (criteria the dialog does not edit) and the loaded
    // snapshot that decides Set- vs Remove+Add on save.
    DNSPolicy p = m_loaded;
    p.name = m_name->text().trimmed();
    p.level = m_levelZone->isChecked() ? QStringLiteral("Zone") : QStringLiteral("Server");
    p.zoneName = p.level == QLatin1String("Zone") ? m_zone->currentText().trimmed()
                                                  : QString();
    p.action = m_action->currentData().toString();
    p.condition = m_condOr->isChecked() ? QStringLiteral("Or") : QStringLiteral("And");
    // Each criterion is cleared first: an unchecked box must empty the field
    // (a loaded criterion that becomes empty triggers the Remove+Add save).
    p.clientSubnet.clear();
    if (m_useSubnet->isChecked() && !m_subnetValue->currentText().trimmed().isEmpty())
        p.clientSubnet = m_subnetOp->currentData().toString() + QLatin1Char(',') +
                         m_subnetValue->currentText().trimmed();
    p.fqdn.clear();
    if (m_useFqdn->isChecked() && !m_fqdnValue->text().trimmed().isEmpty())
        p.fqdn = m_fqdnOp->currentData().toString() + QLatin1Char(',') +
                 m_fqdnValue->text().trimmed();
    p.qtype.clear();
    if (m_useQtype->isChecked() && !m_qtypeValue->text().trimmed().isEmpty()) {
        // Критерий -QType: "eq,A,AAAA" — без пробелов после запятых.
        QString types = m_qtypeValue->text();
        types.remove(QLatin1Char(' '));
        p.qtype = m_qtypeOp->currentData().toString() + QLatin1Char(',') + types.trimmed();
    }
    p.timeOfDay.clear();
    if (m_useTime->isChecked() && !m_timeValue->text().trimmed().isEmpty())
        p.timeOfDay = m_timeOp->currentData().toString() + QLatin1Char(',') +
                      m_timeValue->text().trimmed();
    if (m_trUdp->isChecked())
        p.transportProtocol = QStringLiteral("eq,UDP");
    else if (m_trTcp->isChecked())
        p.transportProtocol = QStringLiteral("eq,TCP");
    else
        p.transportProtocol.clear();
    if (p.level == QLatin1String("Zone"))
        p.zoneScope = weightRows().join(QLatin1Char(';'));
    else
        p.zoneScope.clear(); // у серверной политики нет областей зоны
    return p;
}

void PolicyDialog::updatePreview()
{
    DNSPolicy p = params();
    if (p.name.isEmpty())
        p.name = QStringLiteral("ИмяПолитики");
    // command() is what save() will run: Add- for a new policy, Set- for a
    // plain edit, Remove+Add when Set- cannot express the change.
    m_preview->setPlainText(p.command());
}

void PolicyDialog::accept()
{
    if (m_name->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, tr("Политика"), tr("Укажите имя политики."));
        return;
    }
    if (m_levelZone->isChecked() && m_zone->currentText().trimmed().isEmpty()) {
        QMessageBox::warning(this, tr("Политика"), tr("Выберите зону."));
        return;
    }
    QDialog::accept();
}
