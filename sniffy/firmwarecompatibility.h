#ifndef FIRMWARECOMPATIBILITY_H
#define FIRMWARECOMPATIBILITY_H

#include <QJsonObject>
#include <QString>
#include <QVersionNumber>
#include <QtGlobal>

namespace FirmwareCompatibility {

struct ReleaseManifest {
    bool isValid = false;
    QString targetMcu;
    QString firmwareVersion;
    QString releaseId;
    QString protocolVersionText;
    quint32 protocolVersion = 0;
    QString minDesktopVersion;
    QString publishedAt;
    QString channel;
    QString binaryName;
};

inline QString applicationVersionText()
{
#ifdef APP_VERSION
    return QString::fromLatin1(APP_VERSION);
#else
    return QStringLiteral("0.0.0");
#endif
}

inline quint32 supportedProtocolVersion()
{
    return 1U;
}

inline quint32 parseProtocolVersion(const QString &value, bool *ok = nullptr)
{
    bool localOk = false;
    const quint32 parsed = value.trimmed().toUInt(&localOk, 10);
    if (ok != nullptr) {
        *ok = localOk;
    }
    return localOk ? parsed : 0U;
}

inline bool isSupportedProtocolVersion(quint32 protocolVersion)
{
    return protocolVersion == supportedProtocolVersion();
}

inline bool isDesktopVersionCompatible(const QString &minimumDesktopVersion)
{
    const QVersionNumber current = QVersionNumber::fromString(applicationVersionText());
    const QVersionNumber minimum = QVersionNumber::fromString(minimumDesktopVersion);
    if (current.isNull() || minimum.isNull()) {
        return false;
    }

    return QVersionNumber::compare(current, minimum) >= 0;
}

inline ReleaseManifest releaseManifestFromJson(const QJsonObject &object)
{
    ReleaseManifest manifest;
    manifest.targetMcu = object.value(QStringLiteral("target_mcu")).toString();
    manifest.firmwareVersion = object.value(QStringLiteral("firmware_version")).toString();
    manifest.releaseId = object.value(QStringLiteral("release_id")).toString();
    manifest.protocolVersionText = object.value(QStringLiteral("protocol_version")).toString();
    manifest.protocolVersion = parseProtocolVersion(manifest.protocolVersionText);
    manifest.minDesktopVersion = object.value(QStringLiteral("min_desktop_version")).toString();
    manifest.publishedAt = object.value(QStringLiteral("published_at")).toString();
    manifest.channel = object.value(QStringLiteral("channel")).toString();
    manifest.binaryName = object.value(QStringLiteral("binary_name")).toString();

    manifest.isValid = !manifest.targetMcu.isEmpty()
        && !manifest.firmwareVersion.isEmpty()
        && !manifest.releaseId.isEmpty()
        && !manifest.protocolVersionText.isEmpty()
        && manifest.protocolVersion > 0
        && !manifest.minDesktopVersion.isEmpty()
        && !manifest.publishedAt.isEmpty()
        && !manifest.channel.isEmpty()
        && !manifest.binaryName.isEmpty();

    return manifest;
}

} // namespace FirmwareCompatibility

#endif // FIRMWARECOMPATIBILITY_H