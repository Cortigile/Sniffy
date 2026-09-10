#include "appupdatemanager.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QVersionNumber>

#include "firmwarecompatibility.h"

namespace {

constexpr auto kMetadataRequestKind = "metadata";
constexpr auto kDownloadRequestKind = "assetDownload";
constexpr auto kUpdateEndpoint = "https://sniffylab.com/scripts/sniffy_desktop_latest.php";

QString safeFileStem(const QString &value)
{
    QString stem = value;
    stem.replace(' ', '_');

    QString sanitized;
    sanitized.reserve(stem.size());
    for (const QChar ch : stem) {
        if (ch.isLetterOrNumber() || ch == '.' || ch == '_' || ch == '-') {
            sanitized.append(ch);
        }
    }

    return sanitized.isEmpty() ? QStringLiteral("sniffy-update") : sanitized;
}

} // namespace

AppUpdateManager::AppUpdateManager(QObject *parent)
    : QObject(parent),
      m_networkManager(new QNetworkAccessManager(this))
{
    connect(m_networkManager, &QNetworkAccessManager::finished, this, &AppUpdateManager::onNetworkFinished);
}

AppUpdateManager::~AppUpdateManager()
{
    clearDownloadState();
}

QString AppUpdateManager::currentVersion() const
{
    return FirmwareCompatibility::applicationVersionText();
}

bool AppUpdateManager::hasAvailableUpdate() const
{
    return m_updateAvailable;
}

QString AppUpdateManager::availableVersion() const
{
    return m_availableRelease.version;
}

void AppUpdateManager::checkForUpdates(bool manual)
{
    m_manualCheckPending = m_manualCheckPending || manual;

    QUrl url(QString::fromLatin1(kUpdateEndpoint));
    QUrlQuery query;
    const QString platform = currentPlatform();
    if (!platform.isEmpty()) {
        query.addQueryItem(QStringLiteral("platform"), platform);
    }
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Sniffy/%1").arg(currentVersion()));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply *reply = m_networkManager->get(request);
    reply->setProperty("sniffyRequestKind", QString::fromLatin1(kMetadataRequestKind));

    if (manual) {
        emit updateStatusTextChanged(QStringLiteral("Checking for updates..."));
    }
}

void AppUpdateManager::installAvailableUpdate()
{
    if (!m_updateAvailable || !m_availableRelease.isValid) {
        emit popupMessageRequested(QStringLiteral("No newer desktop update is available."));
        return;
    }

    if (m_installInProgress) {
        emit popupMessageRequested(QStringLiteral("Update download is already in progress."));
        return;
    }

    const QString sourceUrl = !m_availableRelease.preferredAssetDownloadUrl.isEmpty()
        ? m_availableRelease.preferredAssetDownloadUrl
        : m_availableRelease.preferredAssetDirectUrl;
    if (sourceUrl.isEmpty()) {
        emit popupMessageRequested(QStringLiteral("The selected update does not expose a downloadable asset."));
        return;
    }

    m_downloadTargetPath = targetDownloadPath(m_availableRelease);
    if (m_downloadTargetPath.isEmpty()) {
        emit popupMessageRequested(QStringLiteral("Unable to resolve the local update file path."));
        return;
    }

    clearDownloadState();
    m_downloadFile = new QSaveFile(m_downloadTargetPath);
    if (!m_downloadFile->open(QIODevice::WriteOnly)) {
        delete m_downloadFile;
        m_downloadFile = nullptr;
        emit popupMessageRequested(QStringLiteral("Unable to open the local update file for writing."));
        return;
    }

    QNetworkRequest request{QUrl(sourceUrl)};
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Sniffy/%1").arg(currentVersion()));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    m_downloadReply = m_networkManager->get(request);
    m_downloadReply->setProperty("sniffyRequestKind", QString::fromLatin1(kDownloadRequestKind));
    connect(m_downloadReply, &QIODevice::readyRead, this, &AppUpdateManager::onDownloadReadyRead);
    connect(m_downloadReply, &QNetworkReply::downloadProgress, this, &AppUpdateManager::onDownloadProgress);

    m_installInProgress = true;
    emit updateActionStateChanged(QStringLiteral("Downloading..."), false);
    emit updateStatusTextChanged(QStringLiteral("Downloading update %1...").arg(m_availableRelease.version));
}

