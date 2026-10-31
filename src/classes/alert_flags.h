#ifndef ALIENORUM_ALERT_FLAGS_H
#define ALIENORUM_ALERT_FLAGS_H

#include <atomic>
#include <cstdint>

namespace alienorum
{
    inline std::atomic<std::uint64_t>& horizon_alert_revision()
    {
        static std::atomic<std::uint64_t> revision{1};
        return revision;
    }
    void invalidate_horizon_alerts();
    class HorizonAlertFlag
    {
        bool value = false;
        public:
        HorizonAlertFlag() = default;
        HorizonAlertFlag(bool initial) : value(initial)
        {
            if (initial) invalidate_horizon_alerts();
        }
        HorizonAlertFlag(const HorizonAlertFlag&) = default;
        operator bool() const { return value; }
        HorizonAlertFlag& operator=(bool next)
        {
            if (value != next)
            {
                value = next;
                invalidate_horizon_alerts();
            }
            return *this;
        }
        HorizonAlertFlag& operator=(const HorizonAlertFlag& other) { return *this = other.value; }
        template<class Json> friend void to_json(Json& json, const HorizonAlertFlag& flag) { json = bool(flag); }
        template<class Json> friend void from_json(const Json& json, HorizonAlertFlag& flag) { flag = json.template get<bool>(); }
    };
    static_assert(sizeof(HorizonAlertFlag) == sizeof(bool), "Alert flags retain their original storage size.");
}
#endif
