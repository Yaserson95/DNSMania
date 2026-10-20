#pragma once

#include <QDialog>

class QListWidget;

// Диалог подтверждения перед выполнением журнала действий (кнопка
// «Сохранить»): описание всех подготовленных действий и кнопки
// «Далее» / «Отмена». По «Далее» команды уходят в PowerShell.
class SaveDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SaveDialog(const QStringList& actions, QWidget* parent = nullptr);
};
