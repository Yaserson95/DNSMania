#include "RecordService.h"

#include "JsonUtil.h"

#include <QJsonArray>

// " -ZoneScope 'x'" when scope is non-empty, otherwise empty.
static QString scopeClause(const QString& scope)
{
    if (scope.trimmed().isEmpty())
        return QString();
    return QStringLiteral(" -ZoneScope ") + PsClient::quote(scope.trimmed());
}

RecordService::RecordService(PsClient* client)
    : DnsEntityService(client)
{
    setCachePrefix(QStringLiteral("records"));
}

void RecordService::getNames(const QString& server, const QString& zoneName, NamesCb cb)
{
    // Host labels of the zone's records (default scope).
    const QList<QJsonObject> cached = readCache(server, zoneName);
    if (!cached.isEmpty()) {
        cb(true, namesFrom(cached, { QStringLiteral("HostName") }), QString());
        return;
    }
    refreshList(server, zoneName, QString(),
                namesListCb({ QStringLiteral("HostName") }, cb));
}

void RecordService::list(const QString& server, const QString& zoneName,
                         const QString& scope, ListCb cb)
{
    // Сначала кэш (файл на каждую зону/область); без него — запрос.
    const QList<QJsonObject> cached = readCache(server, zoneName, scope);
    if (!cached.isEmpty() && !recordsUnreadable(cached)) {
        cb(true, cached, QString());
        return;
    }
    refreshList(server, zoneName, scope, cb);
}

// Кэш непригоден, если в нём есть записи известных типов, но ни одно
// значение не читается (так выглядит файл, записанный старой сборкой с
// пустым проецированным RecordData) — тогда список берётся с сервера.
bool RecordService::recordsUnreadable(const QList<QJsonObject>& items)
{
    static const QStringList kKnown = {
        QStringLiteral("A"),    QStringLiteral("AAAA"), QStringLiteral("CNAME"),
        QStringLiteral("NS"),   QStringLiteral("MX"),   QStringLiteral("TXT"),
        QStringLiteral("PTR"),  QStringLiteral("SRV")
    };
    bool hasKnown = false;
    for (const QJsonObject& r : items) {
        const QString t = Json::propStr(r, { "RecordType" }).toUpper();
        if (!kKnown.contains(t))
            continue;
        hasKnown = true;
        const QString data =
            Json::recordDataText(Json::prop(r, { "RecordData" }).toObject(), t);
        if (!data.trimmed().isEmpty())
            return false;
    }
    return hasKnown;
}

void RecordService::refreshList(const QString& server, const QString& zoneName,
                                const QString& scope, ListCb cb)
{
    // RecordData остаётся сырым CimInstance — ConvertTo-Json сериализует его
    // как {CimClass, CimInstanceProperties: 'IPv4Address = "10.0.0.3"', ...}.
    // Значения разбираются из CimInstanceProperties (Json::recordDataStr),
    // поэтому здесь ничего не проецируем — форма совпадает с примером
    // records_example.json (Get-DnsServerResourceRecord | ConvertTo-Json).
    const QString body = QStringLiteral(
        "Get-DnsServerResourceRecord -ZoneName ") + PsClient::quote(zoneName) +
        QStringLiteral(" @cn") + scopeClause(scope) +
        QStringLiteral(" | Select-Object HostName,"
                       "@{n='RecordType';e={\"$($_.RecordType)\"}},"
                       "@{n='TTLSeconds';e={$_.TimeToLive.TotalSeconds}},"
                       "@{n='Timestamp';e={ if($_.Timestamp){$_.Timestamp.ToString('s')}else{''} }},"
                       "RecordData");

    submitList(server, body, [this, server, zoneName, scope, cb](bool ok,
                                                                 const QList<QJsonObject>& items,
                                                                 const QString& error) {
        if (ok)
            updateCache(server, items, zoneName, scope);
        cb(ok, items, error);
    });
}

