#pragma once

#include <QTextEdit>

// A multi-line editor holding a plain list of values — one per line.
// Used where the user enters a set of addresses (client subnets).
class LineListEdit : public QTextEdit
{
    Q_OBJECT
public:
    explicit LineListEdit(QWidget* parent = nullptr);

    // The values, one per line; blank lines are dropped and entries trimmed.
    QStringList lines() const;
    void setLines(const QStringList& lines);
};
