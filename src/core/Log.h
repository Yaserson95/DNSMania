#pragma once

#include <QString>

// Append-to-file diagnostics log. The file lives in the per-user data
// directory (%LOCALAPPDATA%\DNS Mania\DNS Mania\dnsmania.log), so writing
// works even when the app is installed under Program Files. init() also
// installs a Qt message handler, so qDebug/qWarning/qCritical land in the
// same file.
//
// Usage: Log::info("..."), Log::warn("..."), Log::error("...").
namespace Log {

// Путь к лог-файлу.
QString filePath();

// Создаёт каталог и файл (ротирует прошлый лог >5 МБ), ставит Qt-хендлер.
void init();

void write(const QString& level, const QString& message);

inline void info(const QString& message) { write(QStringLiteral("INFO"), message); }
inline void warn(const QString& message) { write(QStringLiteral("WARN"), message); }
inline void error(const QString& message) { write(QStringLiteral("ERROR"), message); }

} // namespace Log
