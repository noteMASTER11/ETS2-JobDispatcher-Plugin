#include "labels.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
int main(int argc, char** argv) {
    try {
        if (argc != 2) return 2;
        unsigned count = 0;
        for (const auto& entry : std::filesystem::directory_iterator(argv[1])) {
            auto name = entry.path().filename().string();
            if (name.rfind("ui-labels.", 0) != 0 || entry.path().extension() != ".tsv") continue;
            auto language = name.substr(10, name.size()-14);
            jd::labels::Load(argv[1], language);
            for (const char* key : {"jd_title", "jd_auto", "jd_immediate", "jd_surprise", "jd_create", "jd_offer", "jd_route_failed", "jd_stop", "jd_detach", "jd_incompatible", "jd_real_time"}) {
                auto text = jd::labels::Ui(key);
                if (text.empty() || text == key || text.find("@@") != std::string::npos)
                    throw std::runtime_error(language + " missing " + key);
            }
            ++count;
        }
        if (count != 43) throw std::runtime_error("Expected 43 UI locales");
        std::cout << "All 43 UI locales passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
