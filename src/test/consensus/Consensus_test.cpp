//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2016 Ripple Labs Inc->

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
#include <ripple/beast/clock/manual_clock.h>
#include <ripple/beast/unit_test.h>
#include <ripple/consensus/Consensus.h>
#include <ripple/consensus/ConsensusProposal.h>
#include <test/csf.h>
#include <utility>

namespace ripple {
namespace test {

class Consensus_test : public beast::unit_test::suite
{
public:
    void
    testShouldCloseLedger()
    {
        using namespace std::chrono_literals;

        // Use default parameters
        ConsensusParms p;
        beast::Journal j;

        // Bizarre times forcibly close
        BEAST_EXPECT(
            shouldCloseLedger(true, 10, 10, 10, -10s, 10s, 1s, 1s, p, j));
        BEAST_EXPECT(
            shouldCloseLedger(true, 10, 10, 10, 100h, 10s, 1s, 1s, p, j));
        BEAST_EXPECT(
            shouldCloseLedger(true, 10, 10, 10, 10s, 100h, 1s, 1s, p, j));

        // Rest of network has closed
        BEAST_EXPECT(
            shouldCloseLedger(true, 10, 3, 5, 10s, 10s, 10s, 10s, p, j));

        // No transactions means wait until end of internval
        BEAST_EXPECT(
            !shouldCloseLedger(false, 10, 0, 0, 1s, 1s, 1s, 10s, p, j));
        BEAST_EXPECT(
            shouldCloseLedger(false, 10, 0, 0, 1s, 10s, 1s, 10s, p, j));

        // Enforce minimum ledger open time
        BEAST_EXPECT(
            !shouldCloseLedger(true, 10, 0, 0, 10s, 10s, 1s, 10s, p, j));

        // Don't go too much faster than last time
        BEAST_EXPECT(
            !shouldCloseLedger(true, 10, 0, 0, 10s, 10s, 3s, 10s, p, j));

        BEAST_EXPECT(
            shouldCloseLedger(true, 10, 0, 0, 10s, 10s, 10s, 10s, p, j));
    }

    void
    testCheckConsensus()
    {
        using namespace std::chrono_literals;

        // Use default parameterss
        ConsensusParms p;
        beast::Journal j;


        // Not enough time has elapsed
        BEAST_EXPECT(
            ConsensusState::No ==
            checkConsensus(10, 2, 2, 0, 3s, 2s, p, true, j));

        // If not enough peers have propsed, ensure
        // more time for proposals
        BEAST_EXPECT(
            ConsensusState::No ==
            checkConsensus(10, 2, 2, 0, 3s, 4s, p, true, j));

        // Enough time has elapsed and we all agree
        BEAST_EXPECT(
            ConsensusState::Yes ==
            checkConsensus(10, 2, 2, 0, 3s, 10s, p, true, j));

        // Enough time has elapsed and we don't yet agree
        BEAST_EXPECT(
            ConsensusState::No ==
            checkConsensus(10, 2, 1, 0, 3s, 10s, p, true, j));

        // Our peers have moved on
        // Enough time has elapsed and we all agree
        BEAST_EXPECT(
            ConsensusState::MovedOn ==
            checkConsensus(10, 2, 1, 8, 3s, 10s, p, true, j));

        // No peers makes it easy to agree
        BEAST_EXPECT(
            ConsensusState::Yes ==
            checkConsensus(0, 0, 0, 0, 3s, 10s, p, true, j));
    }

    void
    testStandalone()
    {
        using namespace std::chrono_literals;
        using namespace csf;

        ConsensusParms parms;
        auto tg = TrustGraph::makeComplete(1);
        Sim s(parms, tg, topology(tg, fixed{parms.ledgerGRANULARITY}));

        auto& p = s.peers[0];

        p.targetLedgers = 1;
        p.start();
        p.submit(Tx{1});

        s.scheduler.step();

        // Inspect that the proper ledger was created
        auto const & lcl =p.lastClosedLedger.get();
        BEAST_EXPECT(p.prevLedgerID() == lcl.id());
        BEAST_EXPECT(lcl.seq() == Ledger::Seq{1});
        BEAST_EXPECT(lcl.txs().size() == 1);
        BEAST_EXPECT(
            lcl.txs().find(Tx{1}) !=
            lcl.txs().end());
        BEAST_EXPECT(p.prevProposers() == 0);
    }

