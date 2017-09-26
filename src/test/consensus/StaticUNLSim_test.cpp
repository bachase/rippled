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
#include <test/csf/random.h>
#include <test/csf/qos.h>
#include <utility>

namespace ripple {
namespace test {

namespace csf {

// Study a
class StaticUNLSim_test : public beast::unit_test::suite
{
    void
    run() override
    {
        using namespace std::chrono;
        using namespace csf;

        // Parameters

        // Number of Peers
        std::uint32_t const size = 5;
        // Tx submission rate
        Rate const rate{10, 1000ms};

        // Fixed messaging delay
        for (milliseconds const delay :
             {100ms, 200ms, 400ms, 800ms, 1600ms, 3200ms, 6400ms, 12800ms})
        {
            Sim sim;
            PeerGroup network = sim.createGroup(size);
            network.trustAndConnect(network, delay);

            TxProgressCollector txProgress{size};
            ForwardProgressCollector fProgress{size};
            StreamCollector sc{std::cout};
            JumpCollector jumps;
            auto coll = makeCollectors(txProgress, fProgress, jumps);  //, sc);
            sim.collectors.add(coll);

            // How long to run simulation?
            // -- delta

            std::chrono::nanoseconds warmup = 10s;
            std::chrono::nanoseconds activeDuration = 5min;
            std::chrono::nanoseconds cooldown = std::max<std::chrono::nanoseconds>(10 * delay, warmup);
            std::chrono::nanoseconds simDuration =
                warmup + activeDuration + cooldown;
            // txs, start/stop/step, target
            auto peerSelector = makeSelector(
                network.begin(),
                network.end(),
                std::vector<double>(size, 1.),
                sim.rng);

            auto txSubmitter = makeSubmitter(
                ConstantDistribution{rate.inv()},
                sim.scheduler.now() + warmup,
                sim.scheduler.now() + warmup + activeDuration,
                peerSelector,
                sim.scheduler,
                sim.rng);

// run simulation for given duration
#if 0
        auto stagger =
            asDurationDist(std::uniform_int_distribution<std::int64_t>{
                0, SimDuration{1s}.count()});
        sim.run(simDuration, [&sim, &stagger](Peer const&) {
            return stagger(sim.rng);
        });
#endif
            sim.run(simDuration);

            fProgress.finalize(sim.scheduler.now());

            // Dump samples or percentile versus delta
            BEAST_EXPECT(jumps.closeJumps.empty());
            BEAST_EXPECT(jumps.fullyValidatedJumps.empty());
            BEAST_EXPECT(sim.branches() == 1);
            BEAST_EXPECT(sim.synchronized());

            // minimum/maximum absolute delay

            std::ofstream fProgressOut{"fProgress.delay" +std::to_string(delay.count()) + ".csv"};
            fProgress.dump(fProgressOut);

            std::ofstream txProgressOut{"txProgress.delay" +std::to_string(delay.count()) + ".csv"};
            txProgress.dump(txProgressOut);
        }
        // bandwidth/messages vs network Size and delay
        // Smeared initial state?
    }
};

BEAST_DEFINE_TESTSUITE_MANUAL(StaticUNLSim, consensus, ripple);

}  // namespace csf
}  // namespace test
}  // namespace ripple