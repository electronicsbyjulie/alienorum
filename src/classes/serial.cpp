
#include <iostream>
#include <cctype>
#include <set>
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
    if ((is_hd || is_hip) && os)
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
            if (looklen > 0.666 * cels[i]->namelen)
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
    if (aliases.empty()) return find_object_impl(term, stars, limit, distance, index);
    // Established object identifiers retain priority over imported aliases.
    const int exact = find_object_impl(term, stars, limit, 1, index);
    if (exact >= 0) return exact;
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

bool Serialization::load_all(std::fstream& fs, CelestialObject **cels, unsigned int max, bool aua)
{
    try
    {
        json allobj;
        fs >> allobj;
        int i, j, l, n = allobj.size();
        for (i=0; cels[i]; i++);
        ncelobjs = i;

        // Stars whose entry in the file states a planet tally of its own. Those tallies are now
        // saved, and the pass further down counts the planets it links back up to their star as
        // well -- do both to the same star and it ends up with twice the planets it has. The file
        // is the better authority of the two: it was written when the count was complete, whereas
        // counting here sees only what this file happens to carry, which for a save of just the
        // edited objects is a fraction of the system, and it credits a star with planets a
        // catalog already counted once. So a star that brought its own tally keeps it, and the
        // count below is left to the stars that did not -- which is every star in a file written
        // before these tallies were saved, so those load exactly as they always have.
        std::set<Star*> tally_stated;
        l = 0;
        for (auto it = allobj.begin(); it != allobj.end(); ++it)
        {
            i = ncelobjs;
            json js = it.value();
            cel_obj_class c;
            js.at("typeclass").get_to(c);
            std::string name, origname, origcenname;
            js.at("!name").get_to(name);
            try { js.at("!origname").get_to(origname); } catch (...) { origname = name; }
            try { js.at("!origcenname").get_to(origcenname); } catch (...) { origcenname = std::to_string(ncelobjs+1000); }     // at least don't match anything
            const char *origname_c = origname.c_str(), *origcenname_c = origcenname.c_str();

            for (j=0; cels[j]; j++)
            {
                if (cels[j]->typeclass() != c) continue;
                if (strcmp(cels[j]->origname.c_str(), origname_c)) continue;
                if (strcmp(cels[j]->origcenname.c_str(), origcenname_c)) continue;
                i = j;
                break;
            }

            #ifdef DEBUG
            if (i != ncelobjs)
                std::cout << "Clobbering " << i << " " << cels[i]->name << " with " << name << ", originally " << origname << std::endl << std::flush;
            else std::cout << "Creating new " << i << " " << name << ", originally " << origname << std::endl << std::flush;
            #endif

            if (cels[i]) cels[i]->deleted = false;

            switch (c)
            {
                case class_galaxy:
                if (!cels[i]) cels[i] = new Galaxy();
                ((Galaxy*)cels[i])->from_json(js);
                break;

                case class_star:
                if (!cels[i]) cels[i] = new Star();
                ((Star*)cels[i])->from_json(js);
                if (js.contains("has_planets")) tally_stated.insert((Star*)cels[i]);
                ((Star*)cels[i])->update_location(J2000_TIME_T);
                break;

                case class_planet:
                if (!cels[i]) cels[i] = new Planet();
                ((Planet*)cels[i])->from_json(js);
                break;

                case class_moon:
                if (!cels[i]) cels[i] = new Moon();
                ((Moon*)cels[i])->from_json(js);
                ((Moon*)cels[i])->orbit_type = ot_equatorial;
                break;

                case class_comet:
                if (!cels[i]) cels[i] = new Comet();
                ((Comet*)cels[i])->from_json(js);
                break;

                case class_satellite:
                if (!cels[i]) cels[i] = new Satellite();
                ((Satellite*)cels[i])->from_json(js);
                break;

                default:
                std::cerr << "Attempted to load celestial object of blank or unknown type class." << std::endl;
                return false;
            }

            cels[i]->user_edited = aua;
            cels[i]->estimated_poles = true;
            cels[i]->seqno = i;

            mtx.lock();
            l++;
            loading_msg = std::string("Loaded ") + std::to_string(l) + std::string(" of ") + std::to_string(n) + std::string(" objects...");
            mtx.unlock();

            // Nothing below this point applies to a body with no orbit -- there is no center to
            // look up and no star to credit a planet to -- but skipping straight past the end of
            // the loop skipped the ncelobjs++ down there as well, and a newly created object that
            // never advanced the count was simply overwritten by the next one loaded. It went
            // unnoticed because the orbitless objects in a universe file are nearly always stars
            // that a catalog has already put in the array, where the count is not meant to
            // advance; a star the catalogs do not have -- an invented one, or any star at all
            // when the file is loaded on its own -- was dropped on the floor.
            if (!cels[i]->orbit)
            {
                if (i==ncelobjs) ncelobjs++;
                continue;
            }
            const char* cenname = cels[i]->orbit->center_name.c_str();
            cels[i]->orbit->center = nullptr;
            for (j=0; cels[j]; j++)
            {
                if (j==i) continue;
                if (!compare_types(cels[j]->type, cels[i]->type))
                {
                    continue;                    // There are two objects named Atlas, and one of them has a companion.
                }
                if (!strcmp(cenname, cels[j]->name) || !strcmp(cenname, cels[j]->origname.c_str()))
                {
                    cels[i]->orbit->center = cels[j];
                    break;
                }
            }
            if (!cels[i]->orbit->center)
                std::cout << "FAILED to place " << cels[i]->name << " in orbit around " << cenname << std::endl;

            if (cels[i]->typeclass() == class_planet || cels[i]->typeclass() == class_moon)
            {
                // get_light_center() answers with whatever the orbital hierarchy leads to, and
                // that is not always a star: it hands back the object itself for anything whose
                // type is star whatever its class, and after five hops it hands back wherever it
                // got to. Casting that to Star* and incrementing has_planets writes four bytes
                // past the end of a smaller object -- which is how a moon left with the default
                // type overwrote the vtable pointer of the planet allocated next to it. Same
                // check as Planet::est_bolometric_flux().
                CelestialObject* lc = cels[i]->get_light_center();
                Star* s = (lc && lc->typeclass() == class_star) ? (Star*)lc : nullptr;
                if (!s) std::cerr << "JSON data integrity error! " << cels[i]->name << " has no illumination star." << std::endl << std::flush;
                else if (cels[i]->typeclass() == class_planet && !tally_stated.count(s) && cels[i]->mass >= 0.01 * earth_mass)        // Do not count asteroids, moons, KBOs in planetary system.
                {
                    s->has_planets++;
                    if (((Planet*)cels[i])->is_in_con_HZ()) s->has_hz_planets++;
                    s->pl_indices.push_back(i);
                }
            }

            if (i==ncelobjs) ncelobjs++;
            if ((unsigned int)ncelobjs >= max-1) return false;                                // Avoid overflowing the array.
        }

        for (i=0; cels[i]; i++)
        {
            cel_obj_class c = cels[i]->typeclass();
            if (c == class_moon) ((Moon*)cels[i])->update_location(J2000_TIME_T);
            else if (c == class_planet) ((Planet*)cels[i])->update_location(J2000_TIME_T);
            else if (c == class_star) ((Star*)cels[i])->update_location(J2000_TIME_T);
        }

        return true;
    }
    catch (...)
    {
        #ifdef DEBUG
        assert(false);
        #else
        std::cerr << "FAILED to load universe: incorrectly formatted JSON." << std::endl;
        #endif
        return false;
    }
}
