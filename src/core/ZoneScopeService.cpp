#include "ZoneScopeService.h"

#include "JsonUtil.h"

ZoneScopeService::ZoneScopeService(PsClient* client)
    : DnsEntityService(client)
{
    setCachePrefix(QStringLiteral("scopes"));
}

QString ZoneScopeService::queryBody(const QString& zoneName, const QString& scopeName)
{
    // DnsZoneScope exposes only FileName/ZoneName/ZoneScope (there is no
    // Name property), so project the scope name explicitly as Name; the raw
    // CIM dump is also full of CimClass/CimInstanceProperties noise. PsClient
    // wraps the body in ConvertTo-Json -Depth 25.
    QString body =
        QStringLiteral("Get-DnsServerZoneScope -ZoneName ") + PsClient::quote(zoneName) +
        QStringLiteral(" @cn");
    if (!scopeName.isEmpty())
        body += QStringLiteral(" -Name ") + PsClient::quote(scopeName);
    body += QStringLiteral(
        " | ForEach-Object { [pscustomobject]@{"
        "Name = $_.ZoneScope;"
        "ZoneName = $_.ZoneName;"
        "FileName = \"$($_.FileName)\""
        "} }");
    return body;
}

void ZoneScopeService::list(const QString& server, const QString& zoneName, ListCb cb)
{
    // Сначала кэш; без него — запрос с записью результата в кэш.
    const QList<QJsonObject> cached = readCache(server, zoneName);
    if (!cached.isEmpty()) {
        cb(true, cached, QString());
        return;
    }
    refreshList(server, zoneName, cb);
}

void ZoneScopeService::refreshList(const QString& server, const QString& zoneName, ListCb cb)
{
    submitList(server, queryBody(zoneName, QString()),
               [this, server, zoneName, cb](bool ok, const QList<QJsonObject>& items,
                                           const QString& error) {
                   if (ok)
                       updateCache(server, items, zoneName);
                   cb(ok, items, error);
               });
}

void ZoneScopeService::get(const QString& server, const QString& zoneName,
                           const QString& scopeName, ScopeCb cb)
{
    // Сначала кэш элемента (список обновляется при старте и при изменениях).
    const QList<QJsonObject> cached = readCache(server, zoneName);
    for (const QJsonObject& o : cached) {
        if (Json::propStr(o, { QStringLiteral("Name") })
                .compare(scopeName, Qt::CaseInsensitive) == 0) {
            cb(true, DNSZoneScope::fromJsonObject(o), QString());
            return;
        }
    }
    client()->submit(server, queryBody(zoneName, scopeName),
                     [this, server, zoneName, cb, scopeName](const PsResult& r) {
        const auto notFound = [cb, scopeName]() {
            cb(false, DNSZoneScope(),
               QStringLiteral("Область \"%1\" не найдена.").arg(scopeName));
        };
        if (!r.ok) { cb(false, DNSZoneScope(), r.error); return; }
        QString err;
        const auto items = Json::toObjectList(r.json.toUtf8(), &err);
        if (!err.isEmpty()) { cb(false, DNSZoneScope(), err); return; }
        if (items.isEmpty()) { notFound(); return; }
        // -Name may accept wildcards; prefer an exact case-insensitive match.
        const QJsonObject* found = nullptr;
        for (const QJsonObject& o : items) {
            if (Json::propStr(o, { QStringLiteral("Name") })
                    .compare(scopeName, Qt::CaseInsensitive) == 0) {
                found = &o;
                break;
            }
        }
        if (!found)
            found = &items.first();
        // Get- returning $null projects into a nameless object - real
        // scopes always have a name, so treat that as not found.
        if (Json::propStr(*found, { QStringLiteral("Name") }).isEmpty()) {
            notFound();
            return;
        }
        updateCacheItem(server, *found, zoneName);
        cb(true, DNSZoneScope::fromJsonObject(*found), QString());
    });
}

void ZoneScopeService::getNames(const QString& server, const QString& zoneName, NamesCb cb)
{
    const QList<QJsonObject> cached = readCache(server, zoneName);
    if (!cached.isEmpty()) {
        cb(true, namesFrom(cached, { QStringLiteral("Name") }), QString());
        return;
    }
    refreshList(server, zoneName, namesListCb({ QStringLiteral("Name") }, cb));
}

QString ZoneScopeService::addCommand(const QString& zoneName, const QString& scopeName) const
{
    return QStringLiteral("Add-DnsServerZoneScope -ZoneName ") + PsClient::quote(zoneName) +
           QStringLiteral(" -Name ") + PsClient::quote(scopeName) + QStringLiteral(" @cn");
}

QString ZoneScopeService::removeCommand(const QString& zoneName, const QString& scopeName) const
{
    return QStringLiteral("Remove-DnsServerZoneScope -ZoneName ") + PsClient::quote(zoneName) +
           QStringLiteral(" -Name ") + PsClient::quote(scopeName) + QStringLiteral(" @cn -Force");
}

void ZoneScopeService::cacheAdd(const QString& server, const QString& zoneName,
                                const QString& scopeName) const
{
    QJsonObject item;
    item[QStringLiteral("Name")] = scopeName;
    item[QStringLiteral("ZoneName")] = zoneName;
    item[QStringLiteral("FileName")] = QString();
    updateCacheItem(server, item, zoneName);
}

void ZoneScopeService::cacheRemove(const QString& server, const QString& zoneName,
                                   const QString& scopeName) const
{
    removeCacheItem(server, scopeName, zoneName);
}
