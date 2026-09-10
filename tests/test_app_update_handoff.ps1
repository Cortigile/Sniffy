param(
    [Parameter(Mandatory = $true)][string]$QtRoot,
    [Parameter(Mandatory = $true)][string]$MinGWRoot,
    [string]$PosixShell
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = New-Object System.Text.UTF8Encoding($false)
$sourceRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../sniffy'))
$testRoot = Join-Path ([IO.Path]::GetTempPath()) ('sniffy-updater-test-' + [guid]::NewGuid().ToString('N'))
$appDirectory = Join-Path $testRoot ("App's space & percent% " + [char]0x010D + '/bin')
$executable = Join-Path $appDirectory 'sniffy.exe'
$originalPath = $env:PATH
$originalSystemRoot = $env:SystemRoot
$generatedDirectory = $null

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

try {
    [IO.Directory]::CreateDirectory($appDirectory) | Out-Null
    $env:PATH = "$QtRoot/bin;$MinGWRoot/bin;$env:PATH"
    $harnessPath = Join-Path $testRoot 'handoff.cpp'
    $mocPath = Join-Path $testRoot 'moc_appupdatemanager.cpp'
    $harness = @'
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>
#define private public
#include "appupdatemanager.h"
#undef private

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QStringList arguments = app.arguments();
    const int scriptArgument = arguments.indexOf(QStringLiteral("-File"));
    if (scriptArgument >= 0) {
        const QString scriptPath = arguments.value(scriptArgument + 1);
        if (!QFileInfo::exists(scriptPath)) return 4;
        QTextStream(stdout) << "Helper stub started\n";
        if (qEnvironmentVariable("SNIFFY_TEST_HELPER_STATE") == QStringLiteral("ready")) {
            QFile ready(scriptPath + QStringLiteral(".ready"));
            return ready.open(QIODevice::WriteOnly) ? 0 : 5;
        }
        if (qEnvironmentVariable("SNIFFY_TEST_HELPER_STATE") == QStringLiteral("preflight-failure")) {
            QFile failure(scriptPath + QStringLiteral(".error"));
            if (!failure.open(QIODevice::WriteOnly)) return 5;
            failure.write("APT is required for automatic updates.");
            return 6;
        }
        return 6;
    }

    QCoreApplication::setOrganizationName(QStringLiteral("SniffyUpdaterTests"));
    QCoreApplication::setApplicationName(qEnvironmentVariable("SNIFFY_TEST_NAME"));
    QStandardPaths::setTestModeEnabled(true);
    AppUpdateManager manager;
    QString error;
    if (arguments.value(1).startsWith(QStringLiteral("generate"))) {
        const QString path = arguments.value(1) == QStringLiteral("generate-linux")
            ? manager.createLinuxInstallerScript(arguments.value(2), &error)
            : manager.createWindowsInstallerScript(arguments.value(2), &error);
        QTextStream(stdout) << (path.isEmpty() ? error : path) << Qt::endl;
        return path.isEmpty() ? 1 : 0;
    }
    QObject::connect(&manager, &AppUpdateManager::quitRequested, &app, [] { QCoreApplication::exit(0); });
    QObject::connect(&manager, &AppUpdateManager::popupMessageRequested, &app, [](const QString &message) {
        QTextStream(stdout) << message << Qt::endl;
        QCoreApplication::exit(2);
    });
    QTimer::singleShot(15000, &app, [] { QCoreApplication::exit(3); });
    if (!manager.prepareInstallerHandoff(arguments.value(2), &error)) {
        QTextStream(stdout) << error << Qt::endl;
        return 2;
    }
    return app.exec();
}
'@
    [IO.File]::WriteAllText($harnessPath, $harness)
    & "$QtRoot/bin/moc.exe" "$sourceRoot/appupdatemanager.h" -o $mocPath
    Assert-True ($LASTEXITCODE -eq 0) 'moc failed'
    & "$MinGWRoot/bin/g++.exe" -std=c++17 -fPIC "-I$sourceRoot" "-I$QtRoot/include" `
        "-I$QtRoot/include/QtCore" "-I$QtRoot/include/QtGui" "-I$QtRoot/include/QtNetwork" `
        $harnessPath "$sourceRoot/appupdatemanager.cpp" $mocPath "-L$QtRoot/lib" `
        -lQt6Network -lQt6Gui -lQt6Core -o $executable
    Assert-True ($LASTEXITCODE -eq 0) 'Updater harness compilation failed'
    $env:SNIFFY_TEST_NAME = Split-Path -Leaf $testRoot
    $installer = Join-Path $testRoot "Installer's space & percent%.exe"
    [IO.File]::WriteAllText($installer, 'Never executed: Start-Process is mocked')

    function Get-Process {
        param($Id, $ErrorAction)
        $parent = [pscustomobject]@{}
        $parent | Add-Member ScriptMethod WaitForExit {
            param($Milliseconds)
            $global:SniffyUpdateTest.Waited = $true
            return $global:SniffyUpdateTest.Scenario -ne 'parent-timeout'
        }
        return $parent
    }

    function Start-Process {
        param($FilePath, $Verb, $ArgumentList, $WorkingDirectory, [switch]$Wait, [switch]$PassThru)
        $global:SniffyUpdateTest.Calls.Add([pscustomobject]@{
            FilePath = $FilePath; Verb = $Verb; Arguments = $ArgumentList; WorkingDirectory = $WorkingDirectory
        })
        if ($Verb -eq 'RunAs') {
            Assert-True $global:SniffyUpdateTest.Waited 'Installer ran before waiting for Sniffy'
            Assert-True (Test-Path -LiteralPath ($global:SniffyUpdateTest.HelperPath + '.ready')) 'Installer ran without readiness'
            if ($global:SniffyUpdateTest.Scenario -eq 'uac-cancel') { throw [ComponentModel.Win32Exception]::new(1223) }
            if ($global:SniffyUpdateTest.Scenario -eq 'installer-launch-failure') { throw 'Cannot start installer' }
            $exitCode = 0
            if ($global:SniffyUpdateTest.Scenario -eq 'installer-failure') { $exitCode = 7 }
            if ($global:SniffyUpdateTest.Scenario -eq 'msi-reboot') { $exitCode = 3010 }
            return [pscustomobject]@{ ExitCode = $exitCode }
        }
        if ($global:SniffyUpdateTest.Scenario -eq 'relaunch-failure') { throw 'Cannot start Sniffy' }
        return [pscustomobject]@{ Id = 12345 }
    }

    function New-Object {
        param($ComObject)
        Assert-True ($ComObject -eq 'WScript.Shell') 'Unexpected COM object'
        $notification = [pscustomobject]@{}
        $notification | Add-Member ScriptMethod Popup {
            param($Message, $Timeout, $Title, $Flags)
            $global:SniffyUpdateTest.Notifications++
            return 1
        }
        return $notification
    }

    foreach ($scenario in @('success', 'uac-cancel', 'installer-launch-failure', 'installer-failure',
                            'parent-timeout', 'cancelled-handoff', 'relaunch-failure', 'msi-reboot')) {
        $asset = $installer
        if ($scenario -eq 'msi-reboot') { $asset = [IO.Path]::ChangeExtension($installer, 'msi') }
        $helperPath = (& $executable generate $asset | Out-String).Trim()
        Assert-True ($LASTEXITCODE -eq 0) 'Production script generation failed'
        $generatedDirectory = Split-Path -Parent $helperPath
        $bytes = [IO.File]::ReadAllBytes($helperPath)
        Assert-True (($bytes[0..2] -join ',') -eq '239,187,191') 'PowerShell script lacks UTF-8 BOM'
        $tokens = $null
        $parseErrors = $null
        $ast = [Management.Automation.Language.Parser]::ParseFile($helperPath, [ref]$tokens, [ref]$parseErrors)
        Assert-True ($parseErrors.Count -eq 0) ('Invalid production script: ' + ($parseErrors | Out-String))
        $invalidFinally = @($ast.FindAll({ param($node)
            $node -is [Management.Automation.Language.CommandAst] -and $node.GetCommandName() -eq 'finally'
        }, $true))
        Assert-True ($invalidFinally.Count -eq 0) 'finally is not a control-flow block'
        $calls = [System.Collections.Generic.List[object]]::new()
        $global:SniffyUpdateTest = @{ Scenario = $scenario; Calls = $calls; Waited = $false; HelperPath = $helperPath; Notifications = 0 }
        if ($scenario -eq 'cancelled-handoff') { [IO.File]::WriteAllText($helperPath + '.cancel', '') }
        $global:LASTEXITCODE = 0
        $log = @(& $helperPath)
        $failed = $scenario -notin @('success', 'msi-reboot')
        Assert-True (($LASTEXITCODE -ne 0) -eq $failed) "$scenario returned incorrect status"
        Assert-True ($global:SniffyUpdateTest.Notifications -eq [int]$failed) "$scenario incorrect error notification"
        $aborted = $scenario -in @('parent-timeout', 'cancelled-handoff')
        Assert-True ($calls.Count -eq $(if ($aborted) { 0 } else { 2 })) "$scenario started unexpected processes"
        if (-not $aborted) {
            Assert-True ($calls[0].Verb -eq 'RunAs') "$scenario did not elevate only the installer"
            Assert-True ([string]::IsNullOrEmpty($calls[1].Verb)) "$scenario elevated the relaunched app"
            Assert-True ($calls[1].FilePath -eq $executable) "$scenario corrupted the application path"
            Assert-True ($calls[1].WorkingDirectory -eq $appDirectory) "$scenario changed the working directory"
            $installRoot = Split-Path -Parent $appDirectory
            $expectedArgs = '/S /D=' + $installRoot
            if ($scenario -eq 'msi-reboot') {
                $expectedArgs = '/i "' + $asset + '" /qn /norestart INSTALL_ROOT="' + $installRoot + '"'
            }
            Assert-True ($calls[0].Arguments -eq $expectedArgs) "$scenario corrupted installer arguments"
        }
        Assert-True (-not (Test-Path -LiteralPath ($helperPath + '.ready'))) "$scenario left stale readiness"
        Assert-True ((Test-Path -LiteralPath $helperPath) -eq $failed) "$scenario incorrect diagnostic script retention"
        Write-Output "PASS script: $scenario"
    }

    $shimDirectory = Join-Path $testRoot 'System32/WindowsPowerShell/v1.0'
    [IO.Directory]::CreateDirectory($shimDirectory) | Out-Null
    Copy-Item -LiteralPath $executable -Destination (Join-Path $shimDirectory 'powershell.exe')
    $env:SystemRoot = $testRoot
    foreach ($state in @('ready', 'startup-failure', 'preflight-failure')) {
        $env:SNIFFY_TEST_HELPER_STATE = $state
        $output = @(& $executable handoff $installer)
        $expectedExit = $(if ($state -eq 'ready') { 0 } else { 2 })
        Assert-True ($LASTEXITCODE -eq $expectedExit) "Handoff ${state}: unexpected quit/failure result: $output"
        Write-Output "PASS QProcess handoff: $state"
    }
    $env:SystemRoot = Join-Path $testRoot 'missing-shell'
    $output = @(& $executable handoff $installer)
    Assert-True ($LASTEXITCODE -eq 2) "Missing shell did not keep the app open: $output"
    Write-Output 'PASS QProcess handoff: missing-shell'
    $env:SystemRoot = $originalSystemRoot
    if ($PosixShell) {
        $helperPath = (& $executable generate-linux "/tmp/Installer's space & percent%.deb" | Out-String).Trim()
        Assert-True ($LASTEXITCODE -eq 0) 'Linux helper generation failed'
        & $PosixShell (Join-Path $PSScriptRoot 'test_app_update_linux.sh') $helperPath
        Assert-True ($LASTEXITCODE -eq 0) 'Linux helper regressions failed'
    }
} finally {
    $env:SystemRoot = $originalSystemRoot
    $env:PATH = $originalPath
    Remove-Variable SniffyUpdateTest -Scope Global -ErrorAction SilentlyContinue
    Remove-Item Env:SNIFFY_TEST_HELPER_STATE, Env:SNIFFY_TEST_NAME -ErrorAction SilentlyContinue
    if ($generatedDirectory -and $generatedDirectory.Contains('sniffy-updater-test-')) {
        Remove-Item -LiteralPath (Split-Path -Parent $generatedDirectory) -Recurse -Force -ErrorAction SilentlyContinue
    }
    Remove-Item -LiteralPath $testRoot -Recurse -Force -ErrorAction SilentlyContinue
}