void AppUpdateManager::onNetworkFinished(QNetworkReply *reply)
{
    const QString requestKind = reply->property("sniffyRequestKind").toString();
    if (requestKind == QLatin1String(kMetadataRequestKind)) {
        const QByteArray payload = reply->readAll();
        handleMetadataReply(reply, payload);
        reply->deleteLater();
        return;
    }

    if (requestKind == QLatin1String(kDownloadRequestKind)) {
        handleDownloadReply(reply);
        reply->deleteLater();
        return;
    }

    reply->deleteLater();
}

void AppUpdateManager::onDownloadReadyRead()
{
    auto *reply = qobject_cast<QNetworkReply *>(sender());
    if (reply == nullptr || reply != m_downloadReply) {
        return;
    }

    writeDownloadChunk(reply);
}

void AppUpdateManager::onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal)
{
    if (!m_installInProgress || bytesReceived <= 0 || bytesTotal <= 0) {
        return;
    }

    const qint64 percent = (bytesReceived * 100) / bytesTotal;
    emit updateStatusTextChanged(QStringLiteral("Downloading update %1... %2%").arg(m_availableRelease.version).arg(percent));
}

QString AppUpdateManager::currentPlatform() const
{
#ifdef Q_OS_WIN
    return QStringLiteral("windows");
#elif defined(Q_OS_LINUX)
    return QStringLiteral("linux");
#else
    return QString();
#endif
}

int AppUpdateManager::compareVersions(const QString &leftVersion, const QString &rightVersion) const
{
    const QVersionNumber left = QVersionNumber::fromString(leftVersion.trimmed());
    const QVersionNumber right = QVersionNumber::fromString(rightVersion.trimmed());
    if (!left.isNull() && !right.isNull()) {
        return QVersionNumber::compare(left, right);
    }

    return QString::compare(leftVersion.trimmed(), rightVersion.trimmed(), Qt::CaseInsensitive);
}

AppUpdateManager::ReleaseInfo AppUpdateManager::parseLatestRelease(const QByteArray &payload, QString *errorMessage) const
{
    ReleaseInfo info;

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("The update server returned invalid JSON.");
        }
        return info;
    }

    const QJsonObject root = document.object();
    info.version = root.value(QStringLiteral("version")).toString().trimmed();
    info.releaseTag = root.value(QStringLiteral("release_tag")).toString().trimmed();
    info.releasePage = root.value(QStringLiteral("release_page")).toString().trimmed();
    info.publishedAt = root.value(QStringLiteral("published_at")).toString().trimmed();
    info.preferredAssetKey = root.value(QStringLiteral("preferred_asset_key")).toString().trimmed();

    QJsonObject assetObject = root.value(QStringLiteral("preferred_asset")).toObject();
    if (assetObject.isEmpty()) {
        const QJsonObject assets = root.value(QStringLiteral("assets")).toObject();
        if (!info.preferredAssetKey.isEmpty()) {
            assetObject = assets.value(info.preferredAssetKey).toObject();
        }
        if (assetObject.isEmpty() && !assets.isEmpty()) {
            const auto firstAsset = assets.begin();
            if (firstAsset != assets.end() && firstAsset->isObject()) {
                info.preferredAssetKey = firstAsset.key();
                assetObject = firstAsset->toObject();
            }
        }
    }

    info.preferredAssetLabel = assetObject.value(QStringLiteral("label")).toString().trimmed();
    info.preferredAssetName = assetObject.value(QStringLiteral("name")).toString().trimmed();
    info.preferredAssetDirectUrl = assetObject.value(QStringLiteral("direct_url")).toString().trimmed();
    info.preferredAssetDownloadUrl = assetObject.value(QStringLiteral("download_url")).toString().trimmed();

    info.isValid = !info.version.isEmpty()
        && !info.releaseTag.isEmpty()
        && (!info.preferredAssetDirectUrl.isEmpty() || !info.preferredAssetDownloadUrl.isEmpty());

    if (!info.isValid && errorMessage != nullptr) {
        *errorMessage = QStringLiteral("The update server response is missing required release fields.");
    }

    return info;
}

