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
#include <BeastConfig.h>
#include <ripple/basics/tagged_integer.h>
#include <ripple/beast/clock/manual_clock.h>
#include <ripple/beast/unit_test.h>
#include <ripple/consensus/Validations.h>
#include <test/csf/Validation.h>

#include <tuple>
#include <type_traits>
#include <vector>
#include <memory>

namespace ripple {
namespace test {
namespace csf {
class Validations_test : public beast::unit_test::suite
{
    using clock_type = beast::abstract_clock<std::chrono::steady_clock> const;

    // Helper to convert steady_clock to a reasonable NetClock
    // This allows a single manual clock in the unit tests
    static NetClock::time_point
    toNetClock(clock_type const& c)
    {
        // We don't care about the actual epochs, but do want the
        // generated NetClock time to be well past its epoch to ensure
        // any subtractions are positive
        using namespace std::chrono;
        return NetClock::time_point(duration_cast<NetClock::duration>(
            c.now().time_since_epoch() + 86400s));
    }

    // Represents a node that can issue validations
    class Node
    {
        clock_type const& c_;
        PeerID nodeID_;
        bool trusted_ = true;
        std::size_t signIdx_ = 1;
        boost::optional<std::uint32_t> loadFee_;

    public:
        Node(PeerID nodeID, clock_type const& c) : c_(c), nodeID_(nodeID)
        {
        }

        void
        untrust()
        {
            trusted_ = false;
        }

        void
        trust()
        {
            trusted_ = true;
        }

        void
        setLoadFee(std::uint32_t fee)
        {
            loadFee_ = fee;
        }

        PeerID
        nodeID() const
        {
            return nodeID_;
        }

        void
        advanceKey()
        {
            signIdx_++;
        }

        PeerKey
        currKey() const
        {
            return std::make_pair(nodeID_, signIdx_);
        }

        PeerKey
        masterKey() const
        {
            return std::make_pair(nodeID_, 0);
        }
        NetClock::time_point
        now() const
        {
            return toNetClock(c_);
        }

        // Issue a new validation with given sequence number and id and
        // with signing and seen times offset from the common clock
        Validation
        validation(Ledger ledger,
            NetClock::duration signOffset,
            NetClock::duration seenOffset,
            bool full = true) const
        {
            Validation v{ledger.id(),
                         ledger.seq(),
                         now() + signOffset,
                         now() + seenOffset,
                         currKey(),
                         nodeID_,
                         full,
                         loadFee_};
            if(trusted_)
                v.setTrusted();
            return v;
        }

        // Issue a new validation with the given sequence number and id
        Validation
        validation(Ledger ledger) const
        {
            return validation(
                ledger, NetClock::duration{0}, NetClock::duration{0}, true);
        }

        Validation
        partial(Ledger ledger) const
        {
            return validation(
                ledger, NetClock::duration{0}, NetClock::duration{0}, false);
        }
    };


    // Saved StaleData for inspection in test
    struct StaleData
    {
        std::vector<Validation> stale;
        hash_map<PeerKey, Validation> flushed;
    };

    // Generic Validations adaptor that saves stale/flushed data into
    // a StaleData instance.
    class Adaptor
    {
        StaleData& staleData_;
        clock_type& c_;
        LedgerOracle& oracle_;

    public:
        // Non-locking mutex to avoid locks in generic Validations
        struct Mutex
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

        using Validation = csf::Validation;
        using Ledger = csf::Ledger;

        Adaptor(StaleData& sd, clock_type& c, LedgerOracle& o, beast::Journal)
            : staleData_{sd}, c_{c}, oracle_{o}
        {
        }

        NetClock::time_point
        now() const
        {
            return toNetClock(c_);
        }

        void
        onStale(Validation&& v)
        {
            staleData_.stale.emplace_back(std::move(v));
        }

        void
        flush(hash_map<PeerKey, Validation>&& remaining)
        {
            staleData_.flushed = std::move(remaining);
        }

