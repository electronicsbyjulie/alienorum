
#include <iostream>
#include <cctype>
#include <set>
#include <memory>
#include <tuple>
#include "cat.h"
#include "serial.h"
#include "cons.h"

using namespace alienorum;

static bool is_alpha3(const char* s)
{
    return isalpha((unsigned char)s[0]) && isalpha((unsigned char)s[1]) && isalpha((unsigned char)s[2]);
}

static int skip_ws(const char* s, int i)
{
    while (isspace((unsigned char)s[i])) i++;
    return i;
}

// Bayer abbreviations include two-letter names and numbered forms such as Gam2Vel.
// Check the Greek prefix so ordinary object names still use the general name search.
static bool is_uppercase_short_greek(const char* s)
{
    return strlen(s) > 2 && isspace((unsigned char)s[2]) &&
        (!strncmp(s, "NU", 2) || !strncmp(s, "MU", 2) ||
         !strncmp(s, "PI", 2) || !strncmp(s, "XI", 2));
}

static bool looks_like_Bayer(const char* s)
{
    if (is_uppercase_short_greek(s)) return false;
    const int n = strlen(s);
    for (const auto &letter : Greek_letter)
    {
        const int prefix = std::min(3, (int)letter.size());
        if (n < prefix || strncasecmp(s, letter.c_str(), prefix))
        {
            continue;
        }
        int j = skip_ws(s, prefix);
        if (s[j] >= '1' && s[j] <= '9')
        {
            j++;
        }
        j = skip_ws(s, j);
        if (j + 3 <= n && is_alpha3(&s[j]) &&
            (j + 3 == n || isspace((unsigned char)s[j + 3])))
        {
            return true;
        }
    }
    return false;
}

// Equivalent to std::regex_search(s, R"([0-9]+\s*[A-Za-z]{3})"). [0-9]+ is greedy, but
// backtracking it can never help: any character exposed by giving back digits from a
// contiguous run is itself still a digit, which immediately fails the following
// (whitespace* then letter) check. So trying the maximal digit run at every digit
// position, with no backtracking, is exactly equivalent to the regex.
static bool looks_like_Flamsteed(const char* s)
{
    int n = strlen(s);
    for (int i=0; i<n; i++)
    {
        if (!isdigit((unsigned char)s[i])) continue;
        int j = i+1;
        while (j<n && isdigit((unsigned char)s[j])) j++;
        j = skip_ws(s, j);
        if (j+3<=n && is_alpha3(&s[j])) return true;
    }
    return false;
}

// Equivalent to std::regex_search(s, R"([0-9]+\s*G[.]?\s*[A-Za-z]{3})"), same reasoning
// as looks_like_Flamsteed for the digit run and whitespace runs; the optional '.' is a
// literal, so greedily consuming it when present (and not otherwise) is unambiguous.
static bool looks_like_Gould(const char* s)
{
    int n = strlen(s);
    for (int i=0; i<n; i++)
    {
        if (!isdigit((unsigned char)s[i])) continue;
        int j = i+1;
        while (j<n && isdigit((unsigned char)s[j])) j++;
        j = skip_ws(s, j);
        if (j>=n || s[j] != 'G') continue;
        j++;
        if (j<n && s[j] == '.') j++;
        j = skip_ws(s, j);
        if (j+3<=n && is_alpha3(&s[j])) return true;
    }
    return false;
}

static std::string search_key(const std::string &value)
{
    std::string key = value;
    for (char &c : key)
    {
        if (c >= 'A' && c <= 'Z') c += 0x20;
    }
    return key;
}

static std::string search_numbers(const std::string &value)
{
    std::string key;
    for (char c : value)
    {
        if (is_digit_or_dot(c)) key += c;
    }
    return key;
}

ExactStarSearchIndex::ExactStarSearchIndex(int first_star)
{
    for (int index = first_star; cels[index]; ++index)
    {
        if (cels[index]->typeclass() == class_star) add(index, *static_cast<Star*>(cels[index]));
    }
}

void ExactStarSearchIndex::add(int index, const Star &star)
{
    const auto record = [index](auto &table, const auto &key)
    {
        auto &entries = table[key];
        if (std::find(entries.begin(), entries.end(), index) == entries.end()) entries.push_back(index);
    };
    record(names, search_key(star.name));
    if (star.HD) record(aliases, stellar_alias_key("HD " + std::to_string(star.HD)));
    if (star.HIP) record(aliases, stellar_alias_key("HIP " + std::to_string(star.HIP)));
    for (const auto &alias : star.catalog_aliases) record(aliases, stellar_alias_key(alias));
    for (const std::string &alias : {star.local_name, std::string(star.Bayer),
        std::string(star.Flamsteed), star.alienorumid})
    {
        if (!alias.empty()) record(aliases, search_key(alias));
    }
    if (strlen(star.Gliese)) record(gliese, search_numbers(star.Gliese));
    if (star.BayerGrkno >= 0) record(greek, star.BayerGrkno);
    if (star.FlamsteedNo > 0) record(numbers, star.FlamsteedNo);
    if (star.GouldNo > 0) record(numbers, star.GouldNo);
    if (!star.gaia_wd_name.empty() || !star.WD.empty() || !wd_identifier_key(star.name).empty())
    {
        if (std::find(white_dwarfs.begin(), white_dwarfs.end(), index) == white_dwarfs.end()) white_dwarfs.push_back(index);
    }
}

