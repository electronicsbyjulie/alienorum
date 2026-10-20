#include "barycenter.h"
#include "../globals.h"
#include "star.h"
#include "moon.h"
#include "comet.h"
#include "satellite.h"
#include <cmath>
#include <array>
#include <deque>
#include <limits>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

using namespace alienorum;

namespace alienorum
{
    struct BarycentricBodyState
    {
        CelestialLocation location;
        double mean, rotation, equinox, equinox_ra, apparent, absolute, radius;

        explicit BarycentricBodyState(CelestialObject* body)
            : location(body->location), mean(body->_currM), rotation(body->_currTOD),
              equinox(body->equinox_eff), equinox_ra(body->equinox_RA),
              apparent(body->typeclass() == class_star ? static_cast<Star*>(body)->apparent_magnitude : 0), absolute(body->absolute_magnitude),
              radius(body->volumetric_mean_radius) {}

        void apply(CelestialObject* body) const
        {
            body->location = location;
            body->_currM = mean;
            body->_currTOD = rotation;
            body->equinox_eff = equinox;
            body->equinox_RA = equinox_ra;
            if (body->typeclass() == class_star) static_cast<Star*>(body)->apparent_magnitude = apparent;
            body->absolute_magnitude = absolute;
            body->volumetric_mean_radius = radius;
        }
    };

}

namespace
{
    struct Member
    {
        CelestialObject* body;
        int parent;
        double mass;
        double total_mass, inner_mass;
        double stellar_body_mass = 0;
        Point relative, offset, inner_offset, outer_offset, position;
    };

    struct CorrectionSpan
    {
        double start, end;
        std::array<std::vector<Point>, 8> values;
    };

    struct System
    {
        std::vector<Member> members;
        std::vector<BarycentricBodyState> states;
        double time = std::numeric_limits<double>::quiet_NaN();
        Point anchor;
        bool anchored = false;
        double span = oneday;
        std::deque<CorrectionSpan> corrections;
        CelestialLocation center;
        bool center_valid = false;
    };

    struct Address
    {
        size_t system, member;
    };

    std::vector<System> systems;
    std::unordered_map<CelestialObject*, Address> addresses;
    bool active = false;
    bool dirty = false;
    thread_local bool evaluating = false;

    struct EvaluationGuard
    {
        bool previous = evaluating;
        EvaluationGuard() { evaluating = true; }
        ~EvaluationGuard() { evaluating = previous; }
    };

    void update_raw(CelestialObject* body, double time)
    {
        switch (body->typeclass())
        {
            case class_star: static_cast<Star*>(body)->update_location(time); break;
            case class_moon: static_cast<Moon*>(body)->update_location(time); break;
            case class_planet: static_cast<Planet*>(body)->update_location(time); break;
            case class_comet: static_cast<Comet*>(body)->update_location(time); break;
            case class_satellite: static_cast<Satellite*>(body)->update_location(time); break;
            default: break;
        }
    }

    bool natural_body(CelestialObject* body)
    {
        const auto cls = body->typeclass();
        return cls == class_star || cls == class_planet || cls == class_moon;
    }

    bool bound_orbit(CelestialObject* body)
    {
        return body->orbit && body->orbit->center &&
            std::isfinite(body->orbit->eccentricity) && body->orbit->eccentricity >= 0 &&
            !body->orbit->is_open() && std::isfinite(body->orbit->semimajor_axis) &&
            (body->orbit->semimajor_axis > 0 || body->orbit->num_osc > 0);
    }

    // Input elements describe a child's subsystem relative to its physical host. Horizons
    // body-center ephemerides are the exception: their target is the individual body.
    bool body_centered(CelestialObject* body)
    {
        return body->orbit && (body->orbit->num_osc || body->orbit->body_centered);
    }

