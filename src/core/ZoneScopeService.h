#pragma once

#include "DnsEntityService.h"
#include "DnsZoneScope.h"

// CRUD for zone scopes (Get/Add/Remove-DnsServerZoneScope cmdlets).
// A scope always belongs to a zone, so every method takes the zone name.
//
// Note: DnsZoneScope has no Name property - the scope name is its ZoneScope
// property - so the list/get projections normalize it to Name.
class ZoneScopeService : public DnsEntityService
{
public:
    using ScopeCb =
        std::function<void(bool ok, const DNSZoneScope& scope, const QString& error)>;

    explicit ZoneScopeService(PsClient* client);

    void list(const QString& server, const QString& zoneName, ListCb cb);
    // Всегда перечитывает список с сервера и обновляет кэш.
    void refreshList(const QString& server, const QString& zoneName, ListCb cb);
    // Fetches one scope by name (Get-DnsServerZoneScope -ZoneName -Name).
    void get(const QString& server, const QString& zoneName, const QString& scopeName,
             ScopeCb cb);
    void getNames(const QString& server, const QString& zoneName, NamesCb cb) override;

    // ----- Команды (без "; 'OK'" — добавляет DnsService::runPs).
    QString addCommand(const QString& zoneName, const QString& scopeName) const;
    QString removeCommand(const QString& zoneName, const QString& scopeName) const;

    // ----- Оптимистичное отражение в файловом кэше (без выполнения в PS).
    void cacheAdd(const QString& server, const QString& zoneName,
                  const QString& scopeName) const;
    void cacheRemove(const QString& server, const QString& zoneName,
                     const QString& scopeName) const;

private:
    // Get-... -ZoneName [-Name] followed by the projection that renames
    // ZoneScope to Name so the UI can use the same "Name" key everywhere.
    static QString queryBody(const QString& zoneName, const QString& scopeName);
};
