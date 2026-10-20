#include "JsonUtil.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>

namespace Json {

QJsonValue prop(const QJsonObject& o, const QStringList& names)
{
    const QStringList keys = o.keys();
    for (const QString& want : names) {
        for (const QString& k : keys) {
            if (k.compare(want, Qt::CaseInsensitive) == 0)
                return o.value(k);
        }
    }
    return QJsonValue();
}

QString propStr(const QJsonObject& o, const QStringList& names, const QString& def)
{
    const QJsonValue v = prop(o, names);
    if (v.isString())
        return v.toString();
    if (v.isDouble())
        return QString::number(v.toDouble());
    if (v.isBool())
        return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    return def;
}

double propNum(const QJsonObject& o, const QStringList& names, double def)
{
    const QJsonValue v = prop(o, names);
    if (v.isDouble())
        return v.toDouble();
    if (v.isString()) {
        bool ok = false;
        const double d = v.toString().toDouble(&ok);
        if (ok)
            return d;
    }
    return def;
}

bool propBool(const QJsonObject& o, const QStringList& names, bool def)
{
    const QJsonValue v = prop(o, names);
    if (v.isBool())
        return v.toBool();
    if (v.isString()) {
        const QString s = v.toString();
        if (s.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0)
            return true;
        if (s.compare(QLatin1String("false"), Qt::CaseInsensitive) == 0)
            return false;
    }
    return def;
}

QList<QJsonObject> toObjectList(const QByteArray& json, QString* errorOut)
{
    QList<QJsonObject> result;
    const QByteArray trimmed = json.trimmed();
    if (trimmed.isEmpty() || trimmed == "null")
        return result;

    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(trimmed, &perr);
    if (perr.error != QJsonParseError::NoError) {
        if (errorOut)
            *errorOut = QStringLiteral("Ошибка разбора JSON: ") + perr.errorString();
        return result;
    }

    if (doc.isArray()) {
        const QJsonArray arr = doc.array();
        for (const QJsonValue& v : arr) {
            if (v.isObject())
                result.append(v.toObject());
        }
    } else if (doc.isObject()) {
        result.append(doc.object());
    }
    return result;
}

QString errorFromStderr(const QByteArray& stderrBytes)
{
    QString s = QString::fromUtf8(stderrBytes).trimmed();
    // PowerShell error records contain a lot of boilerplate; keep the first
    // meaningful lines.
    QStringList lines;
    const QStringList all = s.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString& line : all) {
        const QString t = line.trimmed();
        if (t.isEmpty())
            continue;
        if (t.startsWith(QLatin1String("CategoryInfo")) ||
            t.startsWith(QLatin1String("FullyQualifiedErrorId")) ||
            t.startsWith(QLatin1String("PSComputerName")) ||
            t.startsWith(QLatin1String("О строке")) ||
            t.startsWith(QLatin1String("At line"))) {
            continue;
        }
        lines << t;
    }
    if (lines.isEmpty())
        return s;
    return lines.join(QLatin1Char('\n'));
}

// Разбирает строку вида 'Name = Value Name2 = "Value 2" …' и возвращает
// значение свойства key. Так ConvertTo-Json сериализует CimInstanceProperties,
// если глубина JSON превышена: свойства склеиваются в одну строку через
// пробел (в records_example.json у SOA — все 7 свойств в одной строке).
// Строковые значения взяты в двойные кавычки (вложенные удвоены),
// числа/TimeSpan идут без кавычек: IPv4Address = "10.20.0.3", SerialNumber = 5.
static bool cimEntryValue(const QString& s, const QString& key, QString* out)
{
    int pos = 0;
    while (pos < s.length()) {
        const int eq = s.indexOf(QStringLiteral(" = "), pos);
        if (eq < 0)
            return false;
        // Имя — токен без пробелов непосредственно перед разделителем.
        int nameStart = eq;
        while (nameStart > 0 && !s.at(nameStart - 1).isSpace())
            --nameStart;
        const QString name = s.mid(nameStart, eq - nameStart);
        // Значение: в кавычках (может содержать пробелы) или до пробела.
        const int valStart = eq + 3;
        QString value;
        int valEnd;
        if (valStart < s.length() && s.at(valStart) == QLatin1Char('"')) {
            QString v;
            int i = valStart + 1;
            while (i < s.length()) {
                const QChar c = s.at(i);
                if (c == QLatin1Char('"')) {
                    if (i + 1 < s.length() && s.at(i + 1) == QLatin1Char('"')) {
                        v += QLatin1Char('"'); // удвоенная кавычка — литерал
                        i += 2;
                        continue;
                    }
                    break; // закрывающая кавычка
                }
                v += c;
                ++i;
            }
            value = v;
            valEnd = i + 1;
        } else {
            int i = valStart;
            while (i < s.length() && !s.at(i).isSpace())
                ++i;
            value = s.mid(valStart, i - valStart);
            valEnd = i;
        }
        if (name.compare(key, Qt::CaseInsensitive) == 0 && !value.isEmpty()) {
            *out = value;
            return true;
        }
        pos = valEnd;
    }
}

