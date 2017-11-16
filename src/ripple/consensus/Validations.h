//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2017 Ripple Labs Inc.

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

#ifndef RIPPLE_CONSENSUS_VALIDATIONS_H_INCLUDED
#define RIPPLE_CONSENSUS_VALIDATIONS_H_INCLUDED

#include <ripple/basics/Log.h>
#include <ripple/basics/UnorderedContainers.h>
#include <ripple/basics/chrono.h>
#include <ripple/beast/container/aged_container_utility.h>
#include <ripple/beast/container/aged_unordered_map.h>
#include <ripple/beast/utility/Journal.h>
#include <ripple/consensus/LedgerTrie.h>
#include <boost/optional.hpp>
#include <mutex>
#include <utility>
#include <vector>

namespace ripple {

/** Timing parameters to control validation staleness and expiration.

    @note These are protocol level parameters that should not be changed without
          careful consideration.  They are *not* implemented as static constexpr
          to allow simulation code to test alternate parameter settings.
 */
struct ValidationParms
{
    /** The number of seconds a validation remains current after its ledger's
        close time.

        This is a safety to protect against very old validations and the time
        it takes to adjust the close time accuracy window.
    */
    std::chrono::seconds validationCURRENT_WALL = std::chrono::minutes{5};

    /** Duration a validation remains current after first observed.

        The number of seconds a validation remains current after the time we
        first saw it. This provides faster recovery in very rare cases where the
        number of validations produced by the network is lower than normal
    */
    std::chrono::seconds validationCURRENT_LOCAL = std::chrono::minutes{3};

    /** Duration pre-close in which validations are acceptable.

        The number of seconds before a close time that we consider a validation
        acceptable. This protects against extreme clock errors
    */
    std::chrono::seconds validationCURRENT_EARLY = std::chrono::minutes{3};

    /** Duration a set of validations for a given ledger hash remain valid

        The number of seconds before a set of validations for a given ledger
        hash can expire.  This keeps validations for recent ledgers available
        for a reasonable interval.
    */
    std::chrono::seconds validationSET_EXPIRES = std::chrono::minutes{10};
};


/** Whether a validation is still current

    Determines whether a validation can still be considered the current
    validation from a node based on when it was signed by that node and first
    seen by this node.

    @param p ValidationParms with timing parameters
    @param now Current time
    @param signTime When the validation was signed
    @param seenTime When the validation was first seen locally
*/
inline bool
isCurrent(
    ValidationParms const& p,
    NetClock::time_point now,
    NetClock::time_point signTime,
    NetClock::time_point seenTime)
{
    // Because this can be called on untrusted, possibly
    // malicious validations, we do our math in a way
    // that avoids any chance of overflowing or underflowing
    // the signing time.

    return (signTime > (now - p.validationCURRENT_EARLY)) &&
        (signTime < (now + p.validationCURRENT_WALL)) &&
        ((seenTime == NetClock::time_point{}) ||
         (seenTime < (now + p.validationCURRENT_LOCAL)));
}

/** Determine the preferred ledger based on its support

    @param current The current ledger the node follows
    @param dist Ledger IDs and corresponding counts of support
    @return The ID of the ledger with most support, preferring to stick with
            current ledger in the case of equal support
*/
template <class ID>
inline ID
getPreferredLedger(
    ID const& current,
    hash_map<ID, std::uint32_t> const& dist)
{
    ID netLgr = current;
    int netLgrCount = 0;
    for (auto const& it : dist)
    {
        // Switch to ledger supported by more peers
        // On a tie, prefer the current ledger, or the one with higher ID
        if ((it.second > netLgrCount) ||
            ((it.second == netLgrCount) &&
             ((it.first == current) ||
              (it.first > netLgr && netLgr != current))))
        {
            netLgr = it.first;
            netLgrCount = it.second;
        }
    }
    return netLgr;
}

/** Monitors the preferred validation chain.

    Uses the LedgerTrie class to monitor the preferred validation chain. This
    is based on trusted partial and full validations. It should not be used
    for determining whether the full validation quorum is reached; only to
    ask questions about which ledger chains validators are operating on.

    In order to determine ledger history, the Ledger validated is needed to
    query the ids of its ancestors.

    // TODO: partial
    // TODO: stale?
    // TODO: untrusted?
*/
template <class Adaptor>
class Preferred
{
    template <typename T>

