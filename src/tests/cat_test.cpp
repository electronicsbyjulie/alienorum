#include <cstring>
#include <gtest/gtest.h>
#include "../classes/cat.h"
#include "universe_fixture.h"

using namespace alienorum;

// The catalog readers are column-oriented: every field is a fixed range of characters in a line of
// a file, transcribed from a ReadMe, and a range that is off by one silently reads the neighbouring
// field's digits. Nothing else in the program can tell you that has happened -- an asteroid with
// the wrong semimajor axis is still an asteroid -- so the parsers are tested here against real
// lines, copied out of the catalogs verbatim. No file is opened and nothing is downloaded: both
// loaders take the line as an argument, which is how read_astorb_catalog() and
// read_comets_catalog() call them once they have one in hand.

class CatalogParsingTest : public UniverseFixture
{
    protected:
    // Both loaders hang what they build off cels[0], the Sun.
    void SetUp() override
    {
        UniverseFixture::SetUp();
        make_star("Sol");
    }
};

// Line 1 of catalogs/astorb/astorb.dat: (1) Ceres.
static const char astorb_ceres[] =
    "     1 Ceres              L.H. Wasserman   3.53  0.15 0.72 848.4 G?      0   0   0   0   0"
    "   0 80351 6661 20210327 226.970751  73.737001  80.268790 10.588056 0.07830817   2.7657784"
    "8 20210311 9.5E-03  6.1E-06 20210428 2.1E-02 20211126 2.5E-02 20270106 2.5E-02 20270106*20"
    "210321";

// Line 2 of catalogs/comets/comets.dat: 19P/Borrelly.
static const char comets_borrelly[] =
    " 0001 27/07/2023 19P                  P/Borrelly                     P. Rocher 2459611.5 1"
    "   4178  0.57 10/06/2001-17/05/2023 +5.17845622005350E-0001 +1.13550075064959E+0000 +3.859"
    "26601003796E-0001 -1.52131972629668E-0002 +2.94536456033094E-0003 +1.14388314216908E-0002 "
    "+1.21805833861709E-0009 -3.90572186443250E-0010 -1.90270625793716E-0010 +2.45961232436633E"
    "+0006 +1.30627991735647E+0000 +6.37644740779076E-0001 +3.51916453503980E+0002 +7.424703303"
    "83701E+0001 +2.93047065198311E+0001 10.39 10.00  5.00 12.80  5.00  5.00";

TEST_F(CatalogParsingTest, LoadsAnAsteroidFromItsAstorbLine)
{
    // read_astorb_catalog() fills these three from the same line before it calls load_asteroid(),
    // which is an undocumented half of the contract: pass a buffer and the diameter, inclination
    // and semimajor axis are expected to be filled in already, because the loader only reads them
    // itself on the path where it goes looking for the line.
    AstorbRow row;
    row.number = 1;
    row.name = "Ceres";
    row.diam = 848.4;
    row.incl = 10.588056;
    row.sma = 2.76577848;

    char buffer[1024];
    strcpy(buffer, astorb_ceres);
    ASSERT_TRUE(CatalogReader::load_asteroid(&row, buffer));
    ASSERT_NE(row.cel, nullptr);

    Planet* p = row.cel;
    EXPECT_STREQ(p->name, "Ceres");
    EXPECT_EQ(p->asteroid_no, 1);
    EXPECT_EQ(p->type, rocky);
    EXPECT_EQ(p->typeclass(), class_planet);

    // Columns 43-47: absolute magnitude 3.53. Compared to float precision, not double: the
    // loader parses it into a float, as AstorbRow does with the diameter, inclination and
    // semimajor axis below -- about seven significant figures, which for a rock a few hundred
    // million kilometres away is a great deal more than is known about it.
    EXPECT_NEAR(p->absolute_magnitude, 3.53, 1e-6);

    // Columns 55-58: B-V 0.72, which is stated here rather than being the 0.71 default.
    EXPECT_NEAR(p->BV_color, 0.72, 1e-9);

    // The diameter is a diameter and the radius is half of it, in metres: 848.4 km across.
    EXPECT_NEAR(p->volumetric_mean_radius, 848.4 * 500, 1e-6);
    EXPECT_GT(p->mass, 0);

    // It orbits the Sun, at two and three quarter AU, tilted ten and a half degrees.
    ASSERT_NE(p->orbit, nullptr);
    EXPECT_EQ(p->orbit->center, cels[0]);
    EXPECT_NEAR(p->orbit->semimajor_axis, 2.76577848 * AU, AU * 1e-6);      // float, as above
    EXPECT_NEAR(p->orbit->inclination, 10.588056 * fiftyseventh, 1e-7);
    EXPECT_NEAR(p->orbit->eccentricity, 0.07830817, 1e-9);                  // this one is a double

    // Columns 116-125 and 127-136: the mean anomaly and argument of perihelion, in degrees.
    EXPECT_NEAR(p->orbit->mean_anomaly, 226.970751 * fiftyseventh, 1e-9);
    EXPECT_NEAR(p->orbit->arg_periapsis, 73.737001 * fiftyseventh, 1e-9);
    EXPECT_NEAR(p->orbit->ascending_node, 80.268790 * fiftyseventh, 1e-9);

    // Columns 107-114: the epoch of osculation, 2021 March 27, as a Julian date.
    EXPECT_GT(p->orbit->epoch, 2459000);
    EXPECT_LT(p->orbit->epoch, 2459400);

    // Both loaders append what they build to the universe themselves -- which is worth knowing,
    // since it means a caller must not delete the object it gets back. The fixture owns it now.
    EXPECT_EQ(cels[p->seqno], p);
    EXPECT_EQ(ncelobjs, 2) << "the Sun and the asteroid";
    delete_the_universe();
}