// Значение свойства CimProperty в строковый вид: строки как есть, целые без
// '.0'/экспоненты, массивы — склейка (DescriptiveText), объекты (TimeSpan,
// DateTime) значением не считаем.
static QString cimValueText(const QJsonValue& v)
{
    if (v.isString())
        return v.toString();
    if (v.isDouble()) {
        const double d = v.toDouble();
        const qint64 i = static_cast<qint64>(d);
        if (static_cast<double>(i) == d) // SerialNumber, Port, Preference…
            return QString::number(i);
        return QString::number(d);
    }
    if (v.isBool())
        return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    if (v.isArray()) {
        QString out;
        const QJsonArray arr = v.toArray();
        for (const QJsonValue& e : arr) {
            const QString s = cimValueText(e);
            if (!s.isEmpty())
                out += s;
        }
        return out;
    }
    return QString();
}

QString recordDataStr(const QJsonObject& recordData, const QString& key,
                      const QString& def)
{
    // 1) Плоская форма: обычные ключи (записи, добавленные самим приложением).
    const QJsonValue v = prop(recordData, { key });
    if (v.isString() && !v.toString().isEmpty())
        return v.toString();
    if (v.isDouble())
        return cimValueText(v);
    if (v.isBool())
        return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");

    // 2) Сырой CimInstance: значения в RecordData.CimInstanceProperties.
    //    Приложение сериализует с -Depth 25, где это массив объектов
    //    {Name, Value, CimType, …}; при меньшей глубине — строка или массив
    //    строк вида 'IPv4Address = "10.20.0.3"' (см. records_example.json).
    const QJsonValue cim =
        prop(recordData, { QStringLiteral("CimInstanceProperties") });
    QString found;
    if (cim.isString()) {
        cimEntryValue(cim.toString(), key, &found);
    } else if (cim.isArray()) {
        const QJsonArray arr = cim.toArray();
        for (const QJsonValue& e : arr) {
            if (e.isString()) {
                if (cimEntryValue(e.toString(), key, &found) && !found.isEmpty())
                    break;
            } else if (e.isObject()) {
                const QJsonObject item = e.toObject();
                if (propStr(item, { QStringLiteral("Name") })
                        .compare(key, Qt::CaseInsensitive) == 0) {
                    found = cimValueText(prop(item, { QStringLiteral("Value") }));
                    if (!found.isEmpty())
                        break;
                }
            }
        }
    }
    return found.isEmpty() ? def : found;
}

double recordDataNum(const QJsonObject& recordData, const QString& key, double def)
{
    const QJsonValue v = prop(recordData, { key });
    if (v.isDouble())
        return v.toDouble();
    if (v.isString()) {
        bool ok = false;
        const double d = v.toString().toDouble(&ok);
        if (ok)
            return d;
    }
    bool ok = false;
    const double d = recordDataStr(recordData, key).toDouble(&ok);
    return ok ? d : def;
}

QString recordDataText(const QJsonObject& recordData, const QString& recordType)
{
    const QString t = recordType.trimmed().toUpper();
    auto pick = [&recordData](const char* key) {
        return recordDataStr(recordData, QLatin1String(key));
    };
    if (t == QLatin1String("A"))
        return pick("IPv4Address");
    if (t == QLatin1String("AAAA"))
        return pick("IPv6Address");
    if (t == QLatin1String("CNAME"))
        return pick("HostNameAlias");
    if (t == QLatin1String("NS"))
        return pick("NameServer");
    if (t == QLatin1String("MX"))
        return pick("MailExchange") +
               QStringLiteral(" (pref ") +
               QString::number(recordDataNum(recordData, "Preference")) +
               QLatin1Char(')');
    if (t == QLatin1String("TXT"))
        return pick("DescriptiveText");
    if (t == QLatin1String("PTR"))
        return pick("PtrDomainName");
    if (t == QLatin1String("SRV"))
        return pick("DomainName");
    if (t == QLatin1String("SOA")) {
        // SOA правится только на сервере (Set-DnsServerPrimaryZone) — здесь
        // показываем содержательную часть для чтения.
        const QString primary = pick("PrimaryServer");
        QString out = primary;
        const double serial = recordDataNum(recordData, "SerialNumber", -1);
        if (serial >= 0)
            out += QStringLiteral(" (serial ") +
                   QString::number(static_cast<qint64>(serial)) + QLatin1Char(')');
        return out;
    }
    static const char* const kAll[] = {
        "IPv4Address", "IPv6Address", "HostNameAlias", "NameServer",
        "MailExchange", "DescriptiveText", "PtrDomainName", "DomainName",
        "PrimaryServer"
    };
    for (const char* k : kAll) {
        const QString s = pick(k);
        if (!s.isEmpty())
            return s;
    }
    return QString();
}

QStringList propStrList(const QJsonObject& o, const QStringList& names)
{
    const QJsonValue v = prop(o, names);
    QStringList out;
    if (v.isArray()) {
        const QJsonArray arr = v.toArray();
        for (const QJsonValue& e : arr) {
            if (e.isString())
                out << e.toString();
            else if (e.isDouble())
                out << QString::number(e.toDouble());
            else if (e.isBool())
                out << (e.toBool() ? QStringLiteral("true") : QStringLiteral("false"));
        }
    } else if (v.isString()) {
        out << v.toString();
    } else if (v.isDouble()) {
        out << QString::number(v.toDouble());
    } else if (v.isBool()) {
        out << (v.toBool() ? QStringLiteral("true") : QStringLiteral("false"));
    }
    return out;
}

} // namespace Json
