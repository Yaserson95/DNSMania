#include "DnsPolicy.h"

#include "JsonUtil.h"
#include "PsClient.h"

#include <QJsonArray>
#include <QJsonDocument>

namespace {

// "eq,X" -> "равно X", "ne,X" -> "не равно X" (table column text).
QString criterionValueText(const QString& raw)
{
    QString v = raw.trimmed();
    if (v.contains(QLatin1Char(','))) {
        const QString op = v.section(QLatin1Char(','), 0, 0).trimmed().toLower();
        const QString rest = v.section(QLatin1Char(','), 1).trimmed();
        if (op == QLatin1String("eq"))
            return QStringLiteral("равно ") + rest;
        if (op == QLatin1String("ne"))
            return QStringLiteral("не равно ") + rest;
    }
    return v;
}

// Normalizes a criterion: lowercase operator, trimmed value; a bare value
// without an operator gets "eq," prepended ("MRG-L18" -> "eq,MRG-L18").
QString normalizeCriterion(const QString& raw)
{
    const QString v = raw.trimmed();
    if (v.isEmpty())
        return QString();
    const int comma = v.indexOf(QLatin1Char(','));
    if (comma < 0)
        return QStringLiteral("eq,") + v;
    return v.left(comma).trimmed().toLower() + QLatin1Char(',') +
           v.mid(comma + 1).trimmed();
}

// Criterion value as a string: scalars directly, arrays joined with commas
// (multi-value criteria such as "EQ,*.contoso.com,NE,*.fabrikam.com").
QString valueText(const QJsonValue& v)
{
    if (v.isString())
        return v.toString();
    if (v.isDouble())
        return QString::number(v.toInt());
    if (v.isBool())
        return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    if (v.isArray()) {
        QStringList parts;
        for (const QJsonValue& e : v.toArray()) {
            const QString s = valueText(e);
            if (!s.isEmpty())
                parts << s;
        }
        return parts.join(QLatin1Char(','));
    }
    return QString();
}

// Criterion JSON array, tolerant of a single unwrapped object and of a
// JSON-encoded string.
QJsonArray jsonArrayOf(const QJsonObject& o, const QStringList& names)
{
    const QJsonValue v = Json::prop(o, names);
    if (v.isArray())
        return v.toArray();
    if (v.isObject())
        return QJsonArray{ v.toObject() };
    if (v.isString() && !v.toString().trimmed().isEmpty()) {
        const QJsonDocument d = QJsonDocument::fromJson(v.toString().toUtf8());
        if (d.isArray())
            return d.array();
        if (d.isObject())
            return QJsonArray{ d.object() };
    }
    return QJsonArray{};
}

// Maps a criterion type/property name to the -Parameter name used in the
// cmdlets; empty when unknown ("ClientSubnetNames" -> "ClientSubnet").
QString canonicalType(const QString& type)
{
    static const QMap<QString, QString> map = {
        { QStringLiteral("clientsubnet"), QStringLiteral("ClientSubnet") },
        { QStringLiteral("fqdn"), QStringLiteral("Fqdn") },
        { QStringLiteral("qtype"), QStringLiteral("QType") },
        { QStringLiteral("timeofday"), QStringLiteral("TimeOfDay") },
        { QStringLiteral("transportprotocol"), QStringLiteral("TransportProtocol") },
        { QStringLiteral("serverinterfaceip"), QStringLiteral("ServerInterfaceIP") },
        { QStringLiteral("internetprotocol"), QStringLiteral("InternetProtocol") },
        { QStringLiteral("recursionscope"), QStringLiteral("RecursionScope") },
        { QStringLiteral("port"), QStringLiteral("Port") },
        { QStringLiteral("ecs"), QStringLiteral("ECS") },
    };
    return map.value(type.trimmed().toLower());
}

// Criterion JSON keys seen across server versions: the generic
// CriteriaType/Criteria pair and the per-type property names.
const char* const kCriterionKeys[] = {
    "ClientSubnetNames", "ClientSubnet", "Fqdn", "QTypeNames", "QType",
    "TimeOfDayNames", "TimeOfDay", "TransportProtocolNames", "TransportProtocol",
    "ServerInterfaceIPNames", "ServerInterfaceIP", "InternetProtocolNames",
    "InternetProtocol", "RecursionScopeNames", "RecursionScope", "PortNumbers",
    "Port", "ECS",
};

// Criterion keys of the dialog-editable fields (used for the loaded snapshot).
const char* const kDialogCriterionKeys[] = {
    "clientSubnet", "fqdn", "qtype", "timeOfDay", "transport", "zoneScope",
};

} // namespace

