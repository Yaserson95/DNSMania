#include "LineListEdit.h"

LineListEdit::LineListEdit(QWidget* parent)
    : QTextEdit(parent)
{
}

QStringList LineListEdit::lines() const
{
    QStringList out;
    const QStringList raw = toPlainText().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString& l : raw) {
        const QString t = l.trimmed();
        if (!t.isEmpty())
            out << t;
    }
    return out;
}

void LineListEdit::setLines(const QStringList& lines)
{
    setPlainText(lines.join(QLatin1Char('\n')));
}
