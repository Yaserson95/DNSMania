#include "BaseDialog.h"

#include "ui/controls/LabeledLineEdit.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QVBoxLayout>

BaseDialog::BaseDialog(const QString& windowTitle, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(windowTitle);

    m_layout = new QVBoxLayout(this);
    m_form = new QFormLayout;
    m_layout->addLayout(m_form);

    // Shared name field (not yet placed into the form).
    m_name = new LabeledLineEdit(QString(), this);

    // Standard OK/Cancel button box.
    m_box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(m_box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    m_layout->addWidget(m_box);
}

QString BaseDialog::name() const
{
    return m_name->text(); // already trimmed by the control
}

void BaseDialog::setName(const QString& name)
{
    m_name->setText(name);
}

void BaseDialog::setNamePlaceholder(const QString& text)
{
    m_name->setPlaceholderText(text);
}

void BaseDialog::addNameRow(const QString& label)
{
    m_name->setLabelText(label);
    m_name->addTo(m_form);
    m_name->setFocus();
}
