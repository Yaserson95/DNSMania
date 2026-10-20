#include "ScopePropsDialog.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

ScopePropsDialog::ScopePropsDialog(const QString& zoneName, const QString& displayName,
                                   const QString& scopeValue, const QString& fileName,
                                   bool isDefault, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(isDefault ? tr("Область по умолчанию — %1").arg(zoneName)
                             : tr("Свойства области %1 — %2").arg(displayName, zoneName));
    resize(420, 220);

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    layout->addLayout(form);

    auto* nameLabel = new QLabel(displayName, this);
    if (!scopeValue.isEmpty())
        nameLabel->setToolTip(tr("Значение области: %1").arg(scopeValue));
    form->addRow(tr("Имя области:"), nameLabel);
    form->addRow(tr("Зона:"), new QLabel(zoneName, this));
    form->addRow(tr("Файл области:"),
                 new QLabel(fileName.isEmpty() ? QStringLiteral("—") : fileName, this));

    m_note = new QLabel(this);
    m_note->setWordWrap(true);
    if (isDefault) {
        m_note->setText(tr("Это область по умолчанию: она создана вместе с зоной, "
                           "защищена от удаления. Имя области совпадает с именем зоны."));
    } else {
        m_note->setText(tr("Имя области задаётся при создании. Windows DNS не поддерживает "
                           "переименование области: при необходимости удалите её и создайте "
                           "заново с тем же именем, затем перенесите записи."));
    }
    layout->addWidget(m_note);

    auto* box = new QDialogButtonBox(this);
    QPushButton* del = nullptr;
    if (!isDefault) {
        del = box->addButton(tr("Удалить область…"), QDialogButtonBox::DestructiveRole);
        connect(del, &QPushButton::clicked, this, [this] {
            m_delete = true;
            accept();
        });
    }
    box->addButton(QDialogButtonBox::Close);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(box);
}
