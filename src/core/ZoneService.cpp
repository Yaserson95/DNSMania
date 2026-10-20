#include "ZoneService.h"

#include "JsonUtil.h"

#include <QJsonArray>

ZoneService::ZoneService(PsClient* client)
    : DnsEntityService(client)
{
    setCachePrefix(QStringLiteral("zones"));
}

void ZoneService::getNames(const QString& server, const QString&, NamesCb cb)
{
    // Zone names come back as ZoneName (DnsServerZone has no Name property).
    const QList<QJsonObject> cached = readCache(server);
    if (!cached.isEmpty()) {
        cb(true, namesFrom(cached, { QStringLiteral("ZoneName") }), QString());
        return;
    }
    refreshList(server, namesListCb({ QStringLiteral("ZoneName") }, cb));
}

void ZoneService::list(const QString& server, ListCb cb)
{
    // Сначала кэш: без него — запрос к серверу с записью результата.
    const QList<QJsonObject> cached = readCache(server);
    if (!cached.isEmpty()) {
        cb(true, cached, QString());
        return;
    }
    refreshList(server, cb);
}

void ZoneService::refreshList(const QString& server, ListCb cb)
{
    const QString body = QStringLiteral(
        "Get-DnsServerZone @cn | Select-Object ZoneName,"
        "@{n='ZoneType';e={\"$($_.ZoneType)\"}},"
        "IsDsIntegrated,IsReverseLookupZone,IsReadOnly,IsPaused,IsShutdown,"
        "@{n='DynamicUpdate';e={\"$($_.DynamicUpdate)\"}},"
        "@{n='ReplicationScope';e={\"$($_.ReplicationScope)\"}},"
        "MasterServers,ZoneFile,IsSigned");

    submitList(server, body, [this, server, cb](bool ok, const QList<QJsonObject>& items,
                                                const QString& error) {
        if (ok)
            updateCache(server, items);
        cb(ok, items, error);
    });
}

QString ZoneService::addCommand(const QString& zoneName, const QString& zoneType, bool reverse,
                                const QString& networkId, const QString& replicationScope,
                                const QString& dynamicUpdate, const QStringList& masters) const
{
    // Command name and identifying parameter.
    QString cmd;
    QString ident;
    if (reverse) {
        cmd = QStringLiteral("Add-DnsServerPrimaryZone");
        ident = QStringLiteral(" -NetworkId ") + PsClient::quote(networkId.trimmed());
    } else {
        ident = QStringLiteral(" -Name ") + PsClient::quote(zoneName.trimmed());
        if (zoneType == QLatin1String("Secondary"))
            cmd = QStringLiteral("Add-DnsServerSecondaryZone");
        else if (zoneType == QLatin1String("Stub"))
            cmd = QStringLiteral("Add-DnsServerStubZone");
        else
            cmd = QStringLiteral("Add-DnsServerPrimaryZone");
    }

    QString body = cmd + ident + QStringLiteral(" @cn");

    // Masters for secondary/stub.
    if (!masters.isEmpty() && zoneType != QLatin1String("Primary")) {
        QStringList m;
        for (const QString& x : masters) {
            if (!x.trimmed().isEmpty())
                m << x.trimmed();
        }
        if (!m.isEmpty())
            body += QStringLiteral(" -MasterServers ") + psArray(m);
    }

    // AD replication scope vs. file-backed zone.
    if (replicationScope != QLatin1String("None")) {
        body += QStringLiteral(" -ReplicationScope ") + PsClient::quote(replicationScope);
    } else {
        QString base = reverse ? networkId : zoneName;
        base.replace(QLatin1Char('/'), QLatin1Char('_'));
        base.replace(QLatin1Char(' '), QLatin1Char('_'));
        body += QStringLiteral(" -ZoneFile ") + PsClient::quote(base + QStringLiteral(".dns"));
    }

    // Dynamic update (primary only).
    if (zoneType == QLatin1String("Primary") && dynamicUpdate != QLatin1String("None")) {
        body += QStringLiteral(" -DynamicUpdate ") + PsClient::quote(dynamicUpdate);
    }

    return body;
}

QString ZoneService::removeCommand(const QString& zoneName) const
{
    return QStringLiteral("Remove-DnsServerZone -Name ") + PsClient::quote(zoneName) +
           QStringLiteral(" @cn -Force");
}