    void calculate(System& system, double time)
    {
        auto& members = system.members;
        std::vector<BarycentricBodyState> saved;
        saved.reserve(members.size());
        for (auto& member : members) saved.emplace_back(member.body);

        // Restore every display state, including on an exception. Sampling an orbit or a
        // retarded epoch must not change another object's displayed position or orientation.
        struct Restore
        {
            std::vector<Member>& members;
            std::vector<BarycentricBodyState>& saved;
            ~Restore()
            {
                for (size_t i = 0; i < members.size(); ++i) saved[i].apply(members[i].body);
            }
        } restore{members, saved};

        for (auto& member : members)
        {
            if (member.parent < 0) member.body->location.local_position = Point();
            update_raw(member.body, time);
            member.relative = member.parent < 0 ? Point() :
                member.body->location.local_position - members[member.parent].body->location.local_position;
            member.offset = member.inner_offset = member.outer_offset = Point();
            member.stellar_body_mass = 0;
        }

        for (size_t i = members.size(); i-- > 0;)
        {
            auto& member = members[i];
            if (member.inner_mass > 0) member.inner_offset = member.inner_offset * (-1.0 / member.inner_mass);
            member.outer_offset += member.inner_offset * member.stellar_body_mass;
            if (member.total_mass > 0) member.outer_offset = member.outer_offset * (-1.0 / member.total_mass);
            member.offset = member.inner_offset + member.outer_offset;
            if (member.parent >= 0)
            {
                Point displacement = member.relative;
                if (body_centered(member.body)) displacement -= member.offset;
                auto& parent = members[member.parent];
                if (member.body->typeclass() == class_star)
                {
                    parent.outer_offset += displacement * member.total_mass;
                    if (body_centered(member.body)) parent.stellar_body_mass += member.total_mass;
                }
                else parent.inner_offset += displacement * member.total_mass;
            }
        }

        system.states.clear();
        system.states.reserve(members.size());
        for (auto& member : members)
        {
            if (member.parent < 0) member.position = member.offset;
            else
            {
                member.position = members[member.parent].position + member.relative;
                if (!body_centered(member.body))
                {
                    member.position += member.offset;
                    if (member.body->typeclass() == class_star)
                        member.position -= members[member.parent].inner_offset;
                }
            }
            system.states.emplace_back(member.body);
            system.states.back().location.local_position = member.position;
        }
        system.time = time;
    }

    std::vector<Point> exact_corrections(System& system, double time)
    {
        calculate(system, time);
        std::vector<Point> result;
        result.reserve(system.members.size());
        // calculate() restores display positions. Its saved states retain the geometric
        // result; relative displacements reconstruct the uncorrected host-centered tree.
        std::vector<Point> raw(system.members.size());
        for (size_t i = 0; i < system.members.size(); ++i)
        {
            auto& member = system.members[i];
            raw[i] = member.parent < 0 ? Point() : raw[member.parent] + member.relative;
            result.push_back(member.position - raw[i]);
        }
        return result;
    }

    Point interpolate(const CorrectionSpan& span, size_t member, double time)
    {
        const double x = 2 * (time - span.start) / (span.end - span.start) - 1;
        Point base = span.values[0][member];
        Point numerator;
        double denominator = 0;
        for (int i = 0; i < 8; ++i)
        {
            const double angle = (2 * i + 1) * _pi / 16;
            const double node = std::cos(angle);
            if (std::abs(x - node) < 1e-12) return span.values[i][member];
            const double weight = ((i & 1) ? -1 : 1) * std::sin(angle) / (x - node);
            Point difference = span.values[i][member];
            difference -= base;
            numerator += difference * weight;
            denominator += weight;
        }
        return base + numerator * (1.0 / denominator);
    }

