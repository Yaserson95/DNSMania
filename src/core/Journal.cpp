#include "Journal.h"

#include <QFile>
#include <QSaveFile>

Journal::Journal(QObject* parent)
    : QObject(parent)
{
}

// ----- Снимки файлов кэша.

void Journal::beginSnapshot(const QStringList& files)
{
    m_pending.clear();
    for (const QString& file : files) {
        Snap s;
        s.file = file;
        QFile f(file);
        if (f.open(QIODevice::ReadOnly)) {
            s.beforeExists = true;
            s.before = f.readAll();
        }
        m_pending.append(s);
    }
}

void Journal::commit(const QString& title, const QString& server, ExecFn exec)
{
    Entry e;
    e.title = title;
    e.server = server;
    e.exec = std::move(exec);
    for (Snap& s : m_pending) {
        QFile f(s.file);
        if (f.open(QIODevice::ReadOnly)) {
            s.afterExists = true;
            s.after = f.readAll();
        }
        e.snaps.append(s);
    }
    m_pending.clear();

    // Новое действие делает недостижимым «возврат» — история после курсора
    // отбрасывается, как в обычном редакторе.
    while (m_entries.size() > m_cursor)
        m_entries.removeLast();
    m_entries.append(e);
    m_cursor = m_entries.size();
    emit changed();
}

// ----- Отмена / возврат.

QString Journal::undoTitle() const
{
    return canUndo() ? m_entries.at(m_cursor - 1).title : QString();
}

QString Journal::redoTitle() const
{
    return canRedo() ? m_entries.at(m_cursor).title : QString();
}

QStringList Journal::pendingTitles() const
{
    QStringList out;
    for (int i = 0; i < m_cursor; ++i)
        out << m_entries.at(i).title;
    return out;
}

void Journal::applySnaps(const QList<Snap>& snaps, bool useAfter) const
{
    for (const Snap& s : snaps) {
        const bool exists = useAfter ? s.afterExists : s.beforeExists;
        const QByteArray data = useAfter ? s.after : s.before;
        if (!exists) {
            QFile::remove(s.file);
            continue;
        }
        QSaveFile f(s.file);
        if (!f.open(QIODevice::WriteOnly))
            continue;
        f.write(data);
        f.commit();
    }
}

void Journal::undo()
{
    if (!canUndo())
        return;
    --m_cursor;
    applySnaps(m_entries.at(m_cursor).snaps, false);
    emit changed();
}

void Journal::redo()
{
    if (!canRedo())
        return;
    applySnaps(m_entries.at(m_cursor).snaps, true);
    ++m_cursor;
    emit changed();
}

void Journal::clearAll()
{
    // Откатываем от последней записи к первой: снимки «до» последовательно
    // возвращают файлы к исходному состоянию.
    for (int i = m_cursor - 1; i >= 0; --i)
        applySnaps(m_entries.at(i).snaps, false);
    m_entries.clear();
    m_cursor = 0;
    m_pending.clear();
    emit changed();
}

void Journal::commitAll()
{
    // Действия применены на сервере — снимки откатывать нельзя.
    m_entries.clear();
    m_cursor = 0;
    m_pending.clear();
    emit changed();
}

void Journal::dropFirst(int n)
{
    if (n <= 0)
        return;
    const int k = qMin(n, m_entries.size());
    for (int i = 0; i < k; ++i)
        m_entries.removeFirst();
    m_cursor = qMax(0, m_cursor - k);
    emit changed();
}
