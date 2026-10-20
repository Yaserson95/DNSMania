#pragma once

#include <QDialog>

class QFormLayout;
class QVBoxLayout;
class QDialogButtonBox;
class LabeledLineEdit;

// Scaffolding for the "add/edit" dialogs: a vertical layout with a form, the
// standard OK/Cancel button box, and the shared name field. The field itself
// is the LabeledLineEdit control (label + edit managed as one unit); this
// class only owns one and exposes thin accessors, subclasses place rows via
// form()/addNameRow() or nameField()->addTo(form()).
class BaseDialog : public QDialog
{
    Q_OBJECT
public:
    explicit BaseDialog(const QString& windowTitle, QWidget* parent = nullptr);

    // Shared name field.
    LabeledLineEdit* nameField() const { return m_name; }
    QString name() const;
    void setName(const QString& name);
    void setNamePlaceholder(const QString& text);
    // Sets the label text and places the name field as a row at the current
    // form position (focus moves into the field).
    void addNameRow(const QString& label);

protected:
    QFormLayout* form() const { return m_form; }
    QVBoxLayout* mainLayout() const { return m_layout; }

private:
    QVBoxLayout* m_layout;
    QFormLayout* m_form;
    QDialogButtonBox* m_box;
    LabeledLineEdit* m_name;
};
