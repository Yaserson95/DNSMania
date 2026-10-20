#pragma once

#include "DnsEntityService.h"

// CRUD for DNS zones (Get/Add/Remove-DnsServer*Zone cmdlets).
//
// Изменения разделены на две части: команды (addCommand/removeCommand) и
// оптимистичные правки кэша (cacheAdd/cacheRemove). Их соединяет журнал
// действий (см. Journal/MainWindow): кэш меняется сразу, команда — только
// по кнопке «Сохранить».
class ZoneService : public DnsEntityService
{
public:
    explicit ZoneService(PsClient* client);

    // Кэш-первичное чтение: список из файла кэша, при его отсутствии —
    // запрос к серверу с записью результата в кэш.
    void list(const QString& server, ListCb cb);
    // Всегда перечитывает список с сервера и обновляет кэш (обновление
    // при старте приложения / после выполнения журнала).
    void refreshList(const QString& server, ListCb cb);
    void getNames(const QString& server, const QString& zoneName, NamesCb cb) override;

    // ----- Команды (без "; 'OK'" — добавляет DnsService::runPs).
    QString addCommand(const QString& zoneName, const QString& zoneType, bool reverse,
                       const QString& networkId, const QString& replicationScope,
                       const QString& dynamicUpdate, const QStringList& masters) const;
    QString removeCommand(const QString& zoneName) const;
    // Правка свойств существующей зоны:
    // Set-DnsServerPrimaryZone — динамическое обновление и область репликации.
    QString setCommand(const QString& zoneName, const QString& dynamicUpdate,
                       const QString& replicationScope) const;
    // Set-DnsServerSecondaryZone / Set-DnsServerStubZone — мастер-серверы.
    QString setMastersCommand(const QString& zoneName, const QString& zoneType,
                              const QStringList& masters) const;

    // ----- Оптимистичное отражение в файловом кэше (без выполнения в PS).
    void cacheAdd(const QString& server, const QString& zoneName, const QString& zoneType,
                  bool reverse, const QString& replicationScope,
                  const QString& dynamicUpdate) const;
    void cacheRemove(const QString& server, const QString& zoneName) const;
    void cacheSet(const QString& server, const QString& zoneName,
                  const QString& dynamicUpdate, const QString& replicationScope) const;
    void cacheSetMasters(const QString& server, const QString& zoneName,
                         const QStringList& masters) const;
};