        boost::optional<Ledger>
        acquire(Ledger::ID const &id)
        {
            return oracle_.lookup(id);
        }

    };

    // Specialize generic Validations using the above types
    using TestValidations = Validations<Adaptor>;

    // Hoist enum for writing simpler tests
    using AddOutcome = TestValidations::AddOutcome;

    // Gather the dependencies of TestValidations in a single class and provide
    // accessors for simplifying test logic
    class TestHarness
    {
        StaleData staleData_;
        ValidationParms p_;
        beast::manual_clock<std::chrono::steady_clock> clock_;
        beast::Journal j_;
        TestValidations tv_;
        PeerID nextNodeId_{0};

    public:
        TestHarness(LedgerOracle& o)
            : tv_(p_, clock_, j_, staleData_, clock_, o)
        {
        }
        // Helper to add an existing validation
        AddOutcome
        add(Node const& n, Validation const& v)
        {
            return tv_.add(n.masterKey(), v);
        }
        AddOutcome
        add(Node const& n, Ledger const& l)
        {
            return add(n, n.validation(l));
        }

        // Helper to directly create the validation
        template <class... Ts>
        std::enable_if_t<(sizeof...(Ts) > 1), AddOutcome>
        add(Node const& n, Ts&&... ts)
        {
            return add(n, n.validation(std::forward<Ts>(ts)...));
        }

        TestValidations&
        vals()
        {
            return tv_;
        }

        Node
        makeNode()
        {
            return Node(nextNodeId_++, clock_);
        }

        ValidationParms
        parms() const
        {
            return p_;
        }

        auto&
        clock()
        {
            return clock_;
        }

        std::vector<Validation> const&
        stale() const
        {
            return staleData_.stale;
        }

        hash_map<PeerKey, Validation> const&
        flushed() const
        {
            return staleData_.flushed;
        }
    };

