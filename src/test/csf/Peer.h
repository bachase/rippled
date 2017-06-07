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
#ifndef RIPPLE_TEST_CSF_PEER_H_INCLUDED
#define RIPPLE_TEST_CSF_PEER_H_INCLUDED

#include <ripple/consensus/Consensus.h>

#include <ripple/consensus/Validations.h>
#include <boost/container/flat_map.hpp>
#include <boost/container/flat_set.hpp>
#include <algorithm>
#include <test/csf/CollectorRef.h>
#include <test/csf/Scheduler.h>
#include <test/csf/Tx.h>
#include <test/csf/UNL.h>
#include <test/csf/Validation.h>
#include <test/csf/events.h>
#include <test/csf/ledgers.h>

namespace ripple {
namespace test {
namespace csf {

namespace bc = boost::container;
class PeerPosition
{
public:
    PeerPosition(Proposal const& p) : proposal_(p)
    {
    }

    Proposal const&
    proposal() const
    {
        return proposal_;
    }

    Json::Value
    getJson() const
    {
        return proposal_.getJson();
    }

private:
    Proposal proposal_;
};

/** Simulated delays in internal peer processing.
 */
struct SimDelays
{
    //! Delay in consensus calling doAccept to accepting and issuing validation
    std::chrono::milliseconds ledgerAccept{0};

    //! Delay in processing validations from remote peers
    std::chrono::milliseconds recvValidation{0};
};

/** Represents a single node participating in the consensus process.
    It implements the Callbacks required by Consensus.
*/
struct Peer
{
    // Generic Validations policy that saves stale/flushed data into
    // a StaleData instance.
    class StalePolicy
    {
        Peer& p_;

    public:
        StalePolicy(Peer& p) : p_{p}
        {
        }

        NetClock::time_point
        now() const
        {
            return p_.now();
        }

        void
        onStale(Validation&& v)
        {
        }

        void
        flush(hash_map<NodeKey, Validation>&& remaining)
        {
        }
    };

    // Non-locking mutex to avoid locks in generic Validations
    struct NotAMutex
    {
        void
        lock()
        {
        }

        void
        unlock()
        {
        }
    };

    /** Helper to simplify a peer notifying a collector */
    class WrappedCollector
    {
        using clock_type = beast::manual_clock<std::chrono::steady_clock>;
        CollectorRef collector_;
        NodeID id_;
        clock_type& clock_;
        using Ledger_t = Ledger;

    public:
        using NodeID_t = NodeID;
        template <class Collector>
        WrappedCollector(Collector& collector, NodeID id, clock_type& clock)
            : collector_{collector}, id_{id}, clock_{clock}
        {
        }
        using TxSet_t = TxSet;
        WrappedCollector(WrappedCollector const&) = delete;
        WrappedCollector&
        operator=(WrappedCollector const&) = delete;
        using PeerPosition_t = PeerPosition;
        WrappedCollector(WrappedCollector&&) = default;
        WrappedCollector&
        operator=(WrappedCollector&&) = default;

        template <class Event>
        void
        on(Event const& e)
        {
            collector_.on(id_, clock_.now(), e);
        }
    };

    using Ledger_t = Ledger;
    using NodeID_t = NodeID;
    using TxSet_t = TxSet;
    using PeerPosition_t = PeerPosition;
    using Result = ConsensusResult<Peer>;

    Consensus<Peer> consensus;

    //! Our unique ID and current signing key
    NodeID id;
    NodeKey key;

    //! The oracle that manages unique ledgers
    LedgerOracle& oracle;

    //! Handle to network for sending messages
    BasicNetwork<Peer*>& net;

    //! Scheduler of events
    Scheduler& scheduler;

    //! UNL of trusted peers
    UNL unl;

    //! openTxs that haven't been closed in a ledger yet
    TxSetType openTxs;

    //! The last ledger closed by this node
    LedgerState lastClosedLedger;

    //! Ledgers this node has closed or loaded from the network
    hash_map<Ledger::ID, Ledger> ledgers;

    //! Validationss from trusted nodes
    Validations<StalePolicy, Validation, NotAMutex> validations;

    //! The most recent ledger that has been fully validated by the network
    LedgerState fullyValidatedLedger;

    //! Map from Ledger::ID to vector of Positions with that ledger
    //! as the prior ledger
    bc::flat_map<Ledger::ID, std::vector<Proposal>> peerPositions;
    bc::flat_map<TxSet::ID, TxSet> txSets;

    //! The number of ledgers this
    int completedLedgers = 0;
    int targetLedgers = std::numeric_limits<int>::max();

    //! Skew samples from the network clock; to be refactored into a
    //! clock time once it is provided separately from the network.
    std::chrono::seconds clockSkew{0};

    //! Simulated delays to use for internal processing
    SimDelays delays;

    //! Whether to simulate running as validator or just consensus observer
    bool runAsValidator = true;

    std::size_t prevProposers_ = 0;
    std::chrono::milliseconds prevRoundTime_;

    // Quorum of validations needed for a ledger to be fully validated
    // TODO: Use the logic in ValidatorList to set this

    std::size_t quorum;
    // Simulation parameters
    ConsensusParms parms_;

    //! The collector to report events to
    WrappedCollector collector;

    //! All peers start from the default constructed ledger
    template <class Collector>
    Peer(
        std::uint32_t i,
        ConsensusParms p,
        Scheduler& s,
        LedgerOracle& o,
        BasicNetwork<Peer*>& n,
        UNL const& u,
        Collector& c)
        : consensus(s.clock(), *this, beast::Journal{})
        , id{i}
        , key{id, 0}
        , oracle{o}
        , scheduler{s}
        , net{n}
        , unl(u)
        , validations{ValidationParms{}, s.clock(), beast::Journal{}, *this}
        , quorum{static_cast<std::size_t>(unl.size() * 0.8)}
        , parms_{p}
        , collector{c, id, s.clock()}
    {
        ledgers[lastClosedLedger.get().id()] = lastClosedLedger.get();
    }

    Ledger const*
    acquireLedger(Ledger::ID const& ledgerHash)
    {
        auto it = ledgers.find(ledgerHash);
        if (it != ledgers.end())
            return &(it->second);

        // TODO Get from network properly!
        for (auto const& link : net.links(this))
        {
            auto const& p = *link.to;
            auto it = p.ledgers.find(ledgerHash);
            if (it != p.ledgers.end())
            {
                auto res = ledgers.emplace(ledgerHash, it->second);
                return &res.first->second;
            }
        }
        return nullptr;
    }

    TxSet const*
    acquireTxSet(TxSet::ID const& setId)
    {
        auto it = txSets.find(setId);
        if (it != txSets.end())
            return &(it->second);
        // TODO Get from network properly!
        for (auto const& link : net.links(this))
        {
            auto const& p = *link.to;
            auto it = p.txSets.find(setId);
            if (it != p.txSets.end())
            {
                auto res = txSets.emplace(setId, it->second);
                return &res.first->second;
            }
        }
        return nullptr;
    }

    bool
    hasOpenTransactions() const
    {
        return !openTxs.empty();
    }

    std::size_t
    proposersValidated(Ledger::ID const& prevLedger)
    {
        return validations.numTrustedForLedger(prevLedger);
    }

    std::size_t
    proposersFinished(Ledger::ID const& prevLedger)
    {
        return validations.getNodesAfter(prevLedger);
    }

    Result
    onClose(
        Ledger const& prevLedger,
        NetClock::time_point closeTime,
        ConsensusMode mode)
    {
        collector.on(CloseLedger{prevLedger, openTxs});

        return Result(
            TxSet{openTxs},
            Proposal(
                prevLedger.id(),
                Proposal::seqJoin,
                TxSet::calcID(openTxs),
                closeTime,
                now(),
                id));
    }

    void
    onForceAccept(
        Result const& result,
        Ledger const& prevLedger,
        NetClock::duration const& closeResolution,
        ConsensusCloseTimes const& rawCloseTimes,
        ConsensusMode const& mode,
        Json::Value&& consensusJson)
    {
        onAccept(
            result,
            prevLedger,
            closeResolution,
            rawCloseTimes,
            mode,
            std::move(consensusJson));
    }

