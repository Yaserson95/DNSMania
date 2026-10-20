#pragma once

#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>

// Model of a query resolution policy: everything needed to render the
// Add-/Set- cmdlets, plus an is_new flag that decides which one to use.
//
// Editing is "smart": Set-DnsServerQueryResolutionPolicy can change every
// criterion, the condition, the zone scope weights and the processing order,
// but it has NO -Action and NO -Enable/-Disable parameters, and deleting a
// criterion through it is not documented. So for an edit the model compares
// its state against the snapshot taken when it was loaded from the server
// (loaded.*) and reports whether a Remove+Add re-create is required
// (needsRecreate()); otherwise a plain Set- is enough.
class DNSPolicy
{
public:
    DNSPolicy() = default;

    // ----- Identity.
    QString name;
    bool is_new = true; // true -> Add-, false -> edit (Set- or Remove+Add)

    // ----- Basic parameters.
    QString action = QStringLiteral("DENY");   // ALLOW | DENY | IGNORE
    QString level = QStringLiteral("Server");  // Server | Zone
    QString zoneName;                          // zone name (level Zone)
    QString condition = QStringLiteral("And"); // And | Or (criteria linkage)
    bool enabled = true;
    int processingOrder = -1; // -1 means "not set"

    // ----- Criteria: "<op>,<value>" (lowercase op), empty when absent.
    QString clientSubnet;      // "eq,<subnet>"
    QString fqdn;              // "eq,<pattern>"
    QString qtype;             // "eq,<TYPE>[,<TYPE>...]"
    QString timeOfDay;         // "eq,<HH:mm-HH:mm>"
    QString transportProtocol; // "eq,UDP" / "eq,TCP" / empty (any)

    // ----- Content: weighted zone scopes "scope-EU,7;scope-US,3" or empty.
    QString zoneScope;

    // Criteria the dialog does not edit (InternetProtocol, ServerInterfaceIP,
    // RecursionScope, ECS, Port, ...), kept as <ParamName, "op,value"> so a
    // re-created policy does not silently lose them.
    QMap<QString, QString> otherCriteria;

    // Snapshot of the policy as it exists on the server, taken by
    // fromJsonObject() when it is loaded. valid stays false for brand-new
    // policies; drives needsRecreate() and removeCommand() (which removes
    // the policy under its ORIGINAL name/level/zone).
    struct Loaded {
        bool valid = false;
        QString name;
        QString action;
        QString level;
        QString zoneName;
        bool enabled = true;
        QStringList criteria; // keys of criteria present on the server
    } loaded;

    // Parses one policy object as produced by PolicyService's projection
    // (plain scalars + Criteria [{CriteriaType, Criteria}] + Content
    // [{ScopeName, Weight}]).
    static DNSPolicy fromJsonObject(const QJsonObject& o);

    // Serializes back to exactly that projection shape — used to write the
    // policy into the file cache (updateCacheItem) so get() can serve edits
    // from the cache without a PowerShell round-trip.
    QJsonObject toJsonObject() const;

    // ----- Command rendering (no trailing "; 'OK'" - the service adds that).
    QString addCommand() const;
    QString setCommand() const;
    QString removeCommand() const; // under the loaded identity
    // What save() will run: Add- for new policies, Set- for plain edits,
    // Remove+Add (two lines) when Set- cannot express the change.
    QString command() const;
    // True when the edit changed something Set- cannot do: Action, enabled
    // state, name, level/zone, or a criterion that existed and is now empty.
    bool needsRecreate() const;

    // ----- UI helpers (Russian table columns).
    QString criteriaText() const; // "Подсеть клиента: равно X" per line
    QString resultText() const;   // weighted scopes / "Зона: x" / "Весь сервер"

private:
    QString criterion(const QString& key) const; // field by criterion key
    void appendCriteria(QString& body) const;    // five dialog criteria
};
