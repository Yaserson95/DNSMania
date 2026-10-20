#include "ServerStore.h"

#include <QSettings>

static const char* kOrg = "DNS Mania";
static const char* kApp = "DNS Mania";

ServerStore::ServerStore()
{
    load();
}

bool ServerStore::contains(const QString& name) const
{
    for (const ServerInfo& s : m_servers) {
        if (s.name.compare(name, Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

void ServerStore::addOrUpdate(const ServerInfo& info)
{
    for (ServerInfo& s : m_servers) {
        if (s.name.compare(info.name, Qt::CaseInsensitive) == 0) {
            s = info;
            save();
            return;
        }
    }
    m_servers.append(info);
    save();
}

bool ServerStore::remove(const QString& name)
{
    for (int i = 0; i < m_servers.size(); ++i) {
        if (m_servers.at(i).name.compare(name, Qt::CaseInsensitive) == 0) {
            m_servers.removeAt(i);
            save();
            return true;
        }
    }
    return false;
}

void ServerStore::setServers(const QList<ServerInfo>& servers)
{
    m_servers = servers;
    save();
}

void ServerStore::load()
{
    m_servers.clear();
    QSettings settings{ QLatin1String(kOrg), QLatin1String(kApp) };
    const int count = settings.beginReadArray(QStringLiteral("servers"));
    for (int i = 0; i < count; ++i) {
        settings.setArrayIndex(i);
        ServerInfo info;
        info.name = settings.value(QStringLiteral("name")).toString();
        info.description = settings.value(QStringLiteral("description")).toString();
        info.localPs = settings.value(QStringLiteral("localps"), false).toBool();
        info.userName = settings.value(QStringLiteral("username")).toString();
        if (!info.name.isEmpty())
            m_servers.append(info);
    }
    settings.endArray();
}

void ServerStore::save() const
{
    QSettings settings{ QLatin1String(kOrg), QLatin1String(kApp) };
    settings.remove(QStringLiteral("servers"));
    settings.beginWriteArray(QStringLiteral("servers"), m_servers.size());
    for (int i = 0; i < m_servers.size(); ++i) {
        settings.setArrayIndex(i);
        settings.setValue(QStringLiteral("name"), m_servers.at(i).name);
        settings.setValue(QStringLiteral("description"), m_servers.at(i).description);
        settings.setValue(QStringLiteral("localps"), m_servers.at(i).localPs);
        settings.setValue(QStringLiteral("username"), m_servers.at(i).userName);
    }
    settings.endArray();
}
