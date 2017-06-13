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
#ifndef RIPPLE_TEST_CSF_SUBMITTERS_H_INCLUDED
#define RIPPLE_TEST_CSF_SUBMITTERS_H_INCLUDED

#include <test/csf/SimTime.h>
#include <test/csf/Scheduler.h>
#include <test/csf/Peer.h>
#include <test/csf/Tx.h>
#include <random>

namespace ripple {
namespace test {
namespace csf {

/** Represents rate as a count/duration */
struct Rate
{
    std::size_t count;
    SimDuration duration;
};

class Fixed
{
    SimDuration next_;
public:
    Fixed(Rate rate) : next_{rate.duration/rate.count} {}

    template <class Generator>
    SimDuration
    next(Generator & ) const
    {
        return next_;
    }
};

class Poisson
{
    std::exponential_distribution<double> dist;

public:
    Poisson(Rate rate) : dist{double(rate.count)/rate.duration.count()}
    {
    }

    template <class Generator>
    SimDuration
    next(Generator & g) const
    {
        return SimDuration(static_cast<SimDuration::rep>(dist(g)));
    }
};

/** Submits transactions to a specified peer

    Submits successive transactions beginning at start, then spaced according
    to succesive calls of rate.next(), until stop.
*/
template <class Rate, class Generator>
class Submitter
{
    Rate rate_;
    SimTime stop_;
    std::uint32_t nextID_ = 0;
    Peer & target_;
    Scheduler & scheduler_;
    Generator & g_;

    void
    submit()
    {
        target_.submit(Tx{nextID_++});
        if (scheduler_.now() < stop_)
            scheduler_.in(rate_.next(g_), [&]() { submit(); });
    }

public:
    Submitter(
        Rate rate,
        SimTime start,
        SimTime end,
        Peer & t,
        Scheduler & s,
        Generator & g)
        : rate_{rate}, stop_{end}, target_{t}, scheduler_{s}, g_{g}
    {
        scheduler_.at(start, [&]() { submit(); });
    }
};

template <class Rate, class Generator>
Submitter<Rate, Generator>
submitter(
    Rate rate,
    SimTime start,
    SimTime end,
    Peer& t,
    Scheduler& s,
    Generator& g)
{
    return Submitter<Rate, Generator>(rate, start ,end, t, s, g);
}

}  // namespace csf
}  // namespace test
}  // namespace ripple

#endif
