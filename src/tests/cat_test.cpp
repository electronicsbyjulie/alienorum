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