    Point correction_at(System& system, size_t member, double time)
    {
        for (const auto& span : system.corrections)
        {
            if (time >= span.start && time <= span.end) return interpolate(span, member, time);
        }

        const double grid = std::floor((time - J2000_TIME_T) / system.span);
        double start = J2000_TIME_T + grid * system.span;
        double end = start + system.span;
        // An interpolated osculating table is smooth only between adjacent records.
        // Split at those epochs, including transitions to the mean-element fallback.
        for (const auto& entry : system.members)
        {
            const auto* orbit = entry.body->orbit;
            if (!orbit || !orbit->num_osc) continue;
            size_t low = 0, high = orbit->num_osc;
            const double epoch = (time - J2000_TIME_T) / oneday + J2000;
            while (low < high)
            {
                const size_t mid = low + (high - low) / 2;
                if (orbit->osculating[mid].epoch <= epoch) low = mid + 1;
                else high = mid;
            }
            if (low > 0)
                start = std::max(start, J2000_TIME_T + (orbit->osculating[low - 1].epoch - J2000) * oneday);
            if (low < orbit->num_osc)
                end = std::min(end, J2000_TIME_T + (orbit->osculating[low].epoch - J2000) * oneday);
        }

        for (int attempt = 0; attempt < 8 && end - start > 0.001; ++attempt)
        {
            CorrectionSpan span{start, end, {}};
            for (int i = 0; i < 8; ++i)
            {
                const double node = std::cos((2 * i + 1) * _pi / 16);
                span.values[i] = exact_corrections(system, start + (node + 1) * (end - start) / 2);
            }
            bool valid = true;
            for (int i = 0; i < 9 && valid; ++i)
            {
                const double sample_time = start + (end - start) * i / 8;
                const auto exact = exact_corrections(system, sample_time);
                for (size_t j = 0; j < exact.size(); ++j)
                {
                    const double tolerance = std::max(0.001,
                        64 * std::numeric_limits<double>::epsilon() * system.members[j].position.magnitude());
                    if (interpolate(span, j, sample_time).distance_to(exact[j]) > tolerance)
                    {
                        valid = false;
                        break;
                    }
                }
            }
            if (valid)
            {
                const Point result = interpolate(span, member, time);
                if (system.corrections.size() == 128) system.corrections.pop_front();
                system.corrections.push_back(std::move(span));
                return result;
            }
            const double half = (end - start) / 4;
            start = std::max(start, time - half);
            end = std::min(end, time + half);
        }
        // Extremely eccentric or numerically awkward data retain exact evaluation.
        return exact_corrections(system, time)[member];
    }

}

bool alienorum::barycentric_evaluation_active()
{
    return evaluating;
}

void alienorum::invalidate_barycentric_systems()
{
    std::lock_guard<std::recursive_mutex> lock(catalog_mutex);
    active = false;
    dirty = false;
    addresses.clear();
    systems.clear();
}

void alienorum::refresh_barycentric_systems(CelestialObject* body)
{
    std::lock_guard<std::recursive_mutex> lock(catalog_mutex);
    if (active && (!body || addresses.count(body))) dirty = true;
}

void alienorum::forget_barycentric_body(CelestialObject* body)
{
    // Unregistered helpers can outlive the catalog index during static destruction.
    if (body->seqno < 0) return;
    std::lock_guard<std::recursive_mutex> lock(catalog_mutex);
    if (addresses.count(body)) refresh_barycentric_systems();
}

