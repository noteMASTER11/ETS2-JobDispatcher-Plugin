#pragma once
#include "dispatch.hpp"
struct ID3D11Device;
struct ID3D11DeviceContext;
namespace jd::preview {
void Suspend(); // Render thread: queues disposal on the next game callback.
void ReleaseGpu();
void Frame(); // SCS frame callback: owns all native UI and vehicle calls.
void Shutdown(); // Same thread as the SCS shutdown callback.
void DrawNative(ID3D11Device*, const Request&, float width, float height, bool interactive);
void QueueOrbit(float horizontal_delta,float vertical_delta,float wheel_steps);
void PrepareWindowClose(); // Window owner: release input references before game input teardown.
}