std::vector<int> ExactStarSearchIndex::candidates(const char *term) const
{
    const std::string key = search_key(term);
    std::vector<int> result;
    const auto collect = [&result](const auto &table, const auto &value)
    {
        const auto found = table.find(value);
        if (found != table.end()) result.insert(result.end(), found->second.begin(), found->second.end());
    };
    // The reference search also accepts long display-name prefixes.
    for (auto entry = names.lower_bound(key); entry != names.end() && entry->first.compare(0, key.size(), key) == 0; ++entry)
    {
        result.insert(result.end(), entry->second.begin(), entry->second.end());
    }
    collect(aliases, key);
    collect(aliases, stellar_alias_key(term));
    collect(gliese, search_numbers(term));
    if (looks_like_Bayer(term)) collect(greek, grkno_from_abbrev(term));
    const int number = atoi(term);
    if (number) collect(numbers, number);
    if (key.compare(0, 2, "wd") == 0) result.insert(result.end(), white_dwarfs.begin(), white_dwarfs.end());
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

static int find_object_impl(const char* search_term, bool os, double ml, int levreq, const ExactStarSearchIndex *index)
{
    const bool indexed = index && os && levreq == 1;
    const std::vector<int> candidates = indexed ? index->candidates(search_term) : std::vector<int>();
    const auto object_at = [&](size_t position)
    {
        if (indexed) return position < candidates.size() ? candidates[position] : -1;
        return cels[position] ? static_cast<int>(position) : -1;
    };
    std::string gaia_term;
    if (strlen(search_term) >= 3 && !strncasecmp(search_term, "WD", 2))
    {
        for (const char *p = search_term; *p; p++)
        {
            if (*p > ' ')
            {
                gaia_term += (*p >= 'a' && *p <= 'z') ? *p - 'a' + 'A' : *p;
            }
        }
    }
    // A catalog's B1950 identifier outranks a published J2000 label with the same printed prefix.
    const std::string canonical_wd = gaia_term.compare(0, 3, "WDJ") == 0 ? "" : wd_identifier_key(gaia_term);
    if (!canonical_wd.empty())
    {
        int matched = -1;
        for (int cursor = 0, index; (index = object_at(cursor)) >= 0; ++cursor)
        {
            if (cels[index]->deleted || cels[index]->typeclass() != class_star) continue;
            if (wd_identifier_key(static_cast<Star*>(cels[index])->WD) != canonical_wd) continue;
            if (matched >= 0) return -1;
            matched = index;
        }
        if (matched >= 0) return matched;
    }
    const std::string color_identity = wd_color_gaia_identifier(gaia_term);
    if (!color_identity.empty()) gaia_term = color_identity;
    if (gaia_term.compare(0, 3, "WDJ") == 0 &&
        gaia_term.find_first_not_of("0123456789+-.", 3) == std::string::npos)
    {
        for (int cursor = 0, index; (index = object_at(cursor)) >= 0; ++cursor)
        {
            if (cels[index]->typeclass() != class_star)
            {
                continue;
            }
            const std::string &name = static_cast<Star*>(cels[index])->gaia_wd_name;
            const size_t sign = name.find_first_of("+-");
            if (name == gaia_term || (sign != std::string::npos &&
                name.substr(0, 7) + name.substr(sign, 5) == gaia_term))
            {
                return index;
            }
        }
        // A host imported before the color-association pass may still have only its legacy identity.
        // Explicit Gaia records retain priority, and conflicting or ambiguous legacy records do not match.
        if (!color_identity.empty())
        {
            int matched = -1;
            for (int cursor = 0, index; (index = object_at(cursor)) >= 0; ++cursor)
            {
                if (cels[index]->deleted || cels[index]->typeclass() != class_star) continue;
                const Star* star = static_cast<Star*>(cels[index]);
                if (!star->gaia_wd_name.empty()) continue;
                if (wd_color_gaia_identifier(star->WD, true) != color_identity
                    && (!star->WD.empty() || wd_color_gaia_identifier(star->name) != color_identity)
                    && wd_color_gaia_identifier(star->Gliese, true) != color_identity) continue;
                if (matched >= 0) return -1;
                matched = index;
            }
            return matched;
        }
        return -1;
    }
    if (strlen(search_term) >= 3 && !strncasecmp(search_term, "WD", 2))
    {
        std::string id;
        for (const char *p = search_term + 2; *p; p++)
        {
            if (*p > ' ')
            {
                id += *p;
            }
        }
        if (!id.empty() && id.find_first_not_of("0123456789+-.") == std::string::npos)
        {
            const std::string search_key = wd_identifier_key("WD" + id);
            if (search_key.empty()) return -1;
            int matched = -1;
            for (int cursor = 0, index; (index = object_at(cursor)) >= 0; ++cursor)
            {
                if (cels[index]->typeclass() != class_star)
                {
                    continue;
                }
                std::string name;
                for (const char *p = cels[index]->name; *p; p++)
                {
                    if (*p > ' ')
                    {
                        name += *p;
                    }
                }
                if (wd_identifier_key(static_cast<Star*>(cels[index])->WD) == search_key ||
                    wd_identifier_key(name) == search_key)
                {
                    if (matched >= 0) return -1;
                    matched = index;
                }
            }
            return matched;
        }
    }
    int i, m, n, termi = atoi(search_term);
    Star *s;

    // if (!strcmp(search_term, cels[0]->name)) return 0;

    uint32_t is_hd  = ((search_term[0]&0x5f) == 'H' && (search_term[1]&0x5f) == 'D') ? atoi(&search_term[2]) : 0,
        is_hip = ((search_term[0]&0x5f) == 'H' && (search_term[1]&0x5f) == 'I' && (search_term[2]&0x5f) == 'P') ? atoi(&search_term[3]) : 0;
    int result = -1;
    const char *match_cons = nullptr;
    char match_comp = 0;
    n = strlen(search_term);
    if (n > 2 && search_term[n-2] == ' '
        && ((search_term[n-1] >= 'A' && search_term[n-1] <= 'Z') || (search_term[n-1] >= 'a' && search_term[n-1] <= 'z')))
        match_comp = search_term[n-1];

    if (is_hd && is_hd <= MAX_HD && hdcache && hdcache[is_hd] && (os || (match_comp < 'a')))   // if comp is lower case and a star is not required, we might be looking for a planet.
    {
        s = hdcache[is_hd];
        if (match_comp)
        {
            // A component letter was explicitly asked for. Only ever return it if it actually
            // exists as its own registered member.
            Star *b = s->multisys ? s->multisys->get_member(match_comp) : nullptr;
            if (b) return b->seqno;
        }
        else return s->seqno;
    }
    if (is_hip && hipcache && is_hip <= MAX_HIP && hipcache[is_hip] && (os || (match_comp < 'a')))
    {
        s = hipcache[is_hip];
        if (match_comp)
        {
            Star *b = s->multisys ? s->multisys->get_member(match_comp) : nullptr;
            if (b) return b->seqno;
        }
        else return s->seqno;
    }
    if ((is_hd || is_hip) && (os || !match_comp))
    {
        // Cached companions can exist before their system membership is rebuilt.
        // Reuse an exact component name instead of constructing a second copy.
        if (match_comp)
        {
            for (int cursor = 0, index; (index = object_at(cursor)) >= 0; ++cursor)
            {
                if (!cels[index]->deleted && cels[index]->typeclass() == class_star &&
                    !strcmp(cels[index]->name, search_term)) return index;
            }
        }
        for (int cursor = 0, object; (object = object_at(cursor)) >= 0; ++cursor)
        {
            if (cels[object]->deleted || cels[object]->typeclass() != class_star) continue;
            const Star* star = static_cast<Star*>(cels[object]);
            if (!match_comp && ((is_hd && star->HD == is_hd) || (is_hip && star->HIP == is_hip))) return object;
        }
        return -1;
    }

    bool is_gliese = (((search_term[0]&0x5f) == 'G' && (search_term[1]&0x5f) == 'L' && (search_term[2] <= '9'))
        || ((search_term[0]&0x5f) == 'G' && (search_term[1]&0x5f) == 'L' && (search_term[2]&0x5f) == 'I' && (search_term[3]&0x5f) == 'E' && (search_term[4]&0x5f) == 'S' && (search_term[5]&0x5f) == 'E')
        || ((search_term[0]&0x5f) == 'G' && (search_term[1]&0x5f) == 'J' && (search_term[2] <= '9'))
        || ((search_term[0]&0x5f) == 'W' && (search_term[1]&0x5f) == 'O' && (search_term[2] <= '9'))
        || ((search_term[0]&0x5f) == 'W' && (search_term[1]&0x5f) == 'O' && (search_term[2]&0x5f) == 'O' && (search_term[3]&0x5f) == 'L' && (search_term[4]&0x5f) == 'L' && (search_term[5]&0x5f) == 'E')
        || ((search_term[0]&0x5f) == 'N' && (search_term[1]&0x5f) == 'N' && (search_term[2] <= '9') && (search_term[3] <= '9'))
        ) && contains_digits_or_dots(search_term);

    if ((search_term[0]&0x5f) == 'W' && (search_term[1]&0x5f) == 'O' && (search_term[2]&0x5f) == 'L' && (search_term[3]&0x5f) == 'F')
        is_gliese = false;                      // AND IT COULDN'T BE MORE FALSE. WOLF != WOLLEY, GOT IT DISMAL COMPUTER?

    int co = n-3;
    if (match_comp) co -= 2;
    if (co>1 && search_term[co-1] <= '9' && ((search_term[co] >= 'A' && search_term[co] <= 'Z') || (search_term[co] >= 'a' && search_term[co] <= 'z'))
        && ((search_term[co+1] >= 'A' && search_term[co+1] <= 'Z') || (search_term[co+1] >= 'a' && search_term[co+1] <= 'z'))
        && ((search_term[co+2] >= 'A' && search_term[co+2] <= 'Z') || (search_term[co+2] >= 'a' && search_term[co+2] <= 'z'))
        )
        match_cons = &search_term[co];

    Constellation *cons2match = nullptr;
    n = constellations.size();
    if (match_cons)
    {
        for (i=0; !cons2match && i<n; i++)
        {
            if (!strcasecmp(match_cons, constellations[i].abbrev.c_str())
                || !strncasecmp(match_cons, constellations[i].name.c_str(), 3))
            {
                cons2match = &constellations[i];
            }
        }
    }

    bool is_Bayer = ((search_term[0] >= 'A' && search_term[0] <= 'Z') || (search_term[0] >= 'a' && search_term[0] <= 'z'))
        ? looks_like_Bayer(search_term) : false;
    bool is_Flamsteed = (search_term[0] >= '0' && search_term[0] <= '9')
        ? looks_like_Flamsteed(search_term) : false;
    bool is_Gould = (search_term[0] >= '0' && search_term[0] <= '9')
        ? looks_like_Gould(search_term) : false;
    const int bayer_index = is_Bayer ? grkno_from_abbrev(search_term) : -1;

    // These constellation-scoped maps are O(log n) lookups, so try them before paying
    // for the full linear scan below.
    if (cons2match && is_Bayer)
    {
        const auto entry = cons2match->Bayer_stars.find(bayer_index);
        if (entry != cons2match->Bayer_stars.end())
        {
            Star *match = entry->second;
            if (match_comp && match_comp != 'A')
            {
                match = match->multisys ? match->multisys->get_member(match_comp) : nullptr;
            }
            if (match && !match->deleted)
            {
                return match->seqno;
            }
        }
    }

    if (cons2match && is_Flamsteed)
    {
        i = atoi(search_term);
        if (cons2match->Flamsteed_stars.find(i) != cons2match->Flamsteed_stars.end())
            return cons2match->Flamsteed_stars[i]->seqno;
    }

    if (cons2match && is_Gould)
    {
        i = atoi(search_term);
        if (cons2match->Gould_stars.find(i) != cons2match->Gould_stars.end())
            return cons2match->Gould_stars[i]->seqno;
    }

    for (int cursor = 0; (i = object_at(cursor)) >= 0; ++cursor)
    {
        if (cels[i]->deleted) continue;
        if (os && (cels[i]->typeclass() != class_star)) continue;
        Star* s = (Star*)cels[i];
        if (!strcmp(cels[i]->name, search_term)
            || (cels[i]->typeclass() == class_star && s->alienorumid.size() && !strcmp(s->alienorumid.c_str(), search_term)))
        {
            result = i;
            break;
        }
        if (cels[i]->typeclass() == class_star)
        {
            for (const std::string& alias : {s->local_name, std::string(s->Bayer), std::string(s->Flamsteed)})
            {
                if (!alias.empty() && !strcasecmp(alias.c_str(), search_term)) return i;
            }
        }
        if (match_cons && cels[i]->typeclass() == class_star
            && (    (s->constellation[0] & 0x5f) != (match_cons[0] & 0x5f)
                ||  (s->constellation[1] & 0x5f) != (match_cons[1] & 0x5f)
                ||  (s->constellation[2] & 0x5f) != (match_cons[2] & 0x5f)
                )) continue;
        if (match_comp)
        {
            m = strlen(cels[i]->name);
            if (cels[i]->name[m-2] == ' ' && cels[i]->name[m-1] != match_comp) continue;
            if (match_comp != 'A' && cels[i]->name[m-2] != ' ') continue;
        }
        if (cels[i]->typeclass() == class_star)
        {
            if (s->apparent_magnitude > ml) continue;
            if (is_Bayer && match_cons && s->BayerGrkno == bayer_index)
            {
                return i;
            }
            if (is_gliese && has_same_numbers(s->Gliese, search_term))
            {
                result = i;
                break;
            }
        }
    }
    if (is_gliese)
    {
        return result;
    }
    // Uppercase variable designations must not fall back to a Greek name differing in case.
    if (is_uppercase_short_greek(search_term)) return result;
    if (is_Bayer && match_cons)
    {
        // A catalog identifier must not turn into a different Greek letter through fuzzy matching.
        return result;
    }

    if (match_cons && termi)
    {
        // TODO: put Bayer search in here too.
        if (result < 0 && !is_Gould) for (int cursor = 0; (i = object_at(cursor)) >= 0; ++cursor)
        {
            if (cels[i]->deleted) continue;
            s = (cels[i]->typeclass() == class_star) ? ((Star*)cels[i]) : nullptr;
            if (!s) continue;
            if (!s->matches_constellation(match_cons)) continue;
            if (termi == s->FlamsteedNo)
            {
                if (match_comp)
                {
                    m = strlen(cels[i]->name);
                    if (cels[i]->name[m-2] == ' ' && cels[i]->name[m-1] != match_comp) continue;
                    if (match_comp != 'A' && cels[i]->name[m-2] != ' ') continue;
                }
                result = i;
            }
        }
        if (result < 0) for (int cursor = 0; (i = object_at(cursor)) >= 0; ++cursor)
        {
            if (cels[i]->deleted) continue;
            s = (cels[i]->typeclass() == class_star) ? ((Star*)cels[i]) : nullptr;
            if (!s) continue;
            if (!s->matches_constellation(match_cons)) continue;
            if (termi == s->GouldNo)
            {
                if (match_comp)
                {
                    m = strlen(cels[i]->name);
                    if (cels[i]->name[m-2] == ' ' && cels[i]->name[m-1] != match_comp) continue;
                    if (match_comp != 'A' && cels[i]->name[m-2] != ' ') continue;
                }
                result = i;
            }
        }

        if (os) return result;
    }

    // Catalog callers pass zero to disable fuzzy matching. In particular, the
    // first solar-system pass encounters moon centers not yet in the catalog.
    // No edit distance can pass that limit, so skip the full alias scan.
    if (levreq <= 0) return result;

    // With an exclusive limit of one, only zero distance can match. Avoid
    // allocating an edit-distance matrix for every alias in the catalog.
    const auto search_distance = [&levreq](const std::string &a, const std::string &b)
    {
        if (levreq != 1) return Damerau_Levenshtein(a, b);
        if (a.size() != b.size()) return 1;
        for (size_t index = 0; index < a.size(); ++index)
        {
            const char x = a[index] >= 'A' && a[index] <= 'Z' ? a[index] + 0x20 : a[index];
            const char y = b[index] >= 'A' && b[index] <= 'Z' ? b[index] + 0x20 : b[index];
            if (x != y) return 1;
        }
        return 0;
    };
    char buffer[256];
    if (result < 0)
    {
        std::string lookstr = search_term;
        int looklen = strlen(search_term);
        for (int cursor = 0; (i = object_at(cursor)) >= 0; ++cursor)
        {
            if (cels[i]->deleted) continue;
            s = (cels[i]->typeclass() == class_star) ? ((Star*)cels[i]) : nullptr;
            if (os && !s) continue;
            if (match_comp)
            {
                m = strlen(cels[i]->name);
                if (cels[i]->name[m-2] == ' ' && cels[i]->name[m-1] != match_comp) continue;
                if (match_comp != 'A' && cels[i]->name[m-2] != ' ') continue;
            }
            if (match_cons && s
                && (    (s->constellation[0] & 0x5f) != (match_cons[0] & 0x5f)
                    ||  (s->constellation[1] & 0x5f) != (match_cons[1] & 0x5f)
                    ||  (s->constellation[2] & 0x5f) != (match_cons[2] & 0x5f)
                )) continue;

            if (s && strlen(s->Gliese)                    // HOW MANY TIMES DO I HAVE TO BEAT THIS INTO YOU, COMPUTER.
                && (lookstr[0]&0x5f) == 'W' && (lookstr[1]&0x5f) == 'O' && (lookstr[2]&0x5f) == 'L' && (lookstr[3]&0x5f) == 'F'
                && has_same_numbers(s->Gliese, lookstr.c_str()))
                continue;

            if (!cels[i]->namelen) cels[i]->namelen = strlen(cels[i]->name);
            memcpy(buffer, cels[i]->name, cels[i]->namelen);
            buffer[cels[i]->namelen] = 0;
            if (levreq > 1 && looklen > 0.666 * cels[i]->namelen)
            {
                buffer[looklen] = 0;
            }

            int lev = (!s || has_same_numbers(cels[i]->name, lookstr.c_str())) 
                    ? search_distance(buffer, lookstr)
                    : 1e9;

            if (s)
            {
                int lev1;
                if (s->local_name.size())
                {
                    lev1 = search_distance(s->local_name, lookstr);
                    if (lev1 < lev) lev = lev1;
                }
                
                for (const auto& alias : s->catalog_aliases)
                {
                    if (!has_same_numbers(alias.c_str(), lookstr.c_str())) continue;
                    lev1 = search_distance(alias, lookstr);
                    if (lev1 < lev) lev = lev1;
                }

                // Bayer check
                lev1 = has_same_numbers(s->Bayer, lookstr.c_str()) 
                    ? search_distance(s->Bayer, lookstr)
                    : 1e9;
                if (lev1 < lev) lev = lev1;
                
                // Flamsteed check
                lev1 = has_same_numbers(s->Flamsteed, lookstr.c_str()) 
                    ? search_distance(s->Flamsteed, lookstr)
                    : 1e9;
                if (lev1 < lev) lev = lev1;
            }

            if (lev < levreq)
            {
                levreq = lev;
                result = i;
                if (!lev) break;
            }
        }
    }

    #if 0
    if (result >= 0)
        std::cout << search_term << " = " << cels[result]->name << " with lev " << levreq << std::endl << std::flush;
    else
        std::cout << "This steaming pile of a search function has dismally failed yet again to identify " <<
            search_term << " with a Damerau-Levenshtein requirement of " << levreq << "." << std::endl << std::flush;
    #endif

    return result;
}

int find_object(const char *term, bool stars, double limit, int distance, const ExactStarSearchIndex *index)
{
    const auto aliases = stellar_alias_matches(term);
    // Published system letters are not internal companion slots.
    if (aliases.size() == 1 && aliases.front()->stellar_system.is_object())
    {
        Star* match = aliases.front();
        const int object = match->seqno;
        if (object >= 0 && object < ncelobjs && cels[object] == match && !match->deleted) return object;
    }
    // Resolve exact identities before considering any approximate display-name match.
    const int exact = find_object_impl(term, stars, limit, distance > 0 ? 1 : 0, index);
    if (exact >= 0) return exact;
    if (!stars)
    {
        const std::string query = trim(term);
        if (query.size() > 2 && query[query.size() - 2] == ' ' && query.back() >= 'b' && query.back() <= 'z')
        {
            const std::string host_name = query.substr(0, query.size() - 2);
            const int host = find_object(host_name.c_str(), true, limit, 1, index);
            if (host >= 0)
            {
                int planet = -1;
                for (int object = 0; cels[object]; ++object)
                {
                    if (cels[object]->deleted || cels[object]->typeclass() != class_planet) continue;
                    const Planet* candidate = static_cast<Planet*>(cels[object]);
                    if (!candidate->orbit || candidate->orbit->center != cels[host]) continue;
                    const std::string designation = !candidate->display_designation.empty() ? candidate->display_designation
                        : (candidate->origname.empty() ? candidate->name : candidate->origname);
                    if (designation.size() < 2 || designation.back() != query.back()) continue;
                    if (planet >= 0) return -1;
                    planet = object;
                }
                // A known host with no such planet must not match another system.
                return planet;
            }
        }
    }
    if (aliases.empty()) return distance <= 1 ? exact : find_object_impl(term, stars, limit, distance, index);
    const std::string wd_key = wd_identifier_key(term);
    if (!wd_key.empty())
    {
        // A rejected canonical WD designation remains rejected even when a
        // source lists that designation as an alias of one candidate.
        for (int object = 0; cels[object]; ++object)
        {
            if (cels[object]->deleted || cels[object]->typeclass() != class_star) continue;
            const Star *match = static_cast<Star*>(cels[object]);
            if (wd_identifier_key(match->WD) == wd_key || wd_identifier_key(match->name) == wd_key) return -1;
        }
    }
    int result = -1;
    for (Star *match : aliases)
    {
        const int object = match->seqno;
        if (object < 0 || object >= ncelobjs || cels[object] != match || match->deleted) continue;
        if (result >= 0 && result != object) return -1;
        result = object;
    }
    return result;
}

bool Serialization::save_one(std::fstream& fs, CelestialObject *cel, json &allobjs)
{
    Star *s;

    std::string key = "";
    key = std::string(cel->name);
    CelestialObject *cursor = cel;
    int depth = 0;
    while (cursor->orbit && cursor->orbit->center && depth++ < 100)
    {
        cursor = cursor->orbit->center;
        key = std::string(cursor->name) + std::string(".") + key;
    }

    const char* l = key.c_str();

    switch (cel->typeclass())
    {
        case class_galaxy:
        allobjs[l] = ((Galaxy*)cel)->to_json();
        break;

        case class_star:
        s = (Star*)cel;
        s->gotta_be_named_something();                            // I am sick of these massive-flaring stars with no massive-flaring names!
        allobjs[l] = s->to_json();
        break;

        case class_planet:
        allobjs[l] = ((Planet*)cel)->to_json();
        break;

        case class_moon:
        allobjs[l] = ((Moon*)cel)->to_json();
        break;

        case class_comet:
        allobjs[l] = ((Comet*)cel)->to_json();
        break;

        case class_satellite:
        allobjs[l] = ((Satellite*)cel)->to_json();
        break;

        default:
        std::cerr << "Attempted to save CelestialObject " << cel->name << " of blank or unknown type class." << std::endl;
    }

    return true;
}

bool Serialization::save_all(std::fstream& fs, CelestialObject **cels, bool oe)
{
    try
    {
        int i;
        json allobjs;
        for (i=0; cels[i]; i++)
        {
            if (cels[i]->deleted) continue;
            if (oe && !cels[i]->user_edited) continue;

            save_one(fs, cels[i], allobjs);
            if (oe && cels[i]->orbit && cels[i]->orbit->center && !cels[i]->orbit->center->user_added)
            {
                CelestialObject *cen = cels[i]->orbit->center;
                while (1)
                {
                    save_one(fs, cen, allobjs);
                    if (!cen->orbit) break;
                    if (!cen->orbit->center) break;
                    cen = cen->orbit->center;
                }
            }
        }

        fs << allobjs.dump(4);
        return true;
    }
    catch (...)
    {
        std::cerr << "FAILED to save universe file." << std::endl;
        return false;
    }
}

namespace
{
    struct UniverseRecordDeleter
    {
        void operator()(CelestialObject* object) const
        {
            if (!object) return;
            // Orbit ownership differs between subclasses; clear it before their destructors run.
            delete object->orbit;
            object->orbit = nullptr;
            delete object;
        }
    };
    using UniverseRecord = std::unique_ptr<CelestialObject, UniverseRecordDeleter>;

    bool read_universe_record(CelestialObject* object, const json& record)
    {
        switch (object->typeclass())
        {
            case class_galaxy: return static_cast<Galaxy*>(object)->from_json(record);
            case class_star: return static_cast<Star*>(object)->from_json(record);
            case class_planet: return static_cast<Planet*>(object)->from_json(record);
            case class_moon: return static_cast<Moon*>(object)->from_json(record);
            case class_comet: return static_cast<Comet*>(object)->from_json(record);
            case class_satellite: return static_cast<Satellite*>(object)->from_json(record);
            default: return false;
        }
    }

    UniverseRecord make_universe_object(int object_class)
    {
        switch (object_class)
        {
            case class_galaxy: return UniverseRecord(new Galaxy());
            case class_star: return UniverseRecord(new Star());
            case class_planet: return UniverseRecord(new Planet());
            case class_moon: return UniverseRecord(new Moon());
            case class_comet: return UniverseRecord(new Comet());
            case class_satellite: return UniverseRecord(new Satellite());
            default: throw std::runtime_error("Unknown object class");
        }
    }
}

bool Serialization::load_all(std::fstream& fs, CelestialObject **cels, unsigned int max, bool aua)
{
    try
    {
        if (!cels || !max) throw std::runtime_error("Invalid universe storage");
        json allobj;
        fs >> allobj;
        // Older empty saves contain null instead of an empty object.
        if (allobj.is_null()) return true;
        if (!allobj.is_object()) throw std::runtime_error("Universe root must be an object");
        unsigned int count = 0;
        while (count < max && cels[count]) ++count;
        if (count == max) throw std::runtime_error("Universe storage has no terminating slot");
        ncelobjs = count;

        struct PendingRecord
        {
            json data;
            UniverseRecord parsed;
            CelestialObject* target = nullptr;
            CelestialObject* parent = nullptr;
            int index = -1;
        };
        std::vector<PendingRecord> pending;
        using Identity = std::tuple<cel_obj_class, std::string, std::string>;
        std::map<Identity, CelestialObject*> identities;
        for (unsigned int i = 0; i < count; ++i)
            identities.emplace(Identity(cels[i]->typeclass(), cels[i]->origname, cels[i]->origcenname), cels[i]);
        std::map<CelestialObject*, int> existing_indices;
        for (unsigned int i = 0; i < count; ++i) existing_indices[cels[i]] = i;
        std::set<CelestialObject*> replacements;
        unsigned int additions = 0;

        // Parse every record before changing live objects. Their addresses remain stable on merge.
        for (const auto& record : allobj.items())
        {
            const json& data = record.value();
            if (!data.is_object() || !data.contains("!name") || !data.at("!name").is_string()
                || !data.contains("typeclass") || !data.at("typeclass").is_number_integer())
                throw std::runtime_error("Object record requires a name and an integer class");
            const std::string name = data.at("!name").get<std::string>();
            if (name.empty() || name.size() >= name_max_len || name.find('\0') != std::string::npos)
                throw std::runtime_error("Invalid object name");
            if (data.contains("orbit") && !data.at("orbit").is_object())
                throw std::runtime_error("Orbit must be an object");
            PendingRecord entry;
            entry.data = data;
            entry.parsed = make_universe_object(data.at("typeclass").get<int>());
            if (!read_universe_record(entry.parsed.get(), data))
                throw std::runtime_error("Invalid object record");
            const cel_obj_class cls = entry.parsed->typeclass();
            const cel_obj_type type = entry.parsed->type;
            if ((cls == class_star && type != star) || (cls == class_galaxy && type != galaxy)
                || ((cls == class_planet || cls == class_moon)
                    && (type == star || type == galaxy || type == icy_tailed || type == artificial)))
                throw std::runtime_error("Object type conflicts with its class");
            if (entry.parsed->orbit && entry.parsed->orbit->center_name.empty())
                throw std::runtime_error("Orbit requires a parent name");
            const std::string original = data.value("!origname", name);
            const std::string center_name = data.value("!origcenname",
                entry.parsed->orbit ? entry.parsed->orbit->center_name : std::string());
            entry.data["!origname"] = entry.parsed->origname = original;
            entry.data["!origcenname"] = entry.parsed->origcenname = center_name;
            const auto match = identities.find(Identity(cls, original, center_name));
            entry.target = match == identities.end() ? entry.parsed.get() : match->second;
            if (entry.target != entry.parsed.get())
            {
                if (!replacements.insert(entry.target).second)
                    throw std::runtime_error("Multiple records replace the same object");
                entry.index = existing_indices.at(entry.target);
            }
            else ++additions;
            pending.push_back(std::move(entry));
        }
        if (additions >= max - count) throw std::runtime_error("Universe object limit exceeded");

        std::map<std::string, std::vector<CelestialObject*>> names, exact_names;
        const auto add_name = [&](const std::string& name, CelestialObject* object)
        {
            if (!name.empty())
            {
                exact_names[name].push_back(object);
                names[stellar_alias_key(name)].push_back(object);
            }
        };
        for (unsigned int i = 0; i < count; ++i)
        {
            if (cels[i]->deleted && !replacements.count(cels[i])) continue;
            add_name(cels[i]->name, cels[i]);
            add_name(cels[i]->origname, cels[i]);
        }
        for (const auto& entry : pending)
        {
            add_name(entry.parsed->name, entry.target);
            add_name(entry.parsed->origname, entry.target);
            if (entry.parsed->typeclass() == class_star)
            {
                const Star* star = static_cast<Star*>(entry.parsed.get());
                for (const auto& alias : star->catalog_aliases) add_name(alias, entry.target);
                for (const std::string& alias : {star->local_name, std::string(star->Bayer),
                    std::string(star->Flamsteed), std::string(star->Gliese)}) add_name(alias, entry.target);
                if (star->HD) add_name("HD " + std::to_string(star->HD), entry.target);
                if (star->HIP) add_name("HIP " + std::to_string(star->HIP), entry.target);
            }
        }
        ExactStarSearchIndex lookup;
        CatalogReader catalogs;
        std::map<CelestialObject*, CelestialObject*> parents;
        const auto hierarchy_type = [](const CelestialObject* object)
        {
            // Unknown planetary surface codes do not change the body's place in the hierarchy.
            return object->typeclass() == class_planet || object->typeclass() == class_moon ? rocky : object->type;
        };
        for (auto& entry : pending)
        {
            Orbit* orbit = entry.parsed->orbit ? entry.parsed->orbit : entry.target->orbit;
            if (!orbit) continue;
            const std::string& name = orbit->center_name;
            // Stellar component B and planet b can share a folded key. Preserve an exact spelling.
            const auto exact = exact_names.find(name);
            const auto found = names.find(stellar_alias_key(name));
            const auto* candidates = exact != exact_names.end() ? &exact->second
                : (found != names.end() ? &found->second : nullptr);
            if (candidates)
            {
                for (CelestialObject* candidate : *candidates)
                {
                    if (candidate == entry.target || !compare_types(hierarchy_type(candidate), hierarchy_type(entry.parsed.get()))) continue;
                    if (entry.parent && entry.parent != candidate)
                        throw std::runtime_error("Ambiguous orbit parent: " + name);
                    entry.parent = candidate;
                }
            }
            if (!entry.parent && cels == ::cels)
            {
                // Explicit saves can reference hosts omitted by deferred exoplanet loading.
                // Resolve aliases first, then import only the referenced host from local catalogs.
                entry.parent = catalogs.resolve_saved_host(name, max - additions, &lookup);
            }
            if (!entry.parent) throw std::runtime_error("Cannot resolve orbit parent: " + name);
            parents[entry.target] = entry.parent;
        }

        // Validate the proposed graph, including existing ancestors, before installing any links.
        std::set<CelestialObject*> checked;
        for (const auto& entry : pending)
        {
            std::set<CelestialObject*> path;
            CelestialObject* cursor = entry.target;
            while (cursor && !checked.count(cursor))
            {
                if (!path.insert(cursor).second) throw std::runtime_error("Circular orbit hierarchy");
                const auto next = parents.find(cursor);
                cursor = next != parents.end() ? next->second : (cursor->orbit ? cursor->orbit->center : nullptr);
            }
            checked.insert(path.begin(), path.end());
        }

        std::vector<int> loaded;
        for (auto& entry : pending)
        {
            const bool is_new = entry.target == entry.parsed.get();
            if (is_new)
            {
                entry.index = ncelobjs++;
                cels[entry.index] = entry.parsed.release();
                cels[ncelobjs] = nullptr;
            }
            else read_universe_record(entry.target, entry.data);
            CelestialObject* object = entry.target;
            object->deleted = false;
            object->user_edited = aua;
            object->estimated_poles = true;
            object->seqno = entry.index;
            object->namelen = strlen(object->name);
            if (object->orbit) object->orbit->center = entry.parent;
            object->cenobj = nullptr;
            if (object->typeclass() == class_moon) static_cast<Moon*>(object)->orbit_type = ot_equatorial;
            loaded.push_back(entry.index);
        }

        // All parents now exist, regardless of record order. Establish system roots before deriving locations.
        for (int index : loaded)
        {
            CelestialObject* object = cels[index];
            object->cenobj = object;
            while (object->cenobj->orbit && object->cenobj->orbit->center
                && object->cenobj->orbit->center->typeclass() != class_galaxy)
                object->cenobj = object->cenobj->orbit->center;
            if (object->typeclass() == class_star && object->cenobj->typeclass() == class_star)
            {
                Star* member = static_cast<Star*>(object);
                Star* root = static_cast<Star*>(object->cenobj);
                if (member->stellar_system.is_object() && root->stellar_system.is_object() &&
                    member->stellar_system.at("system") == root->stellar_system.at("system"))
                {
                    if (!root->multisys)
                    {
                        root->multisys = new StarMulti();
                        root->multisys->add_member(root, 'A');
                    }
                    const char slot = member->stellar_system.value("slot", static_cast<int>(
                        member == root ? 'A' : root->multisys->next_available()));
                    if (slot >= 'A' && slot <= 'Z') root->multisys->add_member(member, slot);
                }
            }
            if ((object->typeclass() == class_planet || object->typeclass() == class_moon)
                && !uses_rocky_map(object->type) && !uses_gaseous_map(object->type))
            {
                static_cast<Planet*>(object)->classify();
            }
            if (object->typeclass() == class_moon && object->user_added)
                static_cast<Moon*>(object)->enforce_orbit_clearance(true);
            if (object->typeclass() != class_planet) continue;
            static_cast<Planet*>(object)->refresh_host_display_designation();
        }
        std::set<CelestialObject*> updated;
        for (int index : loaded)
        {
            std::vector<CelestialObject*> path;
            CelestialObject* object = cels[index];
            while (object && updated.insert(object).second)
            {
                path.push_back(object);
                object = object->orbit ? object->orbit->center : nullptr;
            }
            for (auto it = path.rbegin(); it != path.rend(); ++it)
            {
                object = *it;
                switch (object->typeclass())
                {
                    case class_star: static_cast<Star*>(object)->update_location(J2000_TIME_T); break;

                    case class_planet: static_cast<Planet*>(object)->update_location(J2000_TIME_T); break;

                    case class_moon: static_cast<Moon*>(object)->update_location(J2000_TIME_T); break;
                    default: break;
                }
            }
        }
        Planet::rebuild_host_statistics();
        std::lock_guard<std::mutex> lock(mtx);
        loading_msg = "Loaded " + std::to_string(loaded.size()) + " universe objects.";
        return true;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAILED to load universe: " << error.what() << std::endl;
        return false;
    }
    catch (...)
    {
        std::cerr << "FAILED to load universe: invalid object data." << std::endl;
        return false;
    }
}
