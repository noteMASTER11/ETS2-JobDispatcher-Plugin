#pragma once
#include <cstdint>
namespace jd::preview::abi {
// ETS2 1.61.1.9 x64 only. The dispatcher verifies the entire executable hash
// before these RVAs are used. See docs/NATIVE-PREVIEW.md for the call chain.
constexpr std::uintptr_t load_prefab=0x33d0a0, find_control=0x385c60;
constexpr std::uintptr_t widget_vtable=0x2397560;
constexpr std::uintptr_t initialize=0x1168880, update=0xff3f10;
constexpr std::uintptr_t set_string=0xf2f60, configure_camera=0xff3340, select_camera=0xff3c10;
constexpr std::uintptr_t window_vtable=0x222df08;
// config_path is a polymorphic string_t at +0x118. Its char* member is +0x120
// and is used by the file loader, but must NOT be passed to string assignment.
constexpr std::uintptr_t string_vtable=0x21d29f0, config_path_string=0x118;
constexpr std::uintptr_t draw=0x11691b0, assign_vehicle=0x1168c50;
constexpr std::uintptr_t create_srv=0x2b8ac0, free_unit=0xfbfa0;
constexpr std::uintptr_t render_global=0x36af808, graphics_global=0x3682920;
constexpr std::uintptr_t world_global=0x36af7c8;
constexpr std::uintptr_t texture_pool=0x1a75fa0, texture_count=0x1a75fa8;
constexpr std::uintptr_t image_pool=0x363fe60, image_count=0x363fe68;
constexpr std::uintptr_t d3d_device=0x363fd18;
}