    void
    testPeersAgree()
    {
        using namespace csf;
        using namespace std::chrono;

        ConsensusParms parms;
        auto tg = TrustGraph::makeComplete(5);
        Sim sim(
            parms,
            tg,
            topology(
                tg,
                fixed{round<milliseconds>(0.2 * parms.ledgerGRANULARITY)}));

        // everyone submits their own ID as a TX and relay it to peers
        for (auto& p : sim.peers)
            p.submit(Tx(static_cast<std::uint32_t>(p.id)));

        // Verify all peers have the same LCL and it has all the Txs
        sim.run(1);
        for (auto& p : sim.peers)
        {
            auto const & lcl = p.lastClosedLedger.get();
            BEAST_EXPECT(lcl.id() == p.prevLedgerID());
            BEAST_EXPECT(lcl.seq() == Ledger::Seq{1});
            BEAST_EXPECT(p.prevProposers() == sim.peers.size() - 1);
            for (std::uint32_t i = 0; i < sim.peers.size(); ++i)
                BEAST_EXPECT(lcl.txs().find(Tx{i}) != lcl.txs().end());
            // Matches peer 0 ledger
            BEAST_EXPECT(lcl.txs() == sim.peers[0].lastClosedLedger.get().txs());
        }
    }

    void
    testSlowPeers()
    {
        using namespace csf;
        using namespace std::chrono;

        // Several tests of a complete trust graph with a subset of peers
        // that have significantly longer network delays to the rest of the
        // network

        // Test when a slow peer doesn't delay a consensus quorum (4/5 agree)
        {
            ConsensusParms parms;
            auto tg = TrustGraph::makeComplete(5);

            // Peers 0 is slow, 1-4 are fast
            // This choice is based on parms.minCONSENSUS_PCT of 80
            Sim sim(parms, tg, topology(tg, [&](std::uint32_t i, std::uint32_t j) {
                        auto delayFactor = (i == 0 || j == 0) ? 1.1 : 0.2;
                        return round<milliseconds>(
                            delayFactor * parms.ledgerGRANULARITY);
                    }));

            // All peers submit their own ID as a transaction and relay it
            // to peers
            for (auto& p : sim.peers)
            {
                p.submit(Tx{static_cast<std::uint32_t>(p.id)});
            }

            sim.run(1);

            // Verify all peers have same LCL but are missing transaction 0
            // which was not received by all peers before the ledger closed
            for (auto& p : sim.peers)
            {
                auto const & lcl = p.lastClosedLedger.get();
                BEAST_EXPECT(lcl.id() == p.prevLedgerID());
                BEAST_EXPECT(lcl.seq() == Ledger::Seq{1});

                BEAST_EXPECT(p.prevProposers() == sim.peers.size() - 1);
                BEAST_EXPECT(p.prevRoundTime() == sim.peers[0].prevRoundTime());

                BEAST_EXPECT(lcl.txs().find(Tx{0}) == lcl.txs().end());
                for (std::uint32_t i = 2; i < sim.peers.size(); ++i)
                    BEAST_EXPECT(lcl.txs().find(Tx{i}) != lcl.txs().end());
                // Matches peer 0 ledger
                BEAST_EXPECT(lcl.txs() == sim.peers[0].lastClosedLedger.get().txs());
            }
            BEAST_EXPECT(
                sim.peers[0].openTxs.find(Tx{0}) != sim.peers[0].openTxs.end());
        }

        // Test when the slow peers delay a consensus quorum (4/6  agree)
        {
            // Run two tests
            //  1. The slow peers are participating in consensus
            //  2. The slow peers are just observing

            for (auto isParticipant : {true, false})
            {
                ConsensusParms parms;
                auto tg = TrustGraph::makeComplete(6);

                // Peers 0,1 are slow, 2-5 are fast
                // This choice is based on parms.minCONSENSUS_PCT of 80
                Sim sim(
                    parms, tg, topology(tg, [&](std::uint32_t i, std::uint32_t j) {
                        auto delayFactor = (i <= 1 || j <= 1) ? 1.1 : 0.2;
                        return round<milliseconds>(
                            delayFactor * parms.ledgerGRANULARITY);
                    }));

                sim.peers[0].runAsValidator = isParticipant;
                sim.peers[1].runAsValidator = isParticipant;

                // All peers submit their own ID as a transaction and relay it
                // to peers
                for (auto& p : sim.peers)
                {
                    p.submit(Tx{static_cast<std::uint32_t>(p.id)});
                }

                sim.run(1);

                // Verify all peers have same LCL but are missing transaction 0
                // which was not received by all peers before the ledger closed
                for (auto& p : sim.peers)
                {
                    auto const & lcl = p.lastClosedLedger.get();
                    BEAST_EXPECT(lcl.id() == p.prevLedgerID());
                    BEAST_EXPECT(lcl.seq() == Ledger::Seq{1});

                    // If peer 0,1 are participating
                    if (isParticipant)
                    {
                        BEAST_EXPECT(p.prevProposers() == sim.peers.size() - 1);
                        // Due to the network link delay settings
                        //    Peer 0 initially proposes {0}
                        //    Peer 1 initially proposes {1}
                        //    Peers 2-5 initially propose {2,3,4,5}
                        // Since peers 2-5 agree, 4/6 > the initial 50%
                        // threshold on disputed transactions, so Peer 0 and 1
                        // change their position to match peers 2-5 and declare
                        // consensus now that 5/6 proposed positions match
                        // (themselves and peers 2-5).
                        //
                        // Peers 2-5 do not change position, since tx 0 or tx 1
                        // have less than the 50% initial threshold.  They also
                        // cannot declare consensus, since 4/6 < 80% threshold
                        // agreement on current positions.  Instead, they have
                        // to wait an additional timerEntry call for the updated
                        // peer 0 and peer 1 positions to arrive.  Once they do,
                        // now peers 2-5 see complete agreement and declare
                        // consensus
                        if (p.id > NodeID{1})
                            BEAST_EXPECT(
                                p.prevRoundTime() >
                                sim.peers[0].prevRoundTime());
                    }
                    else  // peer 0,1 are not participating
                    {
                        auto const proposers = p.prevProposers();
                        if (p.id <= NodeID{1})
                            BEAST_EXPECT(proposers == sim.peers.size() - 2);
                        else
                            BEAST_EXPECT(proposers == sim.peers.size() - 3);

                        // so all peers should have closed together
                        BEAST_EXPECT(
                            p.prevRoundTime() == sim.peers[0].prevRoundTime());
                    }

                    BEAST_EXPECT(lcl.txs().find(Tx{0}) == lcl.txs().end());
                    for (std::uint32_t i = 2; i < sim.peers.size(); ++i)
                        BEAST_EXPECT(lcl.txs().find(Tx{i}) != lcl.txs().end());
                    // Matches peer 0 ledger
                    BEAST_EXPECT(
                        lcl.txs() == sim.peers[0].lastClosedLedger.get().txs());
                }
                BEAST_EXPECT(
                    sim.peers[0].openTxs.find(Tx{0}) !=
                    sim.peers[0].openTxs.end());
            }
        }
    }

