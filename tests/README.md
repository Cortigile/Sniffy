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
History navigation also checks the top-slider arrow buttons, their placement and end-of-history
enabled states, and synchronization with the Adv tab controls.
The adjacent capture-position label is checked before the first capture, during live acquisition
and history browsing, after explicit stop/resume, and on single-capture completion.
The signal-position slider is checked against its 33-pixel background image to prevent
vertical cropping and keep the groove and zoom handle within the widget.
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

## Frequency analysis

Configure with the same Qt/toolchain settings as the application:

```powershell
cmake -S sniffy -B build-tests -DSNIFFY_BUILD_FREQUENCY_ANALYSIS_TESTS=ON
cmake --build build-tests --target frequencyanalysis_engine_tests frequencyanalysis_tests
ctest --test-dir build-tests --output-on-failure -R frequencyanalysis
```

The numerical tests use synthetic signals, including off-bin tones, DC offsets,
positive and negative aliases, skew correction, and cross-spectrum averaging.
Coherence-derived one-sigma standard errors are checked analytically, against
seeded independent-noise scatter, and for unavailable or extreme inputs; the
actual averaging count is checked across captures and averager resets.
Planner tests cover automatic record geometry, safe captures below the planning
period target, memory/rate limits, and transport/observed-overhead timing estimates.
The offscreen module tests exercise the upload/readback/acquisition sequence,
cancellation, malformed or clipped captures, versioned configuration persistence,
firmware-backed pin choices and conflicts, standard widget controls, linked plot
cursors with linear/log-frequency interpolation in separate and combined dual-axis
plots, SI frequency units with exactly three decimal places for A/B and zero/negative
differences, the View tab's display controls during idle and running sweeps, session persistence,
CH1 trace/cursor colors, minor grids, manual multi-digit editing, zero-decimal
integer controls matching the counter's sample-count dials, board-backed voltage limits, the 512-period and 10-second limits,
ms/s settling units and session compatibility, aliasing enabled by default, stable
timing labels during learned-overhead updates, and capability gating without a connected board.
Uncertainty tests cover the View toggle and its session compatibility, CH1 shaded
three-sigma bands on linear/logarithmic and separate/combined plots, axis ranges and legends,
phase wrapping, unavailable-estimate gaps, measured averaging-count provenance,
the compact Data table, CSV standard errors and agent-state three-sigma half-widths.
The shared ADC input catalog is checked through both its parser and the voltmeter
consumer, including one to four acquisition slots, shared-ADC/default-pin mappings,
internal inputs, optional framing and transactional rejection of malformed responses.

Run the numerical target on its own for a fast DSP-only check. The module target
needs the same Qt, compiler-runtime, libusb, and ST-Link DLL directories on
`PATH` as the application. Physical loopback and DUT measurements are still
required to characterize DAC/ADC bandwidth, channel skew, settling, harmonic
aliasing, absolute voltage calibration, and behavior on each board/firmware.

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