    void
    testAddValidation()
    {
        using namespace std::chrono_literals;

        testcase("Add validation");
        LedgerHistoryHelper h;
        Ledger ledgerA = h["a"];
        Ledger ledgerAB = h["ab"];
        Ledger ledgerABC = h["abc"];
        Ledger ledgerABCD = h["abcd"];
        Ledger ledgerABCDE = h["abcde"];

        {
            TestHarness harness(h.oracle);
            Node n = harness.makeNode();

            auto const v = n.validation(ledgerA);

            // Add a current validation
            BEAST_EXPECT(AddOutcome::current == harness.add(n, v));

            // Re-adding violates the increasing seq requirement for full
            // validations
            BEAST_EXPECT(AddOutcome::badFull == harness.add(n, v));

            harness.clock().advance(1s);
            // Replace with a new validation and ensure the old one is stale
            BEAST_EXPECT(harness.stale().empty());

            BEAST_EXPECT(AddOutcome::current == harness.add(n, ledgerAB));

            BEAST_EXPECT(harness.stale().size() == 1);

            BEAST_EXPECT(harness.stale()[0].ledgerID() == ledgerA.id());

            // Test the node changing signing key

            // Confirm old ledger on hand, but not new ledger
            BEAST_EXPECT(
                harness.vals().numTrustedForLedger(ledgerAB.id()) == 1);
            BEAST_EXPECT(
                harness.vals().numTrustedForLedger(ledgerABC.id()) == 0);

            // Rotate signing keys
            n.advanceKey();

            harness.clock().advance(1s);

            // Cannot re-do the same full validation sequence
            BEAST_EXPECT(AddOutcome::badFull == harness.add(n, ledgerAB));
            // Can send a new partial validation
            BEAST_EXPECT(
                AddOutcome::current == harness.add(n, n.partial(ledgerAB)));

            // Now trusts the newest ledger too
            harness.clock().advance(1s);
            BEAST_EXPECT(AddOutcome::current == harness.add(n, ledgerABC));
            BEAST_EXPECT(
                harness.vals().numTrustedForLedger(ledgerAB.id()) == 1);
            BEAST_EXPECT(
                harness.vals().numTrustedForLedger(ledgerABC.id()) == 1);

            // Processing validations out of order should ignore the older
            // validation
            harness.clock().advance(2s);
            auto const val3 = n.validation(ledgerABCDE);

            harness.clock().advance(4s);
            auto const val4 = n.validation(ledgerABCD);

            BEAST_EXPECT(AddOutcome::current == harness.add(n, val4));

            BEAST_EXPECT(AddOutcome::stale == harness.add(n, val3));
        }

        {
            // Process validations out of order with shifted times

            TestHarness harness(h.oracle);
            Node n = harness.makeNode();

            // Establish a new current validation
            BEAST_EXPECT(AddOutcome::current == harness.add(n, ledgerA));

            // Process a validation that has "later" seq but early sign time
            BEAST_EXPECT(
                AddOutcome::stale == harness.add(n, ledgerAB, -1s, -1s));

            // Process a validation that has an "leter" seq and later sign
            // time
            BEAST_EXPECT(
                AddOutcome::current == harness.add(n, ledgerABC, 1s, 1s));
        }

        {
            // Test stale on arrival validations
            TestHarness harness(h.oracle);
            Node n = harness.makeNode();

            BEAST_EXPECT(
                AddOutcome::stale ==
                harness.add(
                    n, ledgerA, -harness.parms().validationCURRENT_EARLY, 0s));

            BEAST_EXPECT(
                AddOutcome::stale ==
                harness.add(
                    n, ledgerA, harness.parms().validationCURRENT_WALL, 0s));

            BEAST_EXPECT(
                AddOutcome::stale ==
                harness.add(
                    n, ledgerA, 0s, harness.parms().validationCURRENT_LOCAL));
        }

        {
            // Test partials for older sequence numbers
            TestHarness harness(h.oracle);
            Node n = harness.makeNode();
            BEAST_EXPECT(AddOutcome::current == harness.add(n, ledgerABC));
            harness.clock().advance(1s);
            BEAST_EXPECT(ledgerAB.seq() < ledgerABC.seq());
            BEAST_EXPECT(AddOutcome::badFull == harness.add(n, ledgerAB));
            BEAST_EXPECT(
                AddOutcome::current == harness.add(n, n.partial(ledgerAB)));
        }
    }

    void
    testOnStale()
    {
        testcase("Stale validation");
        // Verify validation becomes stale based solely on time passing
        LedgerHistoryHelper h;
        TestHarness harness(h.oracle);
        Node n = harness.makeNode();
        Ledger a = h["a"];

        BEAST_EXPECT(AddOutcome::current == harness.add(n, a));
        harness.vals().currentTrusted();
        BEAST_EXPECT(harness.stale().empty());
        harness.clock().advance(harness.parms().validationCURRENT_LOCAL);

        // trigger iteration over current
        harness.vals().currentTrusted();

        BEAST_EXPECT(harness.stale().size() == 1);
        BEAST_EXPECT(harness.stale()[0].ledgerID() == a.id());
    }