    void
    testCloseTimeDisagree()
    {
        using namespace csf;
        using namespace std::chrono;

        // This is a very specialized test to get ledgers to disagree on
        // the close time.  It unfortunately assumes knowledge about current
        // timing constants.  This is a necessary evil to get coverage up
        // pending more extensive refactorings of timing constants.

        // In order to agree-to-disagree on the close time, there must be no
        // clear majority of nodes agreeing on a close time.  This test
        // sets a relative offset to the peers internal clocks so that they
        // send proposals with differing times.

        // However, they have to agree on the effective close time, not the
        // exact close time. The minimum closeTimeResolution is given by
        // ledgerPossibleTimeResolutions[0], which is currently 10s. This means
        // the skews need to be at least 10 seconds.

        // Complicating this matter is that nodes will ignore proposals
        // with times more than proposeFRESHNESS =20s in the past. So at
        // the minimum granularity, we have at most 3 types of skews
        // (0s,10s,20s).

        // This test therefore has 6 nodes, with 2 nodes having each type of
        // skew.  Then no majority (1/3 < 1/2) of nodes will agree on an
        // actual close time.

        ConsensusParms parms;
        auto tg = TrustGraph::makeComplete(6);
        Sim sim(
            parms,
            tg,
            topology(
                tg,
                fixed{round<milliseconds>(0.2 * parms.ledgerGRANULARITY)}));

        // Run consensus without skew until we have a short close time
        // resolution
        while (sim.peers.front().lastClosedLedger.get().closeTimeResolution() >=
               parms.proposeFRESHNESS)
            sim.run(1);

        // Introduce a shift on the time of half the peers
        sim.peers[0].clockSkew = parms.proposeFRESHNESS / 2;
        sim.peers[1].clockSkew = parms.proposeFRESHNESS / 2;
        sim.peers[2].clockSkew = parms.proposeFRESHNESS;
        sim.peers[3].clockSkew = parms.proposeFRESHNESS;

        // Verify all peers have the same LCL and it has all the Txs
        sim.run(1);
        for (auto& p : sim.peers)
        {
            BEAST_EXPECT(!p.lastClosedLedger.get().closeAgree());
        }
    }