// Assigns a parsed criterion to its field (dialog criteria) or to the
// otherCriteria map (everything else), keyed by the cmdlet parameter name.
static void assignCriterion(DNSPolicy& p, const QString& type, const QString& value)
{
    const QString v = normalizeCriterion(value);
    if (v.isEmpty())
        return;
    const QString param = canonicalType(type);
    if (param.isEmpty())
        return;
    if (param == QLatin1String("ClientSubnet"))
        p.clientSubnet = v;
    else if (param == QLatin1String("Fqdn"))
        p.fqdn = v;
    else if (param == QLatin1String("QType"))
        p.qtype = v;
    else if (param == QLatin1String("TimeOfDay"))
        p.timeOfDay = v;
    else if (param == QLatin1String("TransportProtocol"))
        p.transportProtocol = v;
    else
        p.otherCriteria.insert(param, v);
}

DNSPolicy DNSPolicy::fromJsonObject(const QJsonObject& o)
{
    DNSPolicy p;
    p.name = Json::propStr(o, { "Name" });
    p.action = Json::propStr(o, { "Action" }, QStringLiteral("DENY"));
    p.condition = Json::propStr(o, { "Condition" }, QStringLiteral("And"));
    p.zoneName = Json::propStr(o, { "ZoneName" });
    // Уровень определяется наличием ZoneName, а не свойством Level (оно
    // возвращается не на всех версиях сервера): есть имя зоны — политика
    // зонная, иначе серверная.
    p.level = p.zoneName.isEmpty() ? QStringLiteral("Server") : QStringLiteral("Zone");
    p.processingOrder = static_cast<int>(Json::propNum(o, { "ProcessingOrder" }, -1));
    p.enabled = Json::propBool(o, { "Enabled", "IsEnabled" }, true);

    for (const QJsonValue& v : jsonArrayOf(o, { QStringLiteral("Criteria") })) {
        const QJsonObject c = v.toObject();
        if (c.isEmpty())
            continue;
        // Generic form: {CriteriaType: "ClientSubnet", Criteria: "EQ,x"}.
        const QString type = Json::propStr(c, { "CriteriaType", "Type" });
        const QString value = valueText(Json::prop(c, { "Criteria" }));
        if (!type.isEmpty() && !value.isEmpty()) {
            assignCriterion(p, type, value);
            continue;
        }
        // Per-type form: {ClientSubnetNames: [...], Fqdn: "...", ...}.
        for (const char* key : kCriterionKeys) {
            const QString val = valueText(Json::prop(c, { QString::fromLatin1(key) }));
            if (!val.isEmpty())
                assignCriterion(p, QString::fromLatin1(key), val);
        }
    }

    // Zone scope weights: Content [{ScopeName, Weight}] -> "scope,weight;...".
    QStringList weights;
    for (const QJsonValue& v : jsonArrayOf(o, { QStringLiteral("Content") })) {
        const QJsonObject c = v.toObject();
        const QString scope = Json::propStr(c, { "ScopeName" });
        if (scope.isEmpty())
            continue;
        const int w = static_cast<int>(Json::propNum(c, { "Weight" }, 1));
        const QString entry = scope + QLatin1Char(',') + QString::number(w);
        if (!weights.contains(entry))
            weights << entry;
    }
    p.zoneScope = weights.join(QLatin1Char(';'));

    // Snapshot for the smart-save decision.
    p.is_new = false;
    p.loaded.valid = true;
    p.loaded.name = p.name;
    p.loaded.action = p.action;
    p.loaded.level = p.level;
    p.loaded.zoneName = p.zoneName;
    p.loaded.enabled = p.enabled;
    for (const char* key : kDialogCriterionKeys) {
        if (!p.criterion(QString::fromLatin1(key)).trimmed().isEmpty())
            p.loaded.criteria << QString::fromLatin1(key);
    }
    return p;
}