    void
    testGetNodesAfter()
    {
        // Test getting number of nodes working on a validation descending
        // a prescribed one. This count should only be for trusted nodes, but
        // includes partial and full validations

        using namespace std::chrono_literals;
        testcase("Get nodes after");

        LedgerHistoryHelper h;
        Ledger ledgerA = h["a"];
        Ledger ledgerAB = h["ab"];
        Ledger ledgerABC = h["abc"];
        Ledger ledgerAD = h["ad"];

        TestHarness harness(h.oracle);
        Node a = harness.makeNode(), b = harness.makeNode(),
             c = harness.makeNode(), d = harness.makeNode();
        c.untrust();

        // first round a,b,c agree, d has differing id
        BEAST_EXPECT(AddOutcome::current == harness.add(a, ledgerA));
        BEAST_EXPECT(AddOutcome::current == harness.add(b, ledgerA));
        BEAST_EXPECT(AddOutcome::current == harness.add(c, ledgerA));
        BEAST_EXPECT(AddOutcome::current == harness.add(d, d.partial(ledgerA)));

        for (Ledger const& ledger : {ledgerA, ledgerAB, ledgerABC, ledgerAD})
            BEAST_EXPECT(
                harness.vals().getNodesAfter(ledger, ledger.id()) == 0);

        harness.clock().advance(5s);

        BEAST_EXPECT(AddOutcome::current == harness.add(a, ledgerAB));
        BEAST_EXPECT(AddOutcome::current == harness.add(b, ledgerABC));
        BEAST_EXPECT(AddOutcome::current == harness.add(c, ledgerAB));
        BEAST_EXPECT(
            AddOutcome::current == harness.add(d, d.partial(ledgerABC)));

        BEAST_EXPECT(harness.vals().getNodesAfter(ledgerA, ledgerA.id()) == 3);
        BEAST_EXPECT(
            harness.vals().getNodesAfter(ledgerAB, ledgerAB.id()) == 2);
        BEAST_EXPECT(
            harness.vals().getNodesAfter(ledgerABC, ledgerABC.id()) == 0);
        BEAST_EXPECT(
            harness.vals().getNodesAfter(ledgerAD, ledgerAD.id()) == 0);

        // If given a ledger inconsistent with the id, is still able to check
        // using slower method
        BEAST_EXPECT(harness.vals().getNodesAfter(ledgerAD, ledgerA.id()) == 1);
        BEAST_EXPECT(harness.vals().getNodesAfter(ledgerAD, ledgerAB.id()) == 2);
    }

    void
    testCurrentTrusted()
    {
        using namespace std::chrono_literals;
        testcase("Current trusted validations");

        LedgerHistoryHelper h;
        Ledger ledgerA = h["a"];
        Ledger ledgerB = h["b"];
        Ledger ledgerAC = h["ac"];

        TestHarness harness(h.oracle);
        Node a = harness.makeNode(), b = harness.makeNode();
        b.untrust();


        BEAST_EXPECT(AddOutcome::current == harness.add(a, ledgerA));
        BEAST_EXPECT(AddOutcome::current == harness.add(b, ledgerB));

        // Only a is trusted
        BEAST_EXPECT(harness.vals().currentTrusted().size() == 1);
        BEAST_EXPECT(
            harness.vals().currentTrusted()[0].ledgerID() == ledgerA.id());
        BEAST_EXPECT(
            harness.vals().currentTrusted()[0].seq() == ledgerA.seq());

        harness.clock().advance(3s);

        for (auto const& node : {a, b})
            BEAST_EXPECT(AddOutcome::current == harness.add(node, ledgerAC));

        // New validation for a
        BEAST_EXPECT(harness.vals().currentTrusted().size() == 1);
        BEAST_EXPECT(
            harness.vals().currentTrusted()[0].ledgerID() == ledgerAC.id());
        BEAST_EXPECT(
            harness.vals().currentTrusted()[0].seq() == ledgerAC.seq());

        // Pass enough time for it to go stale
        harness.clock().advance(harness.parms().validationCURRENT_LOCAL);
        BEAST_EXPECT(harness.vals().currentTrusted().empty());
    }

    void
    testGetCurrentPublicKeys()
    {
        using namespace std::chrono_literals;
        testcase("Current public keys");

        LedgerHistoryHelper h;
        Ledger ledgerA = h["a"];
        Ledger ledgerAC = h["ac"];

        TestHarness harness(h.oracle);
        Node a = harness.makeNode(), b = harness.makeNode();
        b.untrust();


        for (auto const& node : {a, b})
            BEAST_EXPECT(AddOutcome::current == harness.add(node, ledgerA));

        {
            hash_set<PeerKey> const expectedKeys = {
                a.masterKey(), b.masterKey()};
            BEAST_EXPECT(harness.vals().getCurrentPublicKeys() == expectedKeys);
        }

        harness.clock().advance(3s);

        // Change keys
        a.advanceKey();
        b.advanceKey();

        for (auto const& node : {a, b})
            BEAST_EXPECT(AddOutcome::current == harness.add(node, ledgerAC));

        {
            hash_set<PeerKey> const expectedKeys = {
                a.masterKey(), b.masterKey()};
            BEAST_EXPECT(harness.vals().getCurrentPublicKeys() == expectedKeys);
        }

        // Pass enough time for them to go stale
        harness.clock().advance(harness.parms().validationCURRENT_LOCAL);
        BEAST_EXPECT(harness.vals().getCurrentPublicKeys().empty());
    }