    void
    testWrongLCL()
    {
        using namespace csf;
        using namespace std::chrono;
        // Specialized test to exercise a temporary fork in which some peers
        // are working on an incorrect prior ledger.

        ConsensusParms parms;


        // Vary the time it takes to process validations to exercise detecting
        // the wrong LCL at different phases of consensus
        for (auto validationDelay : {0ms, parms.ledgerMIN_CLOSE})
        {
            // Consider 10 peers:
            // 0 1    2 3 4    5 6 7 8 9
            //
            // Nodes 0-1 trust nodes 0-4
            // Nodes 2-9 trust nodes 2-9
            //
            // By submitting tx 0 to nodes 0-4 and tx 1 to nodes 5-9,
            // nodes 0-1 will generate the wrong LCL (with tx 0).  The remaining
            // nodes will instead accept the ledger with tx 1.

            // Nodes 0-1 will detect this mismatch during a subsequent round
            // since nodes 2-4 will validate a different ledger.

            // Nodes 0-1 will acquire the proper ledger from the network and
            // resume consensus and eventually generate the dominant network
            // ledger

            std::vector<UNL> unls;
            unls.push_back({2, 3, 4, 5, 6, 7, 8, 9});
            unls.push_back({0, 1, 2, 3, 4});
            std::vector<int> membership(10, 0);
            membership[0] = 1;
            membership[1] = 1;

            TrustGraph tg{unls, membership};

            // This topology can fork, which is why we are using it for this
            // test.
            BEAST_EXPECT(tg.canFork(parms.minCONSENSUS_PCT / 100.));

            auto netDelay = round<milliseconds>(0.2 * parms.ledgerGRANULARITY);
            Sim sim(parms, tg, topology(tg, fixed{netDelay}));

            // initial round to set prior state
            sim.run(1);

            // Nodes in smaller UNL have seen tx 0, nodes in other unl have seen
            // tx 1
            for (auto& p : sim.peers)
            {
                p.delays.recvValidation= validationDelay;
                if (unls[1].find(static_cast<std::uint32_t>(p.id)) != unls[1].end())
                    p.openTxs.insert(Tx{0});
                else
                    p.openTxs.insert(Tx{1});
            }

            // Run for additional rounds
            // With no validation delay, only 2 more rounds are needed.
            //  1. Round to generate different ledgers
            //  2. Round to detect different prior ledgers (but still generate
            //    wrong ones) and recover within that round since wrong LCL
            //    is detected before we close
            //
            // With a validation delay of ledgerMIN_CLOSE, we need 3 more
            // rounds.
            //  1. Round to generate different ledgers
            //  2. Round to detect different prior ledgers (but still generate
            //     wrong ones) but end up declaring consensus on wrong LCL (but
            //     with the right transaction set!).  This is because we detect
            //     the wrong LCL after we have closed the ledger, so we declare
            //     consensus based solely on our peer proposals. But we haven't
            //     had time to acquire the right LCL
            //  3. Round to correct
            sim.run(3);

            // The network never actually forks, since node 0-1 never see a
            // quorum of validations to validate the incorrect chain.

            // However, for a non zero-validation delay, the network is not
            // synchronized because nodes 0 and 1 are running one ledger behind
            if (BEAST_EXPECT(sim.forks() == 1))
            {
                for(auto const & peer : sim.peers)
                {
                    if(peer.id >= NodeID{2})
                    {
                        // No jumps
                        BEAST_EXPECT(peer.fullyValidatedLedger.jumps().empty());
                        BEAST_EXPECT(peer.lastClosedLedger.jumps().empty());
                    }
                    else
                    {
                        // last closed ledger jump between chains
                        {
                            BEAST_EXPECT(
                                peer.lastClosedLedger.jumps().size() == 1);
                            LedgerState::Jump const& jump =
                                peer.lastClosedLedger.jumps().front();
                            // Jump is to a different chain
                            BEAST_EXPECT(jump.from.seq() <= jump.to.seq());
                            BEAST_EXPECT(
                                !sim.oracle.isAncestor(jump.from, jump.to));
                        }
                        // fully validted jump forward in same chain
                        {
                            BEAST_EXPECT(
                                peer.fullyValidatedLedger.jumps().size() == 1);
                            LedgerState::Jump const& jump =
                                peer.fullyValidatedLedger.jumps().front();
                            // Jump is to a different chain with same seq
                            BEAST_EXPECT(jump.from.seq() < jump.to.seq());
                            BEAST_EXPECT(
                                sim.oracle.isAncestor(jump.from, jump.to));
                        }
                    }
                }
            }
        }

        {
            // Additional test engineered to switch LCL during the establish
            // phase. This was added to trigger a scenario that previously
            // crashed, in which switchLCL switched from establish to open
            // phase, but still processed the establish phase logic.

            // Node 0 will accept an initial ledger A, but all other nodes
            // accept ledger B a bit later.  By delaying the time it takes
            // to process a validation, node 0 will detect the wrongLCL after it
            // is already in the establish phase of the next round.

            // UNL:
            //  - Node 0 trusts nodes 0-3
            //  - All other nodes trust all nodes
            std::vector<UNL> unls;
            unls.push_back({0, 1, 2, 3});
            unls.push_back({0, 1, 2, 3, 4, 5, 6, 7, 8, 9});
            std::vector<int> membership = {0, 1, 1, 1, 1, 1, 1, 1, 1, 1};
            TrustGraph tg{unls, membership};

            Sim sim(parms,
                tg,
                topology(
                    tg, fixed{round<milliseconds>(0.2 * parms.ledgerGRANULARITY)}));

            // initial round to set prior state
            sim.run(1);
            for (auto& p : sim.peers)
            {
                // Nodes 0 - 3 see only Tx 0
                if (p.id < NodeID{4})
                    p.openTxs.insert(Tx(0));
                else  // Nodes 4+ see Tx 1
                    p.openTxs.insert(Tx(1));

                // Delay validation processing
                p.delays.recvValidation = parms.ledgerGRANULARITY;
            }
            // additional rounds to generate wrongLCL and recover
            sim.run(2);

            // Check all peers recovered
            for (auto& p : sim.peers)
                BEAST_EXPECT(p.prevLedgerID() == sim.peers[0].prevLedgerID());
        }
    }

