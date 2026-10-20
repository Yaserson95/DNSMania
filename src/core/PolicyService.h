#pragma once

#include "DnsEntityService.h"
#include "DnsPolicy.h"

// CRUD for query resolution policies
// (Get/Add/Set/Remove-DnsServerQueryResolutionPolicy cmdlets).
//
// list() returns the raw projected objects (the policy table keeps them for
// its context actions); get() returns a parsed DNSPolicy - the data model the
// edit dialog works with. save() is "smart": Add- for new policies, Set- for
// plain edits and Remove+Add when the change cannot be expressed by Set-
// (see DNSPolicy::needsRecreate()).
class PolicyService : public DnsEntityService
{
public:
    using PolicyCb =
        std::function<void(bool ok, const DNSPolicy& policy, const QString& error)>;

    explicit PolicyService(PsClient* client);

    void list(const QString& server, ListCb cb);
    void list(const QString& server, const QString& zoneName, ListCb cb);
    // Всегда перечитывает список с сервера и обновляет кэш.
    void refreshList(const QString& server, const QString& zoneName, ListCb cb);
    // Fetches one policy by name (+ optional zone), e.g. for the edit dialog.
    void get(const QString& server, const QString& zoneName, const QString& name,
             PolicyCb cb);
    void getNames(const QString& server, const QString& zoneName, NamesCb cb) override;

    // ----- Команды (без "; 'OK'" — добавляет DnsService::runPs).
    // Add- для новых, Set- для простых правок, Remove+Add когда Set- не может
    // выразить изменение (Action/включённость/имя/уровень/снятый критерий).
    QString saveCommand(const DNSPolicy& p) const;
    QString removeCommand(const QString& name, const QString& zoneName) const;
    QString setOrderCommand(const QString& name, const QString& zoneName, int order) const;

    // ----- Оптимистичное отражение в файловом кэше (без выполнения в PS).
    void cacheSave(const QString& server, const DNSPolicy& p) const;
    void cacheRemove(const QString& server, const QString& name,
                     const QString& zoneName) const;
    void cacheSetOrder(const QString& server, const QString& name, const QString& zoneName,
                       int order) const;

private:
    // Get-... with optional -ZoneName/-Name followed by the projection that
    // flattens each policy into plain scalars plus Criteria/Content arrays.
    static QString queryBody(const QString& zoneName, const QString& name);
};