TEST_F(CatalogParsingTest, LoadsACometFromItsCometsLine)
{
    CometRow row;
    row.code = "19P";

    char buffer[1024];
    strcpy(buffer, comets_borrelly);
    ASSERT_TRUE(CatalogReader::load_comet(&row, buffer));
    ASSERT_NE(row.cel, nullptr);

    Comet* c = row.cel;
    EXPECT_EQ(c->designation, "19P");
    EXPECT_EQ(c->typeclass(), class_comet);
    EXPECT_EQ(c->type, icy_tailed);
    EXPECT_NE(std::string(c->name).find("Borrelly"), std::string::npos)
        << "got name: " << c->name;

    ASSERT_NE(c->orbit, nullptr);
    EXPECT_EQ(c->orbit->center, cels[0]);

    // A short-period comet: closed orbit, perihelion inside the asteroid belt, and an eccentricity
    // between a circle and a parabola.
    EXPECT_GT(c->orbit->eccentricity, 0.5);
    EXPECT_LT(c->orbit->eccentricity, 1.0);
    EXPECT_FALSE(c->orbit->is_open());
    EXPECT_GT(c->orbit->periapsis_distance, 0.5 * AU);
    EXPECT_LT(c->orbit->periapsis_distance, 2.0 * AU);
    EXPECT_GT(c->orbit->semimajor_axis, AU);

    // The epoch is the perihelion passage itself, which is what lets the mean anomaly be zero.
    EXPECT_DOUBLE_EQ(c->orbit->mean_anomaly, 0);
    EXPECT_DOUBLE_EQ(c->epoch, c->orbit->T_periapsis);
    EXPECT_GT(c->orbit->T_periapsis, 2400000);

    // The angles are in radians once parsed, and inside the range angles live in.
    EXPECT_GE(c->orbit->inclination, 0);
    EXPECT_LE(c->orbit->inclination, _pi);
    EXPECT_GE(c->orbit->ascending_node, 0);
    EXPECT_LE(c->orbit->ascending_node, _pi*2);

    // The light curve: a real comet has a total-magnitude set, and R1 is the steep exponent that
    // makes it a comet rather than a rock.
    double h, sr, sd;
    c->light_curve_parameters(h, sr, sd);
    EXPECT_GT(sr, 0);
    EXPECT_GT(sd, 0);

    EXPECT_EQ(cels[c->seqno], c) << "load_comet() appends what it builds, as load_asteroid() does";
    delete_the_universe();
}

TEST_F(CatalogParsingTest, RefusesALineItCannotMatch)
{
    // Asked for a comet whose code is not in the line it was handed, with no file to fall back on
    // -- load_comet() only searches when it is given no buffer at all.
    CometRow row;
    row.code = "999P";
    char buffer[1024];
    strcpy(buffer, comets_borrelly);

    // It builds from the line it is given regardless of the code, which is the caller's business:
    // read_comets_catalog() matches the line first and then hands it over.
    ASSERT_TRUE(CatalogReader::load_comet(&row, buffer));
    EXPECT_EQ(row.cel->designation, "19P") << "it parses the line it was handed, not the code asked for";

    delete_the_universe();
}

