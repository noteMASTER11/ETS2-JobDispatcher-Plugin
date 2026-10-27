# Dependencies

- Dear ImGui 1.93.0 — https://github.com/ocornut/imgui ; MIT, license in licenses/.
- MinHook 1.3.4 — https://github.com/TsudaKageyu/minhook ; BSD, license in licenses/.
- SCS Telemetry SDK headers provide the plugin ABI.
- ConverterPIX — https://github.com/mwl4/ConverterPIX , pinned commit 3cd4e73a86d0c6bd28e117664c50a36cabeccf38. Separate offline conversion executable, LGPL-3.0; license copied to licenses/ConverterPIX-LICENSE.txt. Its code is not linked into the plugin.

Preview geometry, textures and native localization tables are derived from this computer's installed game resources. They are used locally by this plugin. The game executable and TruckersMP binaries are not included.