    void
    testFork()
    {
        using namespace csf;
        using namespace std::chrono;

        int numPeers = 10;
        for (int overlap = 0; overlap <= numPeers; ++overlap)
        {
            ConsensusParms parms;
            auto tg = TrustGraph::makeClique(numPeers, overlap);
            Sim sim(
                parms,
                tg,
                topology(
                    tg,
                    fixed{
                        round<milliseconds>(0.2 * parms.ledgerGRANULARITY)}));

            // Initial round to set prior state
            sim.run(1);
            for (auto& p : sim.peers)
            {
                // Nodes have only seen transactions from their neighbors
                p.openTxs.insert(Tx{static_cast<std::uint32_t>(p.id)});
                for (auto const link : sim.net.links(&p))
                    p.openTxs.insert(Tx{static_cast<std::uint32_t>(link.to->id)});
            }
            sim.run(1);

            // See if the network forked
            std::set<Ledger::ID> ledgers;
            for (auto& p : sim.peers)
            {
                ledgers.insert(p.prevLedgerID());
            }

            // Fork should not happen for 40% or greater overlap
            // Since the overlapped nodes have a UNL that is the union of the
            // two cliques, the maximum sized UNL list is the number of peers
            if (overlap > 0.4 * numPeers)
                BEAST_EXPECT(ledgers.size() == 1);
            else  // Even if we do fork, there shouldn't be more than 3 ledgers
                // One for cliqueA, one for cliqueB and one for nodes in both
                BEAST_EXPECT(ledgers.size() <= 3);
        }
    }

