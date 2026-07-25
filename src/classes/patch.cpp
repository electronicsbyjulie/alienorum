#include "patch.h"

namespace alienorum
{
    std::vector<LocalPatchPoint> dataset_bond_albedines =
    {
        {0.97, 0.142, 0.088, 0, 0, 0},
        {0.81, 0.689, 0.760, 0, 0, 0},
        {0.20, 0.434, 0.294, 0, 0, 0},
        {1.43, 0.170, 0.250, 0, 0, 0},
        {0.87, 0.538, 0.503, 0, 0, 0},
        {1.09, 0.499, 0.342, 0, 0, 0},
        {0.56, 0.488, 0.300, 0, 0, 0},
        {0.41, 0.442, 0.290, 0, 0, 0}
    };

    // Linearizes values exponentially approaching 1
    double LocalPatchPredictor::linearize(double v) const
    {
        if (v >= 0.999) return -std::log(0.001); // Safety clamp to prevent infinity
        return -std::log(1.0 - v);
    }

    // Returns transformed values back to the bounded 0-1 scale
    double LocalPatchPredictor::delinearize(double v_t) const
    {
        return 1.0 - std::exp(-v_t);
    }

    void LocalPatchPredictor::load_data(const std::vector<LocalPatchPoint>& data)
    {
        dataset = data;
        // Transform the dataset upon loading
        for (auto& p : dataset)
        {
            p.a_t = p.a;
            p.b_t = linearize(p.b);
            p.c_t = linearize(p.c);
        }
    }

    double LocalPatchPredictor::predict(double target_a, double target_b)
    {
        double a_t = target_a;
        double b_t = linearize(target_b);

        // Calculate spatial distance from the target to all known points
        for (auto& p : dataset)
        {
            p.dist = std::hypot(p.a_t - a_t, p.b_t - b_t);
        }

        // Sort to isolate the 3 nearest neighbors for the interpolation patch
        std::sort(dataset.begin(), dataset.end(), [](const LocalPatchPoint& p1, const LocalPatchPoint& p2)
        {
            return p1.dist < p2.dist;
        });

        // If the target is precisely on a known point, return actual C
        if (dataset[0].dist < 1e-7)
        {
            return delinearize(dataset[0].c_t);
        }

        // Isolate coordinates for the local triangular patch
        double x1 = dataset[0].a_t, y1 = dataset[0].b_t, z1 = dataset[0].c_t;
        double x2 = dataset[1].a_t, y2 = dataset[1].b_t, z2 = dataset[1].c_t;
        double x3 = dataset[2].a_t, y3 = dataset[2].b_t, z3 = dataset[2].c_t;

        // Calculate the normal vector of the plane (Cross Product)
        double v1x = x2 - x1, v1y = y2 - y1, v1z = z2 - z1;
        double v2x = x3 - x1, v2y = y3 - y1, v2z = z3 - z1;

        double nx = v1y * v2z - v1z * v2y;
        double ny = v1z * v2x - v1x * v2z;
        double nz = v1x * v2y - v1y * v2x;

        double predicted_c_t;

        if (std::abs(nz) < 1e-7)
        {
            // Fallback to nearest neighbor if points are perfectly collinear
            predicted_c_t = dataset[0].c_t;
        }
        else
        {
            // Solve the plane equation for Z (transformed C)
            predicted_c_t = z1 - (nx * (a_t - x1) + ny * (b_t - y1)) / nz;
        }

        // Return the final prediction to the bounded scale
        return delinearize(predicted_c_t);
    }
}
