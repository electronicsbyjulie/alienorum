#ifndef ALIENORUM_FICTIONAL_STARS_UI_H
#define ALIENORUM_FICTIONAL_STARS_UI_H
#include <string>

namespace alienorum { struct FictionalGalaxy; struct FictionalRegion; }
bool fictional_visit_center(const alienorum::FictionalGalaxy& galaxy, const alienorum::FictionalRegion& region);
int fictional_find_galaxy(const std::string& query);

int fictional_default_galaxy();
void open_fictional_region();
void draw_fictional_region();
void close_fictional_region();

#endif
