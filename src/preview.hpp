#pragma once
#include <string>
struct ID3D11Device;
struct ID3D11DeviceContext;
namespace jd::preview {
void Pump(); // Existing background worker: index/one selected mesh only.
bool HasModel(const std::string& token);
void Draw(ID3D11Device*, ID3D11DeviceContext*, const std::string& token, float width, float height, bool interactive);
void ReleaseGpu();
}
