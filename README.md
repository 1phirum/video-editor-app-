# Cut Pro

A Windows video editor.

**Download:** grab the latest installer from the
[Releases](https://github.com/1phirum/video-editor-app-/releases/latest) page
and run `CutPro-<version>-Setup.exe`. Existing installs upgrade in place, and
the app updates itself from this page.

Source code is in `python-app/`: C++/Qt 6, QML, Python tools, tests, and
CMake/installer scripts. Build outputs, installers, media, caches, downloaded
reference artwork, private credentials, and model weights are excluded.

Build instructions are in `python-app/CMakeLists.txt`. Install a Qt 6 kit
with ShaderTools and LinguistTools and its matching C/C++ compiler, CMake,
and Ninja. CMake fetches the pinned whisper.cpp source on first configuration.
Provide the FFmpeg 9 shared build through `CUTPRO_FFMPEG_ROOT` (the default is
`ffmpeg-9.0.1-full_build-shared` beside `python-app`). For speech cleanup,
place the externally obtained `std.rnnn` model in
`python-app/assets/models/noise-removal/`; upstream links and license notices
are included in that folder.

Account-pool OAuth secrets are supplied at runtime through
`CUTPRO_ANTIGRAVITY_CLIENT_SECRET` and `CUTPRO_GEMINI_CLI_CLIENT_SECRET`.
Set the variables required by your account provider before starting the app;
keep their values outside Git. The optional long-media regression tests read
`CUTPRO_LONG_MEDIA_TEST_FILE`, which should identify the original regression
video when those tests are run.

