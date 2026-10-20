#include "MainWindow.h"

#include "core/DnsZoneScope.h"
#include "core/JsonUtil.h"
#include "core/Log.h"
#include "core/SecureStore.h"
#include "ui/dialogs/PolicyDialog.h"
#include "ui/dialogs/RecordDialog.h"
#include "ui/dialogs/SaveDialog.h"
#include "ui/dialogs/ScopePropsDialog.h"
#include "ui/dialogs/ServerSelectDialog.h"
#include "ui/dialogs/SubnetDialog.h"
#include "ui/dialogs/ZoneDialog.h"
#include "ui/dialogs/ZoneEditDialog.h"
#include "ui/dialogs/ZoneScopeDialog.h"
#include "ui/TreeIcons.h"

#include <QAction>
#include <QColor>
#include <QCoreApplication>
#include <QCursor>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequence>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QListView>
#include <QMap>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QProcess>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>
#include <memory>

namespace {
// Custom data roles for tree items.
enum DataRole {
    TypeRole = Qt::UserRole,       // item kind (see the class comment)
    ServerRole = Qt::UserRole + 1,
    ZoneRole = Qt::UserRole + 2,
    ScopeRole = Qt::UserRole + 3,  // значение области (пусто = по умолчанию)
    LoadedRole = Qt::UserRole + 4, // whether the node content was fetched
    ZoneJsonRole = Qt::UserRole + 5, // whole zone object (для свойств/правки)
    HostLabelRole = Qt::UserRole + 6, // host node label ("srv01", "@")
    FqdnRole = Qt::UserRole + 7,      // full name of the host node
    RecordsRole = Qt::UserRole + 8,   // QVariantList of the node's records
    HostNameRole = Qt::UserRole + 9,  // DNS -Name value for the node's records
    DefaultScopeRole = Qt::UserRole + 10, // область «по умолчанию»
    ScopeFileRole = Qt::UserRole + 11,    // файл области (для таблицы)
    // Обратная зона: области не используются, записи лежат прямо в зоне
    // (зона сама служит контейнером записей с пустым ScopeRole).
    DirectRecordsRole = Qt::UserRole + 12
};

// Категория зоны для верхнего уровня дерева.
enum class ZoneCat { Forward, Reverse, Forwarder };

ZoneCat zoneCategory(const QJsonObject& o)
{
    if (Json::propBool(o, { "IsReverseLookupZone" }))
        return ZoneCat::Reverse;
    const QString t = Json::propStr(o, { "ZoneType" });
    if (t.compare(QLatin1String("Forwarder"), Qt::CaseInsensitive) == 0)
        return ZoneCat::Forwarder;
    return ZoneCat::Forward;
}

QString categoryTypeOf(ZoneCat cat)
{
    switch (cat) {
    case ZoneCat::Reverse: return QStringLiteral("reverseZones");
    case ZoneCat::Forwarder: return QStringLiteral("forwarderZones");
    default: return QStringLiteral("forwardZones");
    }
}

// Основная AD-интегрированная зона — «зона домена», защищена от удаления.
bool isDomainPrimaryZone(const QJsonObject& o)
{
    return Json::propStr(o, { "ZoneType" }) == QLatin1String("Primary")
           && Json::propBool(o, { "IsDsIntegrated" });
}

// Область по умолчанию: её имя на сервере совпадает с именем зоны
// (записи без -ZoneScope лежат именно в ней).
bool isDefaultScopeName(const QString& scopeName, const QString& zoneName)
{
    return scopeName.trimmed().isEmpty()
           || scopeName.compare(zoneName, Qt::CaseInsensitive) == 0;
}

// stretchCol — столбец, растягивающийся на освободившееся место (все
// остальные подстраиваются по содержимому); -1 = растягивается последний.
void configureTable(QTableWidget* table, const QStringList& headers,
                    int stretchCol = -1)
{
    table->setColumnCount(headers.size());
    table->setHorizontalHeaderLabels(headers);
    auto* header = table->horizontalHeader();
    header->setSectionResizeMode(QHeaderView::ResizeToContents);
    if (stretchCol >= 0 && stretchCol < headers.size())
        header->setSectionResizeMode(stretchCol, QHeaderView::Stretch);
    else
        header->setStretchLastSection(true);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    // Выделенная строка — сплошная подсветка на всю ширину: в тёмной теме
    // Windows стиль рисует выделение отдельными «плашками» по ячейкам,
    // из-за чего строка выглядела плохо заметными вертикальными полосками.
    table->setStyleSheet(QStringLiteral(
        "QTableWidget::item:selected { background-color: #2f6fb2;"
        " color: #ffffff; }"));
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->verticalHeader()->setVisible(false);
    table->setContextMenuPolicy(Qt::CustomContextMenu);
}

bool isDescendantOf(QTreeWidgetItem* item, QTreeWidgetItem* ancestor)
{
    for (QTreeWidgetItem* p = item ? item->parent() : nullptr; p; p = p->parent()) {
        if (p == ancestor)
            return true;
    }
    return false;
}

// The RecordData value used to pinpoint a record for deletion.
QString recordMatchValue(const QJsonObject& r)
{
    const QString t = Json::propStr(r, { "RecordType" }).toUpper();
    const QJsonObject rd = Json::prop(r, { "RecordData" }).toObject();
    // recordDataStr понимает и плоский RecordData, и сырой CimInstance
    // (значения из CimInstanceProperties) — серверное сравнение в
    // Remove-... получит те же строки.
    if (t == QLatin1String("A")) return Json::recordDataStr(rd, "IPv4Address");
    if (t == QLatin1String("AAAA")) return Json::recordDataStr(rd, "IPv6Address");
    if (t == QLatin1String("CNAME")) return Json::recordDataStr(rd, "HostNameAlias");
    if (t == QLatin1String("NS")) return Json::recordDataStr(rd, "NameServer");
    if (t == QLatin1String("MX")) return Json::recordDataStr(rd, "MailExchange");
    if (t == QLatin1String("PTR")) return Json::recordDataStr(rd, "PtrDomainName");
    if (t == QLatin1String("SRV")) return Json::recordDataStr(rd, "DomainName");
    return QString();
}

// Типы, которые поддерживает диалог записи (Add-DnsServerResourceRecord).
// SOA и прочие специальные записи правятся только средствами сервера —
// для них ✏ в таблице отключается, а двойной щелчок объясняет причину.
bool isEditableRecordType(const QString& type)
{
    const QString t = type.trimmed().toUpper();
    return t == QLatin1String("A") || t == QLatin1String("AAAA")
           || t == QLatin1String("CNAME") || t == QLatin1String("NS")
           || t == QLatin1String("MX") || t == QLatin1String("TXT")
           || t == QLatin1String("PTR") || t == QLatin1String("SRV");
}

// Builds dialog parameters from a listed record (for editing).
RecordAddParams makeRecordParams(const QString& zone, const QString& scope, const QJsonObject& r)
{
    RecordAddParams p;
    p.zone = zone;
    p.scope = scope;
    p.type = Json::propStr(r, { "RecordType" });
    p.name = Json::propStr(r, { "HostName" });
    p.ttlSeconds = static_cast<int>(Json::propNum(r, { "TTLSeconds" }, 3600));
    const QJsonObject rd = Json::prop(r, { "RecordData" }).toObject();
    const QString t = p.type.toUpper();
    if (t == QLatin1String("A")) {
        p.ipv4 = Json::recordDataStr(rd, "IPv4Address");
    } else if (t == QLatin1String("AAAA")) {
        p.ipv6 = Json::recordDataStr(rd, "IPv6Address");
    } else if (t == QLatin1String("CNAME")) {
        p.cnameTarget = Json::recordDataStr(rd, "HostNameAlias");
    } else if (t == QLatin1String("NS")) {
        p.nsServer = Json::recordDataStr(rd, "NameServer");
    } else if (t == QLatin1String("MX")) {
        p.mxServer = Json::recordDataStr(rd, "MailExchange");
        p.mxPreference = static_cast<int>(Json::recordDataNum(rd, "Preference"));
    } else if (t == QLatin1String("TXT")) {
        p.txtText = Json::propStrList(rd, { "DescriptiveText" }).join(QString());
        if (p.txtText.isEmpty())
            p.txtText = Json::recordDataStr(rd, "DescriptiveText");
    } else if (t == QLatin1String("PTR")) {
        p.ptrTarget = Json::recordDataStr(rd, "PtrDomainName");
    } else if (t == QLatin1String("SRV")) {
        p.srvService = Json::recordDataStr(rd, "Service");
        p.srvProtocol = Json::recordDataStr(rd, "Protocol");
        // В сыром RecordData протокол может прийти числом (6/17), а диалогу
        // нужны значения списка «_tcp»/«_udp».
        if (p.srvProtocol == QLatin1String("6"))
            p.srvProtocol = QStringLiteral("_udp");
        else if (p.srvProtocol == QLatin1String("17"))
            p.srvProtocol = QStringLiteral("_tcp");
        p.srvTarget = Json::recordDataStr(rd, "DomainName");
        p.srvPort = static_cast<int>(Json::recordDataNum(rd, "Port"));
        p.srvPriority = static_cast<int>(Json::recordDataNum(rd, "Priority"));
        p.srvWeight = static_cast<int>(Json::recordDataNum(rd, "Weight", 100));
    }
    return p;
}

} // namespace

