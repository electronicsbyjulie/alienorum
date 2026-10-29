#ifndef ALIENORUM_SCENERY_ORDER_H
#define ALIENORUM_SCENERY_ORDER_H

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace alienorum
{
    // Keep the input untouched until a bounded repair proves its final order.
    template<class Record> class SceneryDepthOrder
    {
        std::vector<size_t> previous, positions;
        std::vector<Record> scratch;
        public:
        void reset() { previous.clear(); }
        bool sort(std::vector<Record>& records, size_t domain)
        {
            const auto farther = [](const Record& a, const Record& b) { return a.dist > b.dist; };
            const size_t missing = std::numeric_limits<size_t>::max();
            positions.assign(domain, missing);
            bool valid = !previous.empty() && !records.empty();
            for (size_t i = 0; i < records.size(); ++i)
            {
                const size_t index = records[i].scenery_index;
                if (index >= domain || !std::isfinite(records[i].dist))
                {
                    valid = false;
                    continue;
                }
                if (positions[index] != missing) valid = false;
                positions[index] = i;
            }
            if (valid)
            {
                scratch.clear();
                scratch.reserve(records.size());
                for (size_t index : previous)
                {
                    if (index >= domain || positions[index] == missing) continue;
                    scratch.push_back(records[positions[index]]);
                    positions[index] = missing;
                }
                for (const Record& record : records)
                {
                    if (positions[record.scenery_index] == missing) continue;
                    scratch.push_back(record);
                    positions[record.scenery_index] = missing;
                }
                size_t moves = 0;
                for (size_t i = 1; i < scratch.size() && valid; ++i)
                {
                    if (!farther(scratch[i], scratch[i - 1])) continue;
                    Record item = scratch[i];
                    size_t j = i;
                    while (j && farther(item, scratch[j - 1]))
                    {
                        scratch[j] = scratch[j - 1];
                        --j;
                        if (++moves > records.size() * 4)
                        {
                            valid = false;
                            break;
                        }
                    }
                    scratch[j] = item;
                }
                // std::sort has no stable tie order. Preserve its exact original-input result.
                for (size_t i = 1; i < scratch.size() && valid; ++i)
                    if (scratch[i].dist == scratch[i - 1].dist) valid = false;
                if (valid) records.swap(scratch);
            }
            if (!valid) std::sort(records.begin(), records.end(), farther);
            previous.clear();
            previous.reserve(records.size());
            for (const Record& record : records) previous.push_back(record.scenery_index);
            return valid;
        }
    };
}
#endif