QString RecordService::addCommand(const RecordAddParams& p) const
{
    QString body = QStringLiteral("Add-DnsServerResourceRecord -ZoneName ") +
                   PsClient::quote(p.zone) + QStringLiteral(" @cn") + scopeClause(p.scope);

    const QString name = p.name.trimmed().isEmpty() ? QStringLiteral("@") : p.name.trimmed();
    body += QStringLiteral(" -Name ") + PsClient::quote(name);
    body += QStringLiteral(" -TimeToLive (New-TimeSpan -Seconds ") +
            QString::number(p.ttlSeconds) + QLatin1Char(')');

    const QString t = p.type.toUpper();
    if (t == QLatin1String("A")) {
        body += QStringLiteral(" -A -IPv4Address ") + PsClient::quote(p.ipv4);
    } else if (t == QLatin1String("AAAA")) {
        body += QStringLiteral(" -AAAA -IPv6Address ") + PsClient::quote(p.ipv6);
    } else if (t == QLatin1String("CNAME")) {
        body += QStringLiteral(" -CNAME -HostNameAlias ") + PsClient::quote(p.cnameTarget);
    } else if (t == QLatin1String("NS")) {
        body += QStringLiteral(" -NS -NameServer ") + PsClient::quote(p.nsServer);
    } else if (t == QLatin1String("MX")) {
        body += QStringLiteral(" -MX -MailExchange ") + PsClient::quote(p.mxServer) +
                QStringLiteral(" -Preference ") + QString::number(p.mxPreference);
    } else if (t == QLatin1String("TXT")) {
        body += QStringLiteral(" -TXT -DescriptiveText ") + PsClient::quote(p.txtText);
    } else if (t == QLatin1String("PTR")) {
        body += QStringLiteral(" -PTR -PtrDomainName ") + PsClient::quote(p.ptrTarget);
    } else if (t == QLatin1String("SRV")) {
        body += QStringLiteral(" -SRV -Service ") + PsClient::quote(p.srvService) +
                QStringLiteral(" -Protocol ") + PsClient::quote(p.srvProtocol) +
                QStringLiteral(" -DomainName ") + PsClient::quote(p.srvTarget) +
                QStringLiteral(" -Port ") + QString::number(p.srvPort) +
                QStringLiteral(" -Priority ") + QString::number(p.srvPriority) +
                QStringLiteral(" -Weight ") + QString::number(p.srvWeight);
    }

    return body;
}

QString RecordService::matchField(const QString& rrType)
{
    const QString t = rrType.toUpper();
    if (t == QLatin1String("A")) return QStringLiteral("IPv4Address");
    if (t == QLatin1String("AAAA")) return QStringLiteral("IPv6Address");
    if (t == QLatin1String("CNAME")) return QStringLiteral("HostNameAlias");
    if (t == QLatin1String("NS")) return QStringLiteral("NameServer");
    if (t == QLatin1String("MX")) return QStringLiteral("MailExchange");
    if (t == QLatin1String("PTR")) return QStringLiteral("PtrDomainName");
    if (t == QLatin1String("SRV")) return QStringLiteral("DomainName");
    return QString();
}

QString RecordService::removeCommand(const QString& zoneName, const QString& scope,
                                     const QString& rrType, const QString& name,
                                     const QString& matchValue) const
{
    const QString field = matchField(rrType);
    const QString t = rrType.toUpper();
    const QString sc = scopeClause(scope);
    QString body = QStringLiteral("$recs=@(Get-DnsServerResourceRecord -ZoneName ") +
                   PsClient::quote(zoneName) + QStringLiteral(" @cn") + sc +
                   QStringLiteral(" -Name ") + PsClient::quote(name) +
                   QStringLiteral(" -RRType ") + PsClient::quote(t) +
                   QStringLiteral(" -ErrorAction Stop");

    // Filter to the specific record by its data value (skip for TXT / empty).
    if (!field.isEmpty() && !matchValue.trimmed().isEmpty()) {
        body += QStringLiteral(" | Where-Object { $_.RecordData.") + field +
                QStringLiteral(" -eq ") + PsClient::quote(matchValue.trimmed()) + QLatin1Char('}');
    }
    body += QStringLiteral("); $recs | Remove-DnsServerResourceRecord -ZoneName ") +
            PsClient::quote(zoneName) + QStringLiteral(" @cn") + sc +
            QStringLiteral(" -Force -ErrorAction Stop");

    return body;
}

