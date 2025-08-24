#ifndef CRTZ_LANG_HPP
#define CRTZ_LANG_HPP

#include <string>

// Forward declare Program
struct Program;

namespace CRTZ {

    // Run a .crtz script directly from file
    void runScript(const std::string& filename, const std::string& playerName);

    // Run a .crtz script from in-memory string
    void runSource(const std::string& source, const std::string& playerName);

} // namespace CRTZ

#endif // CRTZ_LANG_HPP


#pragma once
#include <string>
#include <cctype>

// Trims leading and trailing whitespace from a string
inline std::string trim(const std::string& s) {
    size_t a = 0;
    while(a < s.size() && std::isspace((unsigned char)s[a])) ++a;
    size_t b = s.size();
    while(b > a && std::isspace((unsigned char)s[b-1])) --b;
    return s.substr(a, b-a);
}