MainWindow::MainWindow(const ServerInfo& server, QWidget* parent)
    : QMainWindow(parent)
    , m_svc(&m_ps)
    , m_activeServer(server.name)
    , m_activeDescription(server.description)
{
    setWindowTitle(tr("DNS Mania — %1").arg(m_activeServer));
    resize(1200, 720);

    // Настройки подключения для всех сохранённых серверов (WinRM / локальный
    // PowerShell с -ComputerName, учётные данные) — до любых запросов.
    for (const ServerInfo& s : m_store.servers()) {
        m_svc.setServerMode(s.name, s.localPs);
        m_svc.setServerCredentials(s.name, s.userName, SecureStore::password(s.name));
    }
    m_svc.setServerMode(m_activeServer, server.localPs);
    m_svc.setServerCredentials(m_activeServer, server.userName,
                               SecureStore::password(m_activeServer));

    // ----- Left: navigation tree.
    m_tree = new QTreeWidget(this);
    m_tree->setHeaderLabels({ tr("DNS-объекты") });
    m_tree->setMinimumWidth(280);
    m_tree->setRootIsDecorated(true);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);

    // ----- Right: journal bar + location header + content stack.
    auto* journalBar = new QWidget(this);
    auto* journalLayout = new QHBoxLayout(journalBar);
    journalLayout->setContentsMargins(6, 4, 6, 2);
    journalLayout->setSpacing(6);
    m_btnSave = new QPushButton(tr("Сохранить"), journalBar);
    m_btnSave->setToolTip(tr("Выполнить накопленные действия (Ctrl+S)"));
    m_btnUndo = new QPushButton(tr("Отменить"), journalBar);
    m_btnUndo->setToolTip(tr("Отменить последнее действие (Ctrl+Z)"));
    m_btnRedo = new QPushButton(tr("Вернуть"), journalBar);
    m_btnRedo->setToolTip(tr("Вернуть отменённое действие (Ctrl+Y)"));
    m_btnClear = new QPushButton(tr("Удалить все действия"), journalBar);
    m_btnClear->setToolTip(tr("Отменить все накопленные действия"));
    journalLayout->addWidget(m_btnSave);
    journalLayout->addWidget(m_btnUndo);
    journalLayout->addWidget(m_btnRedo);
    journalLayout->addWidget(m_btnClear);
    journalLayout->addStretch();

    m_location = new QLabel(this);
    m_location->setContentsMargins(10, 5, 10, 5);

    m_stack = new QStackedWidget(this);

    m_pageBlank = new QLabel(tr("Выберите элемент в дереве слева"), m_stack);
    m_pageBlank->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    static_cast<QLabel*>(m_pageBlank)->setAlignment(Qt::AlignCenter);
    m_stack->addWidget(m_pageBlank);

    auto makeTablePage = [this](QTableWidget** tableOut, const QStringList& headers,
                                int stretchCol) {
        auto* page = new QWidget(m_stack);
        auto* l = new QVBoxLayout(page);
        l->setContentsMargins(4, 4, 4, 4);
        auto* table = new QTableWidget(page);
        configureTable(table, headers, stretchCol);
        l->addWidget(table);
        *tableOut = table;
        return page;
    };

    // Во всех таблицах есть колонка «Управление» (✏ / 🗑) — как в таблице
    // правил; контекстное меню и двойной щелчок действуют так же.
    QWidget* zonePage = makeTablePage(&m_zoneTable,
        { tr("Зона"), tr("Тип"), tr("DS"), tr("Дин. обн."),
          tr("Репликация"), tr("Статус"), tr("Управление") }, 0);
    m_stack->addWidget(zonePage);

    // Таблица записей — плоский список: одна запись = строка, как её отдаёт
    // Get-DnsServerResourceRecord (без группировки по узлам). Именно поэтому
    // ✏/🗑 действуют на конкретную запись: правка = удалить старую с её
    // параметрами и добавить новую.
    QWidget* hostPage = makeTablePage(&m_hostTable,
        { tr("Имя"), tr("Тип"), tr("TTL"), tr("Данные"),
          tr("Управление") }, 3);
    m_stack->addWidget(hostPage);

    QWidget* propsPage = makeTablePage(&m_nodeProps,
        { tr("Свойство"), tr("Значение") }, 1);
    m_stack->addWidget(propsPage);

    QWidget* scopePage = makeTablePage(&m_scopeTable,
        { tr("Область"), tr("Файл"), tr("Управление") }, 0);
    m_stack->addWidget(scopePage);

    QWidget* policyPage = makeTablePage(&m_policyTable,
        { tr("Приоритет"), tr("Статус"), tr("Имя политики"), tr("Действие"),
          tr("Критерии"), tr("Результат / Область"), tr("Управление") }, 4);
    m_stack->addWidget(policyPage);

    QWidget* subnetPage = makeTablePage(&m_subnetTable,
        { tr("Имя"), tr("IPv4-подсети"), tr("IPv6-подсети"), tr("Управление") }, 1);
    m_stack->addWidget(subnetPage);

    auto* right = new QWidget(this);
    auto* rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);
    rightLayout->addWidget(journalBar);
    rightLayout->addWidget(m_location);
    rightLayout->addWidget(m_stack, 1);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(m_tree);
    splitter->addWidget(right);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    setCentralWidget(splitter);

    // ----- Menus (no toolbar — actions live here, in the journal bar and in
    // context menus). Серверы выбираются в отдельном окне (ServerSelectDialog).
    auto* fileMenu = menuBar()->addMenu(tr("Файл"));
    m_actSave = fileMenu->addAction(tr("Сохранить"), this, &MainWindow::saveJournal);
    m_actSave->setShortcut(QKeySequence::Save);
    m_actUndo = fileMenu->addAction(tr("Отменить"), this, &MainWindow::undoAction);
    m_actUndo->setShortcut(QKeySequence::Undo);
    m_actRedo = fileMenu->addAction(tr("Вернуть"), this, &MainWindow::redoAction);
    m_actRedo->setShortcut(QKeySequence::Redo);
    m_actClearJournal = fileMenu->addAction(tr("Удалить все действия"),
                                            this, &MainWindow::clearJournal);
    fileMenu->addSeparator();
    m_actRefresh = fileMenu->addAction(tr("Обновить"), this, &MainWindow::refreshCurrent);
    m_actRefresh->setShortcut(QKeySequence::Refresh);
    fileMenu->addAction(tr("Выбрать сервер…"), this, [this] {
        ServerSelectDialog dlg(this);
        if (dlg.exec() != QDialog::Accepted)
            return;
        const ServerInfo s = dlg.selected();
        if (s.name.isEmpty())
            return;
        // Настройки подключения (режим, учётные данные) применяются сразу —
        // даже если этот сервер уже активен (могли изменить параметры).
        m_svc.setServerMode(s.name, s.localPs);
        m_svc.setServerCredentials(s.name, s.userName, SecureStore::password(s.name));
        if (s.name.compare(m_activeServer, Qt::CaseInsensitive) == 0)
            return;
        if (m_journal.count() > 0 &&
            QMessageBox::question(this, tr("Смена сервера"),
                tr("Есть несохранённые действия (%1). Отменить их и подключиться к %2?")
                    .arg(m_journal.count())
                    .arg(s.name)) != QMessageBox::Yes)
            return;
        m_journal.clearAll();
        m_activeServer = s.name;
        m_activeDescription = s.description;
        setWindowTitle(tr("DNS Mania — %1").arg(s.name));
        m_zoneTable->setRowCount(0);
        m_subnetTable->setRowCount(0);
        m_scopeTable->setRowCount(0);
        m_policyTable->setRowCount(0);
        m_hostTable->setRowCount(0);
        m_nodeProps->setRowCount(0);
        m_allZones.clear(); // список зон прежнего сервера
        rebuildTree();
    });
    fileMenu->addSeparator();
    fileMenu->addAction(tr("Открыть диспетчер DNS"), this, [] {
        // Стандартная консоль Windows (dnsmgmt.msc) — для действий,
        // которых пока нет в DNS Mania. mmc.exe запускается detached:
        // консоль живёт своей жизнью и не блокирует приложение.
        QProcess::startDetached(QStringLiteral("mmc.exe"),
                                { QStringLiteral("dnsmgmt.msc") });
    });
    fileMenu->addSeparator();
    fileMenu->addAction(tr("Выход"), this, &QWidget::close);

    auto* zoneMenu = menuBar()->addMenu(tr("Зоны"));
    m_actAddZone = zoneMenu->addAction(tr("Добавить зону…"), this, &MainWindow::addZone);
    m_actEditZone = zoneMenu->addAction(tr("Свойства зоны…"), this, &MainWindow::editZone);
    zoneMenu->addAction(tr("Удалить зону…"), this, &MainWindow::removeZone);
    zoneMenu->addAction(tr("Открыть в дереве"), this, &MainWindow::openZoneInTree);

    auto* subnetMenu = menuBar()->addMenu(tr("Подсети"));
    m_actAddSubnet = subnetMenu->addAction(tr("Добавить подсеть…"), this, &MainWindow::addSubnet);
    subnetMenu->addAction(tr("Изменить подсеть…"), this, &MainWindow::editSubnet);
    subnetMenu->addAction(tr("Удалить подсеть…"), this, &MainWindow::removeSubnet);

    auto* policyMenu = menuBar()->addMenu(tr("Правила"));
    m_actAddPolicy = policyMenu->addAction(tr("Добавить правило…"), this, &MainWindow::addPolicy);
    policyMenu->addAction(tr("Изменить правило…"), this, &MainWindow::editPolicy);
    policyMenu->addAction(tr("Удалить правило…"), this, &MainWindow::removePolicy);

    auto* helpMenu = menuBar()->addMenu(tr("Справка"));
    helpMenu->addAction(tr("О программе"), this, [this] {
        QMessageBox::about(this, tr("DNS Mania"),
            tr("DNS Mania %1 — настольная утилита управления зонами Windows DNS-сервера.\n\n"
               "По умолчанию связь идёт через PowerShell-ремоутинг (WinRM, Invoke-Command): "
               "на целевых серверах должен быть включён WinRM и установлен модуль DnsServer.\n"
               "Если WinRM недоступен, сервер можно добавить в режиме «Локальный PowerShell "
               "с -ComputerName» (нужен RSAT DNS на этой машине).\n\n"
               "Изменения не выполняются сразу: они копятся в журнале и применяются "
               "пачкой по кнопке «Сохранить» (Ctrl+S).")
                .arg(QCoreApplication::applicationVersion()));
    });

    // ----- Status bar.
    m_statusLabel = new QLabel(tr("Готово"), this);
    statusBar()->addWidget(m_statusLabel);

    // ----- Signals.
    connect(m_tree, &QTreeWidget::currentItemChanged, this, &MainWindow::onTreeCurrentChanged);
    connect(m_tree, &QTreeWidget::itemExpanded, this, &MainWindow::onTreeItemExpanded);
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, &MainWindow::showTreeMenu);

    connect(m_zoneTable, &QTableWidget::customContextMenuRequested,
            this, &MainWindow::showZoneTableMenu);
    connect(m_subnetTable, &QTableWidget::customContextMenuRequested,
            this, &MainWindow::showSubnetTableMenu);
    connect(m_hostTable, &QTableWidget::customContextMenuRequested,
            this, &MainWindow::showHostTableMenu);
    connect(m_scopeTable, &QTableWidget::customContextMenuRequested,
            this, &MainWindow::showScopeTableMenu);
    connect(m_policyTable, &QTableWidget::customContextMenuRequested,
            this, &MainWindow::showPolicyTableMenu);

    connect(m_zoneTable, &QTableWidget::itemDoubleClicked, this, [this](QTableWidgetItem*) {
        openZoneInTree();
    });
    connect(m_hostTable, &QTableWidget::itemDoubleClicked, this, [this](QTableWidgetItem*) {
        // Как в таблице правил: двойной щелчок — основное действие (правка).
        // При нескольких записях узла открывается меню выбора записи.
        editRecordAction();
    });
    connect(m_subnetTable, &QTableWidget::itemDoubleClicked, this,
            [this](QTableWidgetItem*) { editSubnet(); });
    connect(m_scopeTable, &QTableWidget::itemDoubleClicked, this,
            [this](QTableWidgetItem*) { editScope(); });
    connect(m_policyTable, &QTableWidget::itemDoubleClicked, this,
            [this](QTableWidgetItem* item) {
                if (item->column() == 1) { // «Статус» — включить/выключить
                    togglePolicyEnabled();
                } else {
                    editPolicy();
                }
            });

    connect(&m_ps, &PsClient::busyChanged, this, [this](bool busy) {
        if (m_saving)
            return; // статус во время выполнения журнала показывает executeNext()
        m_statusLabel->setText(busy ? tr("Выполняется запрос к DNS-серверу…") : tr("Готово"));
    });

    connect(&m_journal, &Journal::changed, this, &MainWindow::updateJournalUi);
    connect(m_btnSave, &QPushButton::clicked, this, &MainWindow::saveJournal);
    connect(m_btnUndo, &QPushButton::clicked, this, &MainWindow::undoAction);
    connect(m_btnRedo, &QPushButton::clicked, this, &MainWindow::redoAction);
    connect(m_btnClear, &QPushButton::clicked, this, &MainWindow::clearJournal);

    rebuildTree();
    updateJournalUi();
    // Пустая страница не нужна: rebuildTree() выбирает первую категорию, и
    // onTreeCurrentChanged сам показывает таблицу зон.
}

// Имена зон из последнего загруженного списка (не из таблицы: таблица
// показывает только одну категорию, а комбобоку правилу нужен полный список).
QStringList MainWindow::zoneNames() const
{
    QStringList zones;
    for (const QJsonObject& o : m_allZones) {
        const QString n = Json::propStr(o, { QStringLiteral("ZoneName") });
        if (!n.isEmpty())
            zones << n;
    }
    return zones;
}

QJsonObject MainWindow::zoneInfo(const QString& zoneName) const
{
    for (const QJsonObject& o : m_allZones) {
        if (Json::propStr(o, { QStringLiteral("ZoneName") })
                .compare(zoneName, Qt::CaseInsensitive) == 0)
            return o;
    }
    return QJsonObject();
}

void MainWindow::setStatusMessage(const QString& text)
{
    m_statusLabel->setText(text);
}

void MainWindow::showError(const QString& title, const QString& error)
{
    Log::error(title + QStringLiteral(": ") + error);

    QString text = error.isEmpty() ? tr("Неизвестная ошибка.") : error;
    // Ошибка WinRM «TrustedHosts»: цель указана IP-адресом — Kerberos для IP не
    // работает, WinRM требует HTTPS или TrustedHosts плюс явные учётные данные.
    if (text.contains(QStringLiteral("TrustedHosts"))
        || text.contains(QStringLiteral("cannot process the request"))) {
        text += QStringLiteral("\n\n")
            + tr("Подсказка WinRM: IP-адрес не поддерживает Kerberos.\n"
                 "1) Проще всего: укажите сервер по FQDN (Kerberos заработает без "
                 "дополнительной настройки).\n"
                 "2) Или добавьте IP в TrustedHosts на этой рабочей машине "
                 "(один раз, от администратора) в повышенном PowerShell:\n"
                 "     Set-Item WSMan:\\localhost\\Client\\TrustedHosts -Value %1\n"
                 "   (в cmd: winrm set winrm/config/client @{TrustedHosts=\"%1\"})\n"
                 "После этого укажите пользователя и пароль WinRM в свойствах сервера "
                 "(«Выбрать сервер…» → «Изменить…»).").arg(m_activeServer);
    }
    QMessageBox::critical(this, title, text);
}

// -------------------------------------------------------------------- Journal

void MainWindow::journalEdit(const QString& title, const QStringList& cacheFiles,
                             const std::function<void()>& optimistic,
                             const Journal::ExecFn& exec, const std::function<void()>& reload)
{
    if (m_saving) {
        QMessageBox::information(this, tr("Журнал"),
                                 tr("Идёт применение изменений — дождитесь его завершения."));
        return;
    }
    // Снимок «до» -> оптимистичная правка кэша -> снимок «после» -> запись в
    // журнале. Всё синхронно, поэтому снимки согласованы.
    m_journal.beginSnapshot(cacheFiles);
    if (optimistic)
        optimistic();
    m_journal.commit(title, m_activeServer, exec);
    if (reload)
        reload();
    setStatusMessage(tr("В журнале: %1").arg(title));
}

void MainWindow::saveJournal()
{
    if (m_saving || m_journal.count() == 0)
        return;
    SaveDialog dlg(m_journal.pendingTitles(), this);
    if (dlg.exec() != QDialog::Accepted)
        return;

    m_saving = true;
    m_saveIndex = 0;
    updateJournalUi();
    Log::info(QStringLiteral("journal: saving %1 action(s)").arg(m_journal.count()));
    executeNext();
}

void MainWindow::executeNext()
{
    if (m_saveIndex >= m_journal.count()) {
        finishSave();
        return;
    }
    const Journal::Entry e = m_journal.at(m_saveIndex);
    setStatusMessage(tr("Выполнение (%1/%2): %3")
                         .arg(m_saveIndex + 1)
                         .arg(m_journal.count())
                         .arg(e.title));
    e.exec([this](bool ok, const QString& error) {
        if (!ok) {
            abortSave(error);
            return;
        }
        ++m_saveIndex;
        executeNext();
    });
}

void MainWindow::finishSave()
{
    m_saving = false;
    const int applied = m_saveIndex;
    m_saveIndex = 0;
    // Действия применены на сервере — очищаем журнал без отката снимков.
    m_journal.commitAll();
    setStatusMessage(tr("Изменения применены (%1)").arg(applied));
    Log::info(QStringLiteral("journal: %1 action(s) applied").arg(applied));
    // Автоматическое обновление кэша и всех данных с сервера.
    refreshCaches();
    reloadAfterJournal();
}

void MainWindow::abortSave(const QString& error)
{
    m_saving = false;
    const int applied = m_saveIndex;
    m_saveIndex = 0;
    showError(tr("Не удалось применить изменения"), error);
    // Уже выполненные записи из журнала убираются: они применены на сервере.
    m_journal.dropFirst(applied);
    setStatusMessage(tr("Изменения применены не полностью (%1)").arg(applied));
    Log::warn(QStringLiteral("journal: aborted after %1 applied action(s)").arg(applied));
    refreshCaches();
    reloadAfterJournal();
}

