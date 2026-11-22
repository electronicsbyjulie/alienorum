#ifndef ALIENORUM_ALIENORUMID_H
#define ALIENORUM_ALIENORUMID_H

#include <string_view>
#include "star.h"

namespace alienorum
{
    inline bool is_catalog_sun(const Star& star)
    {
        return star.seqno == 0 && (std::string_view(star.name) == "Sun" || std::string_view(star.name) == "Sol");
    }

    inline bool valid_alienorum_id(std::string_view id)
    {
        const auto digit = [](char c) { return c >= '0' && c <= '9'; };
        const auto first_space = id.find(' ');
        if (first_space == std::string_view::npos || first_space < 2) return false;
        const char color = id[first_space - 1];
        if (std::string_view("bcwyorp").find(color) == std::string_view::npos) return false;
        auto magnitude = id.substr(0, first_space - 1);
        if (magnitude == "V")
        {
            if (color == 'p') return false;
        }
        else if (magnitude.front() == '-')
        {
            magnitude.remove_prefix(1);
            if (magnitude.empty() || magnitude.size() > 2 || magnitude.front() == '0' ||
                !digit(magnitude.front()) || !digit(magnitude.back())) return false;
        }
        else if (magnitude.size() != 1 || !digit(magnitude.front())) return false;

        auto tail = id.substr(first_space + 1);
        if (tail.size() < 3) return false;
        constexpr std::string_view constellations =
            " And Ant Aps Aql Aqr Ara Ari Aur Boo CMa CMi CVn Cae Cam Cap Car Cas Cen Cep Cet Cha Cir Cnc Col Com CrA CrB Crt Cru Crv Cyg Del Dor Dra Equ Eri For Gem Gru Her Hor Hya Hyi Ind LMi Lac Leo Lep Lib Lup Lyn Lyr Men Mic Mon Mus Nor Oct Oph Ori Pav Peg Per Phe Pic PsA Psc Pup Pyx Ret Scl Sco Sct Ser Sex Sge Sgr Tau Tel TrA Tri Tuc UMa UMi Vel Vir Vol Vul ";
        const auto position = constellations.find(tail.substr(0, 3));
        if (position == std::string_view::npos || !position || position + 3 >= constellations.size() ||
            constellations[position - 1] != ' ' ||
            constellations[position + 3] != ' ') return false;
        tail.remove_prefix(3);
        if (tail.empty()) return true;
        if (tail.front() != ' ') return false;
        tail.remove_prefix(1);
        if (!tail.empty() && tail.front() >= '1' && tail.front() <= '9')
        {
            do { tail.remove_prefix(1); } while (!tail.empty() && digit(tail.front()));
            if (tail.empty()) return true;
            if (tail.front() != ' ') return false;
            tail.remove_prefix(1);
        }
        return tail.size() == 1 && tail.front() >= 'B' && tail.front() <= 'Z';
    }

    inline std::string_view shown_alienorum_id(const CelestialObject& object)
    {
        if (object.typeclass() != class_star) return {};
        const auto& star = static_cast<const Star&>(object);
        return valid_alienorum_id(star.alienorumid) || (star.alienorumid == "-26" && is_catalog_sun(star))
            ? std::string_view(star.alienorumid) : std::string_view();
    }
}

#endif
