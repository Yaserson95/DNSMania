#include "DnsService.h"

DnsService::DnsService(PsClient* client)
    : m_client(client)
    , m_zones(client)
    , m_scopes(client)
    , m_records(client)
    , m_subnets(client)
    , m_policies(client)
{
}

void DnsService::testConnection(const QString& server,
                                std::function<void(bool, const QString&, const QString&)> cb)
{
    const QString body =
        QStringLiteral("$s=Get-DnsServer @cn; \"OK|$env:COMPUTERNAME\"");

    m_client->submit(server, body, [cb](const PsResult& r) {
        if (!r.ok) {
            cb(false, QString(), r.error);
            return;
        }
        QString info = r.json;
        if (info.startsWith(QLatin1String("\"")) && info.endsWith(QLatin1String("\"")))
            info = info.mid(1, info.size() - 2);
        cb(true, info, QString());
    });
}

void DnsService::setServerMode(const QString& server, bool localPs)
{
    m_client->setLocalMode(server, localPs);
}

void DnsService::setServerCredentials(const QString& server, const QString& user,
                                      const QString& password)
{
    m_client->setCredentials(server, user, password);
}

void DnsService::runPs(const QString& server, const QString& body, OkCb cb)
{
    m_client->submit(server, body + QStringLiteral("; 'OK'"),
                     [cb](const PsResult& r) {
                         cb(r.ok, r.ok ? QString() : r.error);
                     });
}

// ---------------------------------------------------------------------- Zones

void DnsService::listZones(const QString& server, ListCb cb)
{
    m_zones.list(server, cb);
}

QString DnsService::zoneAddCommand(const QString& zoneName, const QString& zoneType, bool reverse,
                                   const QString& networkId, const QString& replicationScope,
                                   const QString& dynamicUpdate,
                                   const QStringList& masters) const
{
    return m_zones.addCommand(zoneName, zoneType, reverse, networkId, replicationScope,
                              dynamicUpdate, masters);
}

QString DnsService::zoneRemoveCommand(const QString& zoneName) const
{
    return m_zones.removeCommand(zoneName);
}

void DnsService::cacheZoneAdd(const QString& server, const QString& zoneName,
                              const QString& zoneType, bool reverse,
                              const QString& replicationScope,
                              const QString& dynamicUpdate) const
{
    m_zones.cacheAdd(server, zoneName, zoneType, reverse, replicationScope, dynamicUpdate);
}

void DnsService::cacheZoneRemove(const QString& server, const QString& zoneName) const
{
    m_zones.cacheRemove(server, zoneName);
}

QString DnsService::zoneSetCommand(const QString& zoneName, const QString& dynamicUpdate,
                                   const QString& replicationScope) const
{
    return m_zones.setCommand(zoneName, dynamicUpdate, replicationScope);
}

QString DnsService::zoneSetMastersCommand(const QString& zoneName, const QString& zoneType,
                                          const QStringList& masters) const
{
    return m_zones.setMastersCommand(zoneName, zoneType, masters);
}

void DnsService::cacheZoneSet(const QString& server, const QString& zoneName,
                              const QString& dynamicUpdate,
                              const QString& replicationScope) const
{
    m_zones.cacheSet(server, zoneName, dynamicUpdate, replicationScope);
}

void DnsService::cacheZoneSetMasters(const QString& server, const QString& zoneName,
                                     const QStringList& masters) const
{
    m_zones.cacheSetMasters(server, zoneName, masters);
}

// --------------------------------------------------------------- Zone scopes

void DnsService::listZoneScopes(const QString& server, const QString& zoneName, ListCb cb)
{
    m_scopes.list(server, zoneName, cb);
}

void DnsService::getZoneScope(const QString& server, const QString& zoneName,
                              const QString& scopeName, ZoneScopeService::ScopeCb cb)
{
    m_scopes.get(server, zoneName, scopeName, cb);
}

QString DnsService::scopeAddCommand(const QString& zoneName, const QString& scopeName) const
{
    return m_scopes.addCommand(zoneName, scopeName);
}

QString DnsService::scopeRemoveCommand(const QString& zoneName, const QString& scopeName) const
{
    return m_scopes.removeCommand(zoneName, scopeName);
}

void DnsService::cacheScopeAdd(const QString& server, const QString& zoneName,
                               const QString& scopeName) const
{
    m_scopes.cacheAdd(server, zoneName, scopeName);
}

void DnsService::cacheScopeRemove(const QString& server, const QString& zoneName,
                                  const QString& scopeName) const
{
    m_scopes.cacheRemove(server, zoneName, scopeName);
}

// ------------------------------------------------------------ Resource records

void DnsService::listRecords(const QString& server, const QString& zoneName,
                             const QString& scope, ListCb cb)
{
    m_records.list(server, zoneName, scope, cb);
}

QString DnsService::recordAddCommand(const RecordAddParams& p) const
{
    return m_records.addCommand(p);
}

QString DnsService::recordRemoveCommand(const QString& zoneName, const QString& scope,
                                        const QString& rrType, const QString& name,
                                        const QString& matchValue) const
{
    return m_records.removeCommand(zoneName, scope, rrType, name, matchValue);
}

void DnsService::cacheRecordAdd(const QString& server, const RecordAddParams& p) const
{
    m_records.cacheAdd(server, p);
}

