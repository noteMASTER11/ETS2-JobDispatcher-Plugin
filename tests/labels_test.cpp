#include "labels.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void Equal(const std::string& actual, const std::string& expected) {
    if (actual != expected) throw std::runtime_error("Expected: " + expected + "\nActual: " + actual);
}
void Write(const std::filesystem::path& path, const std::string& contents) {
    std::ofstream output(path, std::ios::binary);
    output << contents;
    output.close();
    if (!output) throw std::runtime_error("Cannot write fixture");
}
}
int main(int argc, char** argv) {
    try {
        if (argc != 3) throw std::runtime_error("Usage: labels_test ASSETS D_FIXTURE_DIRECTORY");
        const std::filesystem::path assets(argv[1]), fixture(argv[2]);
        jd::labels::Load(assets);
        if (jd::labels::Count() < 12000) throw std::runtime_error("Native catalogue incomplete");
        Equal(jd::labels::Resolve("@@cn_alu_ingots@@"), u8"Алюминиевые слитки");
        Equal(jd::labels::City("berlin"), u8"Берлин");
        Equal(jd::labels::City("city.berlin"), u8"Берлин");
        Equal(jd::labels::City("@@berlin@@"), u8"Берлин");
        Equal(jd::labels::Company("EuroGoodies", "berlin"), u8"EuroGoodies — Берлин");
        Equal(jd::labels::Company("EuroGoodies", ""), "EuroGoodies");
        Equal(jd::labels::Trailer("trailer_def.krone.boxliner.single_3.container", "", "container"),
              u8"Контейнеровоз Krone Box Liner — 3 оси");
        Equal(jd::labels::Trailer("trailer_def.scs.box.single_3.curtain", "", "curtain"),
              u8"Шторный прицеп — 3 оси");
        Equal(jd::labels::Trailer("trailer_def.schwmuller.curtain.single_3.curtain", "", "curtain"),
              u8"Шторный Schwarzmüller — 3 оси");
        Equal(jd::labels::Trailer("trailer_def.unknown", "@@body_curtain@@", ""), u8"Шторный прицеп");
        Equal(jd::labels::Resolve("Literal @@missing_key@@"), "Literal @@missing_key@@");
        Equal(jd::labels::City("mod_city_unknown"), "mod_city_unknown");
        Equal(jd::labels::Trailer("trailer_def.unknown", "", "unmapped"), "trailer_def.unknown");
        jd::labels::Load(assets, "en_gb");
        Equal(jd::labels::City("berlin"), "Berlin");
        unsigned locales=0;
        for(const auto& file:std::filesystem::directory_iterator(assets)){
            const auto filename=file.path().filename().string();
            if(filename.rfind("ui-labels.",0)!=0||file.path().extension()!=".tsv")continue;
            const auto language=filename.substr(10,filename.size()-14);
            jd::labels::RequestLanguage(language);jd::labels::PumpLanguage(assets);
            Equal(jd::labels::Language(),language);
            for(const auto* key:{"jd_title","jd_auto","jd_immediate","jd_surprise","jd_create","jd_offer","jd_route_failed","jd_stop","jd_detach","jd_incompatible","jd_real_time","hours_short","minutes_short","job_income","body_cargo","cb_trailer","source_company","target_company","route_length","cargo_mass","cargo_waiting1","mp_job_loading_placement_not_found"}){
                const auto text=jd::labels::Ui(key);
                if(text.empty()||text.find("@@")!=std::string::npos)throw std::runtime_error(language+" missing UI key "+key);
            }
            ++locales;
        }
        if(locales!=43)throw std::runtime_error("Expected all 43 installed game locales");
        jd::labels::Load(assets,"ru_ru");Equal(jd::labels::Ui("jd_immediate"),u8"Поместить груз сразу");
        jd::labels::Load(assets,"en_gb");
        jd::labels::Load(assets, "../ru_ru");
        Equal(jd::labels::City("berlin"), "Berlin");

        std::filesystem::create_directories(fixture);
        Write(fixture / "game-labels.en_gb.tsv",
              "\xef\xbb\xbf" "base\tBase\r\nnested\t@@base@@\ncycle_a\t@@cycle_b@@\ncycle_b\t@@cycle_a@@\nempty\t\n");
        Write(fixture / "game-labels.ru_ru.tsv", u8"base\tОснова\n");
        jd::labels::Load(fixture);
        Equal(jd::labels::Resolve("@@nested@@ / @@base@@"), u8"Основа / Основа");
        Equal(jd::labels::Resolve("@@cycle_a@@"), "@@cycle_a@@");
        Equal(jd::labels::Resolve("@@empty@@"), "");
        Equal(jd::labels::Resolve("unclosed @@base"), "unclosed @@base");
        Equal(jd::labels::Resolve("@@cn_alu_ingots@@"), "@@cn_alu_ingots@@");
        jd::labels::Load(fixture, "missing_locale");
        Equal(jd::labels::Resolve("@@nested@@"), "Base");
        std::cout << "Native label, trailer, fallback, reload and cycle checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
