#include "PolicyService.h"

#include "JsonUtil.h"

#include <algorithm>

PolicyService::PolicyService(PsClient* client)
    : DnsEntityService(client)
{
    setCachePrefix(QStringLiteral("policies"));
}

void PolicyService::getNames(const QString& server, const QString& zoneName, NamesCb cb)
{
    const QList<QJsonObject> cached = readCache(server, zoneName);
    if (!cached.isEmpty()) {
        cb(true, namesFrom(cached, { QStringLiteral("Name") }), QString());
        return;
    }
    refreshList(server, zoneName, namesListCb({ QStringLiteral("Name") }, cb));
}

QString PolicyService::queryBody(const QString& zoneName, const QString& name)
{
    // Raw CIM serialization of Criteria/Content is lossy at shallow depth and
    // mixes in CimClass/CimInstanceProperties noise the UI cannot use, so
    // project every policy into plain scalars plus plain object arrays the
    // C++ side parses directly: Criteria [{CriteriaType, Criteria}] and
    // Content [{ScopeName, Weight}]. The outer ConvertTo-Json is applied by
    // PsClient (depth 25).
    QString body = QStringLiteral("Get-DnsServerQueryResolutionPolicy");
    if (!zoneName.isEmpty())
        body += QStringLiteral(" -ZoneName ") + PsClient::quote(zoneName);
    if (!name.isEmpty())
        body += QStringLiteral(" -Name ") + PsClient::quote(name);
    body += QStringLiteral(
        " @cn"
        " | ForEach-Object { [pscustomobject]@{"
        "Name = $_.Name;"
        "Action = \"$($_.Action)\";"
        "Condition = \"$($_.Condition)\";"
        "Level = \"$($_.Level)\";"
        "ZoneName = $_.ZoneName;"
        "ProcessingOrder = $_.ProcessingOrder;"
        "Enabled = [bool]$_.IsEnabled;"
        "Criteria = @( @($_.Criteria) | Where-Object { $_ } | Select-Object *"
        " -ExcludeProperty CimClass,CimInstanceProperties,CimSystemProperties,"
        "PSComputerName,RunspaceId,PSShowComputerName);"
        "Content = @( @($_.Content) | Where-Object { $_ } | Select-Object *"
        " -ExcludeProperty CimClass,CimInstanceProperties,CimSystemProperties,"
        "PSComputerName,RunspaceId,PSShowComputerName)"
        "} }");
    return body;
}

void PolicyService::list(const QString& server, ListCb cb)
{
    list(server, QString(), cb);
}

void PolicyService::list(const QString& server, const QString& zoneName, ListCb cb)
{
    // Сначала кэш (файл на сервер+зону); без него — запрос с записью в кэш.
    const QList<QJsonObject> cached = readCache(server, zoneName);
    if (!cached.isEmpty()) {
        cb(true, cached, QString());
        return;
    }
    refreshList(server, zoneName, cb);
}

void PolicyService::refreshList(const QString& server, const QString& zoneName, ListCb cb)
{
    submitList(server, queryBody(zoneName, QString()),
               [this, server, zoneName, cb](bool ok, const QList<QJsonObject>& items,
                                           const QString& error) {
                   if (ok)
                       updateCache(server, items, zoneName);
                   cb(ok, items, error);
               });
}

void PolicyService::get(const QString& server, const QString& zoneName,
                        const QString& name, PolicyCb cb)
{
    // Сначала кэш элемента; если политики в кэше нет — свежий запрос.
    const QList<QJsonObject> cached = readCache(server, zoneName);
    for (const QJsonObject& o : cached) {
        if (Json::propStr(o, { "Name" }).compare(name, Qt::CaseInsensitive) == 0) {
            cb(true, DNSPolicy::fromJsonObject(o), QString());
            return;
        }
    }
    client()->submit(server, queryBody(zoneName, name),
                     [this, server, cb, name](const PsResult& r) {
        if (!r.ok) { cb(false, DNSPolicy(), r.error); return; }
        QString err;
        const auto items = Json::toObjectList(r.json.toUtf8(), &err);
        if (!err.isEmpty()) { cb(false, DNSPolicy(), err); return; }
        if (items.isEmpty()) {
            cb(false, DNSPolicy(),
               QStringLiteral("Политика \"%1\" не найдена на сервере.").arg(name));
            return;
        }
        // -Name accepts wildcards; prefer an exact match.
        const QJsonObject* found = &items.first();
        for (const QJsonObject& o : items) {
            if (Json::propStr(o, { "Name" }).compare(name, Qt::CaseInsensitive) == 0) {
                found = &o;
                break;
            }
        }
        // Ключом кэша служит ZoneName самой политики (запрос мог идти
        // без -ZoneName и вернуть зонную политику).
        updateCacheItem(server, *found, Json::propStr(*found, { "ZoneName" }));
        cb(true, DNSPolicy::fromJsonObject(*found), QString());
    });
}