void AppUpdateManager::handleMetadataReply(QNetworkReply *reply, const QByteArray &payload)
{
    const bool manual = m_manualCheckPending;
    m_manualCheckPending = false;

    if (reply->error() != QNetworkReply::NoError) {
        emit updateStatusTextChanged(QStringLiteral("Unable to verify the latest desktop version."));
        if (manual) {
            emit popupMessageRequested(QStringLiteral("Update check failed: %1").arg(reply->errorString()));
        }
        return;
    }

    QString parseErrorMessage;
    const ReleaseInfo latestRelease = parseLatestRelease(payload, &parseErrorMessage);
    if (!latestRelease.isValid) {
        emit updateStatusTextChanged(QStringLiteral("Unable to verify the latest desktop version."));
        if (manual) {
            emit popupMessageRequested(parseErrorMessage.isEmpty() ? QStringLiteral("Update check failed.") : parseErrorMessage);
        }
        return;
    }

    const int compareResult = compareVersions(latestRelease.version, currentVersion());
    if (compareResult > 0) {
        m_availableRelease = latestRelease;
        m_updateAvailable = true;
        emit updateAvailabilityChanged(true, latestRelease.version);
        emit updateActionStateChanged(QStringLiteral("Install and relaunch"), true);
        emit updateStatusTextChanged(QStringLiteral("Update available: %1").arg(latestRelease.version));
        if (manual) {
            emit popupMessageRequested(QStringLiteral("A newer desktop version is available: %1").arg(latestRelease.version));
        }
        return;
    }

    m_availableRelease = ReleaseInfo();
    if (m_updateAvailable) {
        m_updateAvailable = false;
        emit updateAvailabilityChanged(false, QString());
    }
    emit updateActionStateChanged(QStringLiteral("Install and relaunch"), true);
    emit updateStatusTextChanged(QStringLiteral("Up to date (%1)").arg(currentVersion()));
    if (manual) {
        emit popupMessageRequested(QStringLiteral("You are using the latest desktop version."));
    }
}

void AppUpdateManager::handleDownloadReply(QNetworkReply *reply)
{
    if (reply != m_downloadReply) {
        return;
    }

    QString writeErrorMessage;
    if (!writeDownloadChunk(reply, &writeErrorMessage)) {
        failInstall(writeErrorMessage);
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        failInstall(QStringLiteral("Update download failed: %1").arg(reply->errorString()));
        return;
    }

    if (m_downloadFile == nullptr) {
        failInstall(QStringLiteral("The local update file is no longer available."));
        return;
    }

    if (!m_downloadFile->commit()) {
        failInstall(QStringLiteral("Unable to finalize the downloaded update package."));
        return;
    }

    delete m_downloadFile;
    m_downloadFile = nullptr;

    QString errorMessage;
    if (!prepareInstallerHandoff(m_downloadTargetPath, &errorMessage)) {
        failInstall(errorMessage);
        return;
    }

    clearDownloadState();
    emit updateStatusTextChanged(QStringLiteral("Update package ready: %1").arg(m_availableRelease.version));
#if defined(Q_OS_WIN) || defined(Q_OS_LINUX)
    m_installInProgress = true;
#else
    emit quitRequested();
#endif
}

void AppUpdateManager::failInstall(const QString &message)
{
    clearDownloadState();
    emit updateActionStateChanged(QStringLiteral("Install and relaunch"), true);
    emit updateStatusTextChanged(QStringLiteral("Update available: %1").arg(m_availableRelease.version));
    emit popupMessageRequested(message);
}

void AppUpdateManager::clearDownloadState()
{
    if (m_downloadReply != nullptr) {
        disconnect(m_downloadReply, nullptr, this, nullptr);
        if (m_downloadReply->isRunning()) {
            m_downloadReply->abort();
        }
        m_downloadReply = nullptr;
    }

    if (m_downloadFile != nullptr) {
        m_downloadFile->cancelWriting();
        delete m_downloadFile;
        m_downloadFile = nullptr;
    }

    m_installInProgress = false;
}

QString AppUpdateManager::updateDownloadDirectory() const
{
    QString baseDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (baseDir.isEmpty()) {
        baseDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    }
    if (baseDir.isEmpty()) {
        return QString();
    }

    const QString path = QDir(baseDir).filePath(QStringLiteral("updates"));
    if (!QDir().mkpath(path)) {
        return QString();
    }

    return path;
}

QString AppUpdateManager::targetDownloadPath(const ReleaseInfo &release) const
{
    const QString directory = updateDownloadDirectory();
    if (directory.isEmpty()) {
        return QString();
    }

    const QString assetName = !release.preferredAssetName.isEmpty()
        ? release.preferredAssetName
        : QStringLiteral("Sniffy-%1").arg(release.version);
    return QDir(directory).filePath(safeFileStem(assetName));
}

