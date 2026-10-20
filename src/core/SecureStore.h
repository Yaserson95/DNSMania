#pragma once

#include <QString>

// Хранение секретов (учётные данные WinRM) через DPAPI Windows.
//
// Пароль шифруется ключом текущего пользователя Windows (CryptProtectData,
// CRYPTPROTECT_LOCAL_MACHINE не используется — файл/реестр читается только от
// имени того же пользователя) и ложится в реестр приложения в виде base64.
// В открытом виде пароль не хранится нигде, в лог не попадает и в командную
// строку powershell.exe не передаётся (только через переменные окружения
// процесса-потомка, см. PsClient).
namespace SecureStore {

// Ключ — имя сервера. Пустой пароль удаляет запись.
void setPassword(const QString& key, const QString& password);
// Пустая строка, если записи нет или расшифровать не удалось.
QString password(const QString& key);

} // namespace SecureStore
