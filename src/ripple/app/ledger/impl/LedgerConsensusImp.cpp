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

#include <BeastConfig.h>
#include <ripple/app/consensus/RCLCxTraits.h>
#include <ripple/app/ledger/InboundLedgers.h>
#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/ledger/LedgerTiming.h>
#include <ripple/app/ledger/OpenLedger.h>
#include <ripple/app/ledger/impl/LedgerConsensusImp.h>
#include <ripple/app/ledger/impl/TransactionAcquire.h>
#include <ripple/app/main/Application.h>
#include <ripple/app/misc/AmendmentTable.h>
#include <ripple/app/misc/CanonicalTXSet.h>
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/app/misc/TxQ.h>
#include <ripple/app/misc/Validations.h>
#include <ripple/basics/contract.h>
#include <ripple/basics/CountedObject.h>
#include <ripple/basics/Log.h>
#include <ripple/core/Config.h>
#include <ripple/core/JobQueue.h>
#include <ripple/core/TimeKeeper.h>
#include <ripple/json/to_string.h>
#include <ripple/overlay/Overlay.h>
#include <ripple/overlay/predicates.h>
#include <ripple/app/misc/HashRouter.h>
#include <ripple/beast/core/LexicalCast.h>
#include <type_traits>


namespace ripple {

template <class Traits>
LedgerConsensusImp<Traits>::LedgerConsensusImp (
        Application& app,
        ConsensusImp& consensus,
        InboundTransactions& inboundTransactions,
        LedgerMaster& ledgerMaster,
        FeeVote& feeVote,
        Callback_t& callbacks)
    : callbacks_ (callbacks)
    , app_ (app)
    , consensus_ (consensus)
    , inboundTransactions_ (inboundTransactions)
    , ledgerMaster_ (ledgerMaster)
    , feeVote_ (feeVote)
    , ourID_ (calcNodeID (app.nodeIdentity().first))
    , state_ (State::open)
    , valPublic_ (app_.config().VALIDATION_PUB)
    , valSecret_ (app_.config().VALIDATION_PRIV)
    , consensusFail_ (false)
    , roundTime_ (0)
    , closePercent_ (0)
    , closeResolution_ (30)
    , haveCloseTimeConsensus_ (false)
    , consensusStartTime_ (std::chrono::steady_clock::now ())
    , previousProposers_ (0)
    , previousRoundTime_ (0)
    , j_ (app.journal ("LedgerConsensus"))
{
    JLOG (j_.debug()) << "Creating consensus object";
}

template <class Traits>
Json::Value LedgerConsensusImp<Traits>::getJson (bool full)
{
    Json::Value ret (Json::objectValue);
    std::lock_guard<std::recursive_mutex> _(lock_);

    ret["proposing"] = proposing_;
    ret["validating"] = validating_;
    ret["proposers"] = static_cast<int> (peerPositions_.size ());

    if (haveCorrectLCL_)
    {
        ret["synched"] = true;
        ret["ledger_seq"] = previousLedger_.seq() + 1;
        ret["close_granularity"] = closeResolution_.count();
    }
    else
        ret["synched"] = false;

    switch (state_)
    {
    case State::open:
        ret[jss::state] = "open";
        break;

    case State::establish:
        ret[jss::state] = "consensus";
        break;

    case State::processing:
        ret[jss::state] = "processing";
        break;

    case State::accepted:
        ret[jss::state] = "accepted";
        break;
    }

    int v = disputes_.size ();

    if ((v != 0) && !full)
        ret["disputes"] = v;

    if (ourPosition_)
        ret["our_position"] = ourPosition_->getJson ();

    if (full)
    {
        using Int = Json::Value::Int;
        ret["current_ms"] = static_cast<Int>(roundTime_.count());
        ret["close_percent"] = closePercent_;
        ret["close_resolution"] = closeResolution_.count();
        ret["have_time_consensus"] = haveCloseTimeConsensus_;
        ret["previous_proposers"] = previousProposers_;
        ret["previous_mseconds"] =
            static_cast<Int>(previousRoundTime_.count());

        if (! peerPositions_.empty ())
        {
            Json::Value ppj (Json::objectValue);

            for (auto& pp : peerPositions_)
            {
                ppj[to_string (pp.first)] = pp.second.getJson ();
            }
            ret["peer_positions"] = std::move(ppj);
        }

        if (! acquired_.empty ())
        {
            Json::Value acq (Json::arrayValue);
            for (auto& at : acquired_)
            {
                acq.append (to_string (at.first));
            }
            ret["acquired"] = std::move(acq);
        }

        if (! disputes_.empty ())
        {
            Json::Value dsj (Json::objectValue);
            for (auto& dt : disputes_)
            {
                dsj[to_string (dt.first)] = dt.second.getJson ();
            }
            ret["disputes"] = std::move(dsj);
        }

        if (! closeTimes_.empty ())
        {
            Json::Value ctj (Json::objectValue);
            for (auto& ct : closeTimes_)
            {
                ctj[std::to_string(ct.first.time_since_epoch().count())] = ct.second;
            }
            ret["close_times"] = std::move(ctj);
        }

        if (! deadNodes_.empty ())
        {
            Json::Value dnj (Json::arrayValue);
            for (auto const& dn : deadNodes_)
            {
                dnj.append (to_string (dn));
            }
            ret["dead_nodes"] = std::move(dnj);
        }
    }

    return ret;
}

template <class Traits>
auto
LedgerConsensusImp<Traits>::getLCL () -> LgrID_t
{
    std::lock_guard<std::recursive_mutex> _(lock_);

    return prevLedgerHash_;
}


// Called when:
// 1) We take our initial position
// 2) We take a new position
// 3) We acquire a position a validator took
//
// We store it, notify peers that we have it,
// and update our tracking if any validators currently
// propose it
template <class Traits>
void
LedgerConsensusImp<Traits>::mapCompleteInternal (
    TxSet_t const& map,
    bool acquired)
{
    auto const hash = map.getID ();

    if (acquired_.find (hash) != acquired_.end())
        return;

    if (acquired)
    {
        JLOG (j_.trace()) << "We have acquired txs " << hash;
    }

    // We now have a map that we did not have before

    if (! acquired)
    {
        // If we generated this locally,
        // put the map where others can get it
        // If we acquired it, it's already shared
        callbacks_.shareSet (map);
    }

    if (! ourPosition_)
    {
        JLOG (j_.debug())
            << "Not creating disputes: no position yet.";
    }
    else if (ourPosition_->isBowOut ())
    {
        JLOG (j_.warn())
            << "Not creating disputes: not participating.";
    }
    else if (hash == ourPosition_->getCurrentHash ())
    {
        JLOG (j_.debug())
            << "Not creating disputes: identical position.";
    }
    else
    {
        // Our position is not the same as the acquired position
        // create disputed txs if needed
        createDisputes (*ourSet_, map);
        compares_.insert(hash);
    }

    // Adjust tracking for each peer that takes this position
    std::vector<NodeID> peers;
    for (auto& it : peerPositions_)
    {
        if (it.second.getCurrentHash () == hash)
            peers.push_back (it.second.getNodeID ());
    }

    if (!peers.empty ())
    {
        adjustCount (map, peers);
    }
    else if (acquired)
    {
        JLOG (j_.warn())
            << "By the time we got the map " << hash
            << " no peers were proposing it";
    }

    acquired_.emplace (hash, map);
}

template <class Traits>
void LedgerConsensusImp<Traits>::gotMap (
    Time_t const& now,
    TxSet_t const& map)
{
    std::lock_guard<std::recursive_mutex> _(lock_);

    now_ = now;

    try
    {
        mapCompleteInternal (map, true);
    }
    catch (SHAMapMissingNode const& mn)
    {
        // This should never happen
        leaveConsensus();
        JLOG (j_.error()) <<
            "Missing node processing complete map " << mn;
        Rethrow();
    }
}

template <class Traits>
void LedgerConsensusImp<Traits>::checkLCL ()
{
    LgrID_t netLgr = callbacks_.getLCL (
        prevLedgerHash_,
        haveCorrectLCL_ ? previousLedger_.parentHash() : uint256(),
        haveCorrectLCL_);

    if (netLgr != prevLedgerHash_)
    {
        // LCL change
        const char* status;

        switch (state_)
        {
        case State::open:
            status = "open";
            break;

        case State::establish:
            status = "establish";
            break;

        case State::processing:
            status = "processing";
            break;

        case State::accepted:
            status = "accepted";
            break;

        default:
            status = "unknown";
        }

        JLOG (j_.warn())
            << "View of consensus changed during " << status
            << " status=" << status << ", "
            << (haveCorrectLCL_ ? "CorrectLCL" : "IncorrectLCL");
        JLOG (j_.warn()) << prevLedgerHash_
            << " to " << netLgr;
        JLOG (j_.warn())
            << previousLedger_.getJson();
        JLOG (j_.warn())
            << getJson (true);

#if 0 // FIXME
        if (auto stream = j_.debug())
        {
            for (auto& it : vals)
                stream
                    << "V: " << it.first << ", " << it.second.first;
            stream << getJson (true);
        }
#endif

        handleLCL (netLgr);
    }
}

// Handle a change in the LCL during a consensus round
template <class Traits>
void LedgerConsensusImp<Traits>::handleLCL (LgrID_t const& lclHash)
{
    assert (lclHash != prevLedgerHash_ ||
            previousLedger_.hash() != lclHash);

    if (prevLedgerHash_ != lclHash)
    {
        // first time switching to this ledger
        prevLedgerHash_ = lclHash;

        if (haveCorrectLCL_ && proposing_ && ourPosition_)
        {
            JLOG (j_.info()) << "Bowing out of consensus";
            leaveConsensus();
        }

        // Stop proposing because we are out of sync
        proposing_ = false;
        peerPositions_.clear ();
        disputes_.clear ();
        compares_.clear ();
        closeTimes_.clear ();
        deadNodes_.clear ();
        // To get back in sync:
        playbackProposals ();
    }

    if (previousLedger_.hash() == prevLedgerHash_)
        return;

    // we need to switch the ledger we're working from
    auto buildLCL =  callbacks_.acquireLedger(prevLedgerHash_);
    if (! buildLCL)
    {
        haveCorrectLCL_ = false;
        return;
    }

    JLOG (j_.info()) <<
        "Have the consensus ledger " << prevLedgerHash_;

    startRound (
        now_,
        lclHash,
        buildLCL,
        previousProposers_,
        previousRoundTime_);
}

template <class Traits>
void LedgerConsensusImp<Traits>::timerEntry (Time_t const& now)
{
    std::lock_guard<std::recursive_mutex> _(lock_);

    now_ = now;

    try
    {
       if ((state_ != State::processing) && (state_ != State::accepted))
           checkLCL ();

        using namespace std::chrono;
        roundTime_ = duration_cast<milliseconds>
                           (steady_clock::now() - consensusStartTime_);

        closePercent_ = roundTime_ * 100 /
            std::max<milliseconds> (
                previousRoundTime_, AV_MIN_CONSENSUS_TIME);

        switch (state_)
        {
        case State::open:
            statePreClose ();

            if (state_ != State::establish) return;

            // Fall through

        case State::establish:
            stateEstablish ();
            return;

        case State::processing:
            // We are processing the finished ledger
            // logic of calculating next ledger advances us out of this state
            // nothing to do
            return;

        case State::accepted:
            // NetworkOPs needs to setup the next round
            // nothing to do
            return;
        }

        assert (false);
    }
    catch (SHAMapMissingNode const& mn)
    {
        // This should never happen
        leaveConsensus ();
        JLOG (j_.error()) <<
           "Missing node during consensus process " << mn;
        Rethrow();
    }
}

template <class Traits>
void LedgerConsensusImp<Traits>::statePreClose ()
{
    // it is shortly before ledger close time
    bool anyTransactions = ! app_.openLedger().empty();
    int proposersClosed = peerPositions_.size ();
    int proposersValidated
        = app_.getValidations ().getTrustedValidationCount
        (prevLedgerHash_);

    // This computes how long since last ledger's close time
    using namespace std::chrono;
    milliseconds sinceClose;
    {
        bool previousCloseCorrect = haveCorrectLCL_
            && previousLedger_.getCloseAgree ()
            && (previousLedger_.closeTime() !=
                (previousLedger_.parentCloseTime() + 1s));

        auto closeTime = previousCloseCorrect
            ? previousLedger_.closeTime() // use consensus timing
            : consensus_.getLastCloseTime(); // use the time we saw

        if (now_ >= closeTime)
            sinceClose = now_ - closeTime;
        else
            sinceClose = -milliseconds{closeTime - now_};
    }

    auto const idleInterval = std::max<seconds>(LEDGER_IDLE_INTERVAL,
        2 * previousLedger_.closeTimeResolution());

    // Decide if we should close the ledger
    if (shouldCloseLedger (anyTransactions
        , previousProposers_, proposersClosed, proposersValidated
        , previousRoundTime_, sinceClose, roundTime_
        , idleInterval, app_.journal ("LedgerTiming")))
    {
        closeLedger ();
    }
}

template <class Traits>
void LedgerConsensusImp<Traits>::stateEstablish ()
{
    // Give everyone a chance to take an initial position
    if (roundTime_ < LEDGER_MIN_CONSENSUS)
        return;

    updateOurPositions ();

    // Nothing to do if we don't have consensus.
    if (!haveConsensus ())
        return;

    if (!haveCloseTimeConsensus_)
    {
        JLOG (j_.info()) <<
            "We have TX consensus but not CT consensus";
        return;
    }

    JLOG (j_.info()) <<
        "Converge cutoff (" << peerPositions_.size () << " participants)";
    state_ = State::processing;
    beginAccept (false);
}

template <class Traits>
bool LedgerConsensusImp<Traits>::haveConsensus ()
{
    // CHECKME: should possibly count unacquired TX sets as disagreeing
    int agree = 0, disagree = 0;
    auto  ourPosition = ourPosition_->getCurrentHash ();

    // Count number of agreements/disagreements with our position
    for (auto& it : peerPositions_)
    {
        if (it.second.isBowOut ())
            continue;

        if (it.second.getCurrentHash () == ourPosition)
        {
            ++agree;
        }
        else
        {
            JLOG (j_.debug()) << to_string (it.first)
                << " has " << to_string (it.second.getCurrentHash ());
            ++disagree;
            if (compares_.count(it.second.getCurrentHash()) == 0)
            { // Make sure we have generated disputes
                auto hash = it.second.getCurrentHash();
                JLOG (j_.debug())
                    << "We have not compared to " << hash;
                auto it1 = acquired_.find (hash);
                auto it2 = acquired_.find(ourPosition_->getCurrentHash ());
                if ((it1 != acquired_.end()) && (it2 != acquired_.end()))
                {
                    compares_.insert(hash);
                    createDisputes(it2->second, it1->second);
                }
            }
        }
    }
    int currentValidations = app_.getValidations ()
        .getNodesAfter (prevLedgerHash_);

    JLOG (j_.debug())
        << "Checking for TX consensus: agree=" << agree
        << ", disagree=" << disagree;

    // Determine if we actually have consensus or not
    auto ret = checkConsensus (previousProposers_, agree + disagree, agree,
        currentValidations, previousRoundTime_, roundTime_, proposing_,
        app_.journal ("LedgerTiming"));

    if (ret == ConsensusState::No)
        return false;

    // There is consensus, but we need to track if the network moved on
    // without us.
    consensusFail_ = (ret == ConsensusState::MovedOn);

    if (consensusFail_)
    {
        JLOG (j_.error()) << "Unable to reach consensus";
        JLOG (j_.error()) << getJson(true);
    }

    return true;
}

template <class Traits>
bool LedgerConsensusImp<Traits>::peerPosition (
    Time_t const& now,
    Pos_t const& newPosition)
{
    auto const peerID = newPosition.getNodeID ();

    std::lock_guard<std::recursive_mutex> _(lock_);

    now_ = now;

    if (newPosition.getPrevLedger() != prevLedgerHash_)
    {
        JLOG (j_.debug()) << "Got proposal for "
            << newPosition.getPrevLedger ()
            << " but we are on " << prevLedgerHash_;
        return false;
    }

    if (deadNodes_.find (peerID) != deadNodes_.end ())
    {
        JLOG (j_.info())
            << "Position from dead node: " << to_string (peerID);
        return false;
    }

    {
        // update current position
        auto currentPosition = peerPositions_.find(peerID);

        if (currentPosition != peerPositions_.end())
        {
            if (newPosition.getProposeSeq ()
                <= currentPosition->second.getProposeSeq ())
            {
                return false;
            }
        }

        if (newPosition.isBowOut ())
        {
            JLOG (j_.info())
                << "Peer bows out: " << to_string (peerID);

            for (auto& it : disputes_)
                it.second.unVote (peerID);
            if (currentPosition != peerPositions_.end())
                peerPositions_.erase (peerID);
            deadNodes_.insert (peerID);

            return true;
        }

        if (currentPosition != peerPositions_.end())
            currentPosition->second = newPosition;
        else
            peerPositions_.emplace (peerID, newPosition);
    }

    if (newPosition.isInitial ())
    {
        // Record the close time estimate
        JLOG (j_.trace())
            << "Peer reports close time as "
            << newPosition.getCloseTime().time_since_epoch().count();
        ++closeTimes_[newPosition.getCloseTime()];
    }

    JLOG (j_.trace()) << "Processing peer proposal "
        << newPosition.getProposeSeq () << "/"
        << newPosition.getCurrentHash ();

    {
        auto ait = acquired_.find (newPosition.getCurrentHash());
        if (ait == acquired_.end())
        {
            if (auto setPtr = inboundTransactions_.getSet (
                newPosition.getCurrentHash(), true))
            {
                ait = acquired_.emplace (newPosition.getCurrentHash(),
                    std::move(setPtr)).first;
            }
        }


        if (ait != acquired_.end())
        {
            for (auto& it : disputes_)
                it.second.setVote (peerID,
                    ait->second.hasEntry (it.first));
        }
        else
        {
            JLOG (j_.debug())
                << "Don't have tx set for peer";
        }
    }

    return true;
}

template <class Traits>
void LedgerConsensusImp<Traits>::simulate (
    Time_t const& now,
    boost::optional<std::chrono::milliseconds> consensusDelay)
{
    std::lock_guard<std::recursive_mutex> _(lock_);

    JLOG (j_.info()) << "Simulating consensus";
    now_ = now;
    closeLedger ();
    roundTime_ = consensusDelay.value_or(100ms);
    beginAccept (true);
    JLOG (j_.info()) << "Simulation complete";
}

template <class Traits>
void LedgerConsensusImp<Traits>::accept (TxSet_t const& set)
{

    auto closeTime = ourPosition_->getCloseTime();
    bool closeTimeCorrect;

    if (closeTime == NetClock::time_point{})
    {
        // We agreed to disagree on the close time
        closeTime = previousLedger_.closeTime() + 1s;
        closeTimeCorrect = false;
    }
    else
    {
        // We agreed on a close time
        closeTime = effectiveCloseTime (closeTime);
        closeTimeCorrect = true;
    }

    JLOG (j_.debug())
        << "Report: Prop=" << (proposing_ ? "yes" : "no")
        << " val=" << (validating_ ? "yes" : "no")
        << " corLCL=" << (haveCorrectLCL_ ? "yes" : "no")
        << " fail=" << (consensusFail_ ? "yes" : "no");
    JLOG (j_.debug())
        << "Report: Prev = " << prevLedgerHash_
        << ":" << previousLedger_.seq();

    // Put transactions into a deterministic, but unpredictable, order
    CanonicalTXSet retriableTxs (set.getID());

    auto sharedLCL = callbacks_.buildLastClosedLedger(previousLedger_, set,
        closeTime, closeTimeCorrect, closeResolution_, now_,
        roundTime_, retriableTxs);

    auto const newLCLHash = sharedLCL.hash();
    JLOG (j_.debug())
        << "Report: NewL  = " << newLCLHash
        << ":" << sharedLCL.seq();

    // Tell directly connected peers that we have a new LCL
    callbacks_.statusChange (RCLCxCalls::ChangeType::Accepted,
        sharedLCL, haveCorrectLCL_);

    if (validating_)
        validating_ = callbacks_.shouldValidate(sharedLCL);

    if (validating_ && ! consensusFail_)
    {
        callbacks_.validate(sharedLCL, now_, proposing_);
        JLOG (j_.info())
            << "CNF Val " << newLCLHash;
    }
    else
        JLOG (j_.info())
            << "CNF buildLCL " << newLCLHash;

    // See if we can accept a ledger as fully-validated
    ledgerMaster_.consensusBuilt (sharedLCL.hackAccess(), getJson (true));

    {
        // Apply disputed transactions that didn't get in
        //
        // The first crack of transactions to get into the new
        // open ledger goes to transactions proposed by a validator
        // we trust but not included in the consensus set.
        //
        // These are done first because they are the most likely
        // to receive agreement during consensus. They are also
        // ordered logically "sooner" than transactions not mentioned
        // in the previous consensus round.
        //
        bool anyDisputes = false;
        for (auto& it : disputes_)
        {
            if (!it.second.getOurVote ())
            {
                // we voted NO
                try
                {
                    JLOG (j_.debug())
                        << "Test applying disputed transaction that did"
                        << " not get in";

                    RCLCxTx cTxn {it.second.tx()};
                    SerialIter sit (cTxn.txn().slice());

                    auto txn = std::make_shared<STTx const>(sit);

                    retriableTxs.insert (txn);

                    anyDisputes = true;
                }
                catch (std::exception const&)
                {
                    JLOG (j_.debug())
                        << "Failed to apply transaction we voted NO on";
                }
            }
        }
        callbacks_.createOpenLedger(sharedLCL, retriableTxs, anyDisputes);

    }

    ledgerMaster_.switchLCL (sharedLCL.hackAccess());

    assert (ledgerMaster_.getClosedLedger()->info().hash == sharedLCL.hash());
    assert (app_.openLedger().current()->info().parentHash == sharedLCL.hash());

    if (haveCorrectLCL_ && ! consensusFail_)
    {
        // we entered the round with the network,
        // see how close our close time is to other node's
        //  close time reports, and update our clock.
        JLOG (j_.info())
            << "We closed at " << closeTime_.time_since_epoch().count();
        using usec64_t = std::chrono::duration<std::uint64_t>;
        usec64_t closeTotal = closeTime_.time_since_epoch();
        int closeCount = 1;

        for (auto const& p : closeTimes_)
        {
            // FIXME: Use median, not average
            JLOG (j_.info())
                << beast::lexicalCastThrow <std::string> (p.second)
                << " time votes for "
                << beast::lexicalCastThrow <std::string>
                       (p.first.time_since_epoch().count());
            closeCount += p.second;
            closeTotal += usec64_t(p.first.time_since_epoch()) * p.second;
        }

        closeTotal += usec64_t(closeCount / 2);  // for round to nearest
        closeTotal /= closeCount;
        using duration = std::chrono::duration<std::int32_t>;
        using time_point = std::chrono::time_point<NetClock, duration>;
        auto offset = time_point{closeTotal} -
                      std::chrono::time_point_cast<duration>(closeTime_);
        JLOG (j_.info())
            << "Our close offset is estimated at "
            << offset.count() << " (" << closeCount << ")";
        callbacks_.adjustCloseTime(offset);
    }

    // we have accepted a new ledger
    bool correct;
    {
        std::lock_guard<std::recursive_mutex> _(lock_);

        state_ = State::accepted;
        correct = haveCorrectLCL_;
    }

    callbacks_.endConsensus (correct);
}

template <class Traits>
void LedgerConsensusImp<Traits>::createDisputes (
    TxSet_t const& m1,
    TxSet_t const& m2)
{
    if (m1.getID() == m2.getID())
        return;

    JLOG (j_.debug()) << "createDisputes "
        << m1.getID() << " to " << m2.getID();
    auto differences = m1.getDifferences (m2);

    int dc = 0;
    // for each difference between the transactions
    for (auto& id : differences)
    {
        ++dc;
        // create disputed transactions (from the ledger that has them)
        assert (
            (id.second && m1.getEntry(id.first) && !m2.getEntry(id.first)) ||
            (!id.second && !m1.getEntry(id.first) && m2.getEntry(id.first))
        );
        if (id.second)
            addDisputedTransaction (*m1.getEntry (id.first));
        else
            addDisputedTransaction (*m2.getEntry (id.first));
    }
    JLOG (j_.debug()) << dc << " differences found";
}

template <class Traits>
void LedgerConsensusImp<Traits>::addDisputedTransaction (
    Tx_t const& tx)
{
    auto txID = tx.getID();

    if (disputes_.find (txID) != disputes_.end ())
        return;

    JLOG (j_.debug()) << "Transaction "
        << txID << " is disputed";

    bool ourVote = false;

    // Update our vote on the disputed transaction
    if (ourSet_)
        ourVote = ourSet_->hasEntry (txID);

    Dispute_t txn {tx, ourVote, j_};

    // Update all of the peer's votes on the disputed transaction
    for (auto& pit : peerPositions_)
    {
        auto cit (acquired_.find (pit.second.getCurrentHash ()));

        if (cit != acquired_.end ())
            txn.setVote (pit.first,
                cit->second.hasEntry (txID));
    }

    // If we didn't relay this transaction recently, relay it to all peers
    if (app_.getHashRouter ().shouldRelay (txID))
    {
        auto const slice = tx.txn().slice();

        protocol::TMTransaction msg;
        msg.set_rawtransaction (slice.data(), slice.size());
        msg.set_status (protocol::tsNEW);
        msg.set_receivetimestamp (
            app_.timeKeeper().now().time_since_epoch().count());
        app_.overlay ().foreach (send_always (
            std::make_shared<Message> (
                msg, protocol::mtTRANSACTION)));
    }

    disputes_.emplace (txID, std::move (txn));
}

template <class Traits>
void LedgerConsensusImp<Traits>::adjustCount (TxSet_t const& map,
    std::vector<NodeID_t> const& peers)
{
    for (auto& it : disputes_)
    {
        bool setHas = map.hasEntry (it.first);
        for (auto const& pit : peers)
            it.second.setVote (pit, setHas);
    }
}

template <class Traits>
void LedgerConsensusImp<Traits>::leaveConsensus ()
{
    if (ourPosition_ && ! ourPosition_->isBowOut ())
    {
        ourPosition_->bowOut(now_);
        callbacks_.propose(*ourPosition_);
    }
    proposing_ = false;
}

template <class Traits>
void LedgerConsensusImp<Traits>::takeInitialPosition()
{
    auto pair = callbacks_.makeInitialPosition(previousLedger_, proposing_,
       haveCorrectLCL_,  closeTime_, now_ );
    auto const& initialSet = pair.first;
    auto const& initialPos = pair.second;
    assert (initialSet.getID() == initialPos.getCurrentHash());

    ourPosition_ = initialPos;
    ourSet_ = initialSet;

    for (auto& it : disputes_)
    {
        it.second.setOurVote (initialSet.hasEntry (it.first));
    }

    // When we take our initial position,
    // we need to create any disputes required by our position
    // and any peers who have already taken positions
    compares_.emplace (initialSet.getID());
    for (auto& it : peerPositions_)
    {
        auto hash = it.second.getCurrentHash();
        auto iit (acquired_.find (hash));
        if (iit != acquired_.end ())
        {
            if (compares_.emplace (hash).second)
                createDisputes (initialSet, iit->second);
        }
    }

    mapCompleteInternal (initialSet, false);

    if (proposing_)
        callbacks_.propose (*ourPosition_);
}

/** How many of the participants must agree to reach a given threshold?

    Note that the number may not precisely yield the requested percentage.
    For example, with with size = 5 and percent = 70, we return 3, but
    3 out of 5 works out to 60%. There are no security implications to
    this.

    @param participants the number of participants (i.e. validators)
    @param the percent that we want to reach

    @return the number of participants which must agree
*/
static
int
participantsNeeded (int participants, int percent)
{
    int result = ((participants * percent) + (percent / 2)) / 100;

    return (result == 0) ? 1 : result;
}

template <class Traits>
NetClock::time_point
LedgerConsensusImp<Traits>::effectiveCloseTime(NetClock::time_point closeTime)
{
    if (closeTime == NetClock::time_point{})
        return closeTime;

    return std::max<NetClock::time_point>(
        roundCloseTime (closeTime, closeResolution_),
        (previousLedger_.closeTime() + 1s));
}

template <class Traits>
void LedgerConsensusImp<Traits>::updateOurPositions ()
{
    // Compute a cutoff time
    auto peerCutoff = now_ - PROPOSE_FRESHNESS;
    auto ourCutoff = now_ - PROPOSE_INTERVAL;

    // Verify freshness of peer positions and compute close times
    std::map<NetClock::time_point, int> closeTimes;
    {
        auto it = peerPositions_.begin ();
        while (it != peerPositions_.end ())
        {
            if (it->second.isStale (peerCutoff))
            {
                // peer's proposal is stale, so remove it
                auto const& peerID = it->second.getNodeID ();
                JLOG (j_.warn())
                    << "Removing stale proposal from " << peerID;
                for (auto& dt : disputes_)
                    dt.second.unVote (peerID);
                it = peerPositions_.erase (it);
            }
            else
            {
                // proposal is still fresh
                ++closeTimes[effectiveCloseTime(it->second.getCloseTime())];
                ++it;
            }
        }
    }

    // This will stay unseated unless there are any changes
    boost::optional <TxSet_t> ourSet;

    // Update votes on disputed transactions
    {
        boost::optional <typename TxSet_t::mutable_t> changedSet;
        for (auto& it : disputes_)
        {
            // Because the threshold for inclusion increases,
            //  time can change our position on a dispute
            if (it.second.updateVote (closePercent_, proposing_))
            {
                if (! changedSet)
                    changedSet.emplace (*ourSet_);

                if (it.second.getOurVote ())
                {
                    // now a yes
                    changedSet->addEntry (it.second.tx());
                }
                else
                {
                    // now a no
                    changedSet->removeEntry (it.first);
                }
            }
        }
        if (changedSet)
        {
            ourSet.emplace (*changedSet);
        }
    }

    int neededWeight;

    if (closePercent_ < AV_MID_CONSENSUS_TIME)
        neededWeight = AV_INIT_CONSENSUS_PCT;
    else if (closePercent_ < AV_LATE_CONSENSUS_TIME)
        neededWeight = AV_MID_CONSENSUS_PCT;
    else if (closePercent_ < AV_STUCK_CONSENSUS_TIME)
        neededWeight = AV_LATE_CONSENSUS_PCT;
    else
        neededWeight = AV_STUCK_CONSENSUS_PCT;

    NetClock::time_point closeTime = {};
    haveCloseTimeConsensus_ = false;

    if (peerPositions_.empty ())
    {
        // no other times
        haveCloseTimeConsensus_ = true;
        closeTime = effectiveCloseTime(ourPosition_->getCloseTime());
    }
    else
    {
        int participants = peerPositions_.size ();
        if (proposing_)
        {
            ++closeTimes[effectiveCloseTime(ourPosition_->getCloseTime())];
            ++participants;
        }

        // Threshold for non-zero vote
        int threshVote = participantsNeeded (participants,
            neededWeight);

        // Threshold to declare consensus
        int const threshConsensus = participantsNeeded (
            participants, AV_CT_CONSENSUS_PCT);

        JLOG (j_.info()) << "Proposers:"
            << peerPositions_.size () << " nw:" << neededWeight
            << " thrV:" << threshVote << " thrC:" << threshConsensus;

        for (auto const& it : closeTimes)
        {
            JLOG (j_.debug()) << "CCTime: seq "
                << previousLedger_.seq() + 1 << ": "
                << it.first.time_since_epoch().count()
                << " has " << it.second << ", "
                << threshVote << " required";

            if (it.second >= threshVote)
            {
                // A close time has enough votes for us to try to agree
                closeTime = it.first;
                threshVote = it.second;

                if (threshVote >= threshConsensus)
                    haveCloseTimeConsensus_ = true;
            }
        }

        if (!haveCloseTimeConsensus_)
        {
            JLOG (j_.debug()) << "No CT consensus:"
                << " Proposers:" << peerPositions_.size ()
                << " Proposing:" << (proposing_ ? "yes" : "no")
                << " Thresh:" << threshConsensus
                << " Pos:" << closeTime.time_since_epoch().count();
        }
    }

    // Temporarily send a new proposal if there's any change to our
    // claimed close time. Once the new close time code is deployed
    // to the full network, this can be relaxed to force a change
    // only if the rounded close time has changed.
    if (! ourSet &&
            ((closeTime != ourPosition_->getCloseTime())
            || ourPosition_->isStale (ourCutoff)))
    {
        // close time changed or our position is stale
        ourSet.emplace (*ourSet_);
    }

    if (ourSet)
    {
        auto newHash = ourSet->getID();

        // Setting ourSet_ here prevents mapCompleteInternal
        // from checking for new disputes. But we only changed
        // positions on existing disputes, so no need to.
        ourSet_ = ourSet;

        JLOG (j_.info())
            << "Position change: CTime "
            << closeTime.time_since_epoch().count()
            << ", tx " << newHash;

        if (ourPosition_->changePosition (
            newHash, closeTime, now_))
        {
            if (proposing_)
                callbacks_.propose (*ourPosition_);

            mapCompleteInternal (*ourSet, false);
        }
    }
}

template <class Traits>
void LedgerConsensusImp<Traits>::playbackProposals ()
{
    callbacks_.getProposals (prevLedgerHash_,
        [=](Pos_t const& pos)
        {
            return peerPosition (now_, pos);
        });
}

template <class Traits>
void LedgerConsensusImp<Traits>::closeLedger ()
{
    state_ = State::establish;
    consensusStartTime_ = std::chrono::steady_clock::now ();
    closeTime_ = now_;
    consensus_.setLastCloseTime(closeTime_);

    callbacks_.statusChange (
        RCLCxCalls::ChangeType::Closing,
        previousLedger_,
        haveCorrectLCL_);

    takeInitialPosition ();
}

template <class Traits>
void LedgerConsensusImp<Traits>::beginAccept (bool synchronous)
{
    if (! ourPosition_ || ! ourSet_)
    {
        JLOG (j_.fatal())
            << "We don't have a consensus set";
        abort ();
    }

    consensus_.newLCL (peerPositions_.size (), roundTime_);

    if (synchronous)
        accept (*ourSet_);
    else
    {
        app_.getJobQueue().addJob (jtACCEPT, "acceptLedger",
            [that = this->shared_from_this(),
            consensusSet = *ourSet_]
            (Job &)
            {
                that->accept (consensusSet);
            });
    }
}

template <class Traits>
void LedgerConsensusImp<Traits>::startRound (
    Time_t const& now,
    LgrID_t const& prevLCLHash,
    Ledger_t const & prevLedger,
    int previousProposers,
    std::chrono::milliseconds previousConvergeTime)
{
    std::lock_guard<std::recursive_mutex> _(lock_);

    if (state_ == State::processing)
    {
        // We can't start a new round while we're processing
        return;
    }

    state_ = State::open;
    now_ = now;
    closeTime_ = now;
    prevLedgerHash_ = prevLCLHash;
    previousLedger_ = prevLedger;
    ourPosition_.reset();
    ourSet_.reset();
    consensusFail_ = false;
    roundTime_ = 0ms;
    closePercent_ = 0;
    haveCloseTimeConsensus_ = false;
    consensusStartTime_ = std::chrono::steady_clock::now();
    previousProposers_ = previousProposers;
    previousRoundTime_ = previousConvergeTime;
    inboundTransactions_.newRound (previousLedger_.seq());

    peerPositions_.clear();
    acquired_.clear();
    disputes_.clear();
    compares_.clear();
    closeTimes_.clear();
    deadNodes_.clear();

    closeResolution_ = getNextLedgerTimeResolution (
        previousLedger_.closeTimeResolution(),
        previousLedger_.getCloseAgree(),
        previousLedger_.seq() + 1);


    haveCorrectLCL_ = (previousLedger_.hash() == prevLedgerHash_);

    // We should not be proposing but not validating
    // Okay to validate but not propose
    std::tie(proposing_, validating_) = callbacks_.getMode(haveCorrectLCL_);
    assert (! proposing_ || validating_);

    if (validating_)
    {
        JLOG (j_.info())
            << "Entering consensus process, validating";
    }
    else
    {
        // Otherwise we just want to monitor the validation process.
        JLOG (j_.info())
            << "Entering consensus process, watching";
    }


    if (! haveCorrectLCL_)
    {
        // If we were not handed the correct LCL, then set our state
        // to not proposing.
        handleLCL (prevLedgerHash_);

        if (! haveCorrectLCL_)
        {
            JLOG (j_.info())
                << "Entering consensus with: "
                << previousLedger_.hash();
            JLOG (j_.info())
                << "Correct LCL is: " << prevLCLHash;
        }
    }

    playbackProposals ();
    if (peerPositions_.size() > (previousProposers_ / 2))
    {
        // We may be falling behind, don't wait for the timer
        // consider closing the ledger immediately
        timerEntry (now_);
    }

}

//------------------------------------------------------------------------------
std::shared_ptr <LedgerConsensusImp<RCLCxTraits>>
make_LedgerConsensus (
    Application& app,
    ConsensusImp& consensus,
    InboundTransactions& inboundTransactions,
    LedgerMaster& ledgerMaster,
    FeeVote& feeVote,
    RCLCxCalls& callbacks)
{
    return std::make_shared <LedgerConsensusImp <RCLCxTraits>> (app, consensus,
        inboundTransactions, ledgerMaster, feeVote, callbacks);
}

template class LedgerConsensusImp <RCLCxTraits>;

} // ripple
