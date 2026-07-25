
#ifndef _Patch
#define _Patch

#include <iostream>
#include <vector>
#include <cmath>
#include <algorithm>
#include <iomanip>

namespace alienorum
{
    struct LocalPatchPoint
    {
        double a, b, c;
        double a_t, b_t, c_t; // Logarithmically transformed values
        double dist;          // Distance to the target prediction point
    };

    extern std::vector<LocalPatchPoint> dataset_bond_albedines;

    class LocalPatchPredictor
    {
        private:
        std::vector<LocalPatchPoint> dataset;

        // Linearizes values exponentially approaching 1
        double linearize(double v) const;

        // Returns transformed values back to the bounded 0-1 scale
        double delinearize(double v_t) const;

        public:
        void load_data(const std::vector<LocalPatchPoint>& data);
        double predict(double target_a, double target_b);
    };
}

#endif