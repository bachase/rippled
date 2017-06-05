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

class ClockSkewSim_test : public beast::unit_test::suite
{
    void
    run() override
    {
        using namespace csf;

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
        ConsensusParms parms;

        for (auto stagger : {800ms, 1600ms, 3200ms, 30000ms, 45000ms, 300000ms})
        {
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
};

BEAST_DEFINE_TESTSUITE_MANUAL(ClockSkewSim, consensus, ripple);

}  // namespace test
}  // namespace ripple