    using decay_result_t = std::decay_t<std::result_of_t<T>>;
    using Validation = typename Adaptor::Validation;
    using Ledger = typename Adaptor::Ledger;
    using ID = typename Ledger::ID;
    using Seq = typename Ledger::Seq;
    using NodeKey = typename Validation::NodeKey;

    Adaptor adaptor_;

    // Represents the ancestry of validated ledgers
    LedgerTrie<Ledger> trie_;

    // Last validation received from a validator (full or partial)
    hash_map<NodeKey, Validation> lastValidation_;

    // Last (validated) ledger successfully acquired. If in this map, it is
    // accounted for in the trie.
    hash_map<NodeKey, Ledger> lastLedger_;

    // Set of ledgers being acquired from the network
    hash_map<ID, hash_set<NodeKey>> acquiring_;

    void
    checkAcquired()
    {
        for (auto it = acquiring_.begin(); it != acquiring_.end();)
        {
            if (Ledger ledger = adaptor_.acquire(it.first))
            {
                for (NodeKey const& key : it.second)
                    updateTrie(key, ledger);

                it = acquiring_.erase(it);
            }
            else
                ++it;
        }
    }

    void
    updateTrie(NodeKey const & key, Ledger ledger)
    {
        assert(val.id() == ledger.id());

        auto const ins = lastLedger_.emplace(key, ledger);
        if(!ins.second)
        {
            trie.remove(ins.first->second);
            ins.first->second = ledger;
        }
        trie.insert(ledger);
    }

public:
    /** Process a new validation

        Process a new trusted validation from a validator. This will be
        reflected only after the validated ledger is succesfully acquired by
        the local node. In the interim, the prior validated ledger from this
        node remains.

        @param key The master public key identifying the validating node
        @param val The trusted validation issued by the node
    */
    void
    update(NodeKey const & key, Validation const & val)
    {
        assert(val.isTrusted());

        auto const ins = lastValidation_.emplace(key, val);
        if(!ins.second)
        {
            // Clear any prior acquiring ledger for this node
            auto it = acquiring_.find(ins.first->second.id());
            if(it != acquiring_.end())
                it->second.erase(key);
            // Set the last validation
            ins.first->second = val;
        }

        checkAcquired();

        if(Ledger ledger = adaptor_.acquire(val.id()))
            updateTrie(key, ledger);
        else
            acquiring_[val.id()].insert(key);

    }

