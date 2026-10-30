# Regression Tests

## Oscilloscope

Configure with the same Qt/toolchain settings as the application:

```powershell
cmake -S sniffy -B build-tests -DSNIFFY_BUILD_SCOPE_TESTS=ON
cmake --build build-tests --target scope_tests
ctest --test-dir build-tests --output-on-failure -R scope_tests
```

The Qt, compiler-runtime, libusb, and ST-Link DLL directories must be on `PATH` on
Windows. Tests run offscreen without a connected board and cover the 128-capture
limit, raw-data preservation, rolling averages and eviction, sampling/geometry
resets, complete-frame validation, history navigation, and acquisition pause/resume.
Frames include the firmware's trailing `S_OK` marker. The default live-acquisition
test checks startup with averaging off, unaveraged sample values, and the next-capture
command after each received frame.
Cursor tests cover chart hit-testing, both drag directions, plot-edge clamping,
control synchronization, voltage scale/offset conversion, and panning away from cursors.
They also check the extra significant
digit in difference/frequency readings, SI prefixes, and equal-cursor frequency handling.
Stable-trigger noise reduction and physical stop/resume timing still need board testing.

## Voltmeter

Configure with the optional Qt Test target, using the same Qt/toolchain settings as the application:

```powershell
cmake -S sniffy -B build-tests -DSNIFFY_BUILD_VOLTMETER_TESTS=ON
cmake --build build-tests --target voltmeter_tests
ctest --test-dir build-tests --output-on-failure -R voltmeter_tests
```

The Qt, compiler-runtime, libusb, and ST-Link DLL directories must be on `PATH` when running
on Windows. The tests use Qt's offscreen platform, require no connected board, and cover
configuration compatibility, session and mode restoration, input-selection limits, sampling
counts, memory-limited period options, and reference/multimode capture sequencing.
Voltage tests inject synthetic measurements; physical ADC settling and accuracy still need board testing.

## Signal / PWM generator

```powershell
cmake -S sniffy -B build-tests -DSNIFFY_BUILD_ARBGENERATOR_TESTS=ON
cmake --build build-tests --target arbgenerator_tests
ctest --test-dir build-tests --output-on-failure -R arbgenerator_tests
```

Use the application's Qt/toolchain settings and put Qt, compiler-runtime, libusb,
and ST-Link DLL directories on `PATH` on Windows. The offscreen tests cover linear
and exponential saw/up-down sweep values, endpoints and per-leg timing, independent
channel updates, CH1-master synchronization, the shared firmware sampling-frequency
command, timer shutdown, vertical Sweep-tab layout, and saved-session compatibility.
No connected board is required. Actual phase stability and DAC/PWM timing still
require hardware measurements.

## Pattern generator

Configure with the same Qt/toolchain settings as the application:

```powershell
cmake -S sniffy -B build-tests -DSNIFFY_BUILD_PATTERNGENERATOR_TESTS=ON
cmake --build build-tests --target patterngenerator_tests
ctest --test-dir build-tests --output-on-failure -R patterngenerator_tests
```

The Qt, compiler-runtime, libusb, and ST-Link DLL directories must be on `PATH` on
Windows. Tests run offscreen without a connected board and cover user-defined grid
edits across length changes, restoration of temporarily hidden ticks, pattern
switching, reset of visible and hidden edits without changing the tick count, and
unchanged regeneration of counter, binary, and Gray-code patterns.

## Updater

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