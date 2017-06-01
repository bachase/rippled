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

#include <boost/container/flat_map.hpp>
#include <boost/container/flat_set.hpp>
#include <ripple/consensus/Validations.h>
#include <test/csf/Ledger.h>
#include <test/csf/Tx.h>
#include <test/csf/UNL.h>
#include <test/csf/Validation.h>
#include <test/csf/LedgerChains.h>
#include <algorithm>
namespace ripple {
namespace test {
namespace csf {


namespace bc = boost::container;

/** Proposal is a position taken in the consensus process and is represented
    directly from the generic types.
*/
using Proposal = ConsensusProposal<NodeID, Ledger::ID, TxSetType>;

struct Traits
{
    using Ledger_t = Ledger;
    using NodeID_t = NodeID;
    using TxSet_t = TxSet;
};

/** Represents a single node participating in the consensus process.
    It implements the Callbacks required by Consensus.
*/
struct Peer : public Consensus<Peer, Traits>
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
        onStale(Validation && v)
        {}

        void
        flush(hash_map<NodeKey, Validation>&& remaining) {}

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

    using Base = Consensus<Peer, Traits>;

    //! Our unique ID
    NodeID id;
    NodeKey key;

    //! openTxs that haven't been closed in a ledger yet
    TxSetType openTxs;

    //! The last ledger closed by this node
    LedgerState lastClosedLedger;

    //! Ledgers this node has closed or loaded from the network
    hash_map<Ledger::ID, Ledger> ledgers;

    //! Handle to network for sending messages
    BasicNetwork<Peer*>& net;

    //! UNL of trusted peers
    UNL unl;

    //! Validationss from trusted nodes
    Validations<StalePolicy, Validation, NotAMutex> validations;

    //! The most recent ledger that has been fully validated by the network
    LedgerState fullyValidatedLedger;



    //! Map from Ledger::ID to vector of Positions with that ledger
    //! as the prior ledger
    bc::flat_map<Ledger::ID, std::vector<Proposal>> peerPositions_;
    bc::flat_map<TxSet::ID, TxSet> txSets;

    int completedLedgers = 0;
    int targetLedgers = std::numeric_limits<int>::max();

    //! Skew samples from the network clock; to be refactored into a
    //! clock time once it is provided separately from the network.
    std::chrono::seconds clockSkew{0};

    //! Delay in processing validations from remote peers
    std::chrono::milliseconds validationDelay{0};

    //! Delay in acquiring missing ledger from the network
    std::chrono::milliseconds missingLedgerDelay{0};

    bool validating_ = true;
    bool proposing_ = true;

    // Quorum of validations needed for a ledger to be fully validated
    // TODO: Use the logic in ValidatorList to set this
    std::size_t quorum;

    //! All peers start from the default constructed ledger
    Peer(std::uint32_t id, BasicNetwork<Peer*>& n, UNL const& u)
        : Consensus<Peer, Traits>(n.clock(), beast::Journal{})
        , id{id}
        , key{id, 0}
        , net{n}
        , unl(u)
        , validations{ValidationParms{}, n.clock(), beast::Journal{}, *this}
        , quorum{static_cast<std::size_t>(unl.size() * 0.8)}
    {
        ledgers[lastClosedLedger.get().id()] = lastClosedLedger.get();
    }

    Ledger const*
    acquireLedger(Ledger::ID const& ledgerHash)
    {
        auto it = ledgers.find(ledgerHash);
        if (it != ledgers.end())
            return &(it->second);

        // TODO Get from network/oracle properly!

        for (auto const& link : net.links(this))
        {
            auto const& p = *link.to;
            auto it = p.ledgers.find(ledgerHash);
            if (it != p.ledgers.end())
            {
                schedule(
                    missingLedgerDelay,
                    [ this, ledgerHash, ledger = it->second ]() {
                        ledgers.emplace(ledgerHash, ledger);
                    });
                if (missingLedgerDelay == 0ms)
                    return &ledgers[ledgerHash];
                break;
            }
        }
        return nullptr;
    }

    auto const&
    proposals(Ledger::ID const& ledgerHash)
    {
        return peerPositions_[ledgerHash];
    }