    void
    testCurrentTrustedDistribution()
    {
#if 0
        // Test the trusted distribution calculation, including ledger slips
        // and sequence cutoffs
        using namespace std::chrono_literals;

        TestHarness harness;

        Node baby = harness.makeNode(), papa = harness.makeNode(),
             mama = harness.makeNode(), goldilocks = harness.makeNode();
        goldilocks.untrust();

        // Stagger the validations around sequence 2
        //  papa on seq 1 is behind
        //  baby on seq 2 is just right
        //  mama on seq 3 is ahead
        //  goldilocks on seq 2, but is not trusted

        for (auto const& node : {baby, papa, mama, goldilocks})
            BEAST_EXPECT(AddOutcome::current ==
                harness.add(node, Ledger::Seq{1}, Ledger::ID{1}));

        harness.clock().advance(1s);
        for (auto const& node : {baby, mama, goldilocks})
            BEAST_EXPECT(AddOutcome::current ==
                harness.add(node, Ledger::Seq{2}, Ledger::ID{2}));

        harness.clock().advance(1s);
        BEAST_EXPECT(AddOutcome::current ==
            harness.add(mama, Ledger::Seq{3}, Ledger::ID{3}));

        {
            // Allow slippage that treats all trusted as the current ledger
            auto res = harness.vals().currentTrustedDistribution(
                Ledger::ID{2},    // Current ledger
                Ledger::ID{1},    // Prior ledger
                Ledger::Seq{0});  // No cutoff

            BEAST_EXPECT(res.size() == 1);
            BEAST_EXPECT(res[Ledger::ID{2}] == 3);
            BEAST_EXPECT(
                getPreferredLedger(Ledger::ID{2}, res) == Ledger::ID{2});
        }

        {
            // Don't allow slippage back for prior ledger
            auto res = harness.vals().currentTrustedDistribution(
                Ledger::ID{2},    // Current ledger
                Ledger::ID{0},    // No prior ledger
                Ledger::Seq{0});  // No cutoff

            BEAST_EXPECT(res.size() == 2);
            BEAST_EXPECT(res[Ledger::ID{2}] == 2);
            BEAST_EXPECT(res[Ledger::ID{1}] == 1);
            BEAST_EXPECT(
                getPreferredLedger(Ledger::ID{2}, res) == Ledger::ID{2});
        }

        {
            // Don't allow any slips
            auto res = harness.vals().currentTrustedDistribution(
                Ledger::ID{0},    // No current ledger
                Ledger::ID{0},    // No prior ledger
                Ledger::Seq{0});  // No cutoff

            BEAST_EXPECT(res.size() == 3);
            BEAST_EXPECT(res[Ledger::ID{1}] == 1);
            BEAST_EXPECT(res[Ledger::ID{2}] == 1);
            BEAST_EXPECT(res[Ledger::ID{3}] == 1);
            BEAST_EXPECT(
                getPreferredLedger(Ledger::ID{0}, res) == Ledger::ID{3});
        }

        {
            // Cutoff old sequence numbers
            auto res = harness.vals().currentTrustedDistribution(
                Ledger::ID{2},    // current ledger
                Ledger::ID{1},    // prior ledger
                Ledger::Seq{2});  // Only sequence 2 or later
            BEAST_EXPECT(res.size() == 1);
            BEAST_EXPECT(res[Ledger::ID{2}] == 2);
            BEAST_EXPECT(
                getPreferredLedger(Ledger::ID{2}, res) == Ledger::ID{2});
        }
#endif
    }

