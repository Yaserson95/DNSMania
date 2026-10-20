#include "SecureStore.h"

#include <QByteArray>
#include <QSettings>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <wincrypt.h>
#include <dpapi.h>
#endif

namespace {

const char* kOrg = "DNS Mania";
const char* kApp = "DNS Mania";

// Имя сервера -> допустимое имя значения реестра (без '/' — QSettings принял
// бы его за разделитель пути).
QString secretKey(const QString& key)
{
    QString k = key.trimmed().toLower();
    k.replace(QLatin1Char('/'), QLatin1Char('_'));
    k.replace(QLatin1Char('\\'), QLatin1Char('_'));
    return k;
}

} // namespace

void SecureStore::setPassword(const QString& key, const QString& password)
{
    QSettings settings{ QLatin1String(kOrg), QLatin1String(kApp) };
    const QString name = QStringLiteral("secrets/") + secretKey(key);
    if (password.isEmpty()) {
        settings.remove(name);
        return;
    }
#ifdef Q_OS_WIN
    const QByteArray utf8 = password.toUtf8();
    DATA_BLOB in;
    in.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(utf8.constData()));
    in.cbData = static_cast<DWORD>(utf8.size());
    DATA_BLOB out{};
    // CRYPTPROTECT_UI_FORBIDDEN: без окон и запросов системы.
    if (!CryptProtectData(&in, L"DNS Mania", nullptr, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &out))
        return;
    const QByteArray blob(reinterpret_cast<const char*>(out.pbData), int(out.cbData));
    LocalFree(out.pbData);
    settings.setValue(name, QString::fromLatin1(blob.toBase64()));
#else
    // Не-Windows платформы — только для сборки/тестов, без шифрования.
    settings.setValue(name, password);
#endif
}

QString SecureStore::password(const QString& key)
{
    QSettings settings{ QLatin1String(kOrg), QLatin1String(kApp) };
    const QString name = QStringLiteral("secrets/") + secretKey(key);
    const QString stored = settings.value(name).toString();
    if (stored.isEmpty())
        return QString();
#ifdef Q_OS_WIN
    const QByteArray blob = QByteArray::fromBase64(stored.toLatin1());
    if (blob.isEmpty())
        return QString();
    DATA_BLOB in;
    in.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(blob.constData()));
    in.cbData = static_cast<DWORD>(blob.size());
    DATA_BLOB out{};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &out))
        return QString();
    const QString res =
        QString::fromUtf8(reinterpret_cast<const char*>(out.pbData), int(out.cbData));
    LocalFree(out.pbData);
    return res;
#else
    return stored;
#endif
}
