#include "LabeledLineEdit.h"

#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>

LabeledLineEdit::LabeledLineEdit(const QString& labelText, QWidget* parent)
    : QObject(parent)
    , m_label(new QLabel(labelText, parent))
    , m_edit(new QLineEdit(parent))
{
}

QString LabeledLineEdit::text() const { return m_edit->text().trimmed(); }

void LabeledLineEdit::setText(const QString& text) { m_edit->setText(text); }

void LabeledLineEdit::setLabelText(const QString& text) { m_label->setText(text); }

QString LabeledLineEdit::labelText() const { return m_label->text(); }

void LabeledLineEdit::setPlaceholderText(const QString& text) { m_edit->setPlaceholderText(text); }

void LabeledLineEdit::setFocus() { m_edit->setFocus(); }

void LabeledLineEdit::setVisible(bool visible)
{
    m_label->setVisible(visible);
    m_edit->setVisible(visible);
}

void LabeledLineEdit::addTo(QFormLayout* form) { form->addRow(m_label, m_edit); }