QString PolicyService::saveCommand(const DNSPolicy& p) const
{
    // Add- для новых, Set- для простых правок, Remove+Add — когда Set- не
    // может выразить изменение (см. DNSPolicy::needsRecreate()).
    return p.command();
}

QString PolicyService::removeCommand(const QString& name, const QString& zoneName) const
{
    QString body = QStringLiteral("Remove-DnsServerQueryResolutionPolicy -Name ") +
                   PsClient::quote(name) + QStringLiteral(" @cn");
    if (!zoneName.trimmed().isEmpty())
        body += QStringLiteral(" -ZoneName ") + PsClient::quote(zoneName.trimmed());
    body += QStringLiteral(" -Force");
    return body;
}

QString PolicyService::setOrderCommand(const QString& name, const QString& zoneName,
                                       int order) const
{
    QString body = QStringLiteral("Set-DnsServerQueryResolutionPolicy -Name ") +
                   PsClient::quote(name) + QStringLiteral(" @cn");
    if (!zoneName.trimmed().isEmpty())
        body += QStringLiteral(" -ZoneName ") + PsClient::quote(zoneName.trimmed());
    body += QStringLiteral(" -ProcessingOrder ") + QString::number(order);
    return body;
}

void PolicyService::cacheSave(const QString& server, const DNSPolicy& p) const
{
    // Смена имени/зоны (level): старая запись могла лежать в другом файле.
    if (p.loaded.valid && p.loaded.name != p.name)
        removeCacheItem(server, p.loaded.name, p.loaded.zoneName);
    if (p.loaded.valid && p.loaded.zoneName != p.zoneName)
        removeCacheItem(server, p.loaded.name, p.loaded.zoneName);
    updateCacheItem(server, p.toJsonObject(), p.zoneName);
}

void PolicyService::cacheRemove(const QString& server, const QString& name,
                                const QString& zoneName) const
{
    removeCacheItem(server, name, zoneName.trimmed());
}

void PolicyService::cacheSetOrder(const QString& server, const QString& name,
                                  const QString& zoneName, int order) const
{
    // При смене приоритета сдвигаются несколько политик сразу, поэтому в
    // кэше перенумеровываем их: сервер пересчитает порядок сам, а список
    // уточнится при следующем обновлении кэша.
    QList<QJsonObject> items = readCache(server, zoneName.trimmed());
    if (items.isEmpty())
        return;
    std::stable_sort(items.begin(), items.end(),
                     [](const QJsonObject& a, const QJsonObject& b) {
                         return Json::propNum(a, { QStringLiteral("ProcessingOrder") }, 1)
                                < Json::propNum(b, { QStringLiteral("ProcessingOrder") }, 1);
                     });
    int from = -1;
    for (int i = 0; i < items.size(); ++i) {
        if (Json::propStr(items.at(i), { QStringLiteral("Name") })
                .compare(name, Qt::CaseInsensitive) == 0) {
            from = i;
            break;
        }
    }
    if (from < 0)
        return;
    QJsonObject item = items.takeAt(from);
    int to = order - 1;
    if (to < 0)
        to = 0;
    if (to > items.size())
        to = items.size();
    items.insert(to, item);
    for (int i = 0; i < items.size(); ++i)
        items[i][QStringLiteral("ProcessingOrder")] = i + 1;
    updateCache(server, items, zoneName.trimmed());
}
