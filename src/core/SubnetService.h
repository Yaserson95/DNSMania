#pragma once

#include "DnsEntityService.h"

// CRUD for client subnets (Get/Add/Set/Remove-DnsServerClientSubnet cmdlets).
// Команды и оптимистичные правки кэша разделены — их соединяет журнал
// действий (см. Journal/MainWindow).
class SubnetService : public DnsEntityService
{
public:
    explicit SubnetService(PsClient* client);

    void list(const QString& server, ListCb cb);
    // Всегда перечитывает список с сервера и обновляет кэш.
    void refreshList(const QString& server, ListCb cb);
    void getNames(const QString& server, const QString& zoneName, NamesCb cb) override;

    // ----- Команды (без "; 'OK'" — добавляет DnsService::runPs).
    QString addCommand(const QString& name, const QStringList& ipv4,
                       const QStringList& ipv6) const;
    QString setCommand(const QString& name, const QStringList& ipv4,
                       const QStringList& ipv6) const;
    QString removeCommand(const QString& name) const;

    // ----- Оптимистичное отражение в файловом кэше (без выполнения в PS).
    void cacheAdd(const QString& server, const QString& name, const QStringList& ipv4,
                  const QStringList& ipv6) const;
    void cacheSet(const QString& server, const QString& name, const QStringList& ipv4,
                  const QStringList& ipv6) const;
    void cacheRemove(const QString& server, const QString& name) const;
};
