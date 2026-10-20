#include "ServerSelectDialog.h"

#include "ServerDialog.h"
#include "core/SecureStore.h"

#include <QAbstractItemView>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

ServerSelectDialog::ServerSelectDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Выбор DNS-сервера"));
    resize(560, 440);

    auto* layout = new QVBoxLayout(this);

    auto* hint = new QLabel(
        tr("Выберите DNS-сервер для управления. По умолчанию связь идёт через "
           "WinRM (Invoke-Command); если WinRM запрещён или требует админа, "
           "добавьте сервер в режиме «Локальный PowerShell с -ComputerName» "
           "(нужен RSAT DNS на этой машине)."),
        this);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    m_list = new QListWidget(this);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(m_list, 1);

    auto* addBtn = new QPushButton(tr("Добавить…"), this);
    m_editBtn = new QPushButton(tr("Изменить…"), this);
    m_removeBtn = new QPushButton(tr("Удалить"), this);
    auto* btnRow = new QHBoxLayout;
    btnRow->addWidget(addBtn);
    btnRow->addWidget(m_editBtn);
    btnRow->addWidget(m_removeBtn);
    btnRow->addStretch();
    layout->addLayout(btnRow);

    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    QPushButton* okBtn = box->button(QDialogButtonBox::Ok);
    okBtn->setText(tr("Подключиться"));
    // Кнопка по умолчанию: Enter принимает диалог, даже когда фокус в списке.
    okBtn->setDefault(true);
    okBtn->setAutoDefault(true);
    layout->addWidget(box);

    connect(addBtn, &QPushButton::clicked, this, &ServerSelectDialog::addServer);
    connect(m_editBtn, &QPushButton::clicked, this, &ServerSelectDialog::editServer);
    connect(m_removeBtn, &QPushButton::clicked, this, &ServerSelectDialog::removeServer);
    connect(box, &QDialogButtonBox::accepted, this, &ServerSelectDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_list, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem*) { accept(); });
    connect(m_list, &QListWidget::itemSelectionChanged, this, [this, box] {
        const bool has = m_list->currentItem() != nullptr;
        m_editBtn->setEnabled(has);
        m_removeBtn->setEnabled(has);
        box->button(QDialogButtonBox::Ok)->setEnabled(has);
    });

    reload();
    if (m_list->count() > 0)
        m_list->setCurrentRow(0);
}

void ServerSelectDialog::reload()
{
    const QString prev = currentServer().name;
    m_list->clear();
    const QIcon icon = style()->standardIcon(QStyle::SP_ComputerIcon);
    for (const ServerInfo& s : m_store.servers()) {
        const QString text = s.description.isEmpty()
                                 ? s.name
                                 : QStringLiteral("%1  —  %2").arg(s.name, s.description);
        auto* item = new QListWidgetItem(icon, text);
        item->setData(Qt::UserRole, s.name);
        item->setData(Qt::UserRole + 1, s.description);
        item->setData(Qt::UserRole + 2, s.localPs);
        item->setData(Qt::UserRole + 3, s.userName);
        QString tip;
        if (s.localPs) {
            tip = tr("Локальный PowerShell с -ComputerName (RSAT DNS)");
        } else if (s.userName.isEmpty()) {
            tip = tr("WinRM (Invoke-Command), текущий пользователь Windows");
        } else {
            tip = tr("WinRM (Invoke-Command), учётные данные: %1").arg(s.userName);
        }
        item->setToolTip(tip);
        m_list->addItem(item);
        if (!prev.isEmpty() && s.name.compare(prev, Qt::CaseInsensitive) == 0)
            m_list->setCurrentItem(item);
    }
}

ServerInfo ServerSelectDialog::currentServer() const
{
    QListWidgetItem* item = m_list->currentItem();
    if (!item)
        return {};
    ServerInfo s;
    s.name = item->data(Qt::UserRole).toString();
    s.description = item->data(Qt::UserRole + 1).toString();
    s.localPs = item->data(Qt::UserRole + 2).toBool();
    s.userName = item->data(Qt::UserRole + 3).toString();
    return s;
}

void ServerSelectDialog::addServer()
{
    ServerDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    if (dlg.name().isEmpty())
        return;
    m_store.addOrUpdate({ dlg.name(), dlg.description(), dlg.localPs(), dlg.userName() });
    // Пароль — в хранилище DPAPI, не в реестр.
    SecureStore::setPassword(dlg.name(), dlg.password());
    reload();
}

void ServerSelectDialog::editServer()
{
    const ServerInfo cur = currentServer();
    if (cur.name.isEmpty())
        return;
    ServerDialog dlg(this);
    dlg.setName(cur.name);
    dlg.setDescription(cur.description);
    dlg.setLocalPs(cur.localPs);
    dlg.setUserName(cur.userName);
    dlg.setPassword(SecureStore::password(cur.name));
    if (dlg.exec() != QDialog::Accepted)
        return;
    if (dlg.name().isEmpty())
        return;
    if (dlg.name().compare(cur.name, Qt::CaseInsensitive) != 0) {
        m_store.remove(cur.name);
        // Старый секрет при смене имени сервера переносим на новое имя.
        SecureStore::setPassword(dlg.name(), dlg.password());
        SecureStore::setPassword(cur.name, QString());
    } else {
        SecureStore::setPassword(cur.name, dlg.password());
    }
    m_store.addOrUpdate({ dlg.name(), dlg.description(), dlg.localPs(), dlg.userName() });
    reload();
}

void ServerSelectDialog::removeServer()
{
    const ServerInfo cur = currentServer();
    if (cur.name.isEmpty())
        return;
    if (QMessageBox::question(this, tr("Удаление"),
            tr("Удалить подключение %1? (Сами DNS-данные не затрагиваются)").arg(cur.name)) !=
        QMessageBox::Yes)
        return;
    m_store.remove(cur.name);
    SecureStore::setPassword(cur.name, QString());
    reload();
}

void ServerSelectDialog::accept()
{
    m_selected = currentServer();
    if (m_selected.name.isEmpty()) {
        QMessageBox::information(this, tr("Выбор сервера"),
                                 tr("Выберите сервер из списка или добавьте новое подключение."));
        return;
    }
    QDialog::accept();
}