    /** Return the ID of the preferred working ledger

        A ledger is preferred if it has more support amongst trusted validators
        and is *not* an ancestor of the current working ledger; otherwise it
        remains the current working ledger.

        @param ledger The local nodes current working ledger
        @param minValidSeq The minimum allowable sequence number of th preferred
                           ledger

    */
    ID
    getPreferred(Ledger const & ledger, Seq minValidSeq)
    {
        checkAcquired();
        Seq seq;
        ID id;
        std::tie(seq, id) = trie.getPreferred();

        if(seq < minValidSeq)
            return ledger.id();

        // A ledger ahead of us is preferred regardless of whether it is
        // a descendent of our working ledger or it is on a different chain
        if(seq > ledger.seq())
            return id;

        // Only switch to earlier sequence numbers if it is a differnt chain
        if(ledger[seq] != id )
            return id;

        // Stay on the current ledger by default
        return ledger.id();

    }
};

/** Maintains current and recent ledger validations.

    Manages storage and queries related to validations received on the network.
    Stores the most current validation from nodes and sets of recent
    validations grouped by ledger identifier.

    Stored validations are not necessarily from trusted nodes, so clients
    and implementations should take care to use `trusted` member functions or
    check the validation's trusted status.

    This class uses a generic interface to allow adapting Validations for
    specific applications. The Adaptor template implements a set of helper
    functions that and type definitions. The code stubs below outline the
    interface and type requirements.


    @warning The Adaptor::MutexType is used to manage concurrent access to
             private members of Validations but does not manage any data in the
             Adaptor instance itself.

    @code

    // Conforms to the Ledger type requirements of LedgerTrie
    struct Ledger
    {
        using ID = ID;
        using Seq = Seq;

        // The default ledger represents a ledger that prefixes all other ledgers
        // (aka the genesis ledger)
        Ledger();

        // Return the sequence number of this ledger
        Seq seq() const;

        // Return the ID of this ledger's ancestor with given sequence number
        ID operator[](Seq s);
    };

    struct Validation
    {
        using NodeKey = ...;
        using NodeID = ...;

        // Ledger ID associated with this validation
        Ledger::ID ledgerID() const;

        // Sequence number of validation's ledger (0 means no sequence number)
        Ledger::Seq seq() const

        // When the validation was signed
        NetClock::time_point signTime() const;

        // When the validation was first observed by this node
        NetClock::time_point seenTime() const;

        // Signing key of node that published the validation
        NodeKey key() const;

        // Identifier of node that published the validation
        NodeID nodeID() const;

        // Whether the publishing node was trusted at the time the validation
        // arrived
        bool trusted() const;

        implementation_specific_t
        unwrap() -> return the implementation-specific type being wrapped

        // ... implementation specific
    };

    class Adaptor
    {
        using Mutex = std::mutex;
        using Validation = Validation;
        using Ledger = Ledger;

        // Handle a newly stale validation, this should do minimal work since
        // it is called by Validations while it may be iterating Validations
        // under lock
        void onStale(Validation && );

        // Flush the remaining validations (typically done on shutdown)
        void flush(hash_map<NodeKey,Validation> && remaining);

        // Return the current network time (used to determine staleness)
        NetClock::time_point now() const;

        // Attempt to acquire a specific ledger.
        boost::optional<Ledger> acquireLedger(Ledger::ID const & ledgerID);

        // ... implementation specific
    };
    @endcode

    @tparam Adaptor Provides type definitions and callbacks

*/
template <class Adaptor>
class Validations
{

    using Mutex = typename Adaptor::Mutex;
    using Validation = typename Adaptor::Validation;
    using Ledger = typename Adaptor::Ledger;
    using ID = typename Ledger::ID;
    using Seq = typename Ledger::Seq;
    using NodeKey = typename Validation::NodeKey;
    using NodeID = typename Validation::NodeID;

    using WrappedValidationType = std::decay_t<
        std::result_of_t<decltype (&Validation::unwrap)(Validation)>>;

    using ScopedLock = std::lock_guard<Mutex>;

    // Manages concurrent access to current_ and byLedger_
    Mutex mutex_;

    //! For the most recent validation, we also want to store the ID
    //! of the ledger it replaces
    struct ValidationAndPrevID
    {
        ValidationAndPrevID(Validation const& v) : val{v}, prevLedgerID{0}
        {
        }

        Validation val;
        ID prevLedgerID;
    };

    //! The latest validation from each node
    hash_map<NodeKey, ValidationAndPrevID> current_;

    //! Recent validations from nodes, indexed by ledger identifier
    beast::aged_unordered_map<
        ID,
        hash_map<NodeKey, Validation>,
        std::chrono::steady_clock,
        beast::uhash<>>
        byLedger_;

    //! Parameters to determine validation staleness
    ValidationParms const parms_;

    beast::Journal j_;

    //! Adaptor instance
    //! Is NOT managed by the mutex_ above
    Adaptor adaptor_;

private:
    /** Iterate current validations.

        Iterate current validations, optionally removing any stale validations
        if a time is specified.

        @param t (Optional) Time used to determine staleness
        @param pre Invokable with signature (std::size_t) called prior to
                   looping.
        @param f Invokable with signature (NodeKey const &, Validations const &)
                 for each current validation.

        @note The invokable `pre` is called _prior_ to checking for staleness
              and reflects an upper-bound on the number of calls to `f.
        @warning The invokable `f` is expected to be a simple transformation of
                 its arguments and will be called with mutex_ under lock.
    */

    template <class Pre, class F>
    void
    current(boost::optional<NetClock::time_point> t, Pre&& pre, F&& f)
    {
        ScopedLock lock{mutex_};
        pre(current_.size());
        auto it = current_.begin();
        while (it != current_.end())
        {
            // Check for staleness, if time specified
            if (t &&
                !isCurrent(
                    parms_, *t, it->second.val.signTime(), it->second.val.seenTime()))
            {
                // contains a stale record
                adaptor_.onStale(std::move(it->second.val));
                it = current_.erase(it);
            }
            else
            {
                auto cit = typename decltype(current_)::const_iterator{it};
                // contains a live record
                f(cit->first, cit->second);
                ++it;
            }
        }
    }