void alienorum::rebuild_barycentric_systems()
{
    std::lock_guard<std::recursive_mutex> lock(catalog_mutex);
    invalidate_barycentric_systems();
    if (!cels) return;

    std::unordered_map<CelestialObject*, std::vector<CelestialObject*>> children;
    std::unordered_set<CelestialObject*> available;
    for (int i = 0; cels[i]; ++i)
    {
        if (!cels[i]->deleted && natural_body(cels[i])) available.insert(cels[i]);
    }
    for (int i = 0; cels[i]; ++i)
    {
        auto* body = cels[i];
        if (!available.count(body)) continue;
        if (bound_orbit(body) && available.count(body->orbit->center))
            children[body->orbit->center].push_back(body);
    }

    for (int i = 0; cels[i]; ++i)
    {
        auto* root = cels[i];
        if (root->deleted || root->typeclass() != class_star || !children.count(root)) continue;
        if (bound_orbit(root) && available.count(root->orbit->center)) continue;
        System system;
        std::unordered_set<CelestialObject*> visited;
        auto append = [&](auto&& self, CelestialObject* body, int parent) -> void
        {
            if (!visited.insert(body).second) return;
            double mass = body->mass;
            if (!(mass > 0) && body->typeclass() == class_star)
                mass = static_cast<Star*>(body)->estimate_mass();
            if (!std::isfinite(mass) || mass < 0) mass = 0;
            const int index = static_cast<int>(system.members.size());
            system.members.push_back({body, parent, mass, mass, mass});
            const auto it = children.find(body);
            if (it != children.end())
            {
                for (auto* child : it->second) self(self, child, index);
            }
        };
        append(append, root, -1);
        for (size_t j = system.members.size(); j-- > 1;)
        {
            auto& member = system.members[j];
            auto& parent = system.members[member.parent];
            parent.total_mass += member.total_mass;
            if (member.body->typeclass() != class_star) parent.inner_mass += member.total_mass;
        }
        if (!(system.members[0].total_mass > 0)) continue;
        double shortest = std::numeric_limits<double>::infinity();
        for (const auto& member : system.members)
        {
            auto* orbit = member.body->orbit;
            if (!orbit || member.parent < 0) continue;
            if (!orbit->period && !orbit->num_osc)
            {
                const auto& parent = system.members[member.parent];
                const double primary_mass = member.body->typeclass() == class_star ? parent.inner_mass : parent.mass;
                const double total_mass = primary_mass + member.total_mass;
                if (total_mass > 0 && orbit->semimajor_axis > 0)
                    orbit->period = 2 * _pi * std::sqrt(std::pow(orbit->semimajor_axis, 3) / (G * total_mass));
                else orbit->compute_period(member.mass);
            }
            const auto consider = [&](double period, double eccentricity)
            {
                const double scale = std::abs(period) * std::pow(1 - eccentricity, 1.5) / 64;
                if (std::isfinite(scale) && scale > 0) shortest = std::min(shortest, scale);
            };
            consider(orbit->period, orbit->eccentricity);
            for (uint64_t k = 0; k < orbit->num_osc; ++k)
                consider(orbit->osculating[k].period, orbit->osculating[k].eccentricity);
        }
        if (std::isfinite(shortest)) system.span = std::max(0.001, shortest);
        const size_t index = systems.size();
        for (size_t j = 0; j < system.members.size(); ++j)
            addresses[system.members[j].body] = {index, j};
        systems.push_back(std::move(system));
    }
    active = true;
}

bool alienorum::update_barycentric_location(CelestialObject* body, double time)
{
    if (evaluating) return false;
    std::lock_guard<std::recursive_mutex> lock(catalog_mutex);
    if (!active || !std::isfinite(time)) return false;
    if (dirty) rebuild_barycentric_systems();
    const auto it = addresses.find(body);
    if (it == addresses.end())
    {
        // Unbound bodies and artificial satellites are test particles. Follow the physical
        // host at the requested epoch, without adding their mass to a bound subsystem.
        if (!body->orbit || !body->orbit->center || !addresses.count(body->orbit->center)) return false;
        auto* host = body->orbit->center;
        BarycentricBodyState saved(host);
        struct RestoreHost
        {
            CelestialObject* host;
            BarycentricBodyState& saved;
            ~RestoreHost() { saved.apply(host); }
        } restore{host, saved};
        update_barycentric_location(host, time);
        EvaluationGuard guard;
        update_raw(body, time);
        return true;
    }

    EvaluationGuard guard;
    auto& system = systems[it->second.system];
    if (!system.anchored)
    {
        const double epoch = system.members[0].body->epoch;
        const double reference_time = std::isfinite(epoch) && epoch > 0
            ? J2000_TIME_T + (epoch - J2000) * oneday : double(J2000_TIME_T);
        calculate(system, reference_time);
        // Catalog coordinates retain the primary's position at the reference epoch. The
        // barycenter is a fixed local offset from that astrometric track.
        system.anchor = system.states[0].location.local_position * -1;
        system.anchored = true;
    }
    const Point correction = correction_at(system, it->second.member, time);
    std::vector<size_t> path;
    for (int index = static_cast<int>(it->second.member); index >= 0; index = system.members[index].parent)
        path.push_back(static_cast<size_t>(index));
    std::vector<BarycentricBodyState> saved;
    saved.reserve(path.size());
    for (size_t index : path) saved.emplace_back(system.members[index].body);
    struct RestorePath
    {
        System& system;
        std::vector<size_t>& path;
        std::vector<BarycentricBodyState>& saved;
        ~RestorePath()
        {
            for (size_t i = 0; i < path.size(); ++i) saved[i].apply(system.members[path[i]].body);
        }
    };
    BarycentricBodyState state(body);
    {
        RestorePath restore{system, path, saved};
        for (auto index = path.rbegin(); index != path.rend(); ++index)
        {
            auto* entry = system.members[*index].body;
            if (system.members[*index].parent < 0) entry->location.local_position = Point();
            update_raw(entry, time);
        }
        system.center = system.members[0].body->location;
        system.center.system_center += system.anchor;
        system.center_valid = true;
        state = BarycentricBodyState(body);
        state.location.local_position += correction;
        state.location.system_center += system.anchor;
    }
    state.apply(body);
    return true;
}

