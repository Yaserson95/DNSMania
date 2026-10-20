#pragma once

#include "DnsEntityService.h"
#include "PolicyService.h"
#include "RecordService.h"
#include "SubnetService.h"
#include "ZoneScopeService.h"
#include "ZoneService.h"

// Facade over the per-entity DNS services. It owns one service object per
// entity (zones, zone scopes, records, client subnets, query resolution
// policies) and forwards the UI-facing calls to them, keeping a single flat
// API for MainWindow and the dialogs. Connection testing lives here too as
// it is a server-level concern rather than an entity one.
class DnsService
{
public:
    explicit DnsService(PsClient* client);

    using ListCb = DnsEntityService::ListCb;
    using OkCb = DnsEntityService::OkCb;
    using NamesCb = DnsEntityService::NamesCb;

    // Connection test: verifies WinRM reachability and that the DnsServer
    // module is present on the target.
    void testConnection(const QString& server,
                        std::function<void(bool ok, const QString& info, const QString& error)> cb);

    // Zones
    void listZones(const QString& server, ListCb cb);
    QString zoneAddCommand(const QString& zoneName, const QString& zoneType, bool reverse,
                           const QString& networkId, const QString& replicationScope,
                           const QString& dynamicUpdate, const QStringList& masters) const;
    QString zoneRemoveCommand(const QString& zoneName) const;
    // Правка свойств зоны (Primary: дин. обновление/репликация;
    // Secondary/Stub: мастер-серверы).
    QString zoneSetCommand(const QString& zoneName, const QString& dynamicUpdate,
                           const QString& replicationScope) const;
    QString zoneSetMastersCommand(const QString& zoneName, const QString& zoneType,
                                  const QStringList& masters) const;
    void cacheZoneSet(const QString& server, const QString& zoneName,
                      const QString& dynamicUpdate, const QString& replicationScope) const;
    void cacheZoneSetMasters(const QString& server, const QString& zoneName,
                             const QStringList& masters) const;
    void cacheZoneAdd(const QString& server, const QString& zoneName, const QString& zoneType,
                      bool reverse, const QString& replicationScope,
                      const QString& dynamicUpdate) const;
    void cacheZoneRemove(const QString& server, const QString& zoneName) const;

    // Zone scopes
    void listZoneScopes(const QString& server, const QString& zoneName, ListCb cb);
    // Fetches one scope by name as a DNSZoneScope (details view).
    void getZoneScope(const QString& server, const QString& zoneName,
                      const QString& scopeName, ZoneScopeService::ScopeCb cb);
    QString scopeAddCommand(const QString& zoneName, const QString& scopeName) const;
    QString scopeRemoveCommand(const QString& zoneName, const QString& scopeName) const;
    void cacheScopeAdd(const QString& server, const QString& zoneName,
                       const QString& scopeName) const;
    void cacheScopeRemove(const QString& server, const QString& zoneName,
                          const QString& scopeName) const;

    // Resource records (scope empty = default scope)
    void listRecords(const QString& server, const QString& zoneName, const QString& scope,
                     ListCb cb);
    QString recordAddCommand(const RecordAddParams& p) const;
    QString recordRemoveCommand(const QString& zoneName, const QString& scope,
                                const QString& rrType, const QString& name,
                                const QString& matchValue) const;
    void cacheRecordAdd(const QString& server, const RecordAddParams& p) const;
    void cacheRecordRemove(const QString& server, const QString& zoneName, const QString& scope,
                           const QString& rrType, const QString& name,
                           const QString& matchValue) const;

    // Client subnets
    void listSubnets(const QString& server, ListCb cb);
    QString subnetAddCommand(const QString& name, const QStringList& ipv4,
                             const QStringList& ipv6) const;
    QString subnetSetCommand(const QString& name, const QStringList& ipv4,
                             const QStringList& ipv6) const;
    QString subnetRemoveCommand(const QString& name) const;
    void cacheSubnetAdd(const QString& server, const QString& name, const QStringList& ipv4,
                        const QStringList& ipv6) const;
    void cacheSubnetSet(const QString& server, const QString& name, const QStringList& ipv4,
                        const QStringList& ipv6) const;
    void cacheSubnetRemove(const QString& server, const QString& name) const;

    // Query resolution policies (zoneName empty = all levels)
    void listPolicies(const QString& server, ListCb cb);
    void listPolicies(const QString& server, const QString& zoneName, ListCb cb);
    // Fetches one policy by name as a DNSPolicy (the edit dialog's input).
    void getPolicy(const QString& server, const QString& zoneName,
                   const QString& name, PolicyService::PolicyCb cb);
    // Add- для новых, Set- для простых правок, Remove+Add — когда Set- не
    // может выразить изменение (Action/включённость/имя/уровень/снятый
    // критерий). Переключение статуса = saveCommand() с flipped p.enabled.
    QString policySaveCommand(const DNSPolicy& p) const;
    QString policyRemoveCommand(const QString& name, const QString& zoneName) const;
    QString policySetOrderCommand(const QString& name, const QString& zoneName, int order) const;
    void cachePolicySave(const QString& server, const DNSPolicy& p) const;
    void cachePolicyRemove(const QString& server, const QString& name,
                           const QString& zoneName) const;
    void cachePolicySetOrder(const QString& server, const QString& name,
                             const QString& zoneName, int order) const;

    // Выполняет готовую команду в PowerShell (дописывает "; 'OK'").
    void runPs(const QString& server, const QString& body, OkCb cb);

    // Пути к файлам кэша — нужны журналу действий для снимков «до/после».
    QString zonesCacheFile(const QString& server) const;
    QString subnetsCacheFile(const QString& server) const;
    QString scopesCacheFile(const QString& server, const QString& zoneName) const;
    QString recordsCacheFile(const QString& server, const QString& zoneName,
                             const QString& scope = QString()) const;
    QString policiesCacheFile(const QString& server, const QString& zoneName) const;

    // Режим выполнения для сервера: true — локальный PowerShell с
    // -ComputerName (в обход WinRM; нужен RSAT DNS), false — WinRM.
    void setServerMode(const QString& server, bool localPs);
    // Учётные данные WinRM для сервера (пустой user — текущий пользователь
    // Windows). Пароль хранится в SecureStore (DPAPI), не в реестре.
    void setServerCredentials(const QString& server, const QString& user,
                              const QString& password);

    // Кэш: перечитывание списков с сервера с записью в файлы кэша
    // (обновление кэша при старте приложения).
    void refreshZones(const QString& server, ListCb cb);
    void refreshZoneScopes(const QString& server, const QString& zoneName, ListCb cb);
    void refreshRecords(const QString& server, const QString& zoneName, const QString& scope,
                        ListCb cb);
    void refreshSubnets(const QString& server, ListCb cb);
    void refreshPolicies(const QString& server, const QString& zoneName, ListCb cb);

    // Plain name lists of each entity for the comboboxes on the forms
    // (DnsEntityService::getNames). zoneName is used by the zone-bound
    // entities (scopes, records, policies) and ignored by the others.
    void getZoneNames(const QString& server, NamesCb cb);
    void getScopeNames(const QString& server, const QString& zoneName, NamesCb cb);
    void getRecordNames(const QString& server, const QString& zoneName, NamesCb cb);
    void getSubnetNames(const QString& server, NamesCb cb);
    void getPolicyNames(const QString& server, const QString& zoneName, NamesCb cb);

private:
    PsClient* m_client = nullptr;
    ZoneService m_zones;
    ZoneScopeService m_scopes;
    RecordService m_records;
    SubnetService m_subnets;
    PolicyService m_policies;
};
