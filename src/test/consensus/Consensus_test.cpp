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

        Sim s;
        PeerGroup peers = s.createGroup(1);
        for (Peer* peer : peers)
        {
            peer->targetLedgers = 1;
            peer->start();
            peer->submit(Tx{1});
        }

        s.scheduler.step();

        // Inspect that the proper ledger was created
        for (Peer const* p : peers)
        {
            auto const& lcl = p->lastClosedLedger.get();
            BEAST_EXPECT(p->prevLedgerID() == lcl.id());
            BEAST_EXPECT(lcl.seq() == Ledger::Seq{1});
            BEAST_EXPECT(lcl.txs().size() == 1);
            BEAST_EXPECT(lcl.txs().find(Tx{1}) != lcl.txs().end());
            BEAST_EXPECT(p->prevProposers == 0);
        }
    }

    void
    testPeersAgree()
    {
        using namespace csf;
        using namespace std::chrono;

        ConsensusParms parms;
        Sim sim;
        PeerGroup peers = sim.createGroup(5);

        // Fully connect the peers
        peers.trustAndConnect(
            peers, round<milliseconds>(0.2 * parms.ledgerGRANULARITY));

        // everyone submits their own ID as a TX and relay it to peers
        for (Peer * p : peers)
            p->submit(Tx(static_cast<std::uint32_t>(p->id)));

        sim.run(1);

        // All peers are in sync
        if (BEAST_EXPECT(sim.synchronized()))
        {
            for (Peer const* peer : peers)
            {
                // Inspect the first node's state
                auto const& lcl = peer->lastClosedLedger.get();
                BEAST_EXPECT(lcl.id() == peer->prevLedgerID());
                BEAST_EXPECT(lcl.seq() == Ledger::Seq{1});
                // All peers proposed
                BEAST_EXPECT(peer->prevProposers == peers.size() - 1);
                // All transactions were accepted
                for (std::uint32_t i = 0; i < peers.size(); ++i)
                    BEAST_EXPECT(lcl.txs().find(Tx{i}) != lcl.txs().end());
            }
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
            Sim sim;
            PeerGroup slow = sim.createGroup(1);
            PeerGroup fast = sim.createGroup(4);
            PeerGroup network = fast + slow;

            // Fully connected trust graph
            network.trust(network);

            // Fast and slow network connections
            fast.connect(
                fast, round<milliseconds>(0.2 * parms.ledgerGRANULARITY));

            slow.connect(
                fast, round<milliseconds>(1.1 * parms.ledgerGRANULARITY));

            // All peers submit their own ID as a transaction and relay it
            // to peers
            for (Peer* peer : network)
                peer->submit(Tx{static_cast<std::uint32_t>(peer->id)});

            sim.run(1);

            // Verify all peers have same LCL but are missing transaction 0
            // All peers are in sync even with a slower peer 0
            if (BEAST_EXPECT(sim.synchronized()))
            {
                for (Peer* peer : network)
                {
                    auto const& lcl = peer->lastClosedLedger.get();
                    BEAST_EXPECT(lcl.id() == peer->prevLedgerID());
                    BEAST_EXPECT(lcl.seq() == Ledger::Seq{1});

                    BEAST_EXPECT(peer->prevProposers == network.size() - 1);
                    BEAST_EXPECT(
                        peer->prevRoundTime == network[0]->prevRoundTime);

                    BEAST_EXPECT(lcl.txs().find(Tx{0}) == lcl.txs().end());
                    for (std::uint32_t i = 2; i < network.size(); ++i)
                        BEAST_EXPECT(lcl.txs().find(Tx{i}) != lcl.txs().end());

                    // Tx 0 didn't make it
                    BEAST_EXPECT(
                        peer->openTxs.find(Tx{0}) != peer->openTxs.end());
                }
            }
        }

        // Test when the slow peers delay a consensus quorum (4/6  agree)
        {
            // Run two tests
            //  1. The slow peers are participating in consensus
            //  2. The slow peers are just observing

            for (auto isParticipant : {true, false})
            {
                ConsensusParms parms;

                Sim sim;
                PeerGroup slow = sim.createGroup(2);
                PeerGroup fast = sim.createGroup(4);
                PeerGroup network = fast + slow;

                // Fully connected trust graph
                network.trust(network);

                // Fast and slow network connections
                fast.connect(
                    fast, round<milliseconds>(0.2 * parms.ledgerGRANULARITY));

                slow.connect(
                    fast, round<milliseconds>(1.1 * parms.ledgerGRANULARITY));

                for (Peer* peer : slow)
                    peer->runAsValidator = isParticipant;

                // All peers submit their own ID as a transaction and relay it
                // to peers
                for (Peer* peer : network)
                    peer->submit(Tx{static_cast<std::uint32_t>(peer->id)});

                sim.run(1);

                if (BEAST_EXPECT(sim.synchronized()))
                {
                    // Verify all peers have same LCL but are missing
                    // transaction 0,1 which was not received by all peers before
                    // the ledger closed
                    for (Peer* peer : network)
                    {
                        // Closed ledger has all but transaction 0,1
                        auto const& lcl = peer->lastClosedLedger.get();
                        BEAST_EXPECT(lcl.seq() == Ledger::Seq{1});
                        BEAST_EXPECT(lcl.txs().find(Tx{0}) == lcl.txs().end());
                        for (std::uint32_t i = slow.size(); i < network.size();
                             ++i)
                            BEAST_EXPECT(
                                lcl.txs().find(Tx{i}) != lcl.txs().end());

                        // Tx 0-1 didn't make it
                        BEAST_EXPECT(
                            peer->openTxs.find(Tx{0}) != peer->openTxs.end());
                        BEAST_EXPECT(
                            peer->openTxs.find(Tx{1}) != peer->openTxs.end());
                    }

                    Peer* slowPeer = *slow.begin();

                    BEAST_EXPECT(
                        slowPeer->prevProposers ==
                        network.size() - slow.size());

                    for (Peer* peer : fast)
                    {
                        // Due to the network link delay settings
                        //    Peer 0 initially proposes {0}
                        //    Peer 1 initially proposes {1}
                        //    Peers 2-5 initially propose {2,3,4,5}
                        // Since peers 2-5 agree, 4/6 > the initial 50% needed
                        // to include a disputed transaction, so Peer 0/1 switch
                        // to agree with those peers.  It then closes with an
                        // 80% quorum of agreeing positions (5/6) match
                        //
                        // Peers 2-5 do not change position, since tx 0 or tx 1
                        // have less than the 50% initial threshold.  They also
                        // cannot declare consensus, since 4/6 < 80% threshold
                        // ..).  They therefore need an additional timer period
                        // to see the updated positions from Peer 0 & 1.

                        if (isParticipant)
                        {
                            BEAST_EXPECT(
                                peer->prevProposers == network.size() - 1);
                            BEAST_EXPECT(
                                peer->prevRoundTime > slowPeer->prevRoundTime);
                        }
                        else
                        {
                            BEAST_EXPECT(
                                peer->prevProposers == fast.size() - 1);
                            // so all peers should have closed together
                            BEAST_EXPECT(
                                peer->prevRoundTime == slowPeer->prevRoundTime);
                        }
                    }
                }

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
        Sim sim;

        PeerGroup groupA = sim.createGroup(2);
        PeerGroup groupB = sim.createGroup(2);
        PeerGroup groupC = sim.createGroup(2);
        PeerGroup network = groupA + groupB + groupC;

        network.trust(network);
        network.connect(
            network, round<milliseconds>(0.2 * parms.ledgerGRANULARITY));

        // Run consensus without skew until we have a short close time
        // resolution
        Peer* firstPeer = *groupA.begin();
        while (firstPeer->lastClosedLedger.get().closeTimeResolution() >=
               parms.proposeFRESHNESS)
            sim.run(1);

        // Introduce a shift on the time of 2/3 of peers
        for (Peer* peer : groupA)
            peer->clockSkew = parms.proposeFRESHNESS / 2;
        for (Peer* peer : groupB)
            peer->clockSkew = parms.proposeFRESHNESS;

        sim.run(1);

        // All nodes agreed to disagree on the close time
        if (BEAST_EXPECT(sim.synchronized()))
        {
            for (Peer* peer : network)
                BEAST_EXPECT(!peer->lastClosedLedger.get().closeAgree());
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
            // 0 1         2 3 4       5 6 7 8 9
            // minority   majorityA   majorityB
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

            Sim sim;

            PeerGroup minority = sim.createGroup(2);
            PeerGroup majorityA = sim.createGroup(3);
            PeerGroup majorityB = sim.createGroup(5);

            PeerGroup majority = majorityA + majorityB;
            PeerGroup network = minority + majority;

            SimDuration delay =
                round<milliseconds>(0.2 * parms.ledgerGRANULARITY);
            minority.trustAndConnect(minority + majorityA, delay);
            majority.trustAndConnect(majority, delay);

            // This topology can potentially fork, which is why we are using it
            // for this test.
            BEAST_EXPECT(sim.trustGraph.canFork(parms.minCONSENSUS_PCT / 100.));

            // initial round to set prior state
            sim.run(1);

            // Nodes in smaller UNL have seen tx 0, nodes in other unl have seen
            // tx 1
            for (Peer* peer : network)
                peer->delays.recvValidation = validationDelay;
            for (Peer* peer : (minority + majorityA))
                peer->openTxs.insert(Tx{0});
            for (Peer* peer : majorityB)
                peer->openTxs.insert(Tx{1});

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
                for(Peer const* peer : majority)
                {
                    // No jumps
                    BEAST_EXPECT(peer->fullyValidatedLedger.jumps().empty());
                    BEAST_EXPECT(peer->lastClosedLedger.jumps().empty());
                }
                for(Peer const* peer : minority)
                {
                    // last closed ledger jump between chains
                    {
                        if (BEAST_EXPECT(
                                peer->lastClosedLedger.jumps().size() == 1))
                        {
                            LedgerState::Jump const& jump =
                                peer->lastClosedLedger.jumps().front();
                            // Jump is to a different chain
                            BEAST_EXPECT(jump.from.seq() <= jump.to.seq());
                            BEAST_EXPECT(
                                !sim.oracle.isAncestor(jump.from, jump.to));
                        }
                    }
                    // fully validted jump forward in same chain
                    {
                        if (BEAST_EXPECT(
                                peer->fullyValidatedLedger.jumps().size() == 1))
                        {
                            LedgerState::Jump const& jump =
                                peer->fullyValidatedLedger.jumps().front();
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

            // Loner node will accept an initial ledger A, but all other nodes
            // accept ledger B a bit later.  By delaying the time it takes
            // to process a validation, loner node will detect the wrongLCL
            // after it is already in the establish phase of the next round.

            Sim sim;
            PeerGroup loner = sim.createGroup(1);
            PeerGroup friends = sim.createGroup(3);
            loner.trust(loner + friends);

            PeerGroup others = sim.createGroup(6);
            PeerGroup clique = friends + others;
            clique.trust(clique);

            PeerGroup network = loner + clique;
            network.connect(
                network, round<milliseconds>(0.2 * parms.ledgerGRANULARITY));

            // initial round to set prior state
            sim.run(1);
            for (Peer* peer : (loner + friends))
                peer->openTxs.insert(Tx(0));
            for (Peer* peer : others)
                peer->openTxs.insert(Tx(1));

            // Delay validation processing
            for (Peer* peer : network)
                peer->delays.recvValidation = parms.ledgerGRANULARITY;

            // additional rounds to generate wrongLCL and recover
            sim.run(2);

            // Check all peers recovered
            for (Peer * p: network)
                BEAST_EXPECT(p->prevLedgerID() == network[0]->prevLedgerID());
        }
    }

    void
    testFork()
    {
        using namespace csf;
        using namespace std::chrono;

        std::uint32_t numPeers = 10;
        for (std::uint32_t overlap = 0; overlap <= numPeers; ++overlap)
        {
            ConsensusParms parms;
            Sim sim;

            std::uint32_t numA = (numPeers - overlap) / 2;
            std::uint32_t numB = numPeers - numA - overlap;

            // Calculate size of groups
            PeerGroup aOnly = sim.createGroup(numA);
            PeerGroup bOnly = sim.createGroup(numB);
            PeerGroup commonOnly = sim.createGroup(overlap);

            PeerGroup a = aOnly + commonOnly;
            PeerGroup b = bOnly + commonOnly;

            PeerGroup network = a + b;

            SimDuration delay =
                round<milliseconds>(0.2 * parms.ledgerGRANULARITY);
            a.trustAndConnect(a, delay);
            b.trustAndConnect(b, delay);

            // Initial round to set prior state
            sim.run(1);
            for (Peer* peer : network)
            {
                // Nodes have only seen transactions from their neighbors
                peer->openTxs.insert(Tx{static_cast<std::uint32_t>(peer->id)});
                for (Peer* to : sim.trustGraph.trustedPeers(peer))
                    peer->openTxs.insert(
                        Tx{static_cast<std::uint32_t>(to->id)});
            }
            sim.run(1);

            // Fork should not happen for 40% or greater overlap
            // Since the overlapped nodes have a UNL that is the union of the
            // two cliques, the maximum sized UNL list is the number of peers
            if (overlap > 0.4 * numPeers)
                BEAST_EXPECT(sim.synchronized());
            else
            {
                // Even if we do fork, there shouldn't be more than 3 ledgers
                // One for cliqueA, one for cliqueB and one for nodes in both
                BEAST_EXPECT(sim.forks() <= 3);
            }
        }
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
    }
};

BEAST_DEFINE_TESTSUITE(Consensus, consensus, ripple);
}  // namespace test
}  // namespace ripple
