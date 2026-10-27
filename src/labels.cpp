#include "labels.hpp"
#include <mutex>
#include <algorithm>
#include <atomic>
#include <fstream>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace jd::labels {
namespace {
using Dictionary = std::unordered_map<std::string, std::string>;
std::shared_ptr<const Dictionary> dictionary = std::make_shared<const Dictionary>();
std::mutex language_mutex;
std::string current_language="en_gb", requested_language;
void ReadFile(const std::filesystem::path& path, Dictionary& target) {
    std::ifstream file(path, std::ios::binary);
    std::string line;
    bool first = true;
    while (std::getline(file, line)) {
        if (first && line.compare(0, 3, "\xef\xbb\xbf") == 0) line.erase(0, 3);
        first = false;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto tab = line.find('\t');
        if (tab == std::string::npos || !tab) continue;
        target[line.substr(0, tab)] = line.substr(tab + 1);
    }
}
std::string Get(const Dictionary& words, const std::string& key) {
    const auto found = words.find(key);
    return found == words.end() ? std::string{} : found->second;
}
std::string ResolveWith(const Dictionary& words, const std::string& original) {
    std::string text = original;
    std::unordered_set<std::string> seen;
    for (unsigned depth = 0; depth < 8; ++depth) {
        if (!seen.insert(text).second) return original;
        std::string result;
        std::size_t position = 0;
        bool changed = false;
        while (position < text.size()) {
            const auto start = text.find("@@", position);
            if (start == std::string::npos) { result.append(text, position, std::string::npos); break; }
            result.append(text, position, start - position);
            const auto end = text.find("@@", start + 2);
            if (end == std::string::npos) { result.append(text, start, std::string::npos); break; }
            const auto found = words.find(text.substr(start + 2, end - start - 2));
            if (found == words.end()) result.append(text, start, end + 2 - start);
            else { result += found->second; changed = changed || found->second != text.substr(start, end + 2 - start); }
            position = end + 2;
        }
        if (!changed || result == text) return result;
        text = std::move(result);
    }
    return text;
}
std::vector<std::string> Split(const std::string& text) {
    std::vector<std::string> result;
    std::size_t begin = 0;
    do {
        const auto end = text.find('.', begin);
        result.push_back(text.substr(begin, end == std::string::npos ? end : end - begin));
        if (end == std::string::npos) break;
        begin = end + 1;
    } while (begin < text.size());
    return result;
}
std::string CityWith(const Dictionary& words, const std::string& input) {
    if (input.find("@@") != std::string::npos) return ResolveWith(words, input);
    std::string key = input;
    if (key.compare(0, 5, "city.") == 0) key.erase(0, 5);
    const auto translated = Get(words, key);
    return translated.empty() ? input : ResolveWith(words, translated);
}
bool Technical(const std::string& text) {
    return text.compare(0, 12, "trailer_def.") == 0 || text.compare(0, 8, "trailer.") == 0 ||
           text.find('/') != std::string::npos || text.find('\\') != std::string::npos;
}
std::string BodyKey(const std::string& token) {
    // Aliases connect SCS body_type tokens to actual SCS localization keys.
    // Values are keys only: translated text is never authored in this helper.
    static const std::unordered_map<std::string, std::string> keys{
        {"curtainside", "body_curtain"}, {"curtain", "body_curtain"},
        {"dryvan", "body_dryvan"}, {"dryvan_m", "body_dryvan_mfloor"},
        {"refrigerated", "body_refrigerated"}, {"reefer", "body_reefer"},
        {"insulated", "body_insulated"}, {"container", "body_container"},
        {"flatbed", "tn_flat_bed"}, {"flatbed_brck", "body_flatbed_brick"},
        {"flatbed_cont", "body_flatbed_container"}, {"dumper", "body_kipper"},
        {"chemtank", "tn_chemical_cistern"}, {"foodtank", "tn_food_cistern"},
        {"cisternfood", "tn_food_cistern"}, {"fueltank", "tn_fuel_cistern"},
        {"gastank", "body_gas_tank"}, {"silo", "body_silo"},
        {"livestock", "tn_livestock"}, {"log", "tn_log_trailer"},
        {"slidepost", "tn_log_trailer"}, {"lowbed", "body_lowbed"},
        {"lowloader", "body_lowloader"}, {"lowboy", "body_lowloader"},
        {"car_trans", "tn_car_transporter"}, {"truck_trans", "tn_truck_transporter"}
    };
    const auto found = keys.find(token);
    return found == keys.end() ? std::string{} : found->second;
}
} // namespace

