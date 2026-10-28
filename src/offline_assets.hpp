#pragma once
#include <filesystem>
#include <functional>
#include <string>
namespace jd::offline {
std::filesystem::path PackageRoot();
bool Available();
std::wstring Quote(const std::wstring& argument);
bool Run(const std::filesystem::path& package, const std::filesystem::path& game,
         const std::filesystem::path& data, const std::filesystem::path& game_home,
         const std::function<bool()>& cancelled, const std::string& preview_token = {});
std::filesystem::path GameDirectory();
std::filesystem::path GameHome();
}
