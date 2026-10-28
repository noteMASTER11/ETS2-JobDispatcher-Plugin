#pragma once
#include <string>
#include <functional>
struct ID3D11Device;
struct ID3D11DeviceContext;
namespace jd::preview {
void Pump(const std::function<bool()>& cancelled); // Worker: prepare/load the latest selection only.
void Suspend(); // Render thread: cancel pending work when the overlay is hidden.
void ReloadIndex(); // Background worker, while the dispatcher is gated for preparation.
bool HasModel(const std::string& token);
void Draw(ID3D11Device*, ID3D11DeviceContext*, const std::string& token, float width, float height, bool interactive);
void ReleaseGpu();
}
