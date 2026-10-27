#pragma once
#include <cstddef>
#include <filesystem>
#include <string>

namespace jd::labels {
// Loads the game's extracted English fallback, then the requested native locale.
// Publishes one immutable dictionary; lookups remain valid during reloads.
void Load(const std::filesystem::path& directory, const std::string& language = "ru_ru");
void RequestLanguage(const std::string& language);
void PumpLanguage(const std::filesystem::path& directory);
std::string Language();
std::string Ui(const std::string& key);
std::size_t Count();
std::string Resolve(const std::string& nativeText);
std::string City(const std::string& tokenOrNativeName);
std::string Company(const std::string& nativeName, const std::string& cityNativeOrToken);
std::string Trailer(const std::string& unitPath, const std::string& nativeName,
                    const std::string& bodyToken);
} // namespace jd::labels
