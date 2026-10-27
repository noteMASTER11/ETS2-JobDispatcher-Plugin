# Dependencies

- Dear ImGui 1.93.0 — https://github.com/ocornut/imgui ; MIT, license in licenses/.
- MinHook 1.3.4 — https://github.com/TsudaKageyu/minhook ; BSD, license in licenses/.
- SCS Telemetry SDK headers provide the plugin ABI.
- ConverterPIX — https://github.com/mwl4/ConverterPIX , pinned commit 3cd4e73a86d0c6bd28e117664c50a36cabeccf38. Separate offline conversion executable, LGPL-3.0. The release includes its unmodified executable, corresponding source archive in runtime/sources/ConverterPIX-source.zip, and license texts in licenses/. Its code is not linked into the plugin. The upstream source archive is retained without modification, including its third-party notices.
- CPython 3.13.1 Windows x64 embeddable distribution — https://www.python.org/downloads/release/python-3131/ . Redistributed unchanged in runtime/python/, including LICENSE.txt. An additional copy of that license is in licenses/Python-LICENSE.txt. No system Python installation or package manager is required.

Preview geometry, textures and native localization tables are derived from this computer's installed game resources. They are used locally by this plugin. The game executable and TruckersMP binaries are not included.