    void
    onAccept(
        Result const& result,
        Ledger const& prevLedger,
        NetClock::duration const& closeResolution,
        ConsensusCloseTimes const& rawCloseTimes,
        ConsensusMode const& mode,
        Json::Value&& consensusJson)
    {
        schedule(delays.ledgerAccept, [&]() {
            auto newLedger = oracle.accept(
                prevLedger,
                result.set.txs(),
                closeResolution,
                result.position.closeTime());
            ledgers[newLedger.id()] = newLedger;

            collector.on(AcceptLedger{newLedger, lastClosedLedger.get()});
            prevProposers_ = result.proposers;
            prevRoundTime_ = result.roundTime.read();
            lastClosedLedger.switchTo(now(), newLedger);

            auto it = std::remove_if(
                openTxs.begin(), openTxs.end(), [&](Tx const& tx) {
                    return result.set.exists(tx.id());
                });
            openTxs.erase(it, openTxs.end());

            if (runAsValidator)
            {
                Validation v{newLedger.id(),
                             newLedger.seq(),
                             now(),
                             now(),
                             key,
                             id,
                             false};
                // relay is not trusted
                relay(v);
                // we trust ourselves
                addTrustedValidation(v);
            }

            checkFullyValidated(newLedger);

            // kick off the next round...
            // in the actual implementation, this passes back through
            // network ops
            ++completedLedgers;
            // startRound sets the LCL state, so we need to call it once after
            // the last requested round completes
            if (completedLedgers <= targetLedgers)
            {
                startRound();
            }
        });
    }

    Ledger::Seq
    earliestAllowedSeq() const
    {
        if (lastClosedLedger.get().seq() > Ledger::Seq{20})
            return lastClosedLedger.get().seq() - 20;
        return Ledger::Seq{0};
    }
    Ledger::ID
    getPrevLedger(
        Ledger::ID const& ledgerID,
        Ledger const& ledger,
        ConsensusMode mode)
    {
        // only do if we are past the genesis ledger
        if (ledger.seq() == Ledger::Seq{0})
            return ledgerID;

        Ledger::ID parentID;
        // Only set the parent ID if we believe ledger is the right ledger
        if (mode != ConsensusMode::wrongLedger)
            parentID = ledger.parentID();

        // Get validators that are on our ledger, or "close" to being on
        // our ledger.
        auto ledgerCounts = validations.currentTrustedDistribution(
            ledgerID, parentID, earliestAllowedSeq());

        Ledger::ID netLgr = getPreferredLedger(ledgerID, ledgerCounts);

        if (netLgr != ledgerID)
        {
            collector.on(WrongPrevLedger{ledgerID, netLgr});
        }
        return netLgr;
    }

    void
    propose(Proposal const& pos)
    {
        relay(pos);
    }

    ConsensusParms const&
    parms() const
    {
        return parms_;
    }

    //-------------------------------------------------------------------------
    // non-callback helpers

    // Generic overlay points

    // Receive a message from a specific peer
    template <class T>
    void
    receive(NodeID from, T const& t)
    {
        collector.on(Receive<T>{from, t});

        handle(t);
    }

    // Relay a message to all connected peers
    template <class T>
    void
    relay(T const& t)
    {
        collector.on(Relay<T>{t});
        for (auto const& link : net.links(this))
            net.send(this, link.to, [ msg = t, to = link.to, id = this->id ] {
                to->receive(id, msg);
            });
    }

    // Unwrap the peerPosition before relaying
    void
    relay(PeerPosition const & p)
    {
        relay(p.proposal());
    }

    // Type specific receive handlers
    void
    handle(Proposal const& p)
    {
        if (unl.find(static_cast<std::uint32_t>(p.nodeID())) == unl.end())
            return;

        // TODO: Supress repeats more efficiently
        auto& dest = peerPositions[p.prevLedger()];
        if (std::find(dest.begin(), dest.end(), p) != dest.end())
            return;

        dest.push_back(p);
        consensus.peerProposal(now(), PeerPosition{p});
    }

    void
    handle(TxSet const& txs)
    {
        // save and map complete?
        auto it = txSets.insert(std::make_pair(txs.id(), txs));
        if (it.second)
            consensus.gotTxSet(now(), txs);
    }