QString AppUpdateManager::installedExecutablePath() const
{
    return QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
}

QString AppUpdateManager::installedRootPath() const
{
    const QFileInfo appInfo(installedExecutablePath());
    QDir appDir = appInfo.dir();
    if (appDir.dirName().compare(QStringLiteral("bin"), Qt::CaseInsensitive) == 0 && appDir.cdUp()) {
        return QDir::toNativeSeparators(appDir.absolutePath());
    }

    return QDir::toNativeSeparators(appInfo.absolutePath());
}

bool AppUpdateManager::writeDownloadChunk(QNetworkReply *reply, QString *errorMessage)
{
    if (reply == nullptr || reply != m_downloadReply || m_downloadFile == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("The local update file is no longer available.");
        }
        return false;
    }

    const QByteArray chunk = reply->readAll();
    if (!chunk.isEmpty() && m_downloadFile->write(chunk) != chunk.size()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Unable to write the downloaded update package to disk.");
        }
        return false;
    }

    return true;
}

bool AppUpdateManager::prepareInstallerHandoff(const QString &installerPath, QString *errorMessage)
{
    if (!QFileInfo::exists(installerPath)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("The downloaded update package is missing on disk.");
        }
        return false;
    }

#ifdef Q_OS_WIN
    const QString scriptPath = createWindowsInstallerScript(installerPath, errorMessage);
#elif defined(Q_OS_LINUX)
    const QString scriptPath = createLinuxInstallerScript(installerPath, errorMessage);
#endif
#if defined(Q_OS_WIN) || defined(Q_OS_LINUX)
    if (scriptPath.isEmpty()) {
        return false;
    }

    const QString readyPath = scriptPath + QStringLiteral(".ready");
    const QString cancelPath = scriptPath + QStringLiteral(".cancel");
    const QString failurePath = scriptPath + QStringLiteral(".error");
    const QString logPath = scriptPath + QStringLiteral(".log");
    QProcess helper;
#ifdef Q_OS_WIN
    helper.setProgram(QDir(qEnvironmentVariable("SystemRoot")).filePath(
        QStringLiteral("System32/WindowsPowerShell/v1.0/powershell.exe")));
    helper.setArguments({QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"),
                        QStringLiteral("-WindowStyle"), QStringLiteral("Hidden"),
                        QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
                        QStringLiteral("-File"), QDir::toNativeSeparators(scriptPath)});
#else
    helper.setProgram(QStringLiteral("/bin/sh"));
    helper.setArguments({scriptPath});
#endif
    helper.setWorkingDirectory(QFileInfo(scriptPath).absolutePath());
    helper.setStandardInputFile(QProcess::nullDevice());
    helper.setStandardOutputFile(logPath);
    helper.setStandardErrorFile(scriptPath + QStringLiteral(".stderr.log"));
    if (!helper.startDetached()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Unable to launch the update helper: %1").arg(helper.errorString());
        }
        return false;
    }

    QElapsedTimer elapsed;
    elapsed.start();
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this, timer, readyPath, cancelPath, failurePath, logPath, elapsed]() {
        if (QFileInfo::exists(failurePath)) {
            timer->stop();
            timer->deleteLater();
            QFile failure(failurePath);
            failure.open(QIODevice::ReadOnly);
            failInstall(QStringLiteral("Update preparation failed: %1\nLog: %2")
                            .arg(QString::fromUtf8(failure.readAll()).trimmed(), logPath));
        } else if (QFileInfo::exists(readyPath)) {
            timer->stop();
            timer->deleteLater();
            emit quitRequested();
        } else if (elapsed.elapsed() >= 10000) {
            timer->stop();
            timer->deleteLater();
            QFile cancel(cancelPath);
            cancel.open(QIODevice::WriteOnly);
            failInstall(QStringLiteral("The update helper did not become ready. Sniffy remains open. See %1 and the adjacent stderr log.").arg(logPath));
        }
    });
    timer->start(100);
    return true;
#else
    const bool opened = QDesktopServices::openUrl(QUrl::fromLocalFile(installerPath));
    if (!opened) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Unable to open the downloaded update package.");
        }
        return false;
    }
    return true;