    /** Iterate the set of validations associated with a given ledger id

        @param ledgerID The identifier of the ledger
        @param pre Invokable with signature(std::size_t)
        @param f Invokable with signature (NodeKey const &, Validation const &)

        @note The invokable `pre` is called prior to iterating validations. The
              argument is the number of times `f` will be called.
        @warning The invokable f is expected to be a simple transformation of
       its arguments and will be called with mutex_ under lock.
    */
    template <class Pre, class F>
    void
    byLedger(ID const& ledgerID, Pre&& pre, F&& f)
    {
        ScopedLock lock{mutex_};
        auto it = byLedger_.find(ledgerID);
        if (it != byLedger_.end())
        {
            // Update set time since it is being used
            byLedger_.touch(it);
            pre(it->second.size());
            for (auto const& keyVal : it->second)
                f(keyVal.first, keyVal.second);
        }
    }

public:
    /** Constructor

        @param p ValidationParms to control staleness/expiration of validaitons
        @param c Clock to use for expiring validations stored by ledger
        @param j Journal used for logging, passed to Adaptor constructor
        @param ts Parameters for constructing Adaptor instance
    */
    template <class... Ts>
    Validations(
        ValidationParms const& p,
        beast::abstract_clock<std::chrono::steady_clock>& c,
        beast::Journal j,
        Ts&&... ts)
        : byLedger_(c), parms_(p), j_(j), adaptor_(std::forward<Ts>(ts)..., j)
    {
    }

    /** Return the validation timing parameters
     */
    ValidationParms const&
    parms() const
    {
        return parms_;
    }

    /** Return the journal
     */
    beast::Journal
    journal() const
    {
        return j_;
    }

    /** Result of adding a new validation
     */
    enum class AddOutcome {
        /// This was a new validation and was added
        current,
        /// Already had this validation
        repeat,
        /// Not current or was older than current from this node
        stale,
        /// Had a validation with same sequence number
        sameSeq,
    };

    /** Add a new validation

        Attempt to add a new validation.

        @param key The NodeKey to use for the validation
        @param val The validation to store
        @return The outcome of the attempt

        @note The provided key may differ from the validation's
              key() member since we might be storing by master key and the
              validation might be signed by a temporary or rotating key.

    */
    AddOutcome
    add(NodeKey const& key, Validation const& val)
    {
        NetClock::time_point t = adaptor_.now();
        if (!isCurrent(parms_, t, val.signTime(), val.seenTime()))
            return AddOutcome::stale;

        ID const& id = val.ledgerID();

        // This is only seated if a validation became stale
        boost::optional<Validation> maybeStaleValidation;

        AddOutcome result = AddOutcome::current;

        {
            ScopedLock lock{mutex_};

            auto const ret = byLedger_[id].emplace(key, val);

            // This validation is a repeat if we already have
            // one with the same id and signing key.
            if (!ret.second && ret.first->second.key() == val.key())
                return AddOutcome::repeat;

            // Attempt to insert
            auto const ins = current_.emplace(key, val);

            if (!ins.second)
            {
                // Had a previous validation from the node, consider updating
                Validation& oldVal = ins.first->second.val;
                ID const previousLedgerID = ins.first->second.prevLedgerID;

                Seq const oldSeq{oldVal.seq()};
                Seq const newSeq{val.seq()};

                // Sequence of 0 indicates a missing sequence number
                if ((oldSeq != Seq{0}) && (newSeq != Seq{0}) &&
                    oldSeq == newSeq)
                {
                    result = AddOutcome::sameSeq;

                    // If the validation key was revoked, update the
                    // existing validation in the byLedger_ set
                    if (val.key() != oldVal.key())
                    {
                        auto const mapIt = byLedger_.find(oldVal.ledgerID());
                        if (mapIt != byLedger_.end())
                        {
                            auto& validationMap = mapIt->second;
                            // If a new validation with the same ID was
                            // reissued we simply replace.
                            if(oldVal.ledgerID() == val.ledgerID())
                            {
                                auto replaceRes = validationMap.emplace(key, val);
                                // If it was already there, replace
                                if(!replaceRes.second)
                                    replaceRes.first->second = val;
                            }
                            else
                            {
                                // If the new validation has a different ID,
                                // we remove the old.
                                validationMap.erase(key);
                                // Erase the set if it is now empty
                                if (validationMap.empty())
                                    byLedger_.erase(mapIt);
                            }
                        }
                    }
                }

                if (val.signTime() > oldVal.signTime() ||
                    val.key() != oldVal.key())
                {
                    // This is either a newer validation or a new signing key
                    ID const prevID = [&]() {
                        // In the normal case, the prevID is the ID of the
                        // ledger we replace
                        if (oldVal.ledgerID() != val.ledgerID())
                            return oldVal.ledgerID();
                        // In the case the key was revoked and a new validation
                        // for the same ledger ID was sent, the previous ledger
                        // is still the one the now revoked validation had
                        return previousLedgerID;
                    }();

                    // Allow impl to take over oldVal
                    maybeStaleValidation.emplace(std::move(oldVal));
                    // Replace old val in the map and set the previous ledger ID
                    ins.first->second.val = val;
                    ins.first->second.prevLedgerID = prevID;
                }
                else
                {
                    // We already have a newer validation from this source
                    result = AddOutcome::stale;
                }
            }
        }

        // Handle the newly stale validation outside the lock
        if (maybeStaleValidation)
        {
            adaptor_.onStale(std::move(*maybeStaleValidation));
        }

        return result;
    }