    void
    testTrustedByLedgerFunctions()
    {
        // Test the Validations functions that calculate a value by ledger ID
        using namespace std::chrono_literals;
        testcase("By ledger functions");

        // Several Validations functions return a set of values associated
        // with trusted ledgers sharing the same ledger ID.  The tests below
        // exercise this logic by saving the set of trusted Validations, and
        // verifying that the Validations member functions all calculate the
        // proper transformation of the available ledgers.

         LedgerHistoryHelper h;
        TestHarness harness(h.oracle);

        Node a = harness.makeNode(), b = harness.makeNode(),
             c = harness.makeNode(), d = harness.makeNode(),
             e = harness.makeNode();

        c.untrust();
        // Mix of load fees
        a.setLoadFee(12);
        b.setLoadFee(1);
        c.setLoadFee(12);
        e.setLoadFee(12);

        hash_map<Ledger::ID, std::vector<Validation>> trustedValidations;

        //----------------------------------------------------------------------
        // checkers
        auto sorted = [](auto vec) {
            std::sort(vec.begin(), vec.end());
            return vec;
        };
        auto compare = [&]() {
            for (auto& it : trustedValidations)
            {
                auto const& id = it.first;
                auto const& expectedValidations = it.second;

                BEAST_EXPECT(harness.vals().numTrustedForLedger(id) ==
                    expectedValidations.size());
                BEAST_EXPECT(sorted(harness.vals().getTrustedForLedger(id)) ==
                    sorted(expectedValidations));

                std::vector<NetClock::time_point> expectedTimes;
                std::uint32_t baseFee = 0;
                std::vector<uint32_t> expectedFees;
                for (auto const& val : expectedValidations)
                {
                    expectedTimes.push_back(val.signTime());
                    expectedFees.push_back(val.loadFee().value_or(baseFee));
                }

                BEAST_EXPECT(sorted(harness.vals().fees(id, baseFee)) ==
                    sorted(expectedFees));

                BEAST_EXPECT(sorted(harness.vals().getTrustedValidationTimes(
                                 id)) == sorted(expectedTimes));
            }
        };

        //----------------------------------------------------------------------
        Ledger ledgerA = h["a"];
        Ledger ledgerB = h["b"];
        Ledger ledgerAC = h["ac"];

        // Add a dummy ID to cover unknown ledger identifiers
        trustedValidations[Ledger::ID{100}] = {};

        // first round a,b,c agree
        for (auto const& node : {a, b, c})
        {
            auto const val = node.validation(ledgerA);
            BEAST_EXPECT(AddOutcome::current == harness.add(node, val));
            if (val.trusted())
                trustedValidations[val.ledgerID()].emplace_back(val);
        }
        // d diagrees
        {
            auto const val = d.validation(ledgerB);
            BEAST_EXPECT(AddOutcome::current == harness.add(d, val));
            trustedValidations[val.ledgerID()].emplace_back(val);
        }
        // e only issues partials
        {
            auto const val = e.partial(ledgerA);
            BEAST_EXPECT(AddOutcome::current == harness.add(e, val));
        }


        harness.clock().advance(5s);
        // second round, a,b,c move to ledger 2
        for (auto const& node : {a, b, c})
        {
            auto const val = node.validation(ledgerAC);
            BEAST_EXPECT(AddOutcome::current == harness.add(node, val));
            if (val.trusted())
                trustedValidations[val.ledgerID()].emplace_back(val);
        }
        // d now thinks ledger 1, but had to issue a partial to switch
        {
            auto const val = d.partial(ledgerA);
            BEAST_EXPECT(AddOutcome::current == harness.add(d, val));
        }
        // e only issues partials
        {
            auto const val = e.partial(ledgerAC);
            BEAST_EXPECT(AddOutcome::current == harness.add(e, val));
        }


        compare();
    }

