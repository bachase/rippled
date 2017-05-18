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
#include <ripple/consensus/Consensus.h>
#include <ripple/consensus/ConsensusProposal.h>
#include <ripple/consensus/Validations.h>
#include <test/csf/Scheduler.h>
#include <test/csf/Ledger.h>
#include <test/csf/Tx.h>
#include <test/csf/UNL.h>
#include <test/csf/Validation.h>
#include <algorithm>
namespace ripple {
namespace test {
namespace csf {

/** Proposal is a position taken in the consensus process and is represented
    directly from the generic types.
*/
using Proposal = ConsensusProposal<NodeID, Ledger::ID, TxSetType>;

namespace bc = boost::container;

class PeerPosition
{
public:
    PeerPosition(Proposal const & p)
        : proposal_(p)
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

    using Ledger_t = Ledger;
    using NodeID_t = NodeID;
    using TxSet_t = TxSet;
    using PeerPosition_t = PeerPosition;
    using Result = ConsensusResult<Peer>;

    Consensus<Peer> consensus;

    //! Our unique ID
    NodeID id;
    NodeKey key;

    //! openTxs that haven't been closed in a ledger yet
    TxSetType openTxs;

    //! last ledger this peer closed
    Ledger lastClosedLedger;

    //! Handle to network for sending messages
    BasicNetwork<Peer*>& net;
    Scheduler& scheduler;

    //! UNL of trusted peers
    UNL unl;

    //! Validationss from trusted nodes
    Validations<StalePolicy, Validation, NotAMutex> validations;

    // The ledgers, proposals, TxSets and Txs this peer has seen
    bc::flat_map<Ledger::ID, Ledger> ledgers;

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

    std::size_t prevProposers_ = 0;
    std::chrono::milliseconds prevRoundTime_;

    // Simulation parameters
    ConsensusParms parms_;

    //! All peers start from the default constructed ledger
    Peer(NodeID i, Scheduler & s, BasicNetwork<Peer*>& n, UNL const& u, ConsensusParms parms)
        : consensus(s.clock(), *this, beast::Journal{})
        , id{i}
        , key{id, 0}
        , scheduler{s}
        , net{n}
        , unl(u)
        , validations{ValidationParms{}, s.clock(), beast::Journal{}, *this}
    {
        ledgers[lastClosedLedger.id()] = lastClosedLedger;
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
    onClose(Ledger const& prevLedger, NetClock::time_point closeTime, ConsensusMode mode)
    {
        TxSet res{openTxs};

        return Result(TxSet{openTxs},
                      Proposal(prevLedger.id(),
                               Proposal::seqJoin,
                               res.id(),
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
        Json::Value && consensusJson)
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
        Json::Value && consensusJson)
    {
        auto newLedger = prevLedger.close(
            result.set.txs_,
            closeResolution,
            rawCloseTimes.self,
            result.position.closeTime() != NetClock::time_point{});
        ledgers[newLedger.id()] = newLedger;
        prevProposers_ = result.proposers;
        prevRoundTime_ = result.roundTime.read();
        lastClosedLedger = newLedger;

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
        if(lastClosedLedger.seq() > Ledger::Seq{20})
            return lastClosedLedger.seq() - 20;
        return Ledger::Seq{0};
    }
    Ledger::ID
    getPrevLedger(
        Ledger::ID const& ledgerID,
        Ledger const& ledger,
        ConsensusMode mode)
    {
        // only do if we are past the genesis ledger
        if(ledger.seq() ==  Ledger::Seq{0})
            return ledgerID;

        Ledger::ID parentID;
        // Only set the parent ID if we believe ledger is the right ledger
        if (mode != ConsensusMode::wrongLedger)
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
            relay(PeerPosition(pos));
    }

    ConsensusParms const &
    parms() const
    {
        return parms_;
    }

    //-------------------------------------------------------------------------
    // non-callback helpers
    void
    receive(PeerPosition const& peerPos)
    {
        Proposal const & p = peerPos.proposal();
        if (unl.find(static_cast<std::uint32_t>(p.nodeID())) == unl.end())
            return;

        // TODO: Supress repeats more efficiently
        auto& dest = peerPositions_[p.prevLedger()];
        if (std::find(dest.begin(), dest.end(), p) != dest.end())
            return;

        dest.push_back(p);
        consensus.peerProposal(now(), peerPos);
    }

    void
    receive(TxSet const& txs)
    {
        // save and map complete?
        auto it = txSets.insert(std::make_pair(txs.id(), txs));
        if (it.second)
            consensus.gotTxSet(now(), txs);
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
        consensus.timerEntry(now());
        // only reschedule if not completed
        if (completedLedgers < targetLedgers)
            scheduler.in(parms().ledgerGRANULARITY, [&]() { timerEntry(); });
    }

    void
    startRound()
    {
        auto valDistribution = validations.currentTrustedDistribution(
            lastClosedLedger.id(),
            lastClosedLedger.parentID(),
            earliestAllowedSeq());

        Ledger::ID bestLCL =
            getPreferredLedger(lastClosedLedger.id(), valDistribution);

        // TODO:
        //  - Get dominant peer ledger if no validated available?
        //  - Check that we are switching to something compatible with our
        //    (network) validated history of ledgers?
        consensus.startRound(now(), bestLCL, lastClosedLedger, proposing_);
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
    void
    onModeChange(ConsensusMode, ConsensusMode) {}
};

}  // csf
}  // test
}  // ripple
#endif
