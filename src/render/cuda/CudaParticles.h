#pragma once
// CUDA particle simulation. Identical model to shaders/particles_sim.comp.
#include "render/RenderParams.h"
#include <string>

namespace bh::cuda {
/// True if a CUDA device is usable; fills the device name.
bool available(std::string* deviceName = nullptr);
/// Launch the simulation kernel on `devParticles` (Particle[pp.count]).
void simulateParticles(void* devParticles, const ParticleParams& pp);
/// Block until all queued CUDA work finished.
void syncDevice();
}  // namespace bh::cuda

namespace bh::cuda {
/// Make the CUDA device whose UUID matches the given 16-byte Vulkan device UUID current.
bool selectDeviceByUuid(const unsigned char* uuid16);
}
