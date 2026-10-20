#pragma once

#include <QObject>

class QFormLayout;
class QLabel;
class QLineEdit;
class QWidget;

// A labeled single-line text field: a QLabel + QLineEdit pair managed as one
// control. The widgets are parented to the dialog; addTo() places them into a
// form row (label | edit) so they keep the normal QFormLayout column
// alignment. Keeping the pair together lets dialogs read, fill or hide it as
// a unit (e.g. the zone name / network rows of ZoneDialog).
class LabeledLineEdit : public QObject
{
    Q_OBJECT
public:
    LabeledLineEdit(const QString& labelText, QWidget* parent);

    QString text() const; // trimmed
    void setText(const QString& text);

    void setLabelText(const QString& text);
    QString labelText() const;

    void setPlaceholderText(const QString& text);
    void setFocus();

    // Shows/hides both the label and the edit; a row whose widgets are all
    // hidden collapses in the form.
    void setVisible(bool visible);

    // Adds label | edit as a row at the current position of the form.
    void addTo(QFormLayout* form);

    QLabel* label() const { return m_label; }
    QLineEdit* edit() const { return m_edit; }

private:
    QLabel* m_label;
    QLineEdit* m_edit;
};
