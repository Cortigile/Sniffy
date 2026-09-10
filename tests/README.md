# Updater Regression Tests

Run from the repository root in Windows PowerShell 5.1 with matching Qt/MinGW toolchains.
Set `$qtRoot` and `$mingwRoot` to their installation directories; optionally set `$bashExecutable`
to Git for Windows Bash to include Linux helper tests.

```powershell
& ./tests/test_app_update_handoff.ps1 -QtRoot $qtRoot -MinGWRoot $mingwRoot -PosixShell $bashExecutable
```

[test_app_update_handoff.ps1](test_app_update_handoff.ps1) compiles the production updater generator
and checks Windows helper behavior and process handoff. With `-PosixShell`, it also runs
[test_app_update_linux.sh](test_app_update_linux.sh) against the generated Linux helper.

Use trusted source and toolchains, without administrator rights. Installation, authorization, and relaunch
are mocked, not sandboxed. Real installations, Linux Qt process launching, and polkit dialogs require
separate platform testing.