    void
    testExpire()
    {
        // Verify expiring clears out validations stored by ledger
        testcase("Expire validations");
        LedgerHistoryHelper h;
        TestHarness harness(h.oracle);
        Node a = harness.makeNode();

        Ledger ledgerA = h["a"];

        BEAST_EXPECT(AddOutcome::current == harness.add(a, ledgerA));
        BEAST_EXPECT(harness.vals().numTrustedForLedger(ledgerA.id()));
        harness.clock().advance(harness.parms().validationSET_EXPIRES);
        harness.vals().expire();
        BEAST_EXPECT(!harness.vals().numTrustedForLedger(ledgerA.id()));
    }

    void
    testFlush()
    {
        // Test final flush of validations
        using namespace std::chrono_literals;
        testcase("Flush validations");

        LedgerHistoryHelper h;
        TestHarness harness(h.oracle);
        Node a = harness.makeNode(), b = harness.makeNode(),
             c = harness.makeNode();
        c.untrust();

        Ledger ledgerA = h["a"];
        Ledger ledgerAB = h["ab"];

        hash_map<PeerKey, Validation> expected;
        for (auto const& node : {a, b, c})
        {
            auto const val = node.validation(ledgerA);
            BEAST_EXPECT(AddOutcome::current == harness.add(node, val));
            expected.emplace(node.masterKey(), val);
        }
        Validation staleA = expected.find(a.masterKey())->second;


        // Send in a new validation for a, saving the new one into the expected
        // map after setting the proper prior ledger ID it replaced
        harness.clock().advance(1s);
        auto newVal = a.validation(ledgerAB);
        BEAST_EXPECT(AddOutcome::current == harness.add(a, newVal));
        expected.find(a.masterKey())->second = newVal;

        // Now flush
        harness.vals().flush();

        // Original a validation was stale
        BEAST_EXPECT(harness.stale().size() == 1);
        BEAST_EXPECT(harness.stale()[0] == staleA);
        BEAST_EXPECT(harness.stale()[0].nodeID() == a.nodeID());

        auto const& flushed = harness.flushed();

        BEAST_EXPECT(flushed == expected);
    }

    void
    testGetPreferredLedger()
    {
#if 0
        using Distribution = hash_map<Ledger::ID, std::uint32_t>;

        {
            Ledger::ID const current{1};
            Distribution dist;
            BEAST_EXPECT(getPreferredLedger(current, dist) == current);
        }

        {
            Ledger::ID const current{1};
            Distribution dist;
            dist[Ledger::ID{2}] = 2;
            BEAST_EXPECT(getPreferredLedger(current, dist) == Ledger::ID{2});
        }

        {
            Ledger::ID const current{1};
            Distribution dist;
            dist[Ledger::ID{1}] = 1;
            dist[Ledger::ID{2}] = 2;
            BEAST_EXPECT(getPreferredLedger(current, dist) == Ledger::ID{2});
        }

        {
            Ledger::ID const current{1};
            Distribution dist;
            dist[Ledger::ID{1}] = 2;
            dist[Ledger::ID{2}] = 2;
            BEAST_EXPECT(getPreferredLedger(current, dist) == current);
        }

        {
            Ledger::ID const current{2};
            Distribution dist;
            dist[Ledger::ID{1}] = 2;
            dist[Ledger::ID{2}] = 2;
            BEAST_EXPECT(getPreferredLedger(current, dist) == current);
        }

        {
            Ledger::ID const current{1};
            Distribution dist;
            dist[Ledger::ID{2}] = 2;
            dist[Ledger::ID{3}] = 2;
            BEAST_EXPECT(getPreferredLedger(current, dist) == Ledger::ID{3});
        }
#endif
    }

    void
    run() override
    {
        testAddValidation();
        testOnStale();
        testGetNodesAfter();
        testCurrentTrusted();
        testGetCurrentPublicKeys();
        testCurrentTrustedDistribution();
        testTrustedByLedgerFunctions();
        testExpire();
        testFlush();
        testGetPreferredLedger();
    }
};

BEAST_DEFINE_TESTSUITE(Validations, consensus, ripple);
}  // csf
}  // test
}  // ripple
