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
#ifndef RIPPLE_TEST_CSF_QOS_H_INCLUDED
#define RIPPLE_TEST_CSF_QOS_H_INCLUDED

#include <test/csf/SimTime.h>
#include <test/csf/collectors.h>
#include <test/csf/events.h>
#include <test/csf/DataFrame.h>

#include <boost/function_output_iterator.hpp>
#include <limits>
#include <map>

namespace ripple {
namespace test {
namespace csf {

/** Collector for branch metric

        Stores the number of fully validated branches and the number of
        active "working" branches over time.
*/
struct BranchCollector
{
    using Samples = DataFrame<PeerID, Ledger::ID, Ledger::Seq>;

    Samples fullValidations;
    Samples acceptLedgers;

    BranchCollector()
        : fullValidations{"Peer", "LedgerID", "LedgerSeq"}
        , acceptLedgers{"Peer", "LedgerID", "LedgerSeq"}
    {
    }

    // For each peer[t] = ledger_id
    // Given a set of ledger_ids, how many branches
    // Ignore most events by default
    template <class E>
    void
    on(PeerID, SimTime, E const& e)
    {
    }

    void
    on(PeerID who, SimTime when, AcceptLedger const& e)
    {
        acceptLedgers.push_back(who, e.ledger.id(), e.ledger.seq());
    }

    void
    on(PeerID who, SimTime when, FullyValidateLedger const& e)
    {
        fullValidations.push_back(who, e.ledger.id(), e.ledger.seq());
    }
};

/** Collector for forward progress metric

    \delta,\rho-forward progress is when at least a fraction \rho of
    validators always fully validate a new ledger within duration \delta of
    their prior full validation.

*/
struct ForwardProgressCollector
{
    struct PeerTracker
    {
        struct Sample
        {
            SimDuration delay;
            Ledger::ID ledger;
        };

        std::deque<Sample> samples;

        static constexpr SimDuration infDelay = std::chrono::hours{24};
        static constexpr Ledger::ID infLedger =
            Ledger::ID{std::numeric_limits<Ledger::ID::value_type>::max()};

        SimTime lastTime{};
        SimDuration maxDelay{};
        bool init = false;

        void
        addSample(SimTime when, Ledger::ID ledgerID)
        {
            if (init)
            {
                SimDuration delay = std::min(when - lastTime, infDelay);
                samples.emplace_back(Sample{delay, ledgerID});
                maxDelay = std::max(delay, maxDelay);
            }
            else
                init = true;

            lastTime = when;
        }

        // Ensure stalled validators have a large sample as the final entry
        void
        finalize(SimTime when)
        {
            if (!init || ((when - lastTime) > 2 * maxDelay))
                samples.emplace_back(Sample{infDelay, infLedger});
        }
    };

    hash_map<PeerID, PeerTracker> peerSamples;

    ForwardProgressCollector(std::size_t numPeers)
    {
        for (auto i = 0; i < numPeers; ++i)
            peerSamples.emplace(PeerID{i}, PeerTracker{});
    }

    // Ignore most events by default
    template <class E>
    void
    on(PeerID, SimTime, E const& e)
    {
    }

    //
    void
    on(PeerID who, SimTime when, FullyValidateLedger const& e)
    {
        peerSamples[who].addSample(when, e.ledger.id());
    }

    /** Finalize samples

        Ensure that any stalled trackers that have not fully validated recently
        add a large sample to indicate they are stalled.

        @param now Current time
    */
    void
    finalize(SimTime now)
    {
        for (auto it : peerSamples)
            it.second.finalize(now);
    }

    using Samples = DataFrame<PeerID, Ledger::ID, std::chrono::milliseconds>;
    Samples
    toDataFrame()
    {
        using namespace std::chrono;

        Samples s{"Peer", "LedgerID", "Delay"};
        for (auto const& it : peerSamples)
        {
            for (auto const& sample : it.second.samples)
            {
                s.push_back(
                    it.first,
                    sample.ledger,
                    duration_cast<milliseconds>(sample.delay));
                ;
            }
        }
        return s;
    }
};

/** Collects for transaction progress metric

    \delta,\rho transaction progress occurs when a fraction \rho of all
    transactions are accepted with duration \delta of submission to the network
*/
struct TxProgressCollector
{
    struct TxTracker
    {
        static constexpr SimDuration infDelay = std::chrono::hours{24};

        // When Tx was *first* submitted to a peer on the network
        SimTime submitted;

        // samples[i] is the duration from submission to fully validation
        // of the Tx by peer i. If infDelay, the tx was never accepted
        std::vector<SimDuration> samples;

        TxTracker(SimTime submitted_, std::size_t numPeers)
            : submitted{submitted_}, samples{numPeers, infDelay}
        {
        }

        void
        addSample(SimTime validated, PeerID peerID)
        {
            samples[static_cast<PeerID::value_type>(peerID)] =
                std::min(validated - submitted, infDelay);
        }
    };

    const std::size_t numPeers;
    hash_map<Tx::ID, TxTracker> txSamples;

    TxProgressCollector(const std::size_t numPeers_) : numPeers{numPeers_}
    {
    }

    // Ignore most events by default
    template <class E>
    void
    on(PeerID, SimTime when, E const& e)
    {
    }

    void
    on(PeerID who, SimTime when, SubmitTx const& e)
    {
        txSamples.emplace(e.tx.id(), TxTracker{when, numPeers});
    }

    void
    on(PeerID who, SimTime when, FullyValidateLedger const& e)
    {
        // Its possible the full validation jumped, so we need to account
        // for any intervening transactions

        auto addTx = [&](Tx const& tx) {
            auto it = txSamples.find(tx.id());
            if(it != txSamples.end())
                it->second.addSample(when, who);
        };

        std::set_difference(
            e.ledger.txs().begin(),
            e.ledger.txs().end(),
            e.prior.txs().begin(),
            e.prior.txs().end(),
            boost::make_function_output_iterator(std::ref(addTx)));
    }

    using Samples = DataFrame<Tx::ID, PeerID, std::chrono::milliseconds>;
    Samples
    toDataFrame()
    {
        using namespace std::chrono;

        Samples s{"TxID", "Peer","Delay"};
        for (auto const& it : txSamples)
        {
            auto const& peerSamples = it.second.samples;
            for (auto peerID = 0; peerID < peerSamples.size(); ++peerID)
            {
                s.push_back(
                    it.first,
                    PeerID{peerID},
                    duration_cast<milliseconds>(peerSamples[peerID]));
            }
        }
        return s;
    }
};

}  // namespace csf
}  // namespace test
}  // namespace ripple

#endif
