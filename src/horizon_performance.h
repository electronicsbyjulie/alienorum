#pragma once

namespace alienorum
{
    // Developer comparisons select one original path at a time. Zero selects
    // the production paths; these controls have no application UI.
    inline unsigned int horizon_reference_trial = 0;
    inline bool horizon_fast_path(unsigned int trial) { return horizon_reference_trial != trial; }
}
