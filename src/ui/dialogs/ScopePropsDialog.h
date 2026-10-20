#pragma once

#include <QDialog>

class QLabel;

// Свойства области (ZoneScope) зоны. Переименование области DnsServer не
// поддерживает (в модуле нет команды переименования ZoneScope), поэтому имя
// доступно только для просмотра; окно даёт справку по области и кнопку
// удаления — кроме области по умолчанию (защищена от удаления).
class ScopePropsDialog : public QDialog
{
    Q_OBJECT
public:
    ScopePropsDialog(const QString& zoneName, const QString& displayName,
                     const QString& scopeValue, const QString& fileName,
                     bool isDefault, QWidget* parent = nullptr);

    // true, если нажали «Удалить область…» (вызывающий код вызывает
    // подтверждение и ставит удаление в журнал).
    bool deleteRequested() const { return m_delete; }

private:
    bool m_delete = false;
    QLabel* m_note = nullptr;
};
