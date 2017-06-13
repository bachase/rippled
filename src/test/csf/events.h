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
#ifndef RIPPLE_TEST_CSF_EVENTS_H_INCLUDED
#define RIPPLE_TEST_CSF_EVENTS_H_INCLUDED

#include <test/csf/Tx.h>
#include <test/csf/Validation.h>
#include <test/csf/ledgers.h>
#include <test/csf/Proposal.h>
#include <chrono>


namespace ripple {
namespace test {
namespace csf {

// Below are events which occur during the course of simulation. Each Peer
// is responsible for issuing the appropriate event.  A simulation can have
// a collector, which listens to the event updates, perhaps calculating
// statistics or storing events to a log for post-processing.
//
// Example collectors can be found in collectors.h, but have the general
// interface:
//
// @code
//     template <class T>
//     struct Collector
//     {
//        template <class Event>
//        void
//        on(NodeID who, SimTime when, Event e);
//     };
// @endcode
//
// CollectorRef.f defines a type-erased holder for arbitrary Collectors.  If
// any new events are added, the interface there needs to be updated.


/** A value received from another node
 */
template <class V>
struct Receive
{
    //! Node that sent the value
    NodeID from;

    //! The received value
    V val;
};

/** A value sent to all other nodes
 */
template <class V>
struct Relay
{
    //! Event that is sent
    V val;
};

/** Node starts a new consensus round
 */
struct StartRound
{
    //! The preferred ledger for the start of consensus
    Ledger::ID bestLedger;

    //! The prior ledger on hand
    Ledger prevLedger;
};

/** Node closed the open ledger
 */
struct CloseLedger
{
    // The ledger closed on
    Ledger prevLedger;

    // Initial txs for including in ledger
    TxSetType txs;
};

//! Node accepted consensus results
struct AcceptLedger
{
    // The newly created ledger
    Ledger ledger;

    // The prior ledger (this is a jump if prior.id() != ledger.parentID())
    Ledger prior;
};

//! Node detected a wrong prior ledger during consensus
struct WrongPrevLedger
{
    // ID of wrong ledger we had
    Ledger::ID wrong;
    // ID of what we think is the correct ledger
    Ledger::ID right;
};

//! Node fully validated a new ledger
struct FullyValidateLedger
{
    //! The new fully validated ledger
    Ledger ledger;

    //! The prior fully validated ledger
    //! This is a jump if prior.id() != ledger.parentID()
    Ledger prior;
};


}  // namespace csf
}  // namespace test
}  // namespace ripple

#endif