    void
    handle(Tx const& tx)
    {
        // Ignore tranasctions already in our ledger
        auto const& lastClosedTxs = lastClosedLedger.get().txs();
        if (lastClosedTxs.find(tx) != lastClosedTxs.end())
            return;
        // Only relay if it is new to us
        // TODO: Figure out better overlay model to manage relay/flood
        if (openTxs.insert(tx).second)
            relay(tx);
    }

    void
    addTrustedValidation(Validation v)
    {
        v.setTrusted();
        v.setSeen(now());
        validations.add(v.key(), v);

        // Acquire will try to get from network if not already local
        Ledger const* lgr = acquireLedger(v.ledgerID());
        if (lgr)
            checkFullyValidated(*lgr);
    }

    void
    handle(Validation const& v)
    {
        if (unl.find(static_cast<std::uint32_t>(v.nodeID())) != unl.end())
        {
            schedule(
                delays.recvValidation, [&, v]() { addTrustedValidation(v); });
        }
    }

    // Receive (handle) a locally submitted transaction
    void
    submit(Tx const& tx)
    {
        // Received this from ourselves
        collector.on(Receive<Tx>{id, tx});
        handle(tx);
    }

    void
    timerEntry()
    {
        consensus.timerEntry(now());
        // only reschedule if not completed
        if (completedLedgers < targetLedgers)
            scheduler.in(parms().ledgerGRANULARITY, [&]() { timerEntry(); });
    }

    void
    startRound()
    {
        auto valDistribution = validations.currentTrustedDistribution(
            lastClosedLedger.get().id(),
            lastClosedLedger.get().parentID(),
            earliestAllowedSeq());

        Ledger::ID bestLCL =
            getPreferredLedger(lastClosedLedger.get().id(), valDistribution);

        collector.on(StartRound{bestLCL, lastClosedLedger.get()});

        // TODO:
        //  - Get dominant peer ledger if no validated available?
        //  - Check that we are switching to something compatible with our
        //    (network) validated history of ledgers?
        consensus.startRound(
            now(), bestLCL, lastClosedLedger.get(), runAsValidator);
    }

    void
    start()
    {
        // TODO: Expire validations less frequently
        validations.expire();
        scheduler.in(parms().ledgerGRANULARITY, [&]() { timerEntry(); });
        startRound();
    }

    NetClock::time_point
    now() const
    {
        // We don't care about the actual epochs, but do want the
        // generated NetClock time to be well past its epoch to ensure
        // any subtractions of two NetClock::time_point in the consensu
        // code are positive. (e.g. proposeFRESHNESS)
        using namespace std::chrono;
        using namespace std::chrono_literals;
        return NetClock::time_point(duration_cast<NetClock::duration>(
            scheduler.now().time_since_epoch() + 86400s + clockSkew));
    }

    // Schedule the provided callback in `when` duration, but if
    // `when` is 0, call immediately
    template <class T>
    void
    schedule(std::chrono::nanoseconds when, T&& what)
    {
        using namespace std::chrono_literals;

        if (when == 0ns)
            what();
        else
            scheduler.in(when, std::forward<T>(what));
    }

    Ledger::ID
    prevLedgerID()
    {
        return consensus.prevLedgerID();
    }

    std::size_t
    prevProposers()
    {
        return prevProposers_;
    }

    std::chrono::milliseconds
    prevRoundTime()
    {
        return prevRoundTime_;
    }

    // Not interested in tracking consensus mode
    void onModeChange(ConsensusMode, ConsensusMode)
    {
    }

    void
    checkFullyValidated(Ledger const& ledger)
    {
        // Only consider ledgers newer than our last fully validated ledger
        if (ledger.seq() < fullyValidatedLedger.get().seq())
            return;

        auto count = validations.numTrustedForLedger(ledger.id());
        if (count >= quorum)
        {
            collector.on(
                FullyValidateLedger{ledger, fullyValidatedLedger.get()});
            fullyValidatedLedger.switchTo(now(), ledger);
        }
    }
};

}  // namespace csf
}  // namespace test
}  // namespace ripple
#endif
