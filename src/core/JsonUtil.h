#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <QJsonValue>

// Helpers for working with JSON produced by PowerShell's ConvertTo-Json.
namespace Json {

// Case-insensitive property lookup. PowerShell preserves property casing, but
// it can vary between cmdlets, so lookups are done ignoring case.
QJsonValue prop(const QJsonObject& o, const QStringList& names);

QString propStr(const QJsonObject& o, const QStringList& names,
                const QString& def = QString());
double propNum(const QJsonObject& o, const QStringList& names, double def = 0.0);
bool propBool(const QJsonObject& o, const QStringList& names, bool def = false);

// Normalizes parsed JSON into a list of objects.
//   - JSON array  -> each element that is an object
//   - JSON object -> a single-element list
//   - null/other  -> empty list
// Returns an empty list and sets *errorOut on a parse failure.
QList<QJsonObject> toObjectList(const QByteArray& json, QString* errorOut = nullptr);

// Extracts a human-readable message from captured stderr of powershell.exe.
QString errorFromStderr(const QByteArray& stderrBytes);

// Human-readable text of a resource record's data (the RecordData object),
// chosen by record type: A/AAAA/CNAME/NS/MX/TXT/PTR/SRV, with a generic
// fallback probing the known properties.
QString recordDataText(const QJsonObject& recordData, const QString& recordType);

// Value of a RecordData property. Handles both real-world shapes:
//  - a flat object (optimistic cache entries written by the app itself):
//    plain keys like IPv4Address;
//  - the raw CimInstance: the values live in RecordData.CimInstanceProperties,
//    which ConvertTo-Json renders depending on the JSON depth:
//      * -Depth 25 (the app's PsClient): an array of objects
//        {Name: "IPv4Address", Value: "10.20.0.3", CimType: 14, ...};
//      * smaller depths (records_example.json): a string or an array of
//        strings 'IPv4Address = "10.20.0.3"', with all properties of the
//        record data joined into one space-separated string when the depth
//        limit is hit (SOA example: seven "Name = Value" pairs in one line).
QString recordDataStr(const QJsonObject& recordData, const QString& key,
                      const QString& def = QString());
// Same, but numeric (Preference/Port/Priority/Weight/SerialNumber...).
double recordDataNum(const QJsonObject& recordData, const QString& key,
                     double def = 0.0);

// Returns string values as a list. Handles JSON arrays (PowerShell lists such
// as subnet arrays {10.11.0.0/18}), single scalars and missing values.
QStringList propStrList(const QJsonObject& o, const QStringList& names);

} // namespace Json