// RecordData записанного вида (плоский объект) — такая форма пишется в кэш
// при оптимистичном добавлении; Json::recordDataStr читает и её, и сырой
// CimInstance, пришедший с сервера.
static QJsonObject recordDataItem(const RecordAddParams& p)
{
    QJsonObject rd;
    const QString t = p.type.toUpper();
    if (t == QLatin1String("A")) {
        rd[QStringLiteral("IPv4Address")] = p.ipv4;
    } else if (t == QLatin1String("AAAA")) {
        rd[QStringLiteral("IPv6Address")] = p.ipv6;
    } else if (t == QLatin1String("CNAME")) {
        rd[QStringLiteral("HostNameAlias")] = p.cnameTarget;
    } else if (t == QLatin1String("NS")) {
        rd[QStringLiteral("NameServer")] = p.nsServer;
    } else if (t == QLatin1String("MX")) {
        rd[QStringLiteral("MailExchange")] = p.mxServer;
        rd[QStringLiteral("Preference")] = p.mxPreference;
    } else if (t == QLatin1String("TXT")) {
        rd[QStringLiteral("DescriptiveText")] = p.txtText;
    } else if (t == QLatin1String("PTR")) {
        rd[QStringLiteral("PtrDomainName")] = p.ptrTarget;
    } else if (t == QLatin1String("SRV")) {
        rd[QStringLiteral("Service")] = p.srvService;
        rd[QStringLiteral("Protocol")] = p.srvProtocol;
        rd[QStringLiteral("DomainName")] = p.srvTarget;
        rd[QStringLiteral("Port")] = p.srvPort;
        rd[QStringLiteral("Priority")] = p.srvPriority;
        rd[QStringLiteral("Weight")] = p.srvWeight;
    }
    return rd;
}

bool RecordService::sameRecord(const QJsonObject& item, const QString& rrType,
                               const QString& name, const QString& matchValue)
{
    if (Json::propStr(item, { QStringLiteral("HostName") }).compare(name, Qt::CaseInsensitive) != 0)
        return false;
    const QString t = rrType.toUpper();
    if (Json::propStr(item, { QStringLiteral("RecordType") }).compare(t, Qt::CaseInsensitive) != 0)
        return false;
    const QString field = matchField(t);
    if (field.isEmpty() || matchValue.trimmed().isEmpty())
        return true; // TXT и прочие: поле не опознаётся — хватает имени+типа
    const QJsonObject rd = Json::prop(item, { QStringLiteral("RecordData") }).toObject();
    // recordDataStr читает и плоский RecordData (оптимистические записи),
    // и сырой CimInstance из кэша (CimInstanceProperties).
    return Json::recordDataStr(rd, field).compare(matchValue.trimmed(), Qt::CaseInsensitive) == 0;
}

void RecordService::cacheAdd(const QString& server, const RecordAddParams& p) const
{
    QList<QJsonObject> items = readCache(server, p.zone, p.scope);
    if (items.isEmpty()) // кэша нет — частичный список не создаём
        return;

    QJsonObject item;
    item[QStringLiteral("HostName")] =
        p.name.trimmed().isEmpty() ? QStringLiteral("@") : p.name.trimmed();
    item[QStringLiteral("RecordType")] = p.type;
    item[QStringLiteral("TTLSeconds")] = p.ttlSeconds;
    item[QStringLiteral("RecordData")] = recordDataItem(p);
    items.append(item);
    updateCache(server, items, p.zone, p.scope);
}

void RecordService::cacheRemove(const QString& server, const QString& zoneName,
                                const QString& scope, const QString& rrType,
                                const QString& name, const QString& matchValue) const
{
    QList<QJsonObject> items = readCache(server, zoneName, scope);
    const int before = items.size();
    items.removeIf([&](const QJsonObject& o) { return sameRecord(o, rrType, name, matchValue); });
    if (items.size() == before)
        return;
    updateCache(server, items, zoneName, scope);
}
