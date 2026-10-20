#pragma once

#include "core/DnsService.h"
#include "core/Journal.h"
#include "core/PsClient.h"
#include "core/ServerStore.h"

#include <QMainWindow>
#include <QHash>
#include <QList>
#include <QJsonObject>

class QTreeWidget;
class QTreeWidgetItem;
class QStackedWidget;
class QTableWidget;
class QTableWidgetItem;
class QLabel;
class QAction;
class QPushButton;

// Главный экран для одного выбранного DNS-сервера (выбор — в
// ServerSelectDialog при старте и через «Файл» → «Выбрать сервер…»).
// Дерево слева повторяет dnsmgmt.msc, но с областями, подсетями и правилами:
//
//   Зоны прямого просмотра   <- таблица зон (добавить/изменить/удалить;
//                               основная зона домена защищена от удаления)
//     company.lan            <- таблица областей (по умолчанию защищена)
//       Область по умолчанию <- таблица с узлами (записи области)
//         srv01                узлы с поддоменами
//       L4-servers
//       Правила               <- таблица правил на зону
//     sites.loc
//   Зоны обратного просмотра
//   Зоны пересылки
//   Подсети                  <- таблица подсетей
//   Правила                  <- таблица правил на сервер
//
// Все таблицы ведут себя одинаково: контекстное меню + колонка «Управление»
// (✏/🗑) + двойной щелчок как основное действие. Изменения НЕ выполняются
// сразу: каждое действие попадает в журнал (Journal) и оптимистично
// отражается в файловом кэше, команды уходят в PowerShell пачкой по кнопке
// «Сохранить».
class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(const ServerInfo& server, QWidget* parent = nullptr);

private slots:
    void refreshCurrent();

    // Journal (deferred changes).
    void saveJournal();
    void undoAction();
    void redoAction();
    void clearJournal();

    // Navigation.
    void onTreeCurrentChanged(QTreeWidgetItem* current, QTreeWidgetItem* previous);
    void onTreeItemExpanded(QTreeWidgetItem* item);

    // Context menus (right click).
    void showTreeMenu(const QPoint& pos);
    void showZoneTableMenu(const QPoint& pos);
    void showSubnetTableMenu(const QPoint& pos);
    void showHostTableMenu(const QPoint& pos);
    void showScopeTableMenu(const QPoint& pos);
    void showPolicyTableMenu(const QPoint& pos);

    // Zones.
    void addZone();
    void editZone();
    void removeZone();
    void openZoneInTree();
    void loadZones();

    // Resource records of the current scope (host node table).
    void addRecordAction();
    void editRecordAction();
    void removeRecordAction();
    void loadRecords();

    // Zone scopes of the current zone.
    void addScope();
    void editScope();
    void removeScope();
    void loadScopes();

    // Query resolution policies (zone- and server-level).
    void addPolicy();
    void editPolicy();
    void removePolicy();
    void loadPolicies();
    void togglePolicyEnabled();
    void movePolicyOrder(int delta);

    // Client subnets.
    void addSubnet();
    void editSubnet();
    void removeSubnet();
    void loadSubnets();