    TxSet const*
    acquireTxSet(TxSet::ID const& setId)
    {
        auto it = txSets.find(setId);
        if (it != txSets.end())
            return &(it->second);
        // TODO Get from network/oracle instead!
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
    onClose(Ledger const& prevLedger, NetClock::time_point closeTime, Mode mode)
    {
        TxSet res{openTxs};

        return Result{TxSet{openTxs},
                      Proposal{prevLedger.id(),
                               Proposal::seqJoin,
                               res.id(),
                               closeTime,
                               now(),
                               id}};
    }

    void
    onForceAccept(
        Result const& result,
        Ledger const& prevLedger,
        NetClock::duration const& closeResolution,
        CloseTimes const& rawCloseTimes,
        Mode const& mode)
    {
        onAccept(result, prevLedger, closeResolution, rawCloseTimes, mode);
    }

    void
    onAccept(
        Result const& result,
        Ledger const& prevLedger,
        NetClock::duration const& closeResolution,
        CloseTimes const& rawCloseTimes,
        Mode const& mode)
    {
        auto newLedger = prevLedger.close(
            result.set.txs_,
            closeResolution,
            rawCloseTimes.self,
            result.position.closeTime() != NetClock::time_point{});
        ledgers[newLedger.id()] = newLedger;

        lastClosedLedger.switchTo(now(), newLedger);

        auto it =
            std::remove_if(openTxs.begin(), openTxs.end(), [&](Tx const& tx) {
                return result.set.exists(tx.id());
            });
        openTxs.erase(it, openTxs.end());

        if (validating_)
        {
            Validation v{newLedger.id(), newLedger.seq(), now(), now(), key, id, false};
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
    }

    Ledger::Seq
    earliestAllowedSeq() const
    {
        if(lastClosedLedger.get().seq() > Ledger::Seq{20})
            return lastClosedLedger.get().seq() - 20;
        return Ledger::Seq{0};
    }
    Ledger::ID
    getPrevLedger(Ledger::ID const& ledgerID, Ledger const& ledger, Mode mode)
    {
        // only do if we are past the genesis ledger
        if(ledger.seq() ==  Ledger::Seq{0})
            return ledgerID;

        Ledger::ID parentID;
        // Only set the parent ID if we believe ledger is the right ledger
        if (mode != Mode::wrongLedger)
            parentID = ledger.parentID();

        // Get validators that are on our ledger, or "close" to being on
        // our ledger.
        auto ledgerCounts =
            validations.currentTrustedDistribution(
                ledgerID,
                parentID,
                earliestAllowedSeq());

        Ledger::ID netLgr = getPreferredLedger(ledgerID, ledgerCounts);

        if (netLgr != ledgerID)
        {
            // signal change?
        }
        return netLgr;
    }

    void
    propose(Proposal const& pos)
    {
        if (proposing_)
            relay(pos);
    }

    //-------------------------------------------------------------------------
    // non-callback helpers
    void
    receive(Proposal const& p)
    {
        if (unl.find(static_cast<std::uint32_t>(p.nodeID())) == unl.end())
            return;

        // TODO: Be sure this is a new proposal!!!!!
        auto& dest = peerPositions_[p.prevLedger()];
        if (std::find(dest.begin(), dest.end(), p) != dest.end())
            return;

        dest.push_back(p);
        peerProposal(now(), p);
    }

    void
    receive(TxSet const& txs)
    {
        // save and map complete?
        auto it = txSets.insert(std::make_pair(txs.id(), txs));
        if (it.second)
            gotTxSet(now(), txs);
    }

    void
    receive(Tx const& tx)
    {
        if (openTxs.find(tx.id()) == openTxs.end())
        {
            openTxs.insert(tx);
            // relay to peers???
            relay(tx);
        }
    }

    void
    addTrustedValidation(Validation v)
    {
        v.setTrusted();
        v.setSeen(now());
        validations.add(v.key(), v);

        auto it = ledgers.find(v.ledgerID());
        if(it != ledgers.end())
            checkFullyValidated(it->second);
        // TODO:
        //   else acquire from the network!
    }

    void
    receive(Validation const& v)
    {
        if (unl.find(static_cast<std::uint32_t>(v.nodeID())) != unl.end())
        {
            schedule(validationDelay, [&, v](){
                addTrustedValidation(v);
            });
        }
    }

    template <class T>
    void
    relay(T const& t)
    {
        for (auto const& link : net.links(this))
            net.send(
                this, link.to, [ msg = t, to = link.to ] { to->receive(msg); });
    }

    // Receive and relay locally submitted transaction
    void
    submit(Tx const& tx)
    {
        receive(tx);
        relay(tx);
    }

    void
    timerEntry()
    {
        Base::timerEntry(now());
        // only reschedule if not completed
        if (completedLedgers < targetLedgers)
            net.timer(LEDGER_GRANULARITY, [&]() { timerEntry(); });
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

        // TODO:
        //  - Get dominant peer ledger if no validated available?
        //  - Check that we are switching to something compatible with our
        //    (network) validated history of ledgers?
        Base::startRound(now(), bestLCL, lastClosedLedger.get(), proposing_);
    }

    void
    start()
    {
        // TODO: Expire validations less frequently
        validations.expire();
        net.timer(LEDGER_GRANULARITY, [&]() { timerEntry(); });
        startRound();
    }

    NetClock::time_point
    now() const
    {
        // We don't care about the actual epochs, but do want the
        // generated NetClock time to be well past its epoch to ensure
        // any subtractions of two NetClock::time_point in the consensu
        // code are positive. (e.g. PROPOSE_FRESHNESS)
        using namespace std::chrono;
        return NetClock::time_point(duration_cast<NetClock::duration>(
            net.now().time_since_epoch() + 86400s + clockSkew));
    }

    // Schedule the provided callback in `when` duration, but if
    // `when` is 0, call immediately
    template <class T>
    void
    schedule(std::chrono::nanoseconds when, T&& what)
    {
        if (when == 0ns)
            what();
        else
            net.timer(when, std::forward<T>(what));
    }

    void
    checkFullyValidated(Ledger const & ledger)
    {
        // Only consider ledgers newer than our last fully validated ledger
        if(ledger.seq() < fullyValidatedLedger.get().seq())
            return;

        auto count = validations.numTrustedForLedger(ledger.id());
        if (count >= quorum)
        {
            fullyValidatedLedger.switchTo(now(), ledger);
        }
    }
};

}  // csf
}  // test
}  // ripple
#endif