    /** Expire old validation sets

        Remove validation sets that were accessed more than
        validationSET_EXPIRES ago.
    */
    void
    expire()
    {
        ScopedLock lock{mutex_};
        beast::expire(byLedger_, parms_.validationSET_EXPIRES);
    }

    /** Distribution of current trusted validations

        Calculates the distribution of current validations but allows
        ledgers one away from the current ledger to count as the current.

        @param currentLedger The identifier of the ledger we believe is current
                             (0 if unknown)
        @param priorLedger The identifier of our previous current ledger
                           (0 if unknown)
        @param cutoffBefore Ignore ledgers with sequence number before this

        @return Map representing the distribution of ledgerID by count
    */
    hash_map<ID, std::uint32_t>
    currentTrustedDistribution(
        ID const& currentLedger,
        ID const& priorLedger,
        Seq cutoffBefore)
    {
        bool const valCurrentLedger = currentLedger != ID{0};
        bool const valPriorLedger = priorLedger != ID{0};

        hash_map<ID, std::uint32_t> ret;

        current(
            adaptor_.now(),
            // The number of validations does not correspond to the number of
            // distinct ledgerIDs so we do not call reserve on ret.
            [](std::size_t) {},
            [this,
             &cutoffBefore,
             &currentLedger,
             &valCurrentLedger,
             &valPriorLedger,
             &priorLedger,
             &ret](NodeKey const&, ValidationAndPrevID const& vp) {
                Validation const& v = vp.val;
                ID const& prevLedgerID = vp.prevLedgerID;
                if (!v.trusted())
                    return;

                Seq const seq = v.seq();
                if ((seq == Seq{0}) || (seq >= cutoffBefore))
                {
                    // contains a live record
                    bool countPreferred =
                        valCurrentLedger && (v.ledgerID() == currentLedger);

                    if (!countPreferred &&  // allow up to one ledger slip in
                                            // either direction
                        ((valCurrentLedger &&
                          (prevLedgerID == currentLedger)) ||
                         (valPriorLedger && (v.ledgerID() == priorLedger))))
                    {
                        countPreferred = true;
                        JLOG(this->j_.trace()) << "Counting for " << currentLedger
                                         << " not " << v.ledgerID();
                    }

                    if (countPreferred)
                        ret[currentLedger]++;
                    else
                        ret[v.ledgerID()]++;
                }
            });

        return ret;
    }

