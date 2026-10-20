#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <functional>

// Журнал отложенных изменений (пункты меню «Файл» и кнопки правой панели).
//
// Пользователь готовит действия (добавление/правка/удаление зон, областей,
// записей, подсетей и правил). Действие попадает в журнал и оптимистично
// отражается в файловом кэше — но в PowerShell НЕ выполняется. Команды
// «улетают» в PS пачкой только после нажатия «Сохранить»
// (см. MainWindow::saveJournal).
//
// Отмена (Ctrl+Z) и возврат (Ctrl+Y) работают на снимках файлов кэша:
// каждая запись хранит содержимое затронутых файлов до и после
// оптимистичного изменения, поэтому отмена — это точное восстановление, а
// не реконструкция изменений.
class Journal : public QObject
{
    Q_OBJECT
public:
    using DoneFn = std::function<void(bool ok, const QString& error)>;
    using ExecFn = std::function<void(DoneFn)>; // выполнить команду в PS

    struct Snap {
        QString file;
        bool beforeExists = false;
        QByteArray before;
        bool afterExists = false;
        QByteArray after;
    };

    struct Entry {
        QString title;  // описание для диалога подтверждения
        QString server; // целевой сервер (для логов/статуса)
        QList<Snap> snaps;
        ExecFn exec;
    };

    explicit Journal(QObject* parent = nullptr);

    // Запись действия: beginSnapshot(files) -> оптимистичное изменение кэша
    // -> commit(). Вызывается подряд, без возврата в цикл событий, поэтому
    // снимки «до/после» согласованы.
    void beginSnapshot(const QStringList& files);
    void commit(const QString& title, const QString& server, ExecFn exec);

    // Ожидают применения (индексы [0, count())).
    int count() const { return m_cursor; }
    bool canUndo() const { return m_cursor > 0; }
    bool canRedo() const { return m_cursor < m_entries.size(); }
    QString undoTitle() const;
    QString redoTitle() const;
    QStringList pendingTitles() const;
    const Entry& at(int index) const { return m_entries.at(index); }

    void undo();    // Ctrl+Z: вернуть состояние кэша на шаг назад
    void redo();    // Ctrl+Y: применить отменённое действие снова
    void clearAll(); // «Удалить все действия»: откатить всё и очистить
    void commitAll(); // все действия выполнены — очистить без отката
    void dropFirst(int n); // убрать n уже выполненных записей

signals:
    void changed();

private:
    void applySnaps(const QList<Snap>& snaps, bool useAfter) const;

    QList<Entry> m_entries;
    int m_cursor = 0;
    QList<Snap> m_pending; // снимки «до» текущего записываемого действия
};