void MainWindow::updateJournalUi()
{
    const int n = m_journal.count();
    const bool busy = m_saving;
    m_btnSave->setEnabled(!busy && n > 0);
    m_btnUndo->setEnabled(!busy && m_journal.canUndo());
    m_btnRedo->setEnabled(!busy && m_journal.canRedo());
    m_btnClear->setEnabled(!busy && n > 0);
    m_actSave->setEnabled(!busy && n > 0);
    m_actUndo->setEnabled(!busy && m_journal.canUndo());
    m_actRedo->setEnabled(!busy && m_journal.canRedo());
    m_actClearJournal->setEnabled(!busy && n > 0);
    m_btnSave->setText(n > 0 ? tr("Сохранить (%1)").arg(n) : tr("Сохранить"));
    m_btnUndo->setToolTip(m_journal.canUndo()
                              ? tr("Отменить: %1 (Ctrl+Z)").arg(m_journal.undoTitle())
                              : tr("Отменить последнее действие (Ctrl+Z)"));
    m_btnRedo->setToolTip(m_journal.canRedo()
                              ? tr("Вернуть: %1 (Ctrl+Y)").arg(m_journal.redoTitle())
                              : tr("Вернуть отменённое действие (Ctrl+Y)"));
}

void MainWindow::undoAction()
{
    if (m_saving || !m_journal.canUndo())
        return;
    setStatusMessage(tr("Отмена: %1").arg(m_journal.undoTitle()));
    m_journal.undo(); // восстанавливает снимки файлов кэша
    reloadAfterJournal();
}

void MainWindow::redoAction()
{
    if (m_saving || !m_journal.canRedo())
        return;
    setStatusMessage(tr("Возврат: %1").arg(m_journal.redoTitle()));
    m_journal.redo();
    reloadAfterJournal();
}

void MainWindow::clearJournal()
{
    if (m_saving || m_journal.count() == 0)
        return;
    m_journal.clearAll(); // откатывает все действия по снимкам
    setStatusMessage(tr("Все действия удалены"));
    reloadAfterJournal();
}

void MainWindow::reloadAfterJournal()
{
    // loadZones() перечитывает ветку зон и обновляет загруженные подузлы
    // (узлы/области/правила), loadSubnets() — таблицу подсетей.
    loadZones();
    loadSubnets();
}

// ---------------------------------------------------------------------- Zones

void MainWindow::rebuildTree()
{
    m_tree->clear();

    // Верхний уровень — как в dnsmgmt.msc: три категории зон, подсети и
    // правила. Содержимое категорий подгружается лениво (placeholder).
    auto addCategory = [this](const QString& title, const QString& type, const QIcon& icon) {
        auto* item = new QTreeWidgetItem(m_tree, QStringList() << title);
        item->setData(0, TypeRole, type);
        item->setData(0, ServerRole, m_activeServer);
        item->setIcon(0, icon);
        auto* ph = new QTreeWidgetItem(item, QStringList() << tr("(разверните для загрузки)"));
        ph->setData(0, TypeRole, QStringLiteral("placeholder"));
        ph->setFlags(Qt::ItemIsEnabled);
        return item;
    };
    addCategory(tr("Зоны прямого просмотра"), QStringLiteral("forwardZones"),
                TreeIcons::forwardZones());
    addCategory(tr("Зоны обратного просмотра"), QStringLiteral("reverseZones"),
                TreeIcons::reverseZones());
    addCategory(tr("Зоны пересылки"), QStringLiteral("forwarderZones"),
                TreeIcons::forwarderZones());

    auto* subnets = new QTreeWidgetItem(m_tree, QStringList() << tr("Подсети"));
    subnets->setData(0, TypeRole, QStringLiteral("subnets"));
    subnets->setData(0, ServerRole, m_activeServer);
    subnets->setIcon(0, TreeIcons::subnets());

    // Правила уровня сервера (без -ZoneName).
    auto* policies = new QTreeWidgetItem(m_tree, QStringList() << tr("Правила"));
    policies->setData(0, TypeRole, QStringLiteral("serverPolicies"));
    policies->setData(0, ServerRole, m_activeServer);
    policies->setIcon(0, TreeIcons::policies());

    // Разворачивать здесь не нужно: и так срабатывают onTreeCurrentChanged
    // (загрузка зон по выбору категории) и onTreeItemExpanded (по щелчку на «+»),
    // двойной вызов loadZones() даёт дублирующий запрос.
    if (m_tree->topLevelItemCount() > 0 && !m_tree->currentItem())
        m_tree->setCurrentItem(m_tree->topLevelItem(0));

    // Файловый кэш обновляем сразу после восстановления дерева (при старте и
    // при смене сервера).
    refreshCaches();
}

void MainWindow::refreshCaches()
{
    const QString server = m_activeServer;
    if (server.isEmpty())
        return;

    setStatusMessage(tr("Обновление кэша…"));
    Log::info(QStringLiteral("cache refresh: start, server '%1'").arg(server));

    // Счётчик незавершённых запросов: постановка запроса увеличивает его,
    // завершение уменьшает; на нуле — кэш обновлён. Все колбэки приходят
    // в поток GUI, поэтому атомики не нужны.
    auto pending = std::make_shared<int>(0);
    auto finish = std::make_shared<std::function<void()>>();
    *finish = [this, pending] {
        if (--(*pending) <= 0) {
            setStatusMessage(tr("Кэш обновлён"));
            Log::info(QStringLiteral("cache refresh: done"));
        }
    };
    const auto warn = [](const QString& what, const QString& error) {
        if (!error.isEmpty())
            Log::warn(QStringLiteral("cache refresh %1: %2").arg(what, error.left(300)));
    };

    ++(*pending); // зоны
    m_svc.refreshZones(server,
        [this, server, pending, finish, warn](bool ok, const QList<QJsonObject>& zones,
                                              const QString& error) {
            if (!ok)
                warn(QStringLiteral("zones '%1'").arg(server), error);

            ++(*pending); // подсети
            m_svc.refreshSubnets(server,
                [server, pending, finish, warn](bool ok2, const QList<QJsonObject>&,
                                                const QString& err2) {
                    if (!ok2)
                        warn(QStringLiteral("subnets '%1'").arg(server), err2);
                    (*finish)();
                });

            // Зональные списки: области, записи (область по умолчанию),
            // политики — для каждой зоны сервера. У зон пересылки областей и
            // записей нет — их не опрашиваем; у обратных зон областей нет,
            // но записи (без области) и политики есть.
            for (const QJsonObject& z : zones) {
                const QString zone = Json::propStr(z, { "ZoneName" });
                const ZoneCat cat = zoneCategory(z);
                if (zone.isEmpty() || cat == ZoneCat::Forwarder)
                    continue;
                const auto zoneDone = [server, zone, pending, finish, warn](
                                          const QString& what, bool ok3,
                                          const QList<QJsonObject>&, const QString& err3) {
                    if (!ok3)
                        warn(QStringLiteral("%1 '%2'/'%3'").arg(what, server, zone), err3);
                    (*finish)();
                };
                if (cat == ZoneCat::Forward) {
                    ++(*pending);
                    m_svc.refreshZoneScopes(server, zone,
                        [zoneDone](bool ok, const QList<QJsonObject>& items, const QString& e) {
                            zoneDone(QStringLiteral("scopes"), ok, items, e);
                        });
                }
                ++(*pending);
                m_svc.refreshRecords(server, zone, QString(),
                    [zoneDone](bool ok, const QList<QJsonObject>& items, const QString& e) {
                        zoneDone(QStringLiteral("records"), ok, items, e);
                    });
                ++(*pending);
                m_svc.refreshPolicies(server, zone,
                    [zoneDone](bool ok, const QList<QJsonObject>& items, const QString& e) {
                        zoneDone(QStringLiteral("policies"), ok, items, e);
                    });
            }
            (*finish)(); // зоны
        });
}

void MainWindow::refreshCurrent()
{
    QTreeWidgetItem* cur = m_tree->currentItem();
    if (!cur)
        return;
    const QString type = cur->data(0, TypeRole).toString();
    if (type == QLatin1String("forwardZones") || type == QLatin1String("reverseZones")
        || type == QLatin1String("forwarderZones")) {
        loadZones();
    } else if (type == QLatin1String("subnets")) {
        loadSubnets();
    } else if (type == QLatin1String("zone")) {
        refreshZoneNode(cur);
    } else if (type == QLatin1String("scope")) {
        loadRecordsFor(cur);
    } else if (type == QLatin1String("zonePolicies")
               || type == QLatin1String("serverPolicies")) {
        loadPoliciesFor(cur);
    }
}

// ---------------------------------------------------------------- Tree nav

void MainWindow::showPage(QWidget* page)
{
    m_stack->setCurrentWidget(page);
}

void MainWindow::updateLocation(QTreeWidgetItem* item)
{
    QStringList parts;
    parts << m_activeServer;
    for (QTreeWidgetItem* i = item; i; i = i->parent())
        parts.prepend(i->text(0));
    m_location->setText(parts.join(QStringLiteral("  ›  ")));
}

void MainWindow::navigateTo(QTreeWidgetItem* item)
{
    m_tree->setCurrentItem(item); // triggers onTreeCurrentChanged
    m_tree->scrollToItem(item, QAbstractItemView::PositionAtCenter);
}

void MainWindow::onTreeCurrentChanged(QTreeWidgetItem* current, QTreeWidgetItem*)
{
    if (!current) {
        m_location->clear();
        showPage(m_pageBlank);
        return;
    }
    updateLocation(current);
    const QString type = current->data(0, TypeRole).toString();

    if (type == QLatin1String("forwardZones") || type == QLatin1String("reverseZones")
        || type == QLatin1String("forwarderZones")) {
        if (current->childCount() == 1 &&
            current->child(0)->data(0, TypeRole).toString() == QLatin1String("placeholder"))
            loadZones();
        fillZoneTable(type);
        showPage(m_zoneTable->parentWidget());
    } else if (type == QLatin1String("subnets")) {
        loadSubnets();
        showPage(m_subnetTable->parentWidget());
    } else if (type == QLatin1String("zone")) {
        // Условная пересылка: областей и записей нет — только свойства.
        QTreeWidgetItem* cat = current->parent();
        if (cat && cat->data(0, TypeRole).toString() == QLatin1String("forwarderZones")) {
            fillZoneProps(current);
            showPage(m_nodeProps->parentWidget());
            return;
        }
        if (current->data(0, DirectRecordsRole).toBool()) {
            // Обратная зона: без областей, сразу плоский список записей.
            if (!current->data(0, LoadedRole).toBool())
                loadRecordsFor(current);
            else
                fillRecordsTable(current->data(0, RecordsRole).toList());
            showPage(m_hostTable->parentWidget());
            return;
        }
        if (!current->data(0, LoadedRole).toBool())
            loadScopesFor(current);
        else
            fillScopeTableFromTree(current);
        showPage(m_scopeTable->parentWidget());
    } else if (type == QLatin1String("scope")) {
        if (!current->data(0, LoadedRole).toBool())
            loadRecordsFor(current);
        else
            fillRecordsTable(current->data(0, RecordsRole).toList());
        showPage(m_hostTable->parentWidget());
    } else if (type == QLatin1String("zonePolicies")
               || type == QLatin1String("serverPolicies")) {
        loadPoliciesFor(current);
        showPage(m_policyTable->parentWidget());
    }
}

void MainWindow::onTreeItemExpanded(QTreeWidgetItem* item)
{
    if (!item)
        return;
    const QString type = item->data(0, TypeRole).toString();
    if (type == QLatin1String("forwardZones") || type == QLatin1String("reverseZones")
        || type == QLatin1String("forwarderZones")) {
        // Expanding a zone category before its zones were fetched — load them
        // (the category still holds only the placeholder item).
        if (item->childCount() == 1 &&
            item->child(0)->data(0, TypeRole).toString() == QLatin1String("placeholder"))
            loadZones();
    } else if (type == QLatin1String("zone")) {
        QTreeWidgetItem* cat = item->parent();
        const bool forwarder =
            cat && cat->data(0, TypeRole).toString() == QLatin1String("forwarderZones");
        if (forwarder)
            return;
        if (item->data(0, DirectRecordsRole).toBool()) {
            if (!item->data(0, LoadedRole).toBool())
                loadRecordsFor(item);
        } else if (!item->data(0, LoadedRole).toBool()) {
            loadScopesFor(item);
        }
    } else if (type == QLatin1String("scope")) {
        if (!item->data(0, LoadedRole).toBool())
            loadRecordsFor(item);
    }
}

// ------------------------------------------------------------- Tree lookups

QTreeWidgetItem* MainWindow::categoryItem(const QString& type) const
{
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* it = m_tree->topLevelItem(i);
        if (it->data(0, TypeRole).toString() == type)
            return it;
    }
    return nullptr;
}

QTreeWidgetItem* MainWindow::findZoneItem(const QString& zone) const
{
    const QStringList cats = { QStringLiteral("forwardZones"), QStringLiteral("reverseZones"),
                               QStringLiteral("forwarderZones") };
    for (const QString& cat : cats) {
        QTreeWidgetItem* group = categoryItem(cat);
        if (!group)
            continue;
        for (int i = 0; i < group->childCount(); ++i) {
            QTreeWidgetItem* z = group->child(i);
            if (z->data(0, TypeRole).toString() == QLatin1String("zone")
                && z->data(0, ZoneRole).toString().compare(zone, Qt::CaseInsensitive) == 0)
                return z;
        }
    }
    return nullptr;
}

QTreeWidgetItem* MainWindow::findChildByType(QTreeWidgetItem* parent, const QString& type) const
{
    if (!parent)
        return nullptr;
    for (int i = 0; i < parent->childCount(); ++i) {
        if (parent->child(i)->data(0, TypeRole).toString() == type)
            return parent->child(i);
    }
    return nullptr;
}

QTreeWidgetItem* MainWindow::scopeOfItem(QTreeWidgetItem* item) const
{
    for (QTreeWidgetItem* i = item; i; i = i->parent()) {
        const QString t = i->data(0, TypeRole).toString();
        if (t == QLatin1String("scope"))
            return i;
        if (t == QLatin1String("zone") && i->data(0, DirectRecordsRole).toBool())
            return i; // обратная зона: записи лежат прямо в ней
    }
    return nullptr;
}

