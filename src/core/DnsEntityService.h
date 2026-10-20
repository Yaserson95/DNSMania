#pragma once

#include "PsClient.h"

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <functional>

// Abstract base class for the per-entity DNS services (zones, zone scopes,
// records, client subnets, query resolution policies).
//
// It owns the shared infrastructure only: the PowerShell remoting client,
// the callback types every service uses, and helpers for submitting a
// command and parsing the JSON object lists returned by the list- cmdlets.
// Entity-specific add/edit/remove methods stay typed in the derived classes
// so mismatches are caught at compile time.
//
// getNames() is the one common operation: every service can return its
// plain name list (zones, scopes, policies, subnets, records) for the
// comboboxes on the forms. It is pure virtual, which also makes the base
// class abstract - each entity defines how its names are read.
class DnsEntityService
{
public:
    using ListCb =
        std::function<void(bool ok, const QList<QJsonObject>& items, const QString& error)>;
    using OkCb = std::function<void(bool ok, const QString& error)>;
    // Names of the entity for populating comboboxes on the forms.
    // zoneName is used by zone-bound entities (scopes, records, policies)
    // and ignored by the others (zones, subnets).
    using NamesCb =
        std::function<void(bool ok, const QStringList& names, const QString& error)>;

    virtual ~DnsEntityService() = default;

    // Returns the plain name list of this entity, e.g. zone names, scope
    // names or policy names - depending on the concrete service.
    virtual void getNames(const QString& server, const QString& zoneName, NamesCb cb) = 0;

    // Builds a PowerShell inline array literal, e.g. @('a','b'). Empty -> @().
    static QString psArray(const QStringList& values);

    // Общий каталог файлов кэша сущностей — вызывается один раз при старте
    // (main.cpp). Пока путь не задан, кэширование отключено.
    static void setCachePath(const QString& dir);
    static QString cachePath();

    // ----- Файловый кэш (доступен и Journal/MainWindow: снимки файлов).
    // Файлы: <cachePath>\<prefix>_<server>[_<zone>][_<suffix>].json —
    // список элементов в том же виде, в котором его парсит приложение.
    // Правило работы: при чтении список берётся из кэша; при изменении
    // сначала обновляется кэш, затем выполняется команда PowerShell
    // (при ошибке кэш перечитывается с сервера).
    QString cacheFile(const QString& server, const QString& zone = QString(),
                      const QString& suffix = QString()) const;

    // Читает список из кэша (пусто, если файла нет или он не разобрался).
    QList<QJsonObject> readCache(const QString& server, const QString& zone = QString(),
                                 const QString& suffix = QString()) const;
    // Пишет весь список в файл кэша.
    bool updateCache(const QString& server, const QList<QJsonObject>& items,
                     const QString& zone = QString(),
                     const QString& suffix = QString()) const;
    // Обновляет элемент в кэше по имени (Name/ZoneName): заменяет или добавляет.
    // Если файла кэша ещё нет (или он пуст) — ничего не пишет: частичный
    // список никогда не должен выдаваться за полный.
    bool updateCacheItem(const QString& server, const QJsonObject& item,
                         const QString& zone = QString(),
                         const QString& suffix = QString()) const;
    // Удаляет элемент из кэша по имени.
    bool removeCacheItem(const QString& server, const QString& name,
                         const QString& zone = QString(),
                         const QString& suffix = QString()) const;
    // Удаляет файл кэша — список загрузится с сервера заново.
    void invalidateCache(const QString& server, const QString& zone = QString(),
                         const QString& suffix = QString()) const;

protected:
    explicit DnsEntityService(PsClient* client);

    PsClient* client() const { return m_client; }

    // Submits a list- cmdlet: parses the JSON object array and reports either
    // the items or a connection/parse error.
    void submitList(const QString& server, const QString& body, ListCb cb) const;
    // Submits a mutating cmdlet: reports success or the error text.
    void submitOk(const QString& server, const QString& body, OkCb cb) const;

    // Adapts a list call into a NamesCb: extracts the entity's name property
    // (keys, first match wins, case-insensitive) from every returned object
    // and drops empty values. Shared implementation behind the getNames()
    // overloads of the derived services.
    static ListCb namesListCb(const QStringList& keys, NamesCb cb);
    // Извлекает имена из уже загруженных элементов (общая часть getNames()).
    static QStringList namesFrom(const QList<QJsonObject>& items, const QStringList& keys);

    // Префикс имён файлов кэша ("zones", "scopes", ...); задаётся в
    // конструкторе наследника.
    void setCachePrefix(const QString& prefix);

private:
    PsClient* m_client = nullptr;
    QString m_cachePrefix;
    static QString s_cachePath;
};
