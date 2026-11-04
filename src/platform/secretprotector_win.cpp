#include "platform/secretprotector.h"

#include <QLoggingCategory>

#include <windows.h>
#include <wincrypt.h>

Q_LOGGING_CATEGORY(lcSecret, "qtranslate.secret", QtWarningMsg)

namespace SecretProtector {

QByteArray protect(const QString &plainText)
{
    if (plainText.isEmpty())
        return {};

    const QByteArray utf8 = plainText.toUtf8();
    DATA_BLOB input = {};
    input.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(utf8.constData()));
    input.cbData = DWORD(utf8.size());

    DATA_BLOB output = {};
    if (!CryptProtectData(&input, L"qtranslate", nullptr, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        qCWarning(lcSecret) << "CryptProtectData failed:" << int(GetLastError());
        return {};
    }

    const QByteArray protectedData(reinterpret_cast<const char *>(output.pbData),
                                   int(output.cbData));
    LocalFree(output.pbData);
    return protectedData;
}

QString unprotect(const QByteArray &protectedData)
{
    if (protectedData.isEmpty())
        return {};

    DATA_BLOB input = {};
    input.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(protectedData.constData()));
    input.cbData = DWORD(protectedData.size());

    DATA_BLOB output = {};
    if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        qCWarning(lcSecret) << "CryptUnprotectData failed:" << int(GetLastError());
        return {};
    }

    const QString plainText = QString::fromUtf8(reinterpret_cast<const char *>(output.pbData),
                                                int(output.cbData));
    LocalFree(output.pbData);
    return plainText;
}

} // namespace SecretProtector