#endif
}

QString AppUpdateManager::createWindowsInstallerScript(const QString &installerPath, QString *errorMessage) const
{
    const QString directory = updateDownloadDirectory();
    if (directory.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Unable to prepare the Windows installer helper directory.");
        }
        return QString();
    }

    QTemporaryFile script(QDir(directory).filePath(QStringLiteral("install-update-XXXXXX.ps1")));
    if (!script.open()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Unable to write the Windows installer helper script.");
        }
        return QString();
    }

    const QString appPath = installedExecutablePath();
    const QString installRoot = installedRootPath();
    const QString body = QStringLiteral(R"PS($ErrorActionPreference = 'Stop'
$installer = '%1'
$appPath = '%2'
$installRoot = '%3'
$pidToWait = %4
$readyPath = $PSCommandPath + '.ready'
$cancelPath = $PSCommandPath + '.cancel'
$appExited = $false
$failed = $false
function Write-UpdateLog([string]$message) {
    Write-Output ((Get-Date -Format o) + ' ' + $message)
}
try {
    Write-UpdateLog ('Helper started for PID ' + $pidToWait)
    $parentProcess = Get-Process -Id $pidToWait -ErrorAction SilentlyContinue
    if (Test-Path -LiteralPath $cancelPath) { throw 'Update handoff was cancelled.' }
    [IO.File]::WriteAllText($readyPath, 'ready')
    if ($null -ne $parentProcess -and -not $parentProcess.WaitForExit(20000)) {
        throw 'Sniffy did not exit within 20 seconds. Installer was not started.'
    }
    if (Test-Path -LiteralPath $cancelPath) { throw 'Update handoff was cancelled.' }
    $appExited = $true
    Write-UpdateLog ('Launching installer ' + $installer)
    if ([IO.Path]::GetExtension($installer) -ieq '.msi') {
        $arguments = '/i "' + $installer + '" /qn /norestart INSTALL_ROOT="' + $installRoot + '"'
        $process = Start-Process -FilePath "$env:SystemRoot\System32\msiexec.exe" -Verb RunAs -ArgumentList $arguments -Wait -PassThru
    } else {
        $process = Start-Process -FilePath $installer -Verb RunAs -ArgumentList ('/S /D=' + $installRoot) -Wait -PassThru
    }
    Write-UpdateLog ('Installer exit code: ' + $process.ExitCode)
    if ($process.ExitCode -notin @(0, 3010)) { throw ('Installer failed with exit code ' + $process.ExitCode) }
} catch {
    $failed = $true
    Write-UpdateLog ('Update failed: ' + $_.Exception.Message)
} finally {
    if ($appExited) {
        try {
            Write-UpdateLog ('Relaunching app from ' + $appPath)
            $restarted = Start-Process -FilePath $appPath -WorkingDirectory (Split-Path -Parent $appPath) -PassThru
            Write-UpdateLog ('Relaunch started with PID ' + $restarted.Id)
        } catch {
            $failed = $true
            Write-UpdateLog ('Relaunch failed: ' + $_.Exception.Message)
        }
    }
    Remove-Item -LiteralPath $readyPath, $cancelPath -Force -ErrorAction SilentlyContinue
    if (-not $failed) { Remove-Item -LiteralPath $PSCommandPath -Force -ErrorAction SilentlyContinue }
}
if ($failed) {
    $notification = New-Object -ComObject WScript.Shell
    $notification.Popup("Sniffy update failed. Details are in:`n$PSCommandPath.log", 0, 'Sniffy update', 48) | Out-Null
    exit 1
}
)PS")
        .arg(
            quoteForPowerShell(QDir::toNativeSeparators(installerPath)),
            quoteForPowerShell(appPath),
            quoteForPowerShell(installRoot),
            QString::number(QCoreApplication::applicationPid())
        );
    const QByteArray scriptBytes = QByteArray::fromHex("efbbbf") + body.toUtf8();
    if (script.write(scriptBytes) != scriptBytes.size() || !script.flush()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Unable to finish writing the Windows update helper.");
        }
        return QString();
    }
    script.setAutoRemove(false);
    script.close();
    return script.fileName();
}