QTreeWidgetItem* MainWindow::policiesItemOfZone(QTreeWidgetItem* zoneItem) const
{
    return findChildByType(zoneItem, QStringLiteral("zonePolicies"));
}

bool MainWindow::isPoliciesItem(QTreeWidgetItem* item) const
{
    if (!item)
        return false;
    const QString t = item->data(0, TypeRole).toString();
    return t == QLatin1String("zonePolicies") || t == QLatin1String("serverPolicies");
}

void MainWindow::refreshZoneNode(QTreeWidgetItem* zoneItem)
{
    if (zoneItem->data(0, DirectRecordsRole).toBool()) {
        // Обратная зона: перечитать записи (без областей).
        loadRecordsFor(zoneItem);
        return;
    }
    // Перечитать области зоны; ранее загруженные записи областей и правила
    // восстановятся в loadScopesFor автоматически.
    loadScopesFor(zoneItem);
}

void MainWindow::reloadRecords()
{
    QTreeWidgetItem* cur = m_tree->currentItem();
    QTreeWidgetItem* sc = cur ? scopeOfItem(cur) : nullptr;
    if (sc)
        loadRecordsFor(sc);
}

void MainWindow::reloadPoliciesView()
{
    QTreeWidgetItem* cur = m_tree->currentItem();
    if (!cur)
        return;
    if (isPoliciesItem(cur)) {
        loadPoliciesFor(cur);
        return;
    }
    // Правила зоны: ближайшая зона-предок (или сама зона).
    for (QTreeWidgetItem* i = cur; i; i = i->parent()) {
        if (i->data(0, TypeRole).toString() == QLatin1String("zone")) {
            if (QTreeWidgetItem* pol = policiesItemOfZone(i))
                loadPoliciesFor(pol);
            return;
        }
    }
}

// --------------------------------------------------------------------- Zones

void MainWindow::loadZones()
{
    if (m_activeServer.isEmpty())
        return;
    const QString server = m_activeServer;
    setStatusMessage(tr("Загрузка зон с %1…").arg(server));
    m_svc.listZones(server,
        [this, server](bool ok, const QList<QJsonObject>& items, const QString& error) {
            if (!ok) {
                showError(tr("Ошибка загрузки зон"), error);
                setStatusMessage(tr("Ошибка загрузки зон"));
                return;
            }

            m_allZones = items;

            // Что было выбрано и у каких зон загружено содержимое — до того,
            // как дочерние узлы будут удалены.
            QString selZone;
            QString selCategory;
            if (QTreeWidgetItem* cur = m_tree->currentItem()) {
                QTreeWidgetItem* zoneAnc = nullptr;
                QString catAnc;
                for (QTreeWidgetItem* p = cur; p; p = p->parent()) {
                    const QString t = p->data(0, TypeRole).toString();
                    if (t == QLatin1String("zone") && !zoneAnc)
                        zoneAnc = p;
                    if (t == QLatin1String("forwardZones") || t == QLatin1String("reverseZones")
                        || t == QLatin1String("forwarderZones"))
                        catAnc = t;
                }
                if (zoneAnc)
                    selZone = zoneAnc->data(0, ZoneRole).toString();
                else
                    selCategory = catAnc;
            }
            QStringList keepLoaded;
            for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
                QTreeWidgetItem* top = m_tree->topLevelItem(i);
                const QString t = top->data(0, TypeRole).toString();
                if (t != QLatin1String("forwardZones") && t != QLatin1String("reverseZones")
                    && t != QLatin1String("forwarderZones"))
                    continue;
                for (int c = 0; c < top->childCount(); ++c) {
                    QTreeWidgetItem* z = top->child(c);
                    if (z->data(0, TypeRole).toString() == QLatin1String("zone")
                        && z->data(0, LoadedRole).toBool())
                        keepLoaded << z->data(0, ZoneRole).toString();
                }
            }

            // Перестроить три категории зон.
            for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
                QTreeWidgetItem* cat = m_tree->topLevelItem(i);
                const QString catType = cat->data(0, TypeRole).toString();
                if (catType != QLatin1String("forwardZones")
                    && catType != QLatin1String("reverseZones")
                    && catType != QLatin1String("forwarderZones"))
                    continue;
                qDeleteAll(cat->takeChildren());
                int n = 0;
                for (const QJsonObject& o : items) {
                    if (categoryTypeOf(zoneCategory(o)) != catType)
                        continue;
                    const QString zoneName = Json::propStr(o, { "ZoneName" });
                    auto* zi = new QTreeWidgetItem(cat, QStringList() << zoneName);
                    zi->setData(0, TypeRole, QStringLiteral("zone"));
                    zi->setData(0, ServerRole, server);
                    zi->setData(0, ZoneRole, zoneName);
                    zi->setData(0, ZoneJsonRole, o);
                    // У обратной зоны областей нет — записи лежат в самой зоне.
                    const bool direct = catType == QLatin1String("reverseZones");
                    zi->setData(0, DirectRecordsRole, direct);
                    if (direct)
                        zi->setData(0, ScopeRole, QString());
                    zi->setIcon(0, TreeIcons::zone());
                    if (catType != QLatin1String("forwarderZones")) {
                        auto* ph =
                            new QTreeWidgetItem(zi, QStringList() << tr("(разверните для загрузки)"));
                        ph->setData(0, TypeRole, QStringLiteral("placeholder"));
                        ph->setFlags(Qt::ItemIsEnabled);
                    }
                    if (keepLoaded.contains(zoneName)) {
                        // async: восстановит содержимое зоны.
                        if (direct)
                            loadRecordsFor(zi);
                        else
                            loadScopesFor(zi);
                    }
                    ++n;
                }
                if (n == 0) {
                    auto* none = new QTreeWidgetItem(cat, QStringList() << tr("(нет зон)"));
                    none->setData(0, TypeRole, QStringLiteral("placeholder"));
                    none->setFlags(Qt::ItemIsEnabled);
                }
            }

            // Восстановить выбор и таблицу текущей категории.
            if (!selZone.isEmpty()) {
                if (QTreeWidgetItem* zi = findZoneItem(selZone))
                    m_tree->setCurrentItem(zi);
            } else if (!selCategory.isEmpty()) {
                // Выбор не изменился (категория не удалялась) — сигнал сам не
                // придёт, поэтому таблицу заполняем вручную.
                fillZoneTable(selCategory);
            }

            setStatusMessage(tr("Зон: %1").arg(items.size()));
        });
}

// Таблица зон одной категории (прямые / обратные / пересылка).
void MainWindow::fillZoneTable(const QString& categoryType)
{
    const ZoneCat cat = categoryType == QLatin1String("reverseZones")
                            ? ZoneCat::Reverse
                        : categoryType == QLatin1String("forwarderZones")
                            ? ZoneCat::Forwarder
                            : ZoneCat::Forward;

    QList<const QJsonObject*> zones;
    for (const QJsonObject& o : m_allZones) {
        if (zoneCategory(o) == cat)
            zones << &o;
    }

    m_zoneTable->setRowCount(zones.size());
    for (int row = 0; row < zones.size(); ++row) {
        const QJsonObject& o = *zones.at(row);
        const QString name = Json::propStr(o, { "ZoneName" });
        auto* nameItem = new QTableWidgetItem(TreeIcons::zone(), name);
        nameItem->setData(Qt::UserRole, name);
        nameItem->setData(Qt::UserRole + 1, o);
        if (isDomainPrimaryZone(o))
            nameItem->setToolTip(tr("Основная зона домена защищена от удаления"));
        m_zoneTable->setItem(row, 0, nameItem);
        m_zoneTable->setItem(row, 1, new QTableWidgetItem(Json::propStr(o, { "ZoneType" })));
        m_zoneTable->setItem(row, 2, new QTableWidgetItem(
            Json::propBool(o, { "IsDsIntegrated" }) ? tr("да") : QString()));
        m_zoneTable->setItem(row, 3, new QTableWidgetItem(Json::propStr(o, { "DynamicUpdate" })));
        m_zoneTable->setItem(row, 4, new QTableWidgetItem(Json::propStr(o, { "ReplicationScope" })));

        QStringList state;
        if (Json::propBool(o, { "IsPaused" })) state << tr("пауза");
        if (Json::propBool(o, { "IsShutdown" })) state << tr("остановлена");
        if (Json::propBool(o, { "IsReadOnly" })) state << tr("только чтение");
        m_zoneTable->setItem(row, 5, new QTableWidgetItem(state.join(QLatin1String(", "))));

        m_zoneTable->setCellWidget(row, 6,
            makeRowTools([this, row] {
                m_zoneTable->setCurrentCell(row, 0);
                editZone();
            },
            [this, row] {
                m_zoneTable->setCurrentCell(row, 0);
                removeZone();
            },
            tr("Свойства зоны…"),
            isDomainPrimaryZone(o) ? tr("Основная зона домена защищена от удаления")
                                   : tr("Удалить зону…")));
    }
}

// Страница свойств зоны пересылки (правка недоступна — только просмотр).
void MainWindow::fillZoneProps(QTreeWidgetItem* zoneItem)
{
    QJsonObject o = zoneItem->data(0, ZoneJsonRole).toJsonObject();
    if (o.isEmpty())
        o = zoneInfo(zoneItem->data(0, ZoneRole).toString());

    m_nodeProps->setRowCount(0);
    int row = 0;
    auto addRow = [this, &row](const QString& key, const QString& value) {
        m_nodeProps->setRowCount(row + 1);
        m_nodeProps->setItem(row, 0, new QTableWidgetItem(key));
        m_nodeProps->setItem(row, 1, new QTableWidgetItem(value));
        ++row;
    };
    addRow(tr("Имя"), Json::propStr(o, { "ZoneName" }));
    addRow(tr("Тип"), Json::propStr(o, { "ZoneType" }));
    addRow(tr("Master-серверы"),
           Json::propStrList(o, { "MasterServers" }).join(QLatin1String(", ")));
    addRow(tr("Статус"), Json::propBool(o, { "IsDsIntegrated" }) ? tr("AD-интегрированная")
                                                                : tr("файловая"));
    m_nodeProps->horizontalHeader()->setStretchLastSection(true);
}

void MainWindow::addZone()
{
    if (m_activeServer.isEmpty()) {
        QMessageBox::information(this, tr("Зона"), tr("Сначала выберите DNS-сервер."));
        return;
    }
    ZoneDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted)
        return;

    const QString name = dlg.name();
    if (!dlg.isReverse() && name.isEmpty()) {
        showError(tr("Зона"), tr("Укажите имя зоны."));
        return;
    }
    if (dlg.isReverse() && dlg.networkId().isEmpty()) {
        showError(tr("Зона"), tr("Укажите сеть в формате CIDR."));
        return;
    }

    const QString server = m_activeServer;
    const QString zoneType = dlg.zoneType();
    const bool reverse = dlg.isReverse();
    const QString networkId = dlg.networkId();
    const QString repl = dlg.replicationScope();
    const QString dyn = dlg.dynamicUpdate();
    const QStringList masters = dlg.masters();
    const QString cmd =
        m_svc.zoneAddCommand(name, zoneType, reverse, networkId, repl, dyn, masters);
    const QString title = name.trimmed().isEmpty()
                              ? tr("Добавить обратную зону %1").arg(networkId)
                              : tr("Добавить зону «%1»").arg(name);

    journalEdit(title, { m_svc.zonesCacheFile(server) },
        [this, server, name, zoneType, reverse, repl, dyn] {
            m_svc.cacheZoneAdd(server, name, zoneType, reverse, repl, dyn);
        },
        [this, server, cmd](Journal::DoneFn done) { m_svc.runPs(server, cmd, done); },
        [this] { loadZones(); });
}

void MainWindow::removeZone()
{
    QString zone;
    QTreeWidgetItem* cur = m_tree->currentItem();
    if (cur && cur->data(0, TypeRole).toString() == QLatin1String("zone"))
        zone = cur->data(0, ZoneRole).toString();
    else {
        const int row = m_zoneTable->currentRow();
        if (row >= 0)
            zone = m_zoneTable->item(row, 0)->data(Qt::UserRole).toString();
    }
    if (zone.isEmpty() || m_activeServer.isEmpty()) {
        QMessageBox::information(this, tr("Удаление зоны"), tr("Выберите зону."));
        return;
    }
    // Основная AD-интегрированная зона — зона домена, удалять не даём
    // (как в dnsmgmt.msc, где зона домена защищена от удаления).
    const QJsonObject info = zoneInfo(zone);
    if (!info.isEmpty() && isDomainPrimaryZone(info)) {
        QMessageBox::information(this, tr("Удаление зоны"),
            tr("Основная зона домена «%1» защищена от удаления.").arg(zone));
        return;
    }
    if (QMessageBox::question(this, tr("Удаление зоны"),
            tr("Удалить зону \"%1\" со всеми записями?").arg(zone)) != QMessageBox::Yes)
        return;

    const QString server = m_activeServer;
    const QString cmd = m_svc.zoneRemoveCommand(zone);
    journalEdit(tr("Удалить зону «%1»").arg(zone), { m_svc.zonesCacheFile(server) },
        [this, server, zone] { m_svc.cacheZoneRemove(server, zone); },
        [this, server, cmd](Journal::DoneFn done) { m_svc.runPs(server, cmd, done); },
        [this] { loadZones(); });
}

