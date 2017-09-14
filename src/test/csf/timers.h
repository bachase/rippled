//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2017 Ripple Labs Inc

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================
#ifndef RIPPLE_TEST_CSF_TIMERS_H_INCLUDED
#define RIPPLE_TEST_CSF_TIMERS_H_INCLUDED

#include <chrono>
#include <ostream>
#include <test/csf/Scheduler.h>
#include <test/csf/SimTime.h>

namespace ripple {
namespace test {
namespace csf {

// Timers are classes that schedule repeated events and are mostly independent
// of simulation-specific details.
/*
    We should consider making DurationDistribution conform to
    RandomNumberDistribution concept and moving to beast.

    struct DurationDistribution
    {
        SimDuration
        operator()();
    };
*/


/*
    ConstantDuration is a DurationDistribution
*/
struct ConstantDuration
{
    SimDuration value;

    SimDuration
    operator()()
    {
        return value;
    }
};


/*
    RandomDuration is a DurationDistribution which takes a Generator, passes it
    through a RandomNumberDistribution to produce a random number, and then uses
    it generate a Duration.
*/
template<class RandomNumberDistribution, class UniformRandomBitGenerator>
struct RandomDuration
{
    RandomNumberDistribution dist;
    UniformRandomBitGenerator & g;

    //------------------------
    // Convert generated durations to SimDuration
    static SimDuration
    asDuration(SimDuration d)
    {
        return d;
    }

    template <class T>
    static
    std::enable_if_t<std::is_arithmetic<T>::value, SimDuration>
    asDuration(T t)
    {
        return SimDuration{static_cast<SimDuration::rep>(t)};
    }

    SimDuration
    operator()()
    {
        return asDuration(dist(g));
    }
};

template<class RandomNumberDistribution, class UniformRandomBitGenerator>
RandomDuration<RandomNumberDistribution, UniformRandomBitGenerator>
randomDuration(RandomNumberDistribution dist, UniformRandomBitGenerator& gen)
{
    return RandomDuration<RandomNumberDistribution, UniformRandomBitGenerator>{
            dist, gen};
};

class DurationDistribution
{
    using tp = SimTime;
    using dur = SimDuration;

    struct IDurationDistribution
    {
        virtual ~IDurationDistribution() = default;

        virtual SimDuration
        operator()() = 0;
    };

    template <class T>
    class Any final : public IDurationDistribution
    {
        T t_;

    public:
        Any(T t) : t_{t}
        {
        }

        // Can't copy
        Any(Any const & ) = delete;
        Any& operator=(Any const & ) = delete;

        Any(Any && ) = default;
        Any& operator=(Any && ) = default;

        virtual SimDuration
        operator()()
        {
            return t_();
        }
    };

    std::shared_ptr<IDurationDistribution> impl_;

public:
    template <
        class T,
        class = std::
            enable_if_t<!std::is_same<std::decay_t<T>, DurationDistribution>::value, void>>
    explicit
    DurationDistribution(T&& t) : impl_{new Any<T>(t)}
    {
    }

    // copyable
    DurationDistribution(DurationDistribution const& o )  = default;
    DurationDistribution& operator=(DurationDistribution const & o) = default;


    // non-movable
    DurationDistribution(DurationDistribution&&) = default;
    DurationDistribution& operator=(DurationDistribution&&) = default;

    SimDuration
    operator()()
    {
        return (*impl_)();
    }
};


/** Gives heartbeat of simulation to signal simulation progression
 */
class HeartbeatTimer
{
    Scheduler & scheduler_;
    SimDuration interval_;
    std::ostream & out_;

    RealTime startRealTime_;
    SimTime startSimTime_;

public:
    HeartbeatTimer(
            Scheduler& sched,
            SimDuration interval = std::chrono::seconds(60),
            std::ostream& out = std::cerr)
            : scheduler_{sched}, interval_{interval}, out_{out},
              startRealTime_{RealClock::now()},
              startSimTime_{sched.now()}
    {
    };

    void
    start()
    {
        scheduler_.in(interval_, [this](){beat(scheduler_.now());});
    };

    void
    beat(SimTime when)
    {
        using namespace std::chrono;
        RealTime realTime = RealClock::now();
        SimTime simTime = when;

        RealDuration realDuration = realTime - startRealTime_;
        SimDuration simDuration = simTime - startSimTime_;
        out_ << "Heartbeat. Time Elapsed: {sim: "
             << duration_cast<seconds>(simDuration).count()
             << "s | real: "
             << duration_cast<seconds>(realDuration).count()
             << "s}\n" << std::flush;

        scheduler_.in(interval_, [this](){beat(scheduler_.now());});
    }
};

}  // namespace csf
}  // namespace test
}  // namespace ripple

#endif