QJsonObject DNSPolicy::toJsonObject() const
{
    QJsonObject o;
    o[QStringLiteral("Name")] = name;
    o[QStringLiteral("Action")] = action;
    o[QStringLiteral("Condition")] = condition;
    if (!zoneName.isEmpty())
        o[QStringLiteral("ZoneName")] = zoneName;
    if (processingOrder >= 0)
        o[QStringLiteral("ProcessingOrder")] = processingOrder;
    o[QStringLiteral("Enabled")] = enabled;

    // Критерии в обобщённой форме Criteria [{CriteriaType, Criteria}] —
    // её же читает fromJsonObject (прямая обратная операция).
    QJsonArray criteria;
    const auto addCriterion = [&criteria](const QString& type, const QString& value) {
        if (value.trimmed().isEmpty())
            return;
        QJsonObject c;
        c[QStringLiteral("CriteriaType")] = type;
        c[QStringLiteral("Criteria")] = value.trimmed();
        criteria.append(c);
    };
    addCriterion(QStringLiteral("ClientSubnet"), clientSubnet);
    addCriterion(QStringLiteral("Fqdn"), fqdn);
    addCriterion(QStringLiteral("QType"), qtype);
    addCriterion(QStringLiteral("TimeOfDay"), timeOfDay);
    addCriterion(QStringLiteral("TransportProtocol"), transportProtocol);
    for (auto it = otherCriteria.constBegin(); it != otherCriteria.constEnd(); ++it)
        addCriterion(it.key(), it.value());
    if (!criteria.isEmpty())
        o[QStringLiteral("Criteria")] = criteria;

    // Веса областей: Content [{ScopeName, Weight}].
    QJsonArray content;
    for (const QString& entry : zoneScope.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
        const QString scope = entry.section(QLatin1Char(','), 0, 0).trimmed();
        if (scope.isEmpty())
            continue;
        const int w = entry.section(QLatin1Char(','), 1, 1).trimmed().toInt();
        QJsonObject c;
        c[QStringLiteral("ScopeName")] = scope;
        c[QStringLiteral("Weight")] = w > 0 ? w : 1;
        content.append(c);
    }
    if (!content.isEmpty())
        o[QStringLiteral("Content")] = content;
    return o;
}

QString DNSPolicy::criterion(const QString& key) const
{
    if (key == QLatin1String("clientSubnet"))
        return clientSubnet;
    if (key == QLatin1String("fqdn"))
        return fqdn;
    if (key == QLatin1String("qtype"))
        return qtype;
    if (key == QLatin1String("timeOfDay"))
        return timeOfDay;
    if (key == QLatin1String("transport"))
        return transportProtocol;
    if (key == QLatin1String("zoneScope"))
        return zoneScope;
    return QString();
}

void DNSPolicy::appendCriteria(QString& body) const
{
    const auto add = [&body](const QString& param, const QString& value) {
        if (!value.trimmed().isEmpty())
            body += QStringLiteral(" -") + param + QLatin1Char(' ') +
                    PsClient::quote(value.trimmed());
    };
    add(QStringLiteral("ClientSubnet"), clientSubnet);
    add(QStringLiteral("Fqdn"), fqdn);
    add(QStringLiteral("QType"), qtype);
    add(QStringLiteral("TimeOfDay"), timeOfDay);
    add(QStringLiteral("TransportProtocol"), transportProtocol);
}

QString DNSPolicy::addCommand() const
{
    // Note: Add- has no -Level parameter - a server-level policy is simply
    // one created without -ZoneName. Disabled policies are created with
    // -Disable (Set- cannot change the enabled flag).
    QString body =
        QStringLiteral("Add-DnsServerQueryResolutionPolicy -Name ") + PsClient::quote(name) +
        QStringLiteral(" @cn");
    body += QStringLiteral(" -Action ") + PsClient::quote(action);
    body += QStringLiteral(" -Condition ") + PsClient::quote(condition.toUpper());
    if (level == QLatin1String("Zone") && !zoneName.trimmed().isEmpty())
        body += QStringLiteral(" -ZoneName ") + PsClient::quote(zoneName.trimmed());
    appendCriteria(body);
    for (auto it = otherCriteria.constBegin(); it != otherCriteria.constEnd(); ++it)
        body += QStringLiteral(" -") + it.key() + QLatin1Char(' ') +
                PsClient::quote(it.value());
    if (level == QLatin1String("Zone") && !zoneScope.trimmed().isEmpty())
        body += QStringLiteral(" -ZoneScope ") + PsClient::quote(zoneScope.trimmed());
    if (processingOrder >= 0)
        body += QStringLiteral(" -ProcessingOrder ") + QString::number(processingOrder);
    if (!enabled)
        body += QStringLiteral(" -Disable");
    return body;
}