void Load(const std::filesystem::path& directory, const std::string& language) {
    auto words = std::make_shared<Dictionary>();
    ReadFile(directory / "game-labels.en_gb.tsv", *words);
    if (language != "en_gb" && language.find_first_not_of("abcdefghijklmnopqrstuvwxyz_") == std::string::npos)
        ReadFile(directory / ("game-labels." + language + ".tsv"), *words);
    ReadFile(directory / "ui-labels.en_gb.tsv", *words);
    if(language!="en_gb"&&language.find_first_not_of("abcdefghijklmnopqrstuvwxyz_")==std::string::npos)
        ReadFile(directory / ("ui-labels."+language+".tsv"),*words);
    std::shared_ptr<const Dictionary> ready = std::move(words);
    std::atomic_store_explicit(&dictionary, std::move(ready), std::memory_order_release);
    std::lock_guard<std::mutex> lock(language_mutex);current_language=language;
}
void RequestLanguage(const std::string& language) {
    if(language.empty()||language.find_first_not_of("abcdefghijklmnopqrstuvwxyz_")!=std::string::npos)return;
    std::lock_guard<std::mutex> lock(language_mutex);if(language!=current_language)requested_language=language;
}
void PumpLanguage(const std::filesystem::path& directory) {
    std::string language;
    {std::lock_guard<std::mutex> lock(language_mutex);language.swap(requested_language);}
    if(!language.empty())Load(directory,language);
}
std::string Language(){std::lock_guard<std::mutex> lock(language_mutex);return current_language;}
std::string Ui(const std::string& key){return Resolve("@@"+key+"@@");}
std::size_t Count() { return std::atomic_load_explicit(&dictionary, std::memory_order_acquire)->size(); }
std::string Resolve(const std::string& text) {
    const auto words = std::atomic_load_explicit(&dictionary, std::memory_order_acquire);
    return ResolveWith(*words, text);
}
std::string City(const std::string& input) {
    const auto words = std::atomic_load_explicit(&dictionary, std::memory_order_acquire);
    return CityWith(*words, input);
}
std::string Company(const std::string& nativeName, const std::string& nativeCity) {
    const auto words = std::atomic_load_explicit(&dictionary, std::memory_order_acquire);
    const auto company = ResolveWith(*words, nativeName);
    const auto city = CityWith(*words, nativeCity);
    if (company.empty()) return city;
    if (city.empty()) return company;
    return company + " — " + city;
}
std::string Trailer(const std::string& path, const std::string& nativeName, const std::string& bodyToken) {
    const auto words = std::atomic_load_explicit(&dictionary, std::memory_order_acquire);
    std::string label = ResolveWith(*words, nativeName);
    if (label == path || Technical(label) || label.find("@@") != std::string::npos) label.clear();
    const auto parts = Split(path);
    if (label.empty() && parts.size() > 2) {
        label = Get(*words, "tn_" + parts[1] + "_" + parts[2]);
        // The native path uses schwmuller; the shipped listing key uses schw.
        if (label.empty() && parts[1] == "schwmuller")
            label = Get(*words, "tn_schw_" + (parts[2] == "cisternfood" ? "foodtank" : parts[2]));
    }
    if (label.empty()) {
        for (const auto& token : {bodyToken, parts.empty() ? std::string{} : parts.back(),
                                 parts.size() > 2 ? parts[2] : std::string{}}) {
            if (token.empty()) continue;
            for (const auto& key : {BodyKey(token), "body_" + token, "tn_" + token}) {
                if (key.empty()) continue;
                label = Get(*words, key);
                if (!label.empty()) break;
            }
            if (!label.empty()) break;
        }
        if (!label.empty() && parts.size() > 1 && parts[1] != "scs") {
            const auto brand = Get(*words, "brand_" + parts[1]);
            if (!brand.empty() && label.find(brand) == std::string::npos) label = brand + " — " + label;
        }
    }
    if (label.empty()) return nativeName.empty() ? path : ResolveWith(*words, nativeName);
    label = ResolveWith(*words, label);
    for (const auto& part : parts) {
        std::string configuration = Get(*words, "tn_" + part);
        if (configuration.empty() && part.compare(0, 7, "single_") == 0) {
            std::size_t end = 7;
            while (end < part.size() && part[end] >= '0' && part[end] <= '9') ++end;
            if (end > 7 && end == part.size()) configuration = Get(*words, "tn_single_" + part.substr(7) + "axle");
        }
        if (!configuration.empty() && label.find(configuration) == std::string::npos) {
            label += " — " + ResolveWith(*words, configuration);
            break;
        }
    }
    return label;
}
} // namespace jd::labels