    /** Count the number of current trusted validators working on the next
        ledger.

        Counts the number of current trusted validations that replaced the
        provided ledger.  Does not check or update staleness of the validations.

        @param ledgerID The identifier of the preceding ledger of interest
        @return The number of current trusted validators with ledgerID as the
                prior ledger.
    */
    std::size_t
    getNodesAfter(ID const& ledgerID)
    {
        std::size_t count = 0;

        // Historically this did not not check for stale validations
        // That may not be important, but this preserves the behavior
        current(
            boost::none,
            [&](std::size_t) {}, // nothing to reserve
            [&](NodeKey const&, ValidationAndPrevID const& v) {
                if (v.val.trusted() && v.prevLedgerID == ledgerID)
                    ++count;
            });
        return count;
    }

    /** Get the currently trusted validations

        @return Vector of validations from currently trusted validators
    */
    std::vector<WrappedValidationType>
    currentTrusted()
    {
        std::vector<WrappedValidationType> ret;

        current(
            adaptor_.now(),
            [&](std::size_t numValidations) { ret.reserve(numValidations); },
            [&](NodeKey const&, ValidationAndPrevID const& v) {
                if (v.val.trusted())
                    ret.push_back(v.val.unwrap());
            });
        return ret;
    }

    /** Get the set of known public keys associated with current validations

        @return The set of of knowns keys for current trusted and untrusted
                validations
    */
    hash_set<NodeKey>
    getCurrentPublicKeys()
    {
        hash_set<NodeKey> ret;
        current(
            adaptor_.now(),
            [&](std::size_t numValidations) { ret.reserve(numValidations); },
            [&](NodeKey const& k, ValidationAndPrevID const&) { ret.insert(k); });

        return ret;
    }

    /** Count the number of trusted validations for the given ledger

        @param ledgerID The identifier of ledger of interest
        @return The number of trusted validations
    */
    std::size_t
    numTrustedForLedger(ID const& ledgerID)
    {
        std::size_t count = 0;
        byLedger(
            ledgerID,
            [&](std::size_t) {}, // nothing to reserve
            [&](NodeKey const&, Validation const& v) {
                if (v.trusted())
                    ++count;
            });
        return count;
    }

    /**  Get set of trusted validations associated with a given ledger

         @param ledgerID The identifier of ledger of interest
         @return Trusted validations associated with ledger
    */
    std::vector<WrappedValidationType>
    getTrustedForLedger(ID const& ledgerID)
    {
        std::vector<WrappedValidationType> res;
        byLedger(
            ledgerID,
            [&](std::size_t numValidations) { res.reserve(numValidations); },
            [&](NodeKey const&, Validation const& v) {
                if (v.trusted())
                    res.emplace_back(v.unwrap());
            });

        return res;
    }

    /** Return the sign times of all validations associated with a given ledger

        @param ledgerID The identifier of ledger of interest
        @return Vector of times
    */
    std::vector<NetClock::time_point>
    getTrustedValidationTimes(ID const& ledgerID)
    {
        std::vector<NetClock::time_point> times;
        byLedger(
            ledgerID,
            [&](std::size_t numValidations) { times.reserve(numValidations); },
            [&](NodeKey const&, Validation const& v) {
                if (v.trusted())
                    times.emplace_back(v.signTime());
            });
        return times;
    }

    /** Returns fees reported by trusted validators in the given ledger

        @param ledgerID The identifier of ledger of interest
        @param baseFee The fee to report if not present in the validation
        @return Vector of fees
    */
    std::vector<std::uint32_t>
    fees(ID const& ledgerID, std::uint32_t baseFee)
    {
        std::vector<std::uint32_t> res;
        byLedger(
            ledgerID,
            [&](std::size_t numValidations) { res.reserve(numValidations); },
            [&](NodeKey const&, Validation const& v) {
                if (v.trusted())
                {
                    boost::optional<std::uint32_t> loadFee = v.loadFee();
                    if (loadFee)
                        res.push_back(*loadFee);
                    else
                        res.push_back(baseFee);
                }
            });
        return res;
    }

    /** Flush all current validations
     */
    void
    flush()
    {
        JLOG(j_.info()) << "Flushing validations";

        hash_map<NodeKey, Validation> flushed;
        {
            ScopedLock lock{mutex_};
            for (auto it : current_)
            {
                flushed.emplace(it.first, std::move(it.second.val));
            }
            current_.clear();
        }

        adaptor_.flush(std::move(flushed));

        JLOG(j_.debug()) << "Validations flushed";
    }
};
}  // namespace ripple
#endif