bool alienorum::barycentric_system_center(CelestialObject* body, CelestialLocation& result)
{
    std::lock_guard<std::recursive_mutex> lock(catalog_mutex);
    if (!active) return false;
    if (dirty) rebuild_barycentric_systems();
    const auto it = addresses.find(body);
    if (it == addresses.end()) return false;
    result = body->location;
    const auto& system = systems[it->second.system];
    if (system.center_valid)
    {
        result.galactic_center = system.center.galactic_center;
        result.system_center = system.center.system_center;
        result.local_position = system.center.local_position;
    }
    else result.local_position = Point();
    return true;
}

struct BarycentricOrbitPath::Impl
{
    System system;
    CelestialObject* body;
    size_t member;
    CelestialLocation origin;
    BarycentricBodyState displayed;

    explicit Impl(CelestialObject* target) : body(target), member(0), displayed(target) {}
    ~Impl() { displayed.apply(body); }
};

BarycentricOrbitPath::BarycentricOrbitPath(CelestialObject* body, double reference_time)
{
    std::lock_guard<std::recursive_mutex> lock(catalog_mutex);
    if (!active || !body->orbit || body->orbit->is_open()) return;
    if (dirty) rebuild_barycentric_systems();
    const auto target = addresses.find(body);
    const auto host = addresses.find(body->orbit->center);
    if (target == addresses.end() || host == addresses.end()) return;
    if (target->second.system != host->second.system) return;
    EvaluationGuard guard;
    impl = std::make_unique<Impl>(body);
    auto& source = systems[target->second.system];
    const size_t root = host->second.member;
    const size_t branch = target->second.member;
    const bool stellar = body->typeclass() == class_star;
    Point host_internal_offset;
    if (stellar)
    {
        calculate(source, reference_time);
        host_internal_offset = source.members[root].inner_offset;
    }
    std::vector<int> indices(source.members.size(), -1);
    Member primary = source.members[root];
    primary.parent = -1;
    primary.mass = stellar ? primary.inner_mass : primary.mass;
    primary.total_mass = primary.inner_mass = primary.mass;
    indices[root] = 0;
    impl->system.members.push_back(primary);
    // A guide is an osculating local orbit: other families remain at their reference
    // positions. Advance this companion's subtree, while retaining the primary's mass.
    for (size_t i = branch; i < source.members.size(); ++i)
    {
        Member member = source.members[i];
        if (i != branch && (member.parent < static_cast<int>(branch) || indices[member.parent] < 0)) continue;
        indices[i] = static_cast<int>(impl->system.members.size());
        member.parent = i == branch ? 0 : indices[member.parent];
        member.total_mass = member.inner_mass = member.mass;
        impl->system.members.push_back(member);
    }
    impl->member = static_cast<size_t>(indices[target->second.member]);
    for (size_t i = impl->system.members.size(); i-- > 1;)
    {
        const auto& member = impl->system.members[i];
        auto& parent = impl->system.members[member.parent];
        parent.total_mass += member.total_mass;
        if (member.body->typeclass() != class_star) parent.inner_mass += member.total_mass;
    }
    calculate(impl->system, reference_time);
    impl->origin = body->orbit->center->location;
    impl->origin.local_position -= impl->system.members[0].position + host_internal_offset;
}

BarycentricOrbitPath::~BarycentricOrbitPath() = default;

bool BarycentricOrbitPath::sample(double time)
{
    std::lock_guard<std::recursive_mutex> lock(catalog_mutex);
    if (!impl) return false;
    EvaluationGuard guard;
    calculate(impl->system, time);
    auto state = impl->system.states[impl->member];
    state.location.galactic_center = impl->origin.galactic_center;
    state.location.system_center = impl->origin.system_center;
    state.location.local_position += impl->origin.local_position;
    state.apply(impl->body);
    return true;
}
