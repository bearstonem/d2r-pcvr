// fx_source.h: read a ReShade effect the way ReShade does, with its own feature headers (#include "D2R_*.fxh", beside it, recursive) pasted where
// they stand, so the shader tests can cut functions out of the whole effect after it was split into headers (D2R_Sky.fxh, D2R_Fog.fxh ...).
// Other includes are left as they are.
#pragma once
#include <fstream>
#include <iterator>
#include <set>
#include <stdexcept>
#include <string>

namespace fxsource {
inline std::string ReadWithIncludes(const std::string& path, std::set<std::string>* open = nullptr) {
    std::set<std::string> local;
    if (!open) open = &local;
    if (!open->insert(path).second) throw std::runtime_error("include cycle at " + path);
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("cannot read " + path);
    const std::string text{std::istreambuf_iterator<char>(file), {}};
    const std::string dir = path.substr(0, path.find_last_of("/\\") + 1);
    std::string out;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t eol = text.find('\n', pos);
        if (eol == std::string::npos) eol = text.size();
        const std::string line = text.substr(pos, eol - pos);
        const size_t i = line.find_first_not_of(" \t");
        bool pasted = false;
        if (i != std::string::npos && line.compare(i, 8, "#include") == 0) {
            const size_t q1 = line.find('"', i), q2 = q1 == std::string::npos ? q1 : line.find('"', q1 + 1);
            if (q2 != std::string::npos) {
                const std::string name = line.substr(q1 + 1, q2 - q1 - 1);
                if (name.compare(0, 4, "D2R_") == 0 && name.find('/') == std::string::npos && name.find('\\') == std::string::npos) {
                    out += ReadWithIncludes(dir + name, open);
                    if (out.empty() || out.back() != '\n') out += '\n';
                    pasted = true;
                }
            }
        }
        if (!pasted) { out += line; if (eol < text.size()) out += '\n'; }
        pos = eol + 1;
    }
    open->erase(path);
    return out;
}
}  // namespace fxsource