void DnsService::cacheRecordRemove(const QString& server, const QString& zoneName,
                                   const QString& scope, const QString& rrType,
                                   const QString& name, const QString& matchValue) const
{
    m_records.cacheRemove(server, zoneName, scope, rrType, name, matchValue);
}

// ------------------------------------------------------------- Client subnets

void DnsService::listSubnets(const QString& server, ListCb cb)
{
    m_subnets.list(server, cb);
}

QString DnsService::subnetAddCommand(const QString& name, const QStringList& ipv4,
                                     const QStringList& ipv6) const
{
    return m_subnets.addCommand(name, ipv4, ipv6);
}

QString DnsService::subnetSetCommand(const QString& name, const QStringList& ipv4,
                                     const QStringList& ipv6) const
{
    return m_subnets.setCommand(name, ipv4, ipv6);
}

QString DnsService::subnetRemoveCommand(const QString& name) const
{
    return m_subnets.removeCommand(name);
}

void DnsService::cacheSubnetAdd(const QString& server, const QString& name,
                                const QStringList& ipv4, const QStringList& ipv6) const
{
    m_subnets.cacheAdd(server, name, ipv4, ipv6);
}

void DnsService::cacheSubnetSet(const QString& server, const QString& name,
                                const QStringList& ipv4, const QStringList& ipv6) const
{
    m_subnets.cacheSet(server, name, ipv4, ipv6);
}

void DnsService::cacheSubnetRemove(const QString& server, const QString& name) const
{
    m_subnets.cacheRemove(server, name);
}

// ----------------------------------------------------- Query resolution policies

void DnsService::listPolicies(const QString& server, ListCb cb)
{
    m_policies.list(server, cb);
}

void DnsService::listPolicies(const QString& server, const QString& zoneName, ListCb cb)
{
    m_policies.list(server, zoneName, cb);
}

void DnsService::getPolicy(const QString& server, const QString& zoneName,
                           const QString& name, PolicyService::PolicyCb cb)
{
    m_policies.get(server, zoneName, name, cb);
}

QString DnsService::policySaveCommand(const DNSPolicy& p) const
{
    return m_policies.saveCommand(p);
}

QString DnsService::policyRemoveCommand(const QString& name, const QString& zoneName) const
{
    return m_policies.removeCommand(name, zoneName);
}

QString DnsService::policySetOrderCommand(const QString& name, const QString& zoneName,
                                          int order) const
{
    return m_policies.setOrderCommand(name, zoneName, order);
}

void DnsService::cachePolicySave(const QString& server, const DNSPolicy& p) const
{
    m_policies.cacheSave(server, p);
}

void DnsService::cachePolicyRemove(const QString& server, const QString& name,
                                   const QString& zoneName) const
{
    m_policies.cacheRemove(server, name, zoneName);
}

void DnsService::cachePolicySetOrder(const QString& server, const QString& name,
                                     const QString& zoneName, int order) const
{
    m_policies.cacheSetOrder(server, name, zoneName, order);
}

// -------------------------------------------------------------------- Кэш

void DnsService::refreshZones(const QString& server, ListCb cb)
{
    m_zones.refreshList(server, cb);
}

void DnsService::refreshZoneScopes(const QString& server, const QString& zoneName, ListCb cb)
{
    m_scopes.refreshList(server, zoneName, cb);
}

void DnsService::refreshRecords(const QString& server, const QString& zoneName,
                                const QString& scope, ListCb cb)
{
    m_records.refreshList(server, zoneName, scope, cb);
}

void DnsService::refreshSubnets(const QString& server, ListCb cb)
{
    m_subnets.refreshList(server, cb);
}

void DnsService::refreshPolicies(const QString& server, const QString& zoneName, ListCb cb)
{
    m_policies.refreshList(server, zoneName, cb);
}

QString DnsService::zonesCacheFile(const QString& server) const
{
    return m_zones.cacheFile(server);
}

QString DnsService::subnetsCacheFile(const QString& server) const
{
    return m_subnets.cacheFile(server);
}

QString DnsService::scopesCacheFile(const QString& server, const QString& zoneName) const
{
    return m_scopes.cacheFile(server, zoneName);
}

QString DnsService::recordsCacheFile(const QString& server, const QString& zoneName,
                                     const QString& scope) const
{
    return m_records.cacheFile(server, zoneName, scope);
}

QString DnsService::policiesCacheFile(const QString& server, const QString& zoneName) const
{
    return m_policies.cacheFile(server, zoneName);
}

// ------------------------------------------------------- Имена для форм

void DnsService::getZoneNames(const QString& server, NamesCb cb)
{
    m_zones.getNames(server, QString(), cb);
}

void DnsService::getScopeNames(const QString& server, const QString& zoneName, NamesCb cb)
{
    m_scopes.getNames(server, zoneName, cb);
}

void DnsService::getRecordNames(const QString& server, const QString& zoneName, NamesCb cb)
{
    m_records.getNames(server, zoneName, cb);
}

void DnsService::getSubnetNames(const QString& server, NamesCb cb)
{
    m_subnets.getNames(server, QString(), cb);
}

void DnsService::getPolicyNames(const QString& server, const QString& zoneName, NamesCb cb)
{
    m_policies.getNames(server, zoneName, cb);
}
