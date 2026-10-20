#include "DnsEntityService.h"

#include "JsonUtil.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

QString DnsEntityService::s_cachePath;

DnsEntityService::DnsEntityService(PsClient* client)
    : m_client(client)
{
}

void DnsEntityService::setCachePath(const QString& dir)
{
    s_cachePath = dir;
    if (!dir.isEmpty())
        QDir().mkpath(dir);
}

QString DnsEntityService::cachePath()
{
    return s_cachePath;
}

// Builds a PowerShell inline array literal, e.g. @('a','b'). Empty -> @().
QString DnsEntityService::psArray(const QStringList& values)
{
    QStringList quoted;
    for (const QString& v : values) {
        if (!v.trimmed().isEmpty())
            quoted << PsClient::quote(v.trimmed());
    }
    if (quoted.isEmpty())
        return QStringLiteral("@()");
    return QStringLiteral("@(") + quoted.join(QLatin1Char(',')) + QLatin1Char(')');
}

DnsEntityService::ListCb DnsEntityService::namesListCb(const QStringList& keys, NamesCb cb)
{
    return [cb, keys](bool ok, const QList<QJsonObject>& items, const QString& error) {
        if (!ok) {
            cb(false, QStringList(), error);
            return;
        }
        cb(true, namesFrom(items, keys), QString());
    };
}

QStringList DnsEntityService::namesFrom(const QList<QJsonObject>& items, const QStringList& keys)
{
    QStringList names;
    for (const QJsonObject& o : items) {
        const QString n = Json::propStr(o, keys);
        if (!n.isEmpty())
            names << n;
    }
    return names;
}

// ----- Файловый кэш.

void DnsEntityService::setCachePrefix(const QString& prefix)
{
    m_cachePrefix = prefix;
}

QString DnsEntityService::cacheFile(const QString& server, const QString& zone,
                                    const QString& suffix) const
{
    QString base = m_cachePrefix;
    if (!server.isEmpty())
        base += QLatin1Char('_') + server;
    if (!zone.isEmpty())
        base += QLatin1Char('_') + zone;
    if (!suffix.isEmpty())
        base += QLatin1Char('_') + suffix;
    // Имя сервера/зоны может содержать символы, недопустимые в путях.
    static const char* const kBad = "/\\:*?\"<>|";
    for (const QChar c : QString::fromLatin1(kBad))
        base.replace(c, QLatin1Char('_'));
    return s_cachePath + QLatin1Char('/') + base + QStringLiteral(".json");
}

QList<QJsonObject> DnsEntityService::readCache(const QString& server, const QString& zone,
                                               const QString& suffix) const
{
    if (s_cachePath.isEmpty())
        return {};
    QFile f(cacheFile(server, zone, suffix));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QString err;
    const auto items = Json::toObjectList(f.readAll(), &err);
    return err.isEmpty() ? items : QList<QJsonObject>();
}

bool DnsEntityService::updateCache(const QString& server, const QList<QJsonObject>& items,
                                   const QString& zone, const QString& suffix) const
{
    if (s_cachePath.isEmpty())
        return false;
    QSaveFile f(cacheFile(server, zone, suffix));
    if (!f.open(QIODevice::WriteOnly))
        return false;
    QJsonArray arr;
    for (const QJsonObject& o : items)
        arr.append(o);
    f.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
    return f.commit();
}

bool DnsEntityService::updateCacheItem(const QString& server, const QJsonObject& item,
                                       const QString& zone, const QString& suffix) const
{
    static const QStringList kNameKeys = { QStringLiteral("Name"), QStringLiteral("ZoneName") };
    const QString name = Json::propStr(item, kNameKeys);
    if (name.isEmpty())
        return false;
    QList<QJsonObject> items = readCache(server, zone, suffix);
    if (items.isEmpty()) // кэша нет — частичный список не создаём
        return false;
    bool replaced = false;
    for (QJsonObject& o : items) {
        if (Json::propStr(o, kNameKeys).compare(name, Qt::CaseInsensitive) == 0) {
            o = item;
            replaced = true;
            break;
        }
    }
    if (!replaced)
        items.append(item);
    return updateCache(server, items, zone, suffix);
}

bool DnsEntityService::removeCacheItem(const QString& server, const QString& name,
                                       const QString& zone, const QString& suffix) const
{
    static const QStringList kNameKeys = { QStringLiteral("Name"), QStringLiteral("ZoneName") };
    QList<QJsonObject> items = readCache(server, zone, suffix);
    const int before = items.size();
    items.removeIf([&name](const QJsonObject& o) {
        return Json::propStr(o, kNameKeys).compare(name, Qt::CaseInsensitive) == 0;
    });
    if (items.size() == before)
        return false;
    return updateCache(server, items, zone, suffix);
}

void DnsEntityService::invalidateCache(const QString& server, const QString& zone,
                                       const QString& suffix) const
{
    if (!s_cachePath.isEmpty())
        QFile::remove(cacheFile(server, zone, suffix));
}

void DnsEntityService::submitList(const QString& server, const QString& body, ListCb cb) const
{
    m_client->submit(server, body, [cb](const PsResult& r) {
        if (!r.ok) { cb(false, {}, r.error); return; }
        QString err;
        const auto items = Json::toObjectList(r.json.toUtf8(), &err);
        cb(err.isEmpty(), items, err);
    });
}

void DnsEntityService::submitOk(const QString& server, const QString& body, OkCb cb) const
{
    m_client->submit(server, body, [cb](const PsResult& r) {
        cb(r.ok, r.ok ? QString() : r.error);
    });
}
