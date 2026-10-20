#pragma once

#include "core/ServerStore.h"

#include <QDialog>

class QListWidget;
class QListWidgetItem;
class QPushButton;

// Стартовое окно выбора DNS-сервера: список сохранённых подключений и
// добавление нового (в том числе в режиме локального PowerShell с
// -ComputerName, когда WinRM недоступен). Открывается при запуске приложения
// и по пункту меню «Файл» → «Выбрать сервер…».
class ServerSelectDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ServerSelectDialog(QWidget* parent = nullptr);

    // Сервер, выбранный по кнопке «Подключиться» (пустой, если не выбран).
    ServerInfo selected() const { return m_selected; }

    // Своё подтверждение: требует выбранного сервера в списке.
    void accept() override;

private:
    void reload();
    void addServer();
    void editServer();
    void removeServer();
    ServerInfo currentServer() const;

    ServerStore m_store;
    QListWidget* m_list = nullptr;
    QPushButton* m_editBtn = nullptr;
    QPushButton* m_removeBtn = nullptr;
    ServerInfo m_selected;
};