QString DNSPolicy::setCommand() const
{
    // Set- mirrors Add-'s criteria but has no -Action, no -Disable and no
    // -Level/-Port: identity is -Name (+ -ZoneName for zone policies).
    // Criteria not passed here are left untouched by the server (the cmdlet
    // updates the criteria it receives), so otherCriteria are not repeated -
    // Set- cannot take every parameter they may map to anyway.
    QString body =
        QStringLiteral("Set-DnsServerQueryResolutionPolicy -Name ") + PsClient::quote(name) +
        QStringLiteral(" @cn");
    if (level == QLatin1String("Zone") && !zoneName.trimmed().isEmpty())
        body += QStringLiteral(" -ZoneName ") + PsClient::quote(zoneName.trimmed());
    body += QStringLiteral(" -Condition ") + PsClient::quote(condition.toUpper());
    appendCriteria(body);
    if (level == QLatin1String("Zone") && !zoneScope.trimmed().isEmpty())
        body += QStringLiteral(" -ZoneScope ") + PsClient::quote(zoneScope.trimmed());
    if (processingOrder >= 0)
        body += QStringLiteral(" -ProcessingOrder ") + QString::number(processingOrder);
    return body;
}

QString DNSPolicy::removeCommand() const
{
    // Remove under the identity the policy has ON THE SERVER: renaming or
    // moving a policy to another level/zone must still remove the original.
    const QString n = loaded.valid ? loaded.name : name;
    const QString lvl = loaded.valid ? loaded.level : level;
    const QString z = loaded.valid ? loaded.zoneName : zoneName;

    QString body =
        QStringLiteral("Remove-DnsServerQueryResolutionPolicy -Name ") + PsClient::quote(n) +
        QStringLiteral(" @cn");
    if (lvl == QLatin1String("Zone") && !z.trimmed().isEmpty())
        body += QStringLiteral(" -ZoneName ") + PsClient::quote(z.trimmed());
    body += QStringLiteral(" -Force");
    return body;
}

QString DNSPolicy::command() const
{
    if (is_new)
        return addCommand();
    if (!needsRecreate())
        return setCommand();
    return removeCommand() + QLatin1Char('\n') + addCommand();
}

bool DNSPolicy::needsRecreate() const
{
    if (is_new || !loaded.valid)
        return false;
    // Set- identifies the policy by name/level: renaming or moving it (or
    // changing Action / the enabled flag, which have no Set- parameter)
    // requires a re-create.
    if (name.compare(loaded.name, Qt::CaseInsensitive) != 0)
        return true;
    if (action.compare(loaded.action, Qt::CaseInsensitive) != 0)
        return true;
    if (enabled != loaded.enabled)
        return true;
    if (level.compare(loaded.level, Qt::CaseInsensitive) != 0)
        return true;
    if (level == QLatin1String("Zone") &&
        zoneName.compare(loaded.zoneName, Qt::CaseInsensitive) != 0)
        return true;
    // Set- can add or replace a criterion but cannot delete one: a criterion
    // that existed on the server and is now cleared forces a re-create.
    for (const QString& key : loaded.criteria) {
        if (criterion(key).trimmed().isEmpty())
            return true;
    }
    return false;
}

QString DNSPolicy::criteriaText() const
{
    const bool isOr = condition.compare(QLatin1String("Or"), Qt::CaseInsensitive) == 0;
    QStringList lines;
    const auto add = [&lines, isOr](const QString& label, const QString& raw) {
        const QString v = criterionValueText(raw);
        if (v.isEmpty())
            return;
        const QString prefix = lines.isEmpty()
                                   ? QString()
                                   : (isOr ? QStringLiteral("ИЛИ ")
                                           : QStringLiteral("И "));
        lines << prefix + label + QStringLiteral(": ") + v;
    };
    add(QStringLiteral("Подсеть клиента"), clientSubnet);
    add(QStringLiteral("FQDN"), fqdn);
    add(QStringLiteral("Тип запроса"), qtype);
    add(QStringLiteral("Время суток"), timeOfDay);
    add(QStringLiteral("Транспорт"), transportProtocol);
    for (auto it = otherCriteria.constBegin(); it != otherCriteria.constEnd(); ++it)
        add(it.key(), it.value());
    return lines.join(QLatin1Char('\n'));
}

QString DNSPolicy::resultText() const
{
    if (!zoneScope.trimmed().isEmpty()) {
        QStringList out;
        for (const QString& entry : zoneScope.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
            const QString scope = entry.section(QLatin1Char(','), 0, 0).trimmed();
            const QString w = entry.section(QLatin1Char(','), 1, 1).trimmed();
            if (scope.isEmpty())
                continue;
            out << (w.isEmpty() ? scope
                                : QStringLiteral("%1 (Вес: %2)").arg(scope, w));
        }
        if (!out.isEmpty())
            return out.join(QLatin1Char('\n'));
    }
    if (level == QLatin1String("Zone"))
        return QStringLiteral("Зона: ") + zoneName;
    return QStringLiteral("Весь сервер");
}