QString ZoneService::setCommand(const QString& zoneName, const QString& dynamicUpdate,
                                const QString& replicationScope) const
{
    QString body = QStringLiteral("Set-DnsServerPrimaryZone -Name ") +
                   PsClient::quote(zoneName) + QStringLiteral(" @cn");
    if (!dynamicUpdate.isEmpty())
        body += QStringLiteral(" -DynamicUpdate ") + PsClient::quote(dynamicUpdate);
    if (!replicationScope.isEmpty() && replicationScope != QLatin1String("None"))
        body += QStringLiteral(" -ReplicationScope ") + PsClient::quote(replicationScope);
    return body;
}

QString ZoneService::setMastersCommand(const QString& zoneName, const QString& zoneType,
                                       const QStringList& masters) const
{
    const QString cmd = zoneType == QLatin1String("Stub")
                            ? QStringLiteral("Set-DnsServerStubZone")
                            : QStringLiteral("Set-DnsServerSecondaryZone");
    QStringList m;
    for (const QString& x : masters) {
        if (!x.trimmed().isEmpty())
            m << x.trimmed();
    }
    return cmd + QStringLiteral(" -Name ") + PsClient::quote(zoneName) +
           QStringLiteral(" @cn -MasterServers ") + psArray(m);
}

// Находит элемент зоны по имени в кэше (nullptr, если файла нет).
static const QJsonObject* findZoneInCache(const QList<QJsonObject>& cache, const QString& zoneName)
{
    for (const QJsonObject& item : cache) {
        if (Json::propStr(item, { QStringLiteral("ZoneName") })
                .compare(zoneName, Qt::CaseInsensitive) == 0)
            return &item;
    }
    return nullptr;
}

void ZoneService::cacheSet(const QString& server, const QString& zoneName,
                           const QString& dynamicUpdate, const QString& replicationScope) const
{
    const QList<QJsonObject> cache = readCache(server);
    const QJsonObject* found = findZoneInCache(cache, zoneName);
    if (!found)
        return;
    QJsonObject upd = *found;
    if (!dynamicUpdate.isEmpty())
        upd[QStringLiteral("DynamicUpdate")] = dynamicUpdate;
    if (!replicationScope.isEmpty() && replicationScope != QLatin1String("None")) {
        upd[QStringLiteral("ReplicationScope")] = replicationScope;
        upd[QStringLiteral("IsDsIntegrated")] = true;
    }
    updateCacheItem(server, upd);
}

void ZoneService::cacheSetMasters(const QString& server, const QString& zoneName,
                                  const QStringList& masters) const
{
    const QList<QJsonObject> cache = readCache(server);
    const QJsonObject* found = findZoneInCache(cache, zoneName);
    if (!found)
        return;
    QJsonObject upd = *found;
    QJsonArray arr;
    for (const QString& m : masters) {
        if (!m.trimmed().isEmpty())
            arr.append(m.trimmed());
    }
    upd[QStringLiteral("MasterServers")] = arr;
    updateCacheItem(server, upd);
}

void ZoneService::cacheAdd(const QString& server, const QString& zoneName,
                           const QString& zoneType, bool reverse,
                           const QString& replicationScope,
                           const QString& dynamicUpdate) const
{
    // Имя зоны неизвестно на клиенте для обратной зоны по NetworkId —
    // оптимистичная запись невозможна (она появится после обновления кэша).
    const QString cacheName = zoneName.trimmed();
    if (cacheName.isEmpty())
        return;

    QJsonObject item;
    item[QStringLiteral("ZoneName")] = cacheName;
    item[QStringLiteral("ZoneType")] = zoneType;
    item[QStringLiteral("IsReverseLookupZone")] = reverse;
    item[QStringLiteral("IsDsIntegrated")] = replicationScope != QLatin1String("None");
    item[QStringLiteral("IsPaused")] = false;
    item[QStringLiteral("IsShutdown")] = false;
    item[QStringLiteral("IsReadOnly")] = false;
    item[QStringLiteral("DynamicUpdate")] = dynamicUpdate;
    item[QStringLiteral("ReplicationScope")] = replicationScope;
    updateCacheItem(server, item);
}

void ZoneService::cacheRemove(const QString& server, const QString& zoneName) const
{
    removeCacheItem(server, zoneName);
}
