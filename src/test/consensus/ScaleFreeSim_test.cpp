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
#include <test/csf.h>
#include <utility>

namespace ripple {
namespace test {

class ScaleFreeSim_test : public beast::unit_test::suite
{
    void
    run() override
    {
        using namespace std::chrono;
        using namespace csf;

        // Generate a quasi-random scale free network and simulate consensus
        // as we vary transaction submission rates

        // NOTE:
        // Currently This simulation is a work in progress and is being used to
        // explore the best design for the simulation framework

        int N = 100;  // Peers

        int numUNLs = 15;  //  UNL lists
        int minUNLSize = N / 4, maxUNLSize = N / 2;

        double transProb = 0.5;

        std::mt19937_64 rng;

        auto tg = TrustGraph::makeRandomRanked(
            N,
            numUNLs,
            PowerLawDistribution{1, 3},
            std::uniform_int_distribution<>{minUNLSize, maxUNLSize},
            rng);

        ConsensusParms parms;
        Sim sim{
            parms,
            tg,
            topology(
                tg, fixed{round<milliseconds>(0.2 * parms.ledgerGRANULARITY)})};

        // Initial round to set prior state
        sim.run(1);

        // Run for 10 minues, submitting 100 tx/second
        std::chrono::nanoseconds simDuration = 10min;
        std::chrono::nanoseconds quiet = 10s;


        struct SteadySubmitter
        {
            using Network = BasicNetwork<Peer*>;
            std::chrono::nanoseconds txRate = 1000ms/100;
            Network::time_point end;
            std::uint32_t txId = 0;
            Peer & target;
            Scheduler & scheduler;

            SteadySubmitter(Peer & t, Scheduler & s,
                 Network::time_point start,
                 Network::time_point endTime)
                : target(t), scheduler(s), end(endTime)
            {
                scheduler.at(start, [&]() { submit(); });
            }

            void
            submit()
            {
                target.submit(Tx{txId});
                txId++;
                if (scheduler.now() < end)
                    scheduler.in(txRate, [&]() { submit(); });
            }
        };

        // txs, start/stop/step, target
        SteadySubmitter ss(
            sim.peers.front(),
            sim.scheduler,
            sim.scheduler.now() + quiet,
            sim.scheduler.now() + (simDuration - quiet));

        // run simulation for given duration
        sim.run(simDuration);

        BEAST_EXPECT(sim.forks() == 1);
        BEAST_EXPECT(sim.synchronized());

        // Print summary?
        // # forks?  # of LCLs?
        // # peers
        // # tx submitted
        // # ledgers/sec etc.?
    }
};

BEAST_DEFINE_TESTSUITE_MANUAL(ScaleFreeSim, consensus, ripple);

}  // namespace test
}  // namespace ripple
