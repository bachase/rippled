//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#ifndef RIPPLE_APP_LEDGER_IMPL_LEDGERCONSENSUSIMP_H_INCLUDED
#define RIPPLE_APP_LEDGER_IMPL_LEDGERCONSENSUSIMP_H_INCLUDED

#include <BeastConfig.h>
#include <ripple/app/ledger/impl/DisputedTx.h>
#include <ripple/basics/CountedObject.h>
#include <ripple/app/consensus/RCLCxTraits.h>

namespace ripple {

/**
  Provides the implementation for LedgerConsensus.

  Achieves consensus on the next ledger.

  Two things need consensus:
    1.  The set of transactions.
    2.  The close time for the ledger.
*/
template <class Traits>
class LedgerConsensusImp
    : public std::enable_shared_from_this <LedgerConsensusImp<Traits>>
    , public CountedObject <LedgerConsensusImp<Traits>>
{
private:
    enum class State
    {
        // We haven't closed our ledger yet, but others might have
        open,

        // Establishing consensus
        establish,

        // We have closed on a transaction set and are
        // processing the new ledger
        processing,

        // We have accepted / validated a new last closed ledger
        // and need to start a new round
        accepted,
    };

public:

    using Callback_t = typename Traits::Callback_t;
    using Clock_t = typename Traits::Clock_t;
    using Time_t = typename Clock_t::time_point;
    using Duration_t = typename Clock_t::duration;
    using Ledger_t = typename Traits::Ledger_t;
    using Pos_t = typename Traits::Pos_t;
    using TxSet_t = typename Traits::TxSet_t;
    using Tx_t = typename Traits::Tx_t;
    using LgrID_t = typename Traits::LgrID_t;
    using TxID_t = typename Traits::TxID_t;
    using TxSetID_t = typename Traits::TxSetID_t;
    using NodeID_t = typename Traits::NodeID_t;
    using RetryTxSet_t = typename Traits::RetryTxSet_t;
    using MissingTx = typename Traits::MissingTx;
    using Dispute_t = DisputedTx <Traits>;

    /**
     * The result of applying a transaction to a ledger.
    */
    enum {resultSuccess, resultFail, resultRetry};

    static char const* getCountedObjectName () { return "LedgerConsensus"; }

    LedgerConsensusImp(LedgerConsensusImp const&) = delete;
    LedgerConsensusImp& operator=(LedgerConsensusImp const&) = delete;

    ~LedgerConsensusImp () = default;


    /**
        @param callbacks implementation specific hooks back into surrouding app
        @param id identifier for this node to use in consensus process
    */
    LedgerConsensusImp (
        Callback_t& callbacks,
        NodeID_t id);

    /**
        @param prevLCLHash The hash of the Last Closed Ledger (LCL).
        @param previousLedger Best guess of what the LCL was.
        @param closeTime Closing time point of the LCL.
    */
    void startRound (
        Time_t const& now,
        LgrID_t const& prevLCLHash,
        Ledger_t const& prevLedger);

    /**
      Get the Json state of the consensus process.
      Called by the consensus_info RPC.

      @param full True if verbose response desired.
      @return     The Json state.
    */
    Json::Value getJson (bool full);

    /* The hash of the last closed ledger */
    LgrID_t getLCL ();

    /**
      We have a complete transaction set, typically acquired from the network

      @param map      the transaction set.
    */
    void gotMap (
        Time_t const& now,
        TxSet_t const& map);

    /**
      On timer call the correct handler for each state.
    */
    void timerEntry (Time_t const& now);

    /**
      A server has taken a new position, adjust our tracking
      Called when a peer takes a new postion.

      @param newPosition the new position
      @return            true if we should do delayed relay of this position.
    */
    bool peerPosition (
        Time_t const& now,
        Pos_t const& newPosition);

    void simulate(
        Time_t const& now,
        boost::optional<std::chrono::milliseconds> consensusDelay);

    bool isProposing() const
    {
        return proposing_;
    }

    bool isValidating() const
    {
        return validating_;
    }

    bool isCorrectLCL() const
    {
        return haveCorrectLCL_;
    }

    Time_t const& now() const
    {
        return now_;
    }

    Time_t const& closeTime() const
    {
        return closeTime_;
    }

    Ledger_t const& prevLedger() const
    {
        return previousLedger_;
    }

    int getLastCloseProposers() const
    {
        return previousProposers_;
    }

    std::chrono::milliseconds getLastCloseDuration() const
    {
        return previousRoundTime_;
    }


private:
    /**
      Handle pre-close state.
    */
    void statePreClose ();

    /** We are establishing a consensus
       Update our position only on the timer, and in this state.
       If we have consensus, move to the finish state
    */
    void stateEstablish ();

    /** Check if we've reached consensus */
    bool haveConsensus ();

    /**
      Check if our last closed ledger matches the network's.
      This tells us if we are still in sync with the network.
      This also helps us if we enter the consensus round with
      the wrong ledger, to leave it with the correct ledger so
      that we can participate in the next round.
    */
    void checkLCL ();

    /**
      Change our view of the last closed ledger

      @param lclHash Hash of the last closed ledger.
    */
    void handleLCL (LgrID_t const& lclHash);

    /**
      We have a complete transaction set, typically acquired from the network

      @param map      the transaction set.
      @param acquired true if we have acquired the transaction set.
    */
    void mapCompleteInternal (
        TxSet_t const& map,
        bool acquired);

    /** We have a new last closed ledger, process it. Final accept logic

      @param set Our consensus set
    */
    void accept (TxSet_t const& set);

    /**
      Compare two proposed transaction sets and create disputed
        transctions structures for any mismatches

      @param m1 One transaction set
      @param m2 The other transaction set
    */
    void createDisputes (TxSet_t const& m1,
                         TxSet_t const& m2);

    /**
      Add a disputed transaction (one that at least one node wants
      in the consensus set and at least one node does not) to our tracking

      @param tx   The disputed transaction
    */
    void addDisputedTransaction (Tx_t const& tx);

    /**
      Adjust the votes on all disputed transactions based
        on the set of peers taking this position

      @param map   A disputed position
      @param peers peers which are taking the position map
    */
    void adjustCount (TxSet_t const& map,
        std::vector<NodeID_t> const& peers);

    /**
      Revoke our outstanding proposal, if any, and
      cease proposing at least until this round ends
    */
    void leaveConsensus ();

    /** Take an initial position on what we think the consensus set should be
    */
    void takeInitialPosition ();

    /**
       Called while trying to avalanche towards consensus.
       Adjusts our positions to try to agree with other validators.
    */
    void updateOurPositions ();

    /** If we radically changed our consensus context for some reason,
        we need to replay recent proposals so that they're not lost.
    */
    void playbackProposals ();

    /** We have just decided to close the ledger. Start the consensus timer,
       stash the close time, inform peers, and take a position
    */
    void closeLedger ();

    /** We have a new LCL and must accept it */
    void beginAccept (bool synchronous);

    /** Convert an advertised close time to an effective close time */
    Time_t effectiveCloseTime(Time_t closeTime);

private:
    Callback_t& callbacks_;

    std::recursive_mutex lock_;

    NodeID_t ourID_;
    State state_;
    Time_t now_;

    // The wall time this ledger closed
    Time_t closeTime_;

    LgrID_t prevLedgerHash_;

    Ledger_t previousLedger_;
    boost::optional<Pos_t> ourPosition_;
    boost::optional<TxSet_t> ourSet_;
    bool proposing_, validating_, haveCorrectLCL_, consensusFail_;

    // How much time has elapsed since the round started
    std::chrono::milliseconds roundTime_;

    // How long the close has taken, expressed as a percentage of the time that
    // we expected it to take.
    int closePercent_;

    Duration_t closeResolution_;

    bool haveCloseTimeConsensus_;

    std::chrono::steady_clock::time_point consensusStartTime_;

    // The number of proposers who participated in the last consensus round
    int previousProposers_;

    // Time it took for the last consensus round to converge
    std::chrono::milliseconds previousRoundTime_;

    // Convergence tracking, trusted peers indexed by hash of public key
    hash_map<NodeID_t, Pos_t>  peerPositions_;

    // Transaction Sets, indexed by hash of transaction tree
    hash_map<TxSetID_t, const TxSet_t> acquired_;

    // Disputed transactions
    hash_map<TxID_t, Dispute_t> disputes_;
    hash_set<TxSetID_t> compares_;

    // Close time estimates, keep ordered for predictable traverse
    std::map <Time_t, int> closeTimes_;

    // nodes that have bowed out of this consensus process
    hash_set<NodeID_t> deadNodes_;
    beast::Journal j_;


};

//------------------------------------------------------------------------------

std::shared_ptr <LedgerConsensusImp <RCLCxTraits>>
make_LedgerConsensus (
    RCLCxCalls& callbacks,
    typename RCLCxTraits::NodeID_t id);

extern template class LedgerConsensusImp <RCLCxTraits>;

} // ripple

#endif
