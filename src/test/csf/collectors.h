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
#ifndef RIPPLE_TEST_CSF_COLLECTORS_H_INCLUDED
#define RIPPLE_TEST_CSF_COLLECTORS_H_INCLUDED

#include <ripple/basics/UnorderedContainers.h>
#include <boost/optional.hpp>
#include <chrono>
#include <test/csf/Histogram.h>
#include <test/csf/events.h>
#include <tuple>

namespace ripple {
namespace test {
namespace csf {


template <class... Cs>
class Collectors
{
    std::tuple<Cs&...> cs;

    template <class C, class E>
    static void
    apply(C & c, NodeID who, EventTime when, E e)
    {
        c.on(who, when, e);
    }

    template <std::size_t... Is, class E>
    static void
    apply(
        std::tuple<Cs&...> & cs,
        NodeID who,
        EventTime when,
        E e,
        std::index_sequence<Is...>)
    {
        // Sean Parent for_each_argument trick (C++ fold expressions would be
        // nice here)
        (void)std::array<int, sizeof...(Cs)>{
            {((apply(std::get<Is>(cs), who, when, e)), 0)...}};
    }
public:
    Collectors(Cs&... cs)
     : cs(std::tie(cs...)) {}

     template <class E>
     void
     on(NodeID who, EventTime when, E e)
     {
        apply(cs, who, when, e, std::index_sequence_for<Cs...>{});
     }
};

template <class... Cs>
Collectors<Cs...> collectors(Cs&... cs)
{
    return Collectors<Cs...>(cs...);
}

/** Collector which ignores all events
 */
struct NullCollector
{
    template <class E>
    void
    on(NodeID, EventTime, E const& e)
    {
    }
};

/** Tracks the overal duration of a simulation
*/
struct SimDurationCollector
{
    bool init = false;
    EventTime start;
    EventTime stop;

    template <class E>
    void
    on(NodeID, EventTime when, E const& e)
    {
        if(!init)
        {
            start = when;
            init = true;
        }
        else
            stop = when;
    }
};

/** Tracks the submission -> accepted -> validated evolution of transactions.

    This collector tracks transactions through the network by monitoring the
    *first* time the transaction is seen by any node in the network, or
    seen by any node's accepted or fully validated ledger.

    If transactions submitted to the network do not have unique IDs, this
    collector will not track subsequent submissions.
*/
struct TxCollector
{
    // Counts
    std::size_t submitted{0};
    std::size_t accepted{0};
    std::size_t validated{0};

    struct Tracker
    {
        Tx tx;
        EventTime submitted;
        boost::optional<EventTime> accepted;
        boost::optional<EventTime> validated;

        Tracker(Tx tx_, EventTime submitted_) : tx{tx_}, submitted{submitted_}
        {
        }
    };

    hash_map<Tx::ID, Tracker> txs;


    using Hist = Histogram<EventTime::duration>;
    Hist submitToAccept;
    Hist submitToValidate;

    // Ignore most events by default
    template <class E>
    void
    on(NodeID, EventTime when, E const& e)
    {
    }

    void
    on(NodeID who, EventTime when, Receive<Tx> const& e)
    {
        // externally submitted tx has self id
        if (who == e.from)
        {
            // save first time it was seen
            if (txs.emplace(e.val.id(), Tracker{e.val, when}).second)
            {
                submitted++;

            }
        }
    }

    void
    on(NodeID who, EventTime when, AcceptLedger const& e)
    {
        for (auto const& tx : e.ledger.txs())
        {
            auto it = txs.find(tx.id());
            if (it != txs.end() && !it->second.accepted)
            {
                Tracker & tracker = it->second;
                tracker.accepted = when;
                accepted++;

                submitToAccept.insert(*tracker.accepted - tracker.submitted);
            }
        }
    }

    void
    on(NodeID who, EventTime when, FullyValidateLedger const& e)
    {
        for (auto const& tx : e.ledger.txs())
        {
            auto it = txs.find(tx.id());
            if (it != txs.end() && !it->second.validated)
            {
                Tracker & tracker = it->second;
                // Should only validated a previously accepted Tx
                assert(tracker.accepted);

                tracker.validated = when;
                validated++;
                submitToValidate.insert(*tracker.validated - tracker.submitted);
            }
        }
    }

    // Returns the number of txs which were never accepted
    std::size_t
    orphaned() const
    {
        return std::count_if(txs.begin(), txs.end(), [](auto const& it) {
            return !it.second.accepted;
        });
    }

    // Returns the number of txs which were never validated
    std::size_t
    unvalidated() const
    {
        return std::count_if(txs.begin(), txs.end(), [](auto const& it) {
            return !it.second.validated;
        });
    }
};

/** Tracks the accepted -> validated evolution of ledgers.

    This collector tracks ledgers through the network by monitoring the
    *first* time the ledger is accepted or fully validated by ANY node.

*/
struct LedgerCollector
{
    std::size_t accepted{0};
    std::size_t fullyValidated{0};

    struct Tracker
    {
        EventTime accepted;
        boost::optional<EventTime> fullyValidated;

        Tracker(EventTime accepted_) : accepted{accepted_}
        {
        }
    };

    hash_map<Ledger::ID, Tracker> ledgers_;


    using Hist = Histogram<EventTime::duration>;
    Hist acceptToFullyValid;
    Hist acceptToAccept;
    Hist fullyValidToFullyValid;

    // Ignore most events by default
    template <class E>
    void
    on(NodeID, EventTime, E const& e)
    {
    }

    void
    on(NodeID who, EventTime when, AcceptLedger const& e)
    {
        // First time this ledger accepted
        if (ledgers_.emplace(e.ledger.id(), Tracker{ when }).second)
        {
            ++accepted;
            // ignore jumps?
            if (e.prior.id() == e.ledger.parentID())
            {
                auto const it = ledgers_.find(e.ledger.parentID());
                if (it != ledgers_.end())
                {
                    acceptToAccept.insert(when - it->second.accepted);
                }
            }
        }
    }

    void
    on(NodeID who, EventTime when, FullyValidateLedger const& e)
    {
        // ignore jumps
        if (e.prior.id() == e.ledger.parentID())
        {
            auto const it = ledgers_.find(e.ledger.id());
            assert(it != ledgers_.end());
            auto& tracker = it->second;
            // first time fully validated
            if (!tracker.fullyValidated)
            {
                ++fullyValidated;
                tracker.fullyValidated = when;
                acceptToFullyValid.insert(when - tracker.accepted);

                auto const parentIt = ledgers_.find(e.ledger.parentID());
                if (parentIt != ledgers_.end())
                {
                    auto& parentTracker = parentIt->second;
                    if (parentTracker.fullyValidated)
                    {
                        fullyValidToFullyValid.insert(
                            when - *parentTracker.fullyValidated);
                    }
                }
            }
        }
    }

    std::size_t
    unvalidated() const
    {
        return std::count_if(
            ledgers_.begin(), ledgers_.end(), [](auto const& it) {
                return !it.second.fullyValidated;
            });
    }

};

// Add LedgerJump collector?


}  // namespace csf
}  // namespace test
}  // namespace ripple

#endif