void MainWindow::editZone()
{
    QString zone;
    QTreeWidgetItem* cur = m_tree->currentItem();
    if (cur && cur->data(0, TypeRole).toString() == QLatin1String("zone"))
        zone = cur->data(0, ZoneRole).toString();
    else {
        const int row = m_zoneTable->currentRow();
        if (row >= 0)
            zone = m_zoneTable->item(row, 0)->data(Qt::UserRole).toString();
    }
    if (zone.isEmpty() || m_activeServer.isEmpty()) {
        QMessageBox::information(this, tr("Свойства зоны"), tr("Выберите зону."));
        return;
    }
    const QJsonObject info = zoneInfo(zone);
    if (info.isEmpty()) {
        QMessageBox::information(this, tr("Свойства зоны"),
                                 tr("Свойства зоны \"%1\" не загружены — обновите список.").arg(zone));
        return;
    }
    const QString zoneType = Json::propStr(info, { QStringLiteral("ZoneType") });
    if (zoneType == QLatin1String("Forwarder")) {
        QMessageBox::information(this, tr("Свойства зоны"),
            tr("Зоны пересылки изменяются в консоли dnsmgmt.msc — здесь доступны "
               "только свойства для просмотра."));
        return;
    }

    ZoneEditDialog dlg(info, this);
    if (dlg.exec() != QDialog::Accepted)
        return;

    const QString server = m_activeServer;
    if (zoneType == QLatin1String("Primary")) {
        const QString cmd = m_svc.zoneSetCommand(zone, dlg.dynamicUpdate(), dlg.replicationScope());
        const QString title = tr("Свойства зоны «%1»").arg(zone);
        journalEdit(title, { m_svc.zonesCacheFile(server) },
            [this, server, zone, du = dlg.dynamicUpdate(), rs = dlg.replicationScope()] {
                m_svc.cacheZoneSet(server, zone, du, rs);
            },
            [this, server, cmd](Journal::DoneFn done) { m_svc.runPs(server, cmd, done); },
            [this] { loadZones(); });
    } else {
        const QStringList masters = dlg.masters();
        const QString cmd = m_svc.zoneSetMastersCommand(zone, zoneType, masters);
        const QString title = tr("Master-серверы зоны «%1»").arg(zone);
        journalEdit(title, { m_svc.zonesCacheFile(server) },
            [this, server, zone, masters] { m_svc.cacheZoneSetMasters(server, zone, masters); },
            [this, server, cmd](Journal::DoneFn done) { m_svc.runPs(server, cmd, done); },
            [this] { loadZones(); });
    }
}

void MainWindow::openZoneInTree()
{
    QString zone;
    QTreeWidgetItem* cur = m_tree->currentItem();
    if (cur && cur->data(0, TypeRole).toString() == QLatin1String("zone"))
        zone = cur->data(0, ZoneRole).toString();
    else {
        const int row = m_zoneTable->currentRow();
        if (row >= 0)
            zone = m_zoneTable->item(row, 0)->data(Qt::UserRole).toString();
    }
    if (zone.isEmpty() || m_activeServer.isEmpty()) {
        QMessageBox::information(this, tr("Зона"), tr("Выберите зону."));
        return;
    }
    if (QTreeWidgetItem* zi = findZoneItem(zone))
        navigateTo(zi);
    else
        QMessageBox::information(this, tr("Зона"), tr("Зона \"%1\" не найдена в дереве.").arg(zone));
}

// ------------------------------------------------------- Records (по областям)

void MainWindow::loadRecords()
{
    QTreeWidgetItem* cur = m_tree->currentItem();
    if (!cur)
        return;
    if (cur->data(0, TypeRole).toString() == QLatin1String("scope"))
        loadRecordsFor(cur);
    else if (QTreeWidgetItem* sc = scopeOfItem(cur))
        loadRecordsFor(sc);
}

void MainWindow::loadRecordsFor(QTreeWidgetItem* scopeItem)
{
    const QString server = scopeItem->data(0, ServerRole).toString();
    const QString zone = scopeItem->data(0, ZoneRole).toString();
    const QString scope = scopeItem->data(0, ScopeRole).toString();
    qDeleteAll(scopeItem->takeChildren());
    scopeItem->setData(0, LoadedRole, true);
    setStatusMessage(tr("Загрузка записей %1%2…")
                         .arg(zone, scope.isEmpty() ? QString() : QStringLiteral(" / ") + scope));

    m_svc.listRecords(server, zone, scope,
        [this, scopeItem, server, zone, scope](bool ok, const QList<QJsonObject>& items,
                                               const QString& error) {
            if (!scopeItem->treeWidget())
                return;
            QTreeWidgetItem* cur = m_tree->currentItem();
            const bool selInside = cur && (cur == scopeItem || isDescendantOf(cur, scopeItem));
            // У обратной зоны узел «Правила» создаётся здесь же — запоминаем,
            // был ли он загружен, чтобы восстановить после перестроения.
            const bool direct = scopeItem->data(0, DirectRecordsRole).toBool();
            bool policiesLoaded = false;
            for (int i = 0; direct && i < scopeItem->childCount(); ++i) {
                QTreeWidgetItem* ch = scopeItem->child(i);
                if (ch->data(0, TypeRole).toString() == QLatin1String("zonePolicies")
                    && ch->data(0, LoadedRole).toBool())
                    policiesLoaded = true;
            }
            qDeleteAll(scopeItem->takeChildren());
            if (!ok) {
                showError(tr("Ошибка загрузки записей"), error);
                auto* err = new QTreeWidgetItem(scopeItem, QStringList() << tr("Ошибка: ") + error);
                err->setFlags(Qt::ItemIsEnabled);
                return;
            }
            if (items.isEmpty()) {
                auto* none = new QTreeWidgetItem(scopeItem, QStringList() << tr("(нет записей)"));
                none->setData(0, TypeRole, QStringLiteral("placeholder"));
                none->setFlags(Qt::ItemIsEnabled);
                if (direct) {
                    auto* pol = new QTreeWidgetItem(scopeItem, QStringList() << tr("Правила"));
                    pol->setData(0, TypeRole, QStringLiteral("zonePolicies"));
                    pol->setData(0, ServerRole, server);
                    pol->setData(0, ZoneRole, zone);
                    pol->setIcon(0, TreeIcons::policies());
                    if (policiesLoaded)
                        loadPoliciesFor(pol);
                }
                scopeItem->setData(0, RecordsRole, QVariantList());
                fillRecordsTable({});
                setStatusMessage(tr("Записей: 0"));
                return;
            }

            // Плоский список записей — хранится на элементе области (или на
            // самой обратной зоне). Без иерархии по узлам: строка таблицы =
            // одна запись ровно в том виде, в каком её вернул
            // Get-DnsServerResourceRecord (HostName/RecordType/TTL/RecordData).
            QVariantList flat;
            for (const QJsonObject& o : items)
                flat << o.toVariantMap();
            scopeItem->setData(0, RecordsRole, flat);
            fillRecordsTable(flat);
            setStatusMessage(tr("Записей: %1").arg(items.size()));

            // У обратной зоны нет областей — узел «Правила» добавляем к самой
            // зоне (как в dnsmgmt, где у обратной зоны есть политики).
            if (direct) {
                auto* pol = new QTreeWidgetItem(scopeItem, QStringList() << tr("Правила"));
                pol->setData(0, TypeRole, QStringLiteral("zonePolicies"));
                pol->setData(0, ServerRole, server);
                pol->setData(0, ZoneRole, zone);
                pol->setIcon(0, TreeIcons::policies());
                if (policiesLoaded)
                    loadPoliciesFor(pol);
            }
            if (selInside && m_tree->currentItem() != scopeItem)
                m_tree->setCurrentItem(scopeItem);
        });
}

// Ячейка «Управление» таблицы: ✏ и 🗑 — одинаковая для всех таблиц (паттерн
// таблицы правил). onEdit == nullptr — кнопка отключена (например, область).
QWidget* MainWindow::makeRowTools(const std::function<void()>& onEdit,
                                  const std::function<void()>& onDelete,
                                  const QString& editTip, const QString& deleteTip)
{
    auto* w = new QWidget(m_zoneTable);
    auto* l = new QHBoxLayout(w);
    l->setContentsMargins(2, 2, 2, 2);
    l->setSpacing(2);
    auto makeBtn = [&w](const QString& text, const QString& tip) {
        auto* b = new QToolButton(w);
        b->setText(text);
        b->setToolTip(tip);
        b->setAutoRaise(true);
        return b;
    };
    QToolButton* edit = makeBtn(QStringLiteral("✏"), editTip);
    QToolButton* del = makeBtn(QStringLiteral("\U0001F5D1"), deleteTip);
    l->addWidget(edit);
    l->addWidget(del);
    l->addStretch();
    if (onEdit) {
        connect(edit, &QToolButton::clicked, this, [onEdit] { onEdit(); });
    } else {
        edit->setEnabled(false);
    }
    if (onDelete) {
        connect(del, &QToolButton::clicked, this, [onDelete] { onDelete(); });
    } else {
        del->setEnabled(false);
    }
    return w;
}

// Таблица записей — плоский список: одна строка = одна запись ровно в том
// виде, в котором её вернул Get-DnsServerResourceRecord. Так ✏/🗑 действуют
// на конкретную запись (правка = удалить старую с её параметрами + добавить
// новую). recs — список JSON-объектов записей (RecordsRole узла/области).
void MainWindow::fillRecordsTable(const QVariantList& recs)
{
    m_hostTable->setRowCount(recs.size());
    for (int row = 0; row < recs.size(); ++row) {
        const QJsonObject r = recs.at(row).toJsonObject();
        const QString type = Json::propStr(r, { "RecordType" });
        const QString name = Json::propStr(r, { "HostName" });
        const QString data =
            Json::recordDataText(Json::prop(r, { "RecordData" }).toObject(), type);
        const int ttl = static_cast<int>(Json::propNum(r, { "TTLSeconds" }, 3600));
        m_hostTable->setItem(row, 0, new QTableWidgetItem(name));
        m_hostTable->setItem(row, 1, new QTableWidgetItem(type));
        m_hostTable->setItem(row, 2, new QTableWidgetItem(QString::number(ttl)));
        m_hostTable->setItem(row, 3, new QTableWidgetItem(data));
        // ✏ отключается для записей, которые не создаёт Add-DnsServerResourceRecord
        // (SOA и т.п.) — их меняют только на сервере; 🗑 остаётся доступен.
        const std::function<void()> onEdit =
            isEditableRecordType(type)
                ? std::function<void()>([this, row] {
                      m_hostTable->setCurrentCell(row, 0);
                      editRecordAction();
                  })
                : std::function<void()>();
        m_hostTable->setCellWidget(row, 4,
            makeRowTools(onEdit,
            [this, row] {
                m_hostTable->setCurrentCell(row, 0);
                removeRecordAction();
            },
            tr("Изменить запись…"), tr("Удалить запись…")));
    }
    if (m_hostTable->rowCount() > 0 && m_hostTable->currentRow() < 0)
        m_hostTable->setCurrentCell(0, 0);
}

void MainWindow::addRecordAction()
{
    QTreeWidgetItem* cur = m_tree->currentItem();
    if (!cur)
        return;
    QTreeWidgetItem* sc = cur->data(0, TypeRole).toString() == QLatin1String("scope")
                              ? cur
                              : scopeOfItem(cur);
    if (!sc) {
        QMessageBox::information(this, tr("Запись"),
                                 tr("Выберите область зоны в дереве."));
        return;
    }
    const QString zone = sc->data(0, ZoneRole).toString();
    const QString scope = sc->data(0, ScopeRole).toString();
    if (zone.isEmpty() || m_activeServer.isEmpty()) {
        QMessageBox::information(this, tr("Запись"), tr("Выберите область зоны в дереве."));
        return;
    }
    addRecordFor(zone, scope, QString());
}

void MainWindow::editRecordAction()
{
    QTreeWidgetItem* cur = m_tree->currentItem();
    if (!cur)
        return;
    const QString zone = cur->data(0, ZoneRole).toString();
    const QString scope = scopeOfItem(cur) ? scopeOfItem(cur)->data(0, ScopeRole).toString()
                                            : QString();
    const int row = m_hostTable->currentRow();
    const QVariantList recs = cur->data(0, RecordsRole).toList();
    if (row < 0 || row >= recs.size())
        return;
    const QJsonObject rec = recs.at(row).toJsonObject();
    const QString rtype = Json::propStr(rec, { "RecordType" });
    if (!isEditableRecordType(rtype)) {
        QMessageBox::information(
            this, tr("Запись"),
            tr("Запись типа %1 изменяется только средствами DNS-сервера "
               "(например, SOA — через свойства зоны).")
                .arg(rtype));
        return;
    }
    editRecordFor(zone, scope, rec);
}

void MainWindow::removeRecordAction()
{
    QTreeWidgetItem* cur = m_tree->currentItem();
    if (!cur)
        return;
    const QString zone = cur->data(0, ZoneRole).toString();
    const QString scope = scopeOfItem(cur) ? scopeOfItem(cur)->data(0, ScopeRole).toString()
                                            : QString();
    const int row = m_hostTable->currentRow();
    const QVariantList recs = cur->data(0, RecordsRole).toList();
    if (row < 0 || row >= recs.size())
        return;
    removeRecordFor(zone, scope, recs.at(row).toJsonObject());
}

void MainWindow::addRecordFor(const QString& zone, const QString& scope,
                              const QString& presetName)
{
    RecordDialog dlg(&m_svc, m_activeServer, zone, scope, QStringLiteral("A"), this);
    if (!presetName.isEmpty())
        dlg.setName(presetName);
    if (dlg.exec() != QDialog::Accepted)
        return;
    const RecordAddParams p = dlg.params();
    if (p.name.isEmpty()) {
        showError(tr("Запись"), tr("Укажите имя хоста."));
        return;
    }

    const QString server = m_activeServer;
    const QString cmd = m_svc.recordAddCommand(p);
    journalEdit(tr("Добавить запись %1 %2").arg(p.type, p.name),
                { m_svc.recordsCacheFile(server, zone, p.scope) },
        [this, server, p] { m_svc.cacheRecordAdd(server, p); },
        [this, server, cmd](Journal::DoneFn done) { m_svc.runPs(server, cmd, done); },
        [this] { reloadRecords(); });
}

