#ifndef ALIENORUM_PLANET_NAMES_H
#define ALIENORUM_PLANET_NAMES_H
#include "planet.h"
#include <set>
#include <cctype>

namespace alienorum
{
    inline std::string next_planet_name(const CelestialObject& host, CelestialObject* const* objects)
    {
        std::set<std::string> occupied;
        const auto remember = [&](const std::string& source)
        {
            if (source.empty()) return;
            const size_t separator = source.find_last_of(" _");
            std::string suffix = separator == std::string::npos ? "" : source.substr(separator+1);
            if (suffix.empty() && source.back() >= 'b' && source.back() <= 'z'
                && source.size() > 1 && !std::islower(static_cast<unsigned char>(source[source.size()-2])))
                suffix = source.back();
            if (suffix.empty()) return;
            for (char c : suffix) if (c < 'a' || c > 'z') return;
            occupied.insert(suffix);
        };
        for (int i = 0; objects && objects[i]; ++i)
        {
            const CelestialObject* object = objects[i];
            if (object->deleted || object->typeclass() != class_planet || !object->orbit || object->orbit->center != &host) continue;
            const Planet* planet = static_cast<const Planet*>(object);
            remember(planet->name);
            remember(planet->origname);
            remember(planet->catalog_designation);
            remember(planet->display_designation);
        }
        for (unsigned int index = 1; ; ++index)
        {
            unsigned int value = index+1;
            std::string suffix;
            do
            {
                --value;
                suffix.insert(suffix.begin(), char('a'+value%26));
                value /= 26;
            } while (value);
            if (occupied.count(suffix)) continue;
            std::string base = host.name;
            const size_t limit = name_max_len-2-suffix.size();
            if (base.size() > limit)
            {
                size_t end = limit;
                while (end && (static_cast<unsigned char>(base[end])&0xc0) == 0x80) --end;
                base.resize(end);
            }
            return base+" "+suffix;
        }
    }
}
#endif
