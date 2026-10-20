#include "SaveDialog.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

SaveDialog::SaveDialog(const QStringList& actions, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Применение изменений"));
    resize(560, 440);

    auto* layout = new QVBoxLayout(this);

    auto* hint = new QLabel(
        tr("Будут выполнены следующие действия (%1). Команды отправятся на "
           "DNS-сервер, после чего кэш приложения и все данные обновятся.")
            .arg(actions.size()),
        this);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto* list = new QListWidget(this);
    list->setSelectionMode(QAbstractItemView::NoSelection);
    for (const QString& a : actions)
        list->addItem(a);
    layout->addWidget(list, 1);

    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    QPushButton* okBtn = box->button(QDialogButtonBox::Ok);
    okBtn->setText(tr("Далее"));
    // Кнопка по умолчанию: Enter = «Далее», даже когда фокус в списке.
    okBtn->setDefault(true);
    okBtn->setAutoDefault(true);
    box->button(QDialogButtonBox::Cancel)->setText(tr("Отмена"));
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(box);
}