    void
    simClockSkew()
    {
        using namespace csf;
        using namespace std::chrono_literals;
        // Attempting to test what happens if peers enter consensus well
        // separated in time.  Initial round (in which peers are not staggered)
        // is used to get the network going, then transactions are submitted
        // together and consensus continues.

        // For all the times below, the same ledger is built but the close times
        // disgree.  BUT THE LEDGER DOES NOT SHOW disagreeing close times.
        // It is probably because peer proposals are stale, so they get ignored
        // but with no peer proposals, we always assume close time consensus is
        // true.

        // Disabled while continuing to understand testt.

        for (auto stagger : {800ms, 1600ms, 3200ms, 30000ms, 45000ms, 300000ms})
        {
            ConsensusParms parms;
            auto tg = TrustGraph::makeComplete(5);
            Sim sim(parms, tg, topology(tg, [](std::uint32_t i, std::uint32_t) {
                        return 200ms * (i + 1);
                    }));

            // all transactions submitted before starting
            // Initial round to set prior state
            sim.run(1);

            for (auto& p : sim.peers)
            {
                p.openTxs.insert(Tx{0});
                p.targetLedgers = p.completedLedgers + 1;
            }

            // stagger start of consensus
            for (auto& p : sim.peers)
            {
                p.start();
                sim.scheduler.step_for(stagger);
            }

            // run until all peers have accepted all transactions
            sim.scheduler.step_while([&]() {
                for (auto& p : sim.peers)
                {
                    if (p.lastClosedLedger.get().txs().size() != 1)
                    {
                        return true;
                    }
                }
                return false;
            });
        }
    }

    void
    simScaleFree()
    {
        using namespace std::chrono;
        using namespace csf;
        // Generate a quasi-random scale free network and simulate consensus
        // for a single transaction

        int N = 100;  // Peers

        int numUNLs = 15;  //  UNL lists
        int minUNLSize = N / 4, maxUNLSize = N / 2;

        double transProb = 0.5;

        std::mt19937_64 rng;
        ConsensusParms parms;

        auto tg = TrustGraph::makeRandomRanked(
            N,
            numUNLs,
            PowerLawDistribution{1, 3},
            std::uniform_int_distribution<>{minUNLSize, maxUNLSize},
            rng);

        Sim sim{
            parms,
            tg,
            topology(
                tg,
                fixed{round<milliseconds>(0.2 * parms.ledgerGRANULARITY)})};

        // Initial round to set prior state
        sim.run(1);

        std::uniform_real_distribution<> u{};
        for (auto& p : sim.peers)
        {
            // 50-50 chance to have seen a transaction
            if (u(rng) >= transProb)
                p.openTxs.insert(Tx{0});
        }
        sim.run(1);

        // See if the network forked
        std::set<Ledger::ID> ledgers;
        for (auto& p : sim.peers)
        {
            ledgers.insert(p.prevLedgerID());
        }

        BEAST_EXPECT(ledgers.size() == 1);
    }

    void
    run() override
    {
        testShouldCloseLedger();
        testCheckConsensus();

        testStandalone();
        testPeersAgree();
        testSlowPeers();
        testCloseTimeDisagree();
        testWrongLCL();
        testFork();

        simClockSkew();
        simScaleFree();
    }
};

BEAST_DEFINE_TESTSUITE(Consensus, consensus, ripple);
}  // test
}  // ripple