TEST_F(CatalogParsingTest, FindsTheCatalogDirectories)
{
    // Read-only, against whatever this working tree actually has.
    CatalogReader cr;
    std::vector<std::string> found = cr.find_catalogs("catalogs");
    EXPECT_GT(found.size(), 0u) << "there is a catalogs directory in the working tree";

    // And a directory that is not there is an empty answer, not a crash.
    std::vector<std::string> none = cr.find_catalogs("no_such_directory_as_this_one");
    EXPECT_EQ(none.size(), 0u);
}

TEST_F(CatalogParsingTest, CondensedStarCatalogNameIsStable)
{
    CatalogReader cr;
    std::string name = cr.get_condensed_starcat_name();
    EXPECT_GT(name.size(), 0u);
    EXPECT_EQ(name, cr.get_condensed_starcat_name());
}

class TestCatalogReader : public CatalogReader
{
public:
    using CatalogReader::resolve_or_create_exostar;
};

TEST_F(CatalogParsingTest, HostStarConsecutiveCachingDoesNotConflateDistinctStars)
{
    if (!hdcache)
    {
        hdcache = new Star*[MAX_HD + 1]();
    }
    if (!hipcache)
    {
        hipcache = new Star*[MAX_HIP + 1]();
    }

    Star* tau_cet = make_star("tau Cet");
    tau_cet->HD = 10700;
    hdcache[10700] = tau_cet;

    Star* tau_gem = make_star("tau Gem");
    tau_gem->HD = 54719;
    hdcache[54719] = tau_gem;

    Star* star_81cet = make_star("81 Cet");
    star_81cet->HD = 16400;
    hdcache[16400] = star_81cet;

    Star* star_82eri = make_star("82 Eri");
    star_82eri->HD = 20794;
    hdcache[20794] = star_82eri;

    Star* cnc_a = make_star("55 Cnc A");
    Star* cnc_b = make_star("GJ 324 B");

    std::string last_hostname;
    Star* last_host_star = nullptr;
    TestCatalogReader tcr;

    auto process_host = [&](const ExoRow& row) -> Star*
    {
        Star* host_star = nullptr;
        bool was_new = false;
        if (!row.hostname.empty() && row.hostname == last_hostname && last_host_star)
        {
            host_star = last_host_star;
        }
        else
        {
            host_star = tcr.resolve_or_create_exostar(row, false, &was_new);
            last_host_star = host_star;
            last_hostname = row.hostname;
        }
        return host_star;
    };

    // 1. tau Cet (7 chars) -> tau Gem (7 chars)
    ExoRow row_cet;
    row_cet.hostname = "tau Cet";
    row_cet.hd_name = "HD 10700";
    Star* resolved_cet = process_host(row_cet);
    EXPECT_EQ(resolved_cet, tau_cet);

    ExoRow row_gem;
    row_gem.hostname = "tau Gem";
    row_gem.hd_name = "HD 54719";
    Star* resolved_gem = process_host(row_gem);
    EXPECT_EQ(resolved_gem, tau_gem);
    EXPECT_NE(resolved_gem, tau_cet);

    // 2. 81 Cet (6 chars) -> 82 Eri (6 chars)
    ExoRow row_81cet;
    row_81cet.hostname = "81 Cet";
    row_81cet.hd_name = "HD 16400";
    Star* resolved_81cet = process_host(row_81cet);
    EXPECT_EQ(resolved_81cet, star_81cet);

    ExoRow row_82eri;
    row_82eri.hostname = "82 Eri";
    row_82eri.hd_name = "HD 20794";
    Star* resolved_82eri = process_host(row_82eri);
    EXPECT_EQ(resolved_82eri, star_82eri);
    EXPECT_NE(resolved_82eri, star_81cet);

    // 3. 55 Cnc A (8 chars) -> 55 Cnc B (8 chars)
    ExoRow row_cnca;
    row_cnca.hostname = "55 Cnc A";
    Star* resolved_cnca = process_host(row_cnca);
    EXPECT_EQ(resolved_cnca, cnc_a);

    ExoRow row_cncb;
    row_cncb.hostname = "55 Cnc B";
    Star* resolved_cncb = process_host(row_cncb);
    EXPECT_EQ(resolved_cncb, cnc_b);
    EXPECT_NE(resolved_cncb, cnc_a);

    hdcache[10700] = nullptr;
    hdcache[54719] = nullptr;
    hdcache[16400] = nullptr;
    hdcache[20794] = nullptr;
    delete_the_universe();
}