void MainWindow::editRecordFor(const QString& zone, const QString& scope,
                               const QJsonObject& record)
{
    const RecordAddParams old = makeRecordParams(zone, scope, record);
    RecordDialog dlg(&m_svc, m_activeServer, zone, scope, old.type, this);
    dlg.setParams(old);
    if (dlg.exec() != QDialog::Accepted)
        return;
    const RecordAddParams p = dlg.params();
    const QString match = recordMatchValue(record);

    // Надёжная правка: удалить старую запись и добавить новую — одной
    // записью журнала, команды выполняются подряд при сохранении. Поле
    // «Область» может измениться — снимаем снимки обоих файлов кэша.
    const QString server = m_activeServer;
    const QString removeCmd =
        m_svc.recordRemoveCommand(zone, old.scope, old.type, old.name, match);
    const QString addCmd = m_svc.recordAddCommand(p);
    QStringList files = { m_svc.recordsCacheFile(server, zone, p.scope) };
    const QString oldFile = m_svc.recordsCacheFile(server, zone, old.scope);
    if (!files.contains(oldFile))
        files << oldFile;
    journalEdit(tr("Изменить запись %1 %2").arg(old.type, old.name), files,
        [this, server, zone, old, match, p] {
            m_svc.cacheRecordRemove(server, zone, old.scope, old.type, old.name, match);
            m_svc.cacheRecordAdd(server, p);
        },
        [this, server, removeCmd, addCmd](Journal::DoneFn done) {
            m_svc.runPs(server, removeCmd,
                [this, server, addCmd, done](bool ok, const QString& error) {
                    if (!ok) {
                        done(false, error);
                        return;
                    }
                    m_svc.runPs(server, addCmd, done);
                });
        },
        [this] { reloadRecords(); });
}

void MainWindow::removeRecordFor(const QString& zone, const QString& scope,
                                 const QJsonObject& record)
{
    const QString type = Json::propStr(record, { "RecordType" });
    const QString name = Json::propStr(record, { "HostName" });
    const QString data =
        Json::recordDataText(Json::prop(record, { "RecordData" }).toObject(), type);
    if (QMessageBox::question(this, tr("Удаление записи"),
            tr("Удалить запись %1 %2 %3?").arg(type, name, data)) != QMessageBox::Yes)
        return;

    const QString server = m_activeServer;
    const QString match = recordMatchValue(record);
    const QString cmd = m_svc.recordRemoveCommand(zone, scope, type, name, match);
    journalEdit(tr("Удалить запись %1 %2").arg(type, name),
                { m_svc.recordsCacheFile(server, zone, scope) },
        [this, server, zone, scope, type, name, match] {
            m_svc.cacheRecordRemove(server, zone, scope, type, name, match);
        },
        [this, server, cmd](Journal::DoneFn done) { m_svc.runPs(server, cmd, done); },
        [this] { reloadRecords(); });
}

// -------------------------------------------------------------------- Scopes

void MainWindow::loadScopes()
{
    QTreeWidgetItem* cur = m_tree->currentItem();
    if (!cur)
        return;
    if (cur->data(0, TypeRole).toString() == QLatin1String("zone"))
        loadScopesFor(cur);
    else {
        for (QTreeWidgetItem* p = cur; p; p = p->parent()) {
            if (p->data(0, TypeRole).toString() == QLatin1String("zone")) {
                loadScopesFor(p);
                return;
            }
        }
    }
}

void MainWindow::loadScopesFor(QTreeWidgetItem* zoneItem)
{
    const QString server = zoneItem->data(0, ServerRole).toString();
    const QString zone = zoneItem->data(0, ZoneRole).toString();
    setStatusMessage(tr("Загрузка областей зоны %1…").arg(zone));

    m_svc.listZoneScopes(server, zone,
        [this, zoneItem, zone, server](bool ok, const QList<QJsonObject>& items,
                                       const QString& error) {
            if (!zoneItem->treeWidget())
                return;
            if (!ok) {
                showError(tr("Ошибка загрузки областей зоны %1").arg(zone), error);
                setStatusMessage(tr("Ошибка загрузки областей"));
                return;
            }

            // Что уже было загружено: записи областей и правила зоны.
            QStringList loadedScopes;
            bool policiesLoaded = false;
            for (int i = 0; i < zoneItem->childCount(); ++i) {
                QTreeWidgetItem* ch = zoneItem->child(i);
                const QString t = ch->data(0, TypeRole).toString();
                if (t == QLatin1String("scope") && ch->data(0, LoadedRole).toBool())
                    loadedScopes << ch->data(0, ScopeRole).toString();
                else if (t == QLatin1String("zonePolicies") && ch->data(0, LoadedRole).toBool())
                    policiesLoaded = true;
            }

            qDeleteAll(zoneItem->takeChildren());
            zoneItem->setData(0, LoadedRole, true);

            auto addScope = [zoneItem, zone, server](const QString& displayName,
                                                     const QString& scopeValue,
                                                     const QString& fileName, bool isDefault) {
                auto* si = new QTreeWidgetItem(zoneItem, QStringList() << displayName);
                si->setData(0, TypeRole, QStringLiteral("scope"));
                si->setData(0, ServerRole, server);
                si->setData(0, ZoneRole, zone);
                si->setData(0, ScopeRole, scopeValue);
                si->setData(0, DefaultScopeRole, isDefault);
                si->setData(0, ScopeFileRole, fileName);
                si->setIcon(0, TreeIcons::scope());
                auto* ph = new QTreeWidgetItem(si, QStringList() << tr("(разверните для загрузки)"));
                ph->setData(0, TypeRole, QStringLiteral("placeholder"));
                ph->setFlags(Qt::ItemIsEnabled);
            };

            // Область по умолчанию — первой (на сервере её имя совпадает с
            // именем зоны), именованные области — по алфавиту.
            DNSZoneScope def;
            bool hasDefault = false;
            QList<DNSZoneScope> named;
            for (const QJsonObject& o : items) {
                const DNSZoneScope s = DNSZoneScope::fromJsonObject(o);
                if (isDefaultScopeName(s.name, zone)) {
                    def = s;
                    hasDefault = true;
                } else {
                    named << s;
                }
            }
            std::sort(named.begin(), named.end(), [](const DNSZoneScope& a, const DNSZoneScope& b) {
                return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
            });
            if (hasDefault)
                addScope(tr("Область по умолчанию"), QString(), def.fileName, true);
            for (const DNSZoneScope& s : named)
                addScope(s.name, s.name, s.fileName, false);

            // Правила зоны — последним пунктом (как в примере дерева).
            auto* pol = new QTreeWidgetItem(zoneItem, QStringList() << tr("Правила"));
            pol->setData(0, TypeRole, QStringLiteral("zonePolicies"));
            pol->setData(0, ServerRole, server);
            pol->setData(0, ZoneRole, zone);
            pol->setIcon(0, TreeIcons::policies());

            // Восстановить ранее загруженное содержимое.
            for (int i = 0; i < zoneItem->childCount(); ++i) {
                QTreeWidgetItem* ch = zoneItem->child(i);
                if (ch->data(0, TypeRole).toString() == QLatin1String("scope")
                    && loadedScopes.contains(ch->data(0, ScopeRole).toString()))
                    loadRecordsFor(ch);
            }
            if (policiesLoaded)
                loadPoliciesFor(pol);

            if (m_tree->currentItem() == zoneItem) {
                fillScopeTableFromTree(zoneItem);
                setStatusMessage(tr("Областей: %1").arg(zoneItem->childCount() - 1));
            } else {
                setStatusMessage(tr("Области зоны %1 загружены").arg(zone));
            }
        });
}

// Таблица областей строится из уже загруженных дочерних узлов зоны.
void MainWindow::fillScopeTableFromTree(QTreeWidgetItem* zoneItem)
{
    QList<QTreeWidgetItem*> scopes;
    for (int i = 0; i < zoneItem->childCount(); ++i) {
        QTreeWidgetItem* ch = zoneItem->child(i);
        if (ch->data(0, TypeRole).toString() == QLatin1String("scope"))
            scopes << ch;
    }
    m_scopeTable->setRowCount(scopes.size());
    for (int row = 0; row < scopes.size(); ++row) {
        QTreeWidgetItem* si = scopes.at(row);
        const bool isDefault = si->data(0, DefaultScopeRole).toBool();
        auto* nameItem = new QTableWidgetItem(TreeIcons::scope(), si->text(0));
        nameItem->setData(Qt::UserRole, si->data(0, ScopeRole));    // значение области
        nameItem->setData(Qt::UserRole + 1, isDefault);
        nameItem->setData(Qt::UserRole + 2, si->data(0, ZoneRole));
        nameItem->setToolTip(isDefault ? tr("Область по умолчанию защищена от удаления")
                                       : tr("Область зоны"));
        m_scopeTable->setItem(row, 0, nameItem);
        m_scopeTable->setItem(row, 1, new QTableWidgetItem(si->data(0, ScopeFileRole).toString()));
        // «Управление»: ✏ — свойства области (Windows DNS не поддерживает
        // переименование — окно даёт справку и кнопку удаления); 🗑 —
        // удалить (область по умолчанию защищена).
        m_scopeTable->setCellWidget(row, 2,
            makeRowTools([this, row] {
                m_scopeTable->setCurrentCell(row, 0);
                editScope();
            },
            isDefault ? nullptr
                      : std::function<void()>([this, row] {
                            m_scopeTable->setCurrentCell(row, 0);
                            removeScope();
                        }),
            tr("Свойства области…"),
            isDefault ? tr("Область по умолчанию защищена от удаления")
                      : tr("Удалить область…")));
    }
}

