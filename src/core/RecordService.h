#pragma once

#include "DnsEntityService.h"

// Parameters describing a resource record to add.
struct RecordAddParams {
    QString zone;
    QString scope;      // zone scope name; empty for the default scope
    QString type;       // "A", "AAAA", "CNAME", "NS", "MX", "TXT", "PTR", "SRV"
    QString name;       // host label, or "@" for the zone apex
    int ttlSeconds = 3600;

    // Type-specific values (only the relevant one is used).
    QString ipv4;
    QString ipv6;
    QString cnameTarget;
    QString nsServer;
    QString mxServer;
    int mxPreference = 0;
    QString txtText;
    QString ptrTarget;
    QString srvService;
    QString srvProtocol;
    QString srvTarget;
    int srvPort = 0;
    int srvPriority = 0;
    int srvWeight = 100;
};

// CRUD for resource records (Get/Add/Remove-DnsServerResourceRecord cmdlets).
// Команды и оптимистичные правки кэша разделены — их соединяет журнал
// действий (см. Journal/MainWindow).
class RecordService : public DnsEntityService
{
public:
    explicit RecordService(PsClient* client);

    void list(const QString& server, const QString& zoneName, const QString& scope, ListCb cb);
    // Всегда перечитывает список с сервера и обновляет кэш.
    void refreshList(const QString& server, const QString& zoneName, const QString& scope,
                     ListCb cb);
    void getNames(const QString& server, const QString& zoneName, NamesCb cb) override;

    // ----- Команды (без "; 'OK'" — добавляет DnsService::runPs).
    QString addCommand(const RecordAddParams& p) const;
    QString removeCommand(const QString& zoneName, const QString& scope, const QString& rrType,
                          const QString& name, const QString& matchValue) const;

    // ----- Оптимистичное отражение в файловом кэше (без выполнения в PS).
    void cacheAdd(const QString& server, const RecordAddParams& p) const;
    void cacheRemove(const QString& server, const QString& zoneName, const QString& scope,
                     const QString& rrType, const QString& name,
                     const QString& matchValue) const;

private:
    // Поле RecordData, по которому запись опознаётся для удаления.
    static QString matchField(const QString& rrType);
    // Идентичность записи: имя+тип+данные.
    static bool sameRecord(const QJsonObject& item, const QString& rrType, const QString& name,
                           const QString& matchValue);
    // true, если кэш есть, но значения записей из него не читаются.
    static bool recordsUnreadable(const QList<QJsonObject>& items);
};
