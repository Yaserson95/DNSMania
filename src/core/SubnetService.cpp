#include "SubnetService.h"

#include <QJsonArray>

namespace {

// Оптимистичная запись подсети в кэш: ровно тот вид, в котором её
// отдаёт проекция list (Name + массивы адресов).
QJsonObject subnetItem(const QString& name, const QStringList& ipv4,
                       const QStringList& ipv6)
{
    QJsonObject item;
    item[QStringLiteral("Name")] = name;
    if (!ipv4.isEmpty())
        item[QStringLiteral("IPv4Subnet")] = QJsonArray::fromStringList(ipv4);
    if (!ipv6.isEmpty())
        item[QStringLiteral("IPv6Subnet")] = QJsonArray::fromStringList(ipv6);
    return item;
}

} // namespace

SubnetService::SubnetService(PsClient* client)
    : DnsEntityService(client)
{
    setCachePrefix(QStringLiteral("subnets"));
}

void SubnetService::getNames(const QString& server, const QString&, NamesCb cb)
{
    const QList<QJsonObject> cached = readCache(server);
    if (!cached.isEmpty()) {
        cb(true, namesFrom(cached, { QStringLiteral("Name") }), QString());
        return;
    }
    refreshList(server, namesListCb({ QStringLiteral("Name") }, cb));
}

void SubnetService::list(const QString& server, ListCb cb)
{
    // Сначала кэш; без него — запрос с записью результата в кэш.
    const QList<QJsonObject> cached = readCache(server);
    if (!cached.isEmpty()) {
        cb(true, cached, QString());
        return;
    }
    refreshList(server, cb);
}

void SubnetService::refreshList(const QString& server, ListCb cb)
{
    const QString body = QStringLiteral(
        "Get-DnsServerClientSubnet @cn | Select-Object Name,IPv4Subnet,IPv6Subnet");

    submitList(server, body, [this, server, cb](bool ok, const QList<QJsonObject>& items,
                                                const QString& error) {
        if (ok)
            updateCache(server, items);
        cb(ok, items, error);
    });
}

QString SubnetService::addCommand(const QString& name, const QStringList& ipv4,
                                  const QStringList& ipv6) const
{
    QString body = QStringLiteral("Add-DnsServerClientSubnet -Name ") + PsClient::quote(name) +
                   QStringLiteral(" @cn");
    if (!ipv4.isEmpty())
        body += QStringLiteral(" -IPv4Subnet ") + psArray(ipv4);
    if (!ipv6.isEmpty())
        body += QStringLiteral(" -IPv6Subnet ") + psArray(ipv6);
    return body;
}

QString SubnetService::setCommand(const QString& name, const QStringList& ipv4,
                                  const QStringList& ipv6) const
{
    QString body = QStringLiteral("Set-DnsServerClientSubnet -Name ") + PsClient::quote(name) +
                   QStringLiteral(" @cn");
    if (!ipv4.isEmpty())
        body += QStringLiteral(" -IPv4Subnet ") + psArray(ipv4);
    else
        body += QStringLiteral(" -IPv4Subnet @()");
    if (!ipv6.isEmpty())
        body += QStringLiteral(" -IPv6Subnet ") + psArray(ipv6);
    else
        body += QStringLiteral(" -IPv6Subnet @()");
    return body;
}

QString SubnetService::removeCommand(const QString& name) const
{
    return QStringLiteral("Remove-DnsServerClientSubnet -Name ") + PsClient::quote(name) +
           QStringLiteral(" @cn");
}

void SubnetService::cacheAdd(const QString& server, const QString& name,
                             const QStringList& ipv4, const QStringList& ipv6) const
{
    updateCacheItem(server, subnetItem(name, ipv4, ipv6));
}

void SubnetService::cacheSet(const QString& server, const QString& name,
                             const QStringList& ipv4, const QStringList& ipv6) const
{
    updateCacheItem(server, subnetItem(name, ipv4, ipv6));
}

void SubnetService::cacheRemove(const QString& server, const QString& name) const
{
    removeCacheItem(server, name);
}
