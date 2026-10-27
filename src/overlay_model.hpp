#pragma once
#include "dispatch.hpp"
#include <algorithm>
#include <cctype>
#include <string>

namespace jd::overlay::model {
inline bool Contains(const std::vector<Choice>& choices, std::uint64_t id) {
    return id != 0 && std::any_of(choices.begin(), choices.end(),
                               [id](const Choice& c) { return c.id == id; });
}
inline std::uint64_t FirstExcept(const std::vector<Choice>& choices, std::uint64_t excluded) {
    for (const auto& item : choices)
        if (item.id && item.id != excluded) return item.id;
    return 0;
}
inline void Normalize(const Snapshot& snapshot, Request& request) {
    // Both accepted pickup jobs and offers in the company's market use company trailers.
    request.owned_trailer = false;
    if (!snapshot.world_ready) {
        request.cargo = request.source = request.destination = request.trailer = 0;
        return;
    }
    if (!Contains(snapshot.cargos, request.cargo)) request.cargo = FirstExcept(snapshot.cargos, 0);
    // Zero is an intentional auto-compatible choice, resolved by the game adapter.
    // A stale manual choice returns to Auto instead of forcing an arbitrary trailer.
    if (request.trailer && !Contains(snapshot.trailers, request.trailer)) request.trailer = 0;
    if (!Contains(snapshot.companies, request.source))
        request.source = Contains(snapshot.companies, snapshot.nearest_company)
                             ? snapshot.nearest_company : FirstExcept(snapshot.companies, 0);
    if (!Contains(snapshot.companies, request.destination) || request.source == request.destination)
        request.destination = FirstExcept(snapshot.companies, request.source);
}
inline bool CanSubmit(const Snapshot& snapshot, const Request& request) {
    return snapshot.supported && snapshot.world_ready && snapshot.can_generate && !snapshot.busy &&
           Contains(snapshot.cargos, request.cargo) &&
           Contains(snapshot.companies, request.source) &&
           Contains(snapshot.companies, request.destination) && request.source != request.destination &&
           !request.owned_trailer &&
           (request.trailer == 0 || Contains(snapshot.trailers, request.trailer));
}
inline std::string FoldSearch(const std::string& input) {
    std::string result;
    result.reserve(input.size());
    for (std::size_t index = 0; index < input.size(); ++index) {
        const auto first = static_cast<unsigned char>(input[index]);
        if (first < 0x80) {
            result += static_cast<char>(first >= 'A' && first <= 'Z' ? first + 32 : first);
        } else if (first >= 0xc2 && first <= 0xdf && index + 1 < input.size() &&
                   (static_cast<unsigned char>(input[index + 1]) & 0xc0) == 0x80) {
            unsigned code = ((first & 0x1f) << 6) | (static_cast<unsigned char>(input[++index]) & 0x3f);
            // Native Russian names include Ё (U+0401), outside А..Я.
            // Also handle Latin-1 accents used in company and city names.
            if ((code >= 0x0410 && code <= 0x042f) ||
                (code >= 0x00c0 && code <= 0x00de && code != 0x00d7)) code += 0x20;
            else if (code >= 0x0400 && code <= 0x040f) code += 0x50;
            result += static_cast<char>(0xc0 | (code >> 6));
            result += static_cast<char>(0x80 | (code & 0x3f));
        } else {
            result += static_cast<char>(first);
        }
    }
    return result;
}
inline bool Matches(const Choice& choice, const char* filter) {
    const auto needle = FoldSearch(filter ? filter : "");
    const auto haystack = FoldSearch(choice.label + " " + choice.token);
    return haystack.find(needle) != std::string::npos;
}
} // namespace jd::overlay::model