TEST_F(CatalogParsingTest, TestLoadExoplanetsFromTap)
{
    CatalogReader cr;
    radio_silence = true;
    cr.load_exoplanets_from_tap(true);
    unsigned int nexo = cr.load_exoplanets_from_tap();
    EXPECT_GT(nexo, 5000u);

    int idx_e = find_object("PDS 70 e", false);
    EXPECT_GE(idx_e, 0);
    if (idx_e >= 0)
    {
        Planet* pe = (Planet*)cels[idx_e];
        ASSERT_NE(pe->orbit, nullptr);
        double pe_incl_deg = pe->orbit->inclination * fiftyseven;
        EXPECT_NEAR(pe_incl_deg, 180.0, 1.0);
    }

    int idx_b = find_object("PDS 70 b", false);
    EXPECT_GE(idx_b, 0);

    int idx_c = find_object("PDS 70 c", false);
    EXPECT_GE(idx_c, 0);

    delete_the_universe();
}

TEST_F(CatalogParsingTest, DedupPlanetsPreservesBinaryCompanions)
{
    // Test 1: Binary star with planet around member A and member B (identical orbit parameters)
    // MUST NOT MERGE!
    json binary_planets = json::array();

    json pl_a;
    pl_a["pl_name"] = "BinaryTest A b";
    pl_a["hostname"] = "BinaryTest A";
    pl_a["ra"] = 120.0;
    pl_a["dec"] = -30.0;
    pl_a["pl_orbper"] = 100.0;
    pl_a["pl_orbsmax"] = 1.0;
    binary_planets.push_back(pl_a);

    json pl_b;
    pl_b["pl_name"] = "BinaryTest B b";
    pl_b["hostname"] = "BinaryTest B";
    pl_b["ra"] = 120.0001;
    pl_b["dec"] = -30.0001;
    pl_b["pl_orbper"] = 100.0;
    pl_b["pl_orbsmax"] = 1.0;
    binary_planets.push_back(pl_b);

    CatalogReader::dedup_planets(binary_planets);
    EXPECT_EQ(binary_planets.size(), 2u);

    // Test 2: Binary star with unspecified orbits around member A and member B
    json binary_unspec = json::array();
    json unspec_a;
    unspec_a["pl_name"] = "2MASS J1450-7841 A";
    unspec_a["hostname"] = "2MASS J1450-7841 A";
    unspec_a["ra"] = 222.67;
    unspec_a["dec"] = -78.69;
    binary_unspec.push_back(unspec_a);

    json unspec_b;
    unspec_b["pl_name"] = "2MASS J1450-7841 B";
    unspec_b["hostname"] = "2MASS J1450-7841 B";
    unspec_b["ra"] = 222.67;
    unspec_b["dec"] = -78.69;
    binary_unspec.push_back(unspec_b);

    CatalogReader::dedup_planets(binary_unspec);
    EXPECT_EQ(binary_unspec.size(), 2u);

    // Test 3: Duplicate planet across catalogs (e.g. EU vs NASA) with differing identifiers
    json dup_planets = json::array();
    json pl_eu;
    pl_eu["pl_name"] = "Alpha Cet b";
    pl_eu["hostname"] = "Alpha Cet";
    pl_eu["ra"] = 45.0;
    pl_eu["dec"] = 10.0;
    pl_eu["pl_orbper"] = 50.0;
    pl_eu["pl_orbsmax"] = 0.5;
    dup_planets.push_back(pl_eu);

    json pl_nasa;
    pl_nasa["pl_name"] = "HD 12345 b";
    pl_nasa["hostname"] = "HD 12345";
    pl_nasa["hd_name"] = "HD 12345";
    pl_nasa["ra"] = 45.0002;
    pl_nasa["dec"] = 10.0001;
    pl_nasa["pl_orbper"] = 50.5; // within 15%
    pl_nasa["pl_orbsmax"] = 0.505;
    dup_planets.push_back(pl_nasa);

    CatalogReader::dedup_planets(dup_planets);
    EXPECT_EQ(dup_planets.size(), 1u);
    EXPECT_EQ(dup_planets[0]["pl_name"], "Alpha Cet b");
    EXPECT_EQ(dup_planets[0]["hd_name"], "HD 12345");
}