private:
    // Layout / navigation helpers.
    void rebuildTree();
    // Обновление файлового кэша при старте и после выполнения журнала:
    // для сервера — зоны и подсети, для каждой зоны — области, записи и
    // политики. Фоново, без модальных окон.
    void refreshCaches();
    void showPage(QWidget* page);
    void updateLocation(QTreeWidgetItem* item);
    void navigateTo(QTreeWidgetItem* item);

    // Tree lookups.
    QTreeWidgetItem* categoryItem(const QString& type) const;
    QTreeWidgetItem* findZoneItem(const QString& zone) const;
    QTreeWidgetItem* findChildByType(QTreeWidgetItem* parent, const QString& type) const;
    QTreeWidgetItem* scopeOfItem(QTreeWidgetItem* item) const;   // ближайшая область
    QTreeWidgetItem* policiesItemOfZone(QTreeWidgetItem* zoneItem) const;
    bool isPoliciesItem(QTreeWidgetItem* item) const;           // зональные или серверные
    void refreshZoneNode(QTreeWidgetItem* zoneItem);
    void reloadRecords();
    void reloadPoliciesView();

    // Async loaders (each fills its content page and/or tree branch).
    void loadScopesFor(QTreeWidgetItem* zoneItem);
    void loadRecordsFor(QTreeWidgetItem* scopeItem);
    void loadPoliciesFor(QTreeWidgetItem* policiesItem);

    // Page fillers.
    void fillZoneTable(const QString& categoryType);
    // Плоский список записей: строка = одна запись (HostName/RecordType/TTL/
    // RecordData) — ровно в том виде, в каком их вернул
    // Get-DnsServerResourceRecord. recs — RecordsRole области/зоны.
    void fillRecordsTable(const QVariantList& recs);
    // Таблица областей строится из уже загруженных дочерних узлов зоны.
    void fillScopeTableFromTree(QTreeWidgetItem* zoneItem);
    // Страница свойств зоны пересылки (правка недоступна).
    void fillZoneProps(QTreeWidgetItem* zoneItem);

    // Ячейка «Управление» таблиц (✏ / 🗑), как в таблице правил.
    QWidget* makeRowTools(const std::function<void()>& onEdit,
                          const std::function<void()>& onDelete, const QString& editTip,
                          const QString& deleteTip);

    // Record helpers (used by the host table and its context menu).
    void addRecordFor(const QString& zone, const QString& scope, const QString& presetName);
    void editRecordFor(const QString& zone, const QString& scope, const QJsonObject& record);
    void removeRecordFor(const QString& zone, const QString& scope, const QJsonObject& record);

    // Journal plumbing: одна запись = снимок файлов кэша + оптимистичная
    // правка кэша + отложенная команда. reload — перечитать затронутый вид.
    void journalEdit(const QString& title, const QStringList& cacheFiles,
                     const std::function<void()>& optimistic,
                     const Journal::ExecFn& exec, const std::function<void()>& reload);
    void executeNext();  // выполнение журнала по одной записи
    void finishSave();   // все записи выполнены
    void abortSave(const QString& error);
    void updateJournalUi();
    void reloadAfterJournal();

    // Names of the zones in the zone table (the policy dialog's zone combo).
    QStringList zoneNames() const;
    // Свойства зоны по имени из последнего загруженного списка.
    QJsonObject zoneInfo(const QString& zoneName) const;

    void setStatusMessage(const QString& text);
    void showError(const QString& title, const QString& error);

    // Core.
    PsClient m_ps;
    DnsService m_svc;
    ServerStore m_store;
    Journal m_journal;

    // Current selection.
    QString m_activeServer;
    QString m_activeDescription;

    // Последний загруженный список зон сервера (распределяется по категориям).
    QList<QJsonObject> m_allZones;

    // Journal execution state.
    int m_saveIndex = 0;
    bool m_saving = false;

    // Widgets.
    QTreeWidget* m_tree = nullptr;
    QStackedWidget* m_stack = nullptr;
    QWidget* m_pageBlank = nullptr;
    QTableWidget* m_zoneTable = nullptr;
    QTableWidget* m_subnetTable = nullptr;
    QTableWidget* m_hostTable = nullptr;
    QTableWidget* m_nodeProps = nullptr;
    QTableWidget* m_scopeTable = nullptr;
    QTableWidget* m_policyTable = nullptr;
    QLabel* m_location = nullptr;
    QLabel* m_statusLabel = nullptr;

    // Journal bar (right pane).
    QPushButton* m_btnSave = nullptr;
    QPushButton* m_btnUndo = nullptr;
    QPushButton* m_btnRedo = nullptr;
    QPushButton* m_btnClear = nullptr;

    // Actions.
    QAction* m_actRefresh = nullptr;
    QAction* m_actSave = nullptr;
    QAction* m_actUndo = nullptr;
    QAction* m_actRedo = nullptr;
    QAction* m_actClearJournal = nullptr;
    QAction* m_actAddZone = nullptr;
    QAction* m_actEditZone = nullptr;
    QAction* m_actAddSubnet = nullptr;
    QAction* m_actAddPolicy = nullptr;
};