QString AppUpdateManager::createLinuxInstallerScript(const QString &installerPath, QString *errorMessage) const
{
    const QString directory = updateDownloadDirectory();
    if (directory.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Unable to prepare the Linux installer helper directory.");
        }
        return QString();
    }

    QTemporaryFile script(QDir(directory).filePath(QStringLiteral("install-update-XXXXXX.sh")));
    if (!script.open()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Unable to write the Linux installer helper script.");
        }
        return QString();
    }

    const QString appPath = installedExecutablePath();
    const QString body = QStringLiteral(R"SH(#!/bin/sh
umask 077
installer=%1
app_path=%2
pid_to_wait=%3
ready_path="$0.ready"
cancel_path="$0.cancel"
error_path="$0.error"
log_path="$0.log"
app_exited=false
status=1

report_failure() {
    printf '%s\n' "$1" >&2
    printf '%s\n' "$1" > "$error_path"
}

finish() {
    trap - EXIT
    rm -f -- "$ready_path" "$cancel_path"
    if [ "$app_exited" = true ]; then
        if test -x "$app_path"; then
            printf '%s\n' "Relaunching $app_path"
            nohup "$app_path" >> "$log_path" 2>&1 < /dev/null &
        else
            status=1
            report_failure "Sniffy executable is missing after installation: $app_path"
        fi
    fi
    if [ "$status" -eq 0 ]; then
        rm -f -- "$0"
    elif [ "$app_exited" = true ]; then
        message="Sniffy update failed. See $log_path and $0.stderr.log"
        if command -v kdialog >/dev/null 2>&1; then
            kdialog --error "$message"
        elif command -v zenity >/dev/null 2>&1; then
            zenity --error --text="$message"
        elif command -v notify-send >/dev/null 2>&1; then
            notify-send --urgency=critical 'Sniffy update' "$message"
        fi
    fi
    exit "$status"
}
trap finish EXIT
trap 'report_failure "Update helper interrupted."; exit 1' HUP INT TERM

printf '%s\n' "Helper started for PID $pid_to_wait"
case "$installer" in
    /*.deb) ;;
    *) report_failure 'Automatic updates require an absolute path to a DEB package.'; exit 1 ;;
esac
apt_get=$(command -v apt-get) || { report_failure 'APT is required for automatic updates.'; exit 1; }
if [ "$(id -u)" -ne 0 ]; then
    command -v pkexec >/dev/null 2>&1 || { report_failure 'Install polkit/pkexec to enable automatic updates.'; exit 1; }
fi
[ -f "$cancel_path" ] && { report_failure 'Update handoff was cancelled.'; exit 1; }
: > "$ready_path" || exit 1
attempts=0
while kill -0 "$pid_to_wait" 2>/dev/null; do
    [ -f "$cancel_path" ] && { report_failure 'Update handoff was cancelled.'; exit 1; }
    [ "$attempts" -ge 20 ] && { report_failure 'Sniffy did not exit within 20 seconds.'; exit 1; }
    sleep 1
    attempts=$((attempts + 1))
done
[ -f "$cancel_path" ] && { report_failure 'Update handoff was cancelled.'; exit 1; }
app_exited=true
if [ "$(id -u)" -eq 0 ]; then
    DEBIAN_FRONTEND=noninteractive "$apt_get" -y --no-remove -o Dpkg::Options::=--force-confold install "$installer"
else
    pkexec /usr/bin/env DEBIAN_FRONTEND=noninteractive "$apt_get" -y --no-remove -o Dpkg::Options::=--force-confold install "$installer"
fi
status=$?
printf '%s\n' "Installer exit code: $status"
[ "$status" -eq 0 ] || report_failure "Installation failed or authorization was cancelled (exit $status)."
exit "$status"
)SH")
        .arg(
            quoteForShell(installerPath),
            quoteForShell(appPath),
            QString::number(QCoreApplication::applicationPid())
        );
    const QByteArray scriptBytes = body.toUtf8();
    if (script.write(scriptBytes) != scriptBytes.size() || !script.flush()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Unable to finish writing the Linux update helper.");
        }
        return QString();
    }
    script.setAutoRemove(false);
    script.close();
    return script.fileName();
}

QString AppUpdateManager::quoteForPowerShell(const QString &value) const
{
    QString escaped = value;
    escaped.replace("'", "''");
    return escaped;
}

QString AppUpdateManager::quoteForShell(const QString &value) const
{
    QString escaped = value;
    escaped.replace("'", "'\"'\"'");
    return QStringLiteral("'%1'").arg(escaped);
}