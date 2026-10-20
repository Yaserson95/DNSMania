#pragma once

#include "JsonUtil.h"

#include <QJsonObject>
#include <QString>

// Model of a zone scope (Get-DnsServerZoneScope). The cmdlet has no Name
// property: the scope name comes back as ZoneScope, the owning zone as
// ZoneName, and FileName is the backing file (empty for AD-integrated
// zones). PolicyService-style projections normalize the name to Name;
// fromJsonObject also accepts the raw ZoneScope key.
struct DNSZoneScope {
    QString name;     // имя области (свойство ZoneScope)
    QString zoneName; // зона-владелец
    QString fileName; // файл области (пусто для AD-интегрированных зон)

    static DNSZoneScope fromJsonObject(const QJsonObject& o)
    {
        DNSZoneScope s;
        s.name = Json::propStr(o, { QStringLiteral("Name"), QStringLiteral("ZoneScope") });
        s.zoneName = Json::propStr(o, { QStringLiteral("ZoneName") });
        s.fileName = Json::propStr(o, { QStringLiteral("FileName") });
        return s;
    }
};