void MainWindow::addScope()
{
    QTreeWidgetItem* zoneItem = m_tree->currentItem();
    if (!zoneItem || zoneItem->data(0, TypeRole).toString() != QLatin1String("zone")
        || zoneItem->data(0, DirectRecordsRole).toBool())
        return; // у обратной зоны областей нет
    const QString zone = zoneItem->data(0, ZoneRole).toString();

    ZoneScopeDialog dlg(zone, this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    if (dlg.name().isEmpty()) {
        showError(tr("Область"), tr("Укажите имя области."));
        return;
    }

    const QString server = m_activeServer;
    const QString cmd = m_svc.scopeAddCommand(zone, dlg.name());
    journalEdit(tr("Добавить область «%1» в %2").arg(dlg.name(), zone),
                { m_svc.scopesCacheFile(server, zone) },
        [this, server, zone, name = dlg.name()] { m_svc.cacheScopeAdd(server, zone, name); },
        [this, server, cmd](Journal::DoneFn done) { m_svc.runPs(server, cmd, done); },
        [this, zoneItem] { if (zoneItem->treeWidget()) loadScopesFor(zoneItem); });
}

void MainWindow::editScope()
{
    // Свойства области: из пункта дерева или из строки таблицы областей.
    QTreeWidgetItem* cur = m_tree->currentItem();
    QTreeWidgetItem* zoneItem = nullptr;
    QString displayName;  // как область показана
    QString scopeValue;   // значение области (пусто = по умолчанию)
    QString fileName;
    bool isDefault = false;

    if (cur && cur->data(0, TypeRole).toString() == QLatin1String("scope")
        && cur->parent()
        && cur->parent()->data(0, TypeRole).toString() == QLatin1String("zone")) {
        zoneItem = cur->parent();
        displayName = cur->text(0);
        scopeValue = cur->data(0, ScopeRole).toString();
        fileName = cur->data(0, ScopeFileRole).toString();
        isDefault = cur->data(0, DefaultScopeRole).toBool();
    } else {
        for (QTreeWidgetItem* p = cur; p; p = p->parent()) {
            if (p->data(0, TypeRole).toString() == QLatin1String("zone")) {
                zoneItem = p;
                break;
            }
        }
        if (!zoneItem)
            return;
        const int row = m_scopeTable->currentRow();
        if (row < 0 || !m_scopeTable->item(row, 0))
            return;
        scopeValue = m_scopeTable->item(row, 0)->data(Qt::UserRole).toString();
        displayName = m_scopeTable->item(row, 0)->text();
        isDefault = m_scopeTable->item(row, 0)->data(Qt::UserRole + 1).toBool();
        for (int i = 0; i < zoneItem->childCount(); ++i) {
            QTreeWidgetItem* ch = zoneItem->child(i);
            if (ch->data(0, TypeRole).toString() == QLatin1String("scope")
                && ch->data(0, ScopeRole).toString() == scopeValue) {
                fileName = ch->data(0, ScopeFileRole).toString();
                break;
            }
        }
    }

    const QString zone = zoneItem->data(0, ZoneRole).toString();
    ScopePropsDialog dlg(zone, displayName, scopeValue, fileName, isDefault, this);
    if (dlg.exec() == QDialog::Accepted && dlg.deleteRequested() && !isDefault) {
        // Удаление — через общий путь с подтверждением и журналом
        // (removeScope возьмёт данные из текущего выделения: пункта области
        // в дереве или строки таблицы).
        removeScope();
    }
}

void MainWindow::removeScope()
{
    QTreeWidgetItem* cur = m_tree->currentItem();
    QTreeWidgetItem* zoneItem = nullptr;
    QString name;        // значение области (пусто = по умолчанию)
    QString displayName; // как показана в дереве/таблице
    bool isDefault = false;

    if (cur && cur->data(0, TypeRole).toString() == QLatin1String("scope")
        && cur->parent()
        && cur->parent()->data(0, TypeRole).toString() == QLatin1String("zone")) {
        // Контекстное меню пункта области в дереве.
        zoneItem = cur->parent();
        name = cur->data(0, ScopeRole).toString();
        displayName = cur->text(0);
        isDefault = cur->data(0, DefaultScopeRole).toBool();
    } else {
        // Таблица областей (текущий узел — зона).
        for (QTreeWidgetItem* p = cur; p; p = p->parent()) {
            if (p->data(0, TypeRole).toString() == QLatin1String("zone")) {
                zoneItem = p;
                break;
            }
        }
        if (!zoneItem || zoneItem->data(0, TypeRole).toString() != QLatin1String("zone"))
            return;
        const int row = m_scopeTable->currentRow();
        if (row < 0 || !m_scopeTable->item(row, 0))
            return;
        name = m_scopeTable->item(row, 0)->data(Qt::UserRole).toString();
        displayName = m_scopeTable->item(row, 0)->text();
        isDefault = m_scopeTable->item(row, 0)->data(Qt::UserRole + 1).toBool();
    }

    const QString zone = zoneItem->data(0, ZoneRole).toString();
    if (isDefault) {
        QMessageBox::information(this, tr("Удаление области"),
            tr("Область по умолчанию зоны %1 защищена от удаления.").arg(zone));
        return;
    }
    if (QMessageBox::question(this, tr("Удаление области"),
            tr("Удалить область \"%1\" зоны %2?").arg(displayName, zone)) != QMessageBox::Yes)
        return;

    const QString server = m_activeServer;
    const QString cmd = m_svc.scopeRemoveCommand(zone, name);
    journalEdit(tr("Удалить область «%1» из %2").arg(displayName, zone),
                { m_svc.scopesCacheFile(server, zone) },
        [this, server, zone, name] { m_svc.cacheScopeRemove(server, zone, name); },
        [this, server, cmd](Journal::DoneFn done) { m_svc.runPs(server, cmd, done); },
        [this, zoneItem] { if (zoneItem->treeWidget()) loadScopesFor(zoneItem); });
}

// ----------------------------------------------------------------- Policies

void MainWindow::loadPolicies()
{
    QTreeWidgetItem* cur = m_tree->currentItem();
    if (isPoliciesItem(cur))
        loadPoliciesFor(cur);
}

void MainWindow::loadPoliciesFor(QTreeWidgetItem* policiesGroup)
{
    const QString server = policiesGroup->data(0, ServerRole).toString();
    const QString zone = policiesGroup->data(0, ZoneRole).toString();
    // Пустая зона = правила уровня сервера (Get-... без -ZoneName).
    setStatusMessage(zone.isEmpty() ? tr("Загрузка правил сервера…")
                                    : tr("Загрузка правил зоны %1…").arg(zone));
    policiesGroup->setData(0, LoadedRole, true);

    m_svc.listPolicies(server, zone,
        [this, policiesGroup, zone](bool ok, const QList<QJsonObject>& items, const QString& error) {
            if (!policiesGroup->treeWidget())
                return;
            if (!ok) {
                showError(tr("Ошибка загрузки правил"), error);
                setStatusMessage(tr("Ошибка загрузки правил"));
                return;
            }
            // Порядок действий задаётся только для правил на зону.
            const bool zoneLevel = !zone.isEmpty();
            m_policyTable->setRowCount(items.size());
            for (int row = 0; row < items.size(); ++row) {
                const QJsonObject& o = items.at(row);
                const DNSPolicy pol = DNSPolicy::fromJsonObject(o);
                const QString name = pol.name;
                const QString action = pol.action;

                auto* orderItem = new QTableWidgetItem(pol.processingOrder < 0
                                                           ? QStringLiteral("—")
                                                           : QString::number(pol.processingOrder));
                orderItem->setData(Qt::UserRole, name);
                orderItem->setData(Qt::UserRole + 1, QJsonValue(o)); // whole object
                orderItem->setData(Qt::UserRole + 2, zone);
                m_policyTable->setItem(row, 0, orderItem);

                auto* statusItem =
                    new QTableWidgetItem(pol.enabled ? tr("🟢 Вкл") : tr("🔴 Выкл"));
                statusItem->setToolTip(tr("Двойной щелчок — включить/выключить"));
                m_policyTable->setItem(row, 1, statusItem);

                m_policyTable->setItem(row, 2, new QTableWidgetItem(name));

                // Цветовое кодирование действия: ALLOW — зелёный,
                // DENY — красный, IGNORE — оранжево-красный.
                auto* actionItem = new QTableWidgetItem(action);
                QColor actionColor(0x2f, 0x6f, 0x44); // ALLOW
                if (action.compare(QLatin1String("DENY"), Qt::CaseInsensitive) == 0)
                    actionColor = QColor(0xb3, 0x26, 0x1e);
                else if (action.compare(QLatin1String("IGNORE"), Qt::CaseInsensitive) == 0)
                    actionColor = QColor(0xc2, 0x53, 0x1b);
                actionItem->setForeground(actionColor);
                QFont actionFont = actionItem->font();
                actionFont.setBold(true);
                actionItem->setFont(actionFont);
                m_policyTable->setItem(row, 3, actionItem);

                m_policyTable->setItem(row, 4, new QTableWidgetItem(pol.criteriaText()));
                m_policyTable->setItem(row, 5, new QTableWidgetItem(pol.resultText()));

                // Управление: ▲ ▼ (приоритет, только для правил на зону),
                // ✏ (правка) — как и в остальных таблицах.
                auto* tools = new QWidget(m_policyTable);
                auto* toolsLayout = new QHBoxLayout(tools);
                toolsLayout->setContentsMargins(2, 2, 2, 2);
                toolsLayout->setSpacing(2);
                auto makeTool = [tools, &toolsLayout](const QString& text,
                                                      const QString& tooltip) {
                    auto* b = new QToolButton(tools);
                    b->setText(text);
                    b->setToolTip(tooltip);
                    b->setAutoRaise(true);
                    toolsLayout->addWidget(b);
                    return b;
                };
                QToolButton* up = nullptr;
                QToolButton* down = nullptr;
                if (zoneLevel) {
                    up = makeTool(QStringLiteral("▲"), tr("Поднять приоритет"));
                    down = makeTool(QStringLiteral("▼"), tr("Опустить приоритет"));
                }
                QToolButton* edit = makeTool(QStringLiteral("✏"), tr("Изменить…"));
                toolsLayout->addStretch();
                if (up && pol.processingOrder > 1)
                    connect(up, &QToolButton::clicked, this, [this, row] {
                        m_policyTable->setCurrentCell(row, 0);
                        movePolicyOrder(-1);
                    });
                else if (up)
                    up->setEnabled(false);
                if (down)
                    connect(down, &QToolButton::clicked, this, [this, row] {
                        m_policyTable->setCurrentCell(row, 0);
                        movePolicyOrder(+1);
                    });
                connect(edit, &QToolButton::clicked, this, [this, row] {
                    m_policyTable->setCurrentCell(row, 0);
                    editPolicy();
                });
                m_policyTable->setCellWidget(row, 6, tools);
            }
            setStatusMessage(tr("Правил: %1").arg(items.size()));
        });
}

void MainWindow::addPolicy()
{
    QTreeWidgetItem* cur = m_tree->currentItem();
    QString zone;
    if (cur) {
        const QString t = cur->data(0, TypeRole).toString();
        if (t == QLatin1String("zone"))
            zone = cur->data(0, ZoneRole).toString();
        else if (t == QLatin1String("zonePolicies"))
            zone = cur->data(0, ZoneRole).toString();
        else if (t == QLatin1String("serverPolicies"))
            zone = QString(); // правило уровня сервера
        else if (t == QLatin1String("scope")) {
            // От ближайшей зоны — правило на зону.
            for (QTreeWidgetItem* p = cur; p; p = p->parent()) {
                if (p->data(0, TypeRole).toString() == QLatin1String("zone")) {
                    zone = p->data(0, ZoneRole).toString();
                    break;
                }
            }
        }
    }
    if (m_activeServer.isEmpty()) {
        QMessageBox::information(this, tr("Политика"),
            tr("Сначала выберите DNS-сервер."));
        return;
    }

    PolicyDialog dlg(&m_svc, m_activeServer, zoneNames(), this);
    if (!zone.isEmpty())
        dlg.setZoneLevel(zone);
    if (dlg.exec() != QDialog::Accepted)
        return;
    const DNSPolicy p = dlg.params();
    Log::info(QStringLiteral("addPolicy: '%1' zone='%2' action=%3")
                  .arg(p.name, p.zoneName, p.action));

    const QString server = m_activeServer;
    const QString cmd = m_svc.policySaveCommand(p);
    journalEdit(tr("Добавить правило «%1»").arg(p.name),
                { m_svc.policiesCacheFile(server, p.zoneName) },
        [this, server, p] { m_svc.cachePolicySave(server, p); },
        [this, server, cmd](Journal::DoneFn done) { m_svc.runPs(server, cmd, done); },
        [this] { reloadPoliciesView(); });
}

void MainWindow::editPolicy()
{
    QTreeWidgetItem* group = m_tree->currentItem();
    const int row = m_policyTable->currentRow();
    if (row < 0 || !isPoliciesItem(group))
        return;
    if (!m_policyTable->item(row, 0))
        return;
    const QString zone = m_policyTable->item(row, 0)->data(Qt::UserRole + 2).toString();
    const QString name = m_policyTable->item(row, 0)->data(Qt::UserRole).toString();
    const QString server = m_activeServer;

    // Fresh copy straight from the server: Get-... -ZoneName -Name. The
    // loaded snapshot it carries is what later picks Set- vs Remove+Add.
    setStatusMessage(tr("Загрузка политики %1…").arg(name));
    m_svc.getPolicy(server, zone, name,
        [this, group, server](bool ok, const DNSPolicy& p, const QString& error) {
            if (!ok) {
                showError(tr("Не удалось загрузить политику"), error);
                setStatusMessage(tr("Ошибка загрузки политики"));
                return;
            }
            if (!group->treeWidget())
                return;
            setStatusMessage(QString());
            Log::info(QStringLiteral("editPolicy: opened '%1' (action=%2, zoneScope='%3')")
                          .arg(p.name, p.action, p.zoneScope));

            PolicyDialog dlg(&m_svc, server, zoneNames(), this);
            dlg.setParams(p);
            if (dlg.exec() != QDialog::Accepted)
                return;
            const DNSPolicy edited = dlg.params();
            Log::info(QStringLiteral("editPolicy: saving '%1' recreate=%2")
                          .arg(edited.name).arg(edited.needsRecreate() ? 1 : 0));

            const QString cmd = m_svc.policySaveCommand(edited);
            // Смена уровня (Сервер <-> Зона) переносит запись между файлами
            // кэша — снимаем оба.
            QStringList files = { m_svc.policiesCacheFile(server, edited.zoneName) };
            const QString oldFile = m_svc.policiesCacheFile(server, p.loaded.zoneName);
            if (!files.contains(oldFile))
                files << oldFile;
            journalEdit(tr("Изменить правило «%1»").arg(edited.name), files,
                [this, server, edited] { m_svc.cachePolicySave(server, edited); },
                [this, server, cmd](Journal::DoneFn done) { m_svc.runPs(server, cmd, done); },
                [this, group] { if (group->treeWidget()) loadPoliciesFor(group); });
        });
}

void MainWindow::movePolicyOrder(int delta)
{
    QTreeWidgetItem* group = m_tree->currentItem();
    const int row = m_policyTable->currentRow();
    if (row < 0 || !isPoliciesItem(group))
        return;
    const QTableWidgetItem* it = m_policyTable->item(row, 0);
    if (!it)
        return;
    const QString name = it->data(Qt::UserRole).toString();
    const QString zone = it->data(Qt::UserRole + 2).toString();
    if (zone.isEmpty())
        return; // порядок действий задаётся только для правил на зону
    const int order = static_cast<int>(it->data(Qt::UserRole + 1).toJsonObject()
                                           .value(QStringLiteral("ProcessingOrder")).toInt(-1));
    const int newOrder = order + delta;
    if (order < 0 || newOrder < 1)
        return;
    Log::info(QStringLiteral("movePolicyOrder: '%1' %2 -> %3").arg(name).arg(order).arg(newOrder));

    const QString server = m_activeServer;
    const QString cmd = m_svc.policySetOrderCommand(name, zone, newOrder);
    journalEdit(tr("Изменить приоритет правила «%1» (%2 → %3)").arg(name).arg(order).arg(newOrder),
                { m_svc.policiesCacheFile(server, zone) },
        [this, server, name, zone, newOrder] {
            m_svc.cachePolicySetOrder(server, name, zone, newOrder);
        },
        [this, server, cmd](Journal::DoneFn done) { m_svc.runPs(server, cmd, done); },
        [this, group] { if (group->treeWidget()) loadPoliciesFor(group); });
}

void MainWindow::togglePolicyEnabled()
{
    QTreeWidgetItem* group = m_tree->currentItem();
    const int row = m_policyTable->currentRow();
    if (row < 0 || !isPoliciesItem(group) || !m_policyTable->item(row, 0))
        return;
    const QJsonObject o = m_policyTable->item(row, 0)->data(Qt::UserRole + 1).toJsonObject();

    // Set- has no -Enable/-Disable: flipping the flag makes needsRecreate()
    // true, so the command removes and re-adds the policy (-Disable when
    // turning it off), keeping criteria, order and otherCriteria intact.
    DNSPolicy p = DNSPolicy::fromJsonObject(o);
    p.enabled = !p.enabled;
    Log::info(QStringLiteral("togglePolicyEnabled: '%1' -> enabled=%2")
                  .arg(p.name).arg(p.enabled ? 1 : 0));

    const QString server = m_activeServer;
    const QString cmd = m_svc.policySaveCommand(p);
    journalEdit(tr("%1 правило «%2»")
                    .arg(p.enabled ? tr("Включить") : tr("Выключить"), p.name),
                { m_svc.policiesCacheFile(server, p.zoneName) },
        [this, server, p] { m_svc.cachePolicySave(server, p); },
        [this, server, cmd](Journal::DoneFn done) { m_svc.runPs(server, cmd, done); },
        [this, group] { if (group->treeWidget()) loadPoliciesFor(group); });
}

void MainWindow::removePolicy()
{
    QTreeWidgetItem* group = m_tree->currentItem();
    const int row = m_policyTable->currentRow();
    if (row < 0 || !isPoliciesItem(group) || !m_policyTable->item(row, 0))
        return;
    const QString zone = m_policyTable->item(row, 0)->data(Qt::UserRole + 2).toString();
    const QString name = m_policyTable->item(row, 0)->data(Qt::UserRole).toString();
    const QString what =
        zone.isEmpty() ? tr("Удалить правило \"%1\" сервера?").arg(name)
                       : tr("Удалить правило \"%1\" зоны %2?").arg(name, zone);
    if (QMessageBox::question(this, tr("Удаление правила"), what) != QMessageBox::Yes)
        return;
    Log::info(QStringLiteral("removePolicy: '%1' zone='%2'").arg(name, zone));

    const QString server = m_activeServer;
    const QString cmd = m_svc.policyRemoveCommand(name, zone);
    journalEdit(tr("Удалить правило «%1»").arg(name),
                { m_svc.policiesCacheFile(server, zone) },
        [this, server, name, zone] { m_svc.cachePolicyRemove(server, name, zone); },
        [this, server, cmd](Journal::DoneFn done) { m_svc.runPs(server, cmd, done); },
        [this, group] { if (group->treeWidget()) loadPoliciesFor(group); });
}

// ------------------------------------------------------------------ Subnets

void MainWindow::loadSubnets()
{
    if (m_activeServer.isEmpty())
        return;
    setStatusMessage(tr("Загрузка подсетей с %1…").arg(m_activeServer));
    m_svc.listSubnets(m_activeServer,
        [this](bool ok, const QList<QJsonObject>& items, const QString& error) {
            m_subnetTable->setRowCount(0);
            if (!ok) {
                showError(tr("Ошибка загрузки подсетей"), error);
                setStatusMessage(tr("Ошибка загрузки подсетей"));
                return;
            }
            m_subnetTable->setRowCount(items.size());
            for (int row = 0; row < items.size(); ++row) {
                const QJsonObject& o = items.at(row);
                const QString name = Json::propStr(o, { "Name" });
                auto* nameItem = new QTableWidgetItem(name);
                nameItem->setData(Qt::UserRole, name);
                m_subnetTable->setItem(row, 0, nameItem);
                // PowerShell returns subnet lists as JSON arrays — join them.
                m_subnetTable->setItem(row, 1, new QTableWidgetItem(
                    Json::propStrList(o, { "IPv4Subnet" }).join(QLatin1String(", "))));
                m_subnetTable->setItem(row, 2, new QTableWidgetItem(
                    Json::propStrList(o, { "IPv6Subnet" }).join(QLatin1String(", "))));
                m_subnetTable->setCellWidget(row, 3,
                    makeRowTools([this, row] {
                        m_subnetTable->setCurrentCell(row, 0);
                        editSubnet();
                    },
                    [this, row] {
                        m_subnetTable->setCurrentCell(row, 0);
                        removeSubnet();
                    },
                    tr("Изменить подсеть…"), tr("Удалить подсеть…")));
            }
            setStatusMessage(tr("Подсетей: %1").arg(items.size()));
        });
}

void MainWindow::addSubnet()
{
    if (m_activeServer.isEmpty())
        return;
    SubnetDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    if (dlg.name().isEmpty()) {
        showError(tr("Подсеть"), tr("Укажите имя подсети."));
        return;
    }

    const QString server = m_activeServer;
    const QString cmd = m_svc.subnetAddCommand(dlg.name(), dlg.ipv4(), dlg.ipv6());
    journalEdit(tr("Добавить подсеть «%1»").arg(dlg.name()),
                { m_svc.subnetsCacheFile(server) },
        [this, server, name = dlg.name(), v4 = dlg.ipv4(), v6 = dlg.ipv6()] {
            m_svc.cacheSubnetAdd(server, name, v4, v6);
        },
        [this, server, cmd](Journal::DoneFn done) { m_svc.runPs(server, cmd, done); },
        [this] { loadSubnets(); });
}

void MainWindow::editSubnet()
{
    const int row = m_subnetTable->currentRow();
    if (row < 0 || m_activeServer.isEmpty())
        return;
    const QString name = m_subnetTable->item(row, 0)->data(Qt::UserRole).toString();
    // Recompose lists from the displayed comma-separated values.
    auto trimAll = [](QStringList l) {
        for (QString& s : l) s = s.trimmed();
        return l;
    };
    const QStringList v4 = trimAll(m_subnetTable->item(row, 1)->text()
        .split(QLatin1String(","), Qt::SkipEmptyParts));
    const QStringList v6 = trimAll(m_subnetTable->item(row, 2)->text()
        .split(QLatin1String(","), Qt::SkipEmptyParts));

    SubnetDialog dlg(this);
    dlg.setName(name);
    dlg.setSubnets(v4, v6);
    if (dlg.exec() != QDialog::Accepted)
        return;

    const QString server = m_activeServer;
    const QString cmd = m_svc.subnetSetCommand(dlg.name(), dlg.ipv4(), dlg.ipv6());
    journalEdit(tr("Изменить подсеть «%1»").arg(dlg.name()),
                { m_svc.subnetsCacheFile(server) },
        [this, server, newName = dlg.name(), newV4 = dlg.ipv4(), newV6 = dlg.ipv6()] {
            m_svc.cacheSubnetSet(server, newName, newV4, newV6);
        },
        [this, server, cmd](Journal::DoneFn done) { m_svc.runPs(server, cmd, done); },
        [this] { loadSubnets(); });
}

void MainWindow::removeSubnet()
{
    const int row = m_subnetTable->currentRow();
    if (row < 0 || m_activeServer.isEmpty())
        return;
    const QString name = m_subnetTable->item(row, 0)->data(Qt::UserRole).toString();
    if (QMessageBox::question(this, tr("Удаление подсети"),
            tr("Удалить подсеть \"%1\"?").arg(name)) != QMessageBox::Yes)
        return;

    const QString server = m_activeServer;
    const QString cmd = m_svc.subnetRemoveCommand(name);
    journalEdit(tr("Удалить подсеть «%1»").arg(name), { m_svc.subnetsCacheFile(server) },
        [this, server, name] { m_svc.cacheSubnetRemove(server, name); },
        [this, server, cmd](Journal::DoneFn done) { m_svc.runPs(server, cmd, done); },
        [this] { loadSubnets(); });
}

// ----------------------------------------------------------- Context menus

void MainWindow::showTreeMenu(const QPoint& pos)
{
    QTreeWidgetItem* item = m_tree->itemAt(pos);
    if (!item)
        return;
    m_tree->setCurrentItem(item);
    const QString type = item->data(0, TypeRole).toString();

    QMenu menu(this);
    if (type == QLatin1String("forwardZones") || type == QLatin1String("reverseZones")
        || type == QLatin1String("forwarderZones")) {
        menu.addAction(tr("Добавить зону…"), this, &MainWindow::addZone);
        menu.addAction(tr("Обновить"), this, &MainWindow::loadZones);
    } else if (type == QLatin1String("zone")) {
        const QTreeWidgetItem* cat = item->parent();
        const bool forwarder =
            cat && cat->data(0, TypeRole).toString() == QLatin1String("forwarderZones");
        if (!forwarder) {
            if (item->data(0, DirectRecordsRole).toBool())
                menu.addAction(tr("Добавить запись…"), this, &MainWindow::addRecordAction);
            else
                menu.addAction(tr("Добавить область…"), this, &MainWindow::addScope);
            menu.addAction(tr("Добавить правило…"), this, &MainWindow::addPolicy);
            menu.addSeparator();
            menu.addAction(tr("Обновить"), this, &MainWindow::refreshCurrent);
        }
        menu.addAction(tr("Свойства зоны…"), this, &MainWindow::editZone);
        menu.addAction(tr("Удалить зону…"), this, &MainWindow::removeZone);
        menu.addAction(tr("Открыть в дереве"), this, &MainWindow::openZoneInTree);
    } else if (type == QLatin1String("scope")) {
        menu.addAction(tr("Свойства области…"), this, &MainWindow::editScope);
        menu.addAction(tr("Добавить запись…"), this, &MainWindow::addRecordAction);
        menu.addAction(tr("Добавить правило на зону…"), this, &MainWindow::addPolicy);
        menu.addSeparator();
        if (!item->data(0, DefaultScopeRole).toBool())
            menu.addAction(tr("Удалить область…"), this, &MainWindow::removeScope);
        menu.addAction(tr("Обновить"), this, &MainWindow::loadRecords);
    } else if (type == QLatin1String("zonePolicies")
               || type == QLatin1String("serverPolicies")) {
        menu.addAction(tr("Добавить правило…"), this, &MainWindow::addPolicy);
        menu.addAction(tr("Обновить"), this, &MainWindow::loadPolicies);
    } else if (type == QLatin1String("subnets")) {
        menu.addAction(tr("Добавить подсеть…"), this, &MainWindow::addSubnet);
        menu.addAction(tr("Обновить"), this, &MainWindow::loadSubnets);
    }
    if (!menu.isEmpty())
        menu.exec(m_tree->viewport()->mapToGlobal(pos));
}

void MainWindow::showZoneTableMenu(const QPoint& pos)
{
    QMenu menu(this);
    menu.addAction(tr("Добавить зону…"), this, &MainWindow::addZone);
    if (m_zoneTable->currentRow() >= 0) {
        menu.addSeparator();
        menu.addAction(tr("Открыть в дереве"), this, &MainWindow::openZoneInTree);
        menu.addAction(tr("Свойства зоны…"), this, &MainWindow::editZone);
        menu.addAction(tr("Удалить зону…"), this, &MainWindow::removeZone);
    }
    menu.addSeparator();
    menu.addAction(tr("Обновить"), this, &MainWindow::loadZones);
    menu.exec(m_zoneTable->viewport()->mapToGlobal(pos));
}

void MainWindow::showSubnetTableMenu(const QPoint& pos)
{
    QMenu menu(this);
    menu.addAction(tr("Добавить подсеть…"), this, &MainWindow::addSubnet);
    if (m_subnetTable->currentRow() >= 0) {
        menu.addSeparator();
        menu.addAction(tr("Изменить подсеть…"), this, &MainWindow::editSubnet);
        menu.addAction(tr("Удалить подсеть…"), this, &MainWindow::removeSubnet);
    }
    menu.addSeparator();
    menu.addAction(tr("Обновить"), this, &MainWindow::loadSubnets);
    menu.exec(m_subnetTable->viewport()->mapToGlobal(pos));
}

void MainWindow::showHostTableMenu(const QPoint& pos)
{
    QTreeWidgetItem* cur = m_tree->currentItem();
    if (!cur)
        return;
    const QString type = cur->data(0, TypeRole).toString();
    if (type != QLatin1String("scope") && type != QLatin1String("zone"))
        return;
    // scopeOfItem вернёт область, а у обратной зоны — саму зону (записи без
    // областей); у зоны прямого просмотра — nullptr, меню неприменимо.
    QTreeWidgetItem* sc = scopeOfItem(cur);
    if (!sc)
        return;
    const QString zone = sc->data(0, ZoneRole).toString();
    const QString scope = sc->data(0, ScopeRole).toString();

    QMenu menu(this);
    menu.addAction(tr("Добавить запись…"), this,
                   [this, zone, scope] { addRecordFor(zone, scope, QString()); });

    // Плоский список записей: строка таблицы = одна запись. Меню действует
    // на запись выбранной строки (правка = удалить старую + добавить новую).
    const int row = m_hostTable->currentRow();
    const QVariantList recs = cur->data(0, RecordsRole).toList();
    if (row >= 0 && row < recs.size()) {
        const QJsonObject r = recs.at(row).toJsonObject();
        const QString rtype = Json::propStr(r, { "RecordType" });
        const QString title = rtype + QLatin1Char(' ') +
            Json::recordDataText(Json::prop(r, { "RecordData" }).toObject(), rtype);
        menu.addSeparator();
        menu.addAction(tr("Изменить %1").arg(title), this,
                       [this, zone, scope, r] { editRecordFor(zone, scope, r); });
        menu.addAction(tr("Удалить %1").arg(title), this,
                       [this, zone, scope, r] { removeRecordFor(zone, scope, r); });
    }
    menu.addSeparator();
    menu.addAction(tr("Обновить"), this, &MainWindow::loadRecords);
    menu.exec(m_hostTable->viewport()->mapToGlobal(pos));
}

void MainWindow::showScopeTableMenu(const QPoint& pos)
{
    QTreeWidgetItem* zoneItem = m_tree->currentItem();
    if (!zoneItem || zoneItem->data(0, TypeRole).toString() != QLatin1String("zone"))
        return;
    QMenu menu(this);
    menu.addAction(tr("Добавить область…"), this, &MainWindow::addScope);
    const int row = m_scopeTable->currentRow();
    if (row >= 0 && m_scopeTable->item(row, 0)
        && !m_scopeTable->item(row, 0)->data(Qt::UserRole + 1).toBool()) {
        menu.addSeparator();
        menu.addAction(tr("Удалить область…"), this, &MainWindow::removeScope);
    }
    menu.addSeparator();
    menu.addAction(tr("Обновить"), this, &MainWindow::loadScopes);
    menu.exec(m_scopeTable->viewport()->mapToGlobal(pos));
}

void MainWindow::showPolicyTableMenu(const QPoint& pos)
{
    QTreeWidgetItem* cur = m_tree->currentItem();
    if (!isPoliciesItem(cur))
        return;
    const bool zoneLevel = !cur->data(0, ZoneRole).toString().isEmpty();
    QMenu menu(this);
    menu.addAction(tr("Добавить политику…"), this, &MainWindow::addPolicy);
    if (m_policyTable->currentRow() >= 0) {
        menu.addSeparator();
        menu.addAction(tr("Изменить политику…"), this, &MainWindow::editPolicy);
        menu.addAction(tr("Включить / выключить"), this, &MainWindow::togglePolicyEnabled);
        if (zoneLevel) {
            menu.addAction(tr("Поднять приоритет"), this, [this] { movePolicyOrder(-1); });
            menu.addAction(tr("Опустить приоритет"), this, [this] { movePolicyOrder(+1); });
        }
        menu.addAction(tr("Удалить политику…"), this, &MainWindow::removePolicy);
    }
    menu.addSeparator();
    menu.addAction(tr("Обновить"), this, &MainWindow::loadPolicies);
    menu.exec(m_policyTable->viewport()->mapToGlobal(pos));
}
