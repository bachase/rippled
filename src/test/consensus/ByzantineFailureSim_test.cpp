//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2017 Ripple Labs Inc->

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

class ByzantineFailureSim_test : public beast::unit_test::suite
{
    void
    run() override
    {
        using namespace csf;
        using namespace std::chrono;

        // This test simulates a specific topology with nodes generating
        // different ledgers due to a simulated byzantine failure (injecting
        // an extra non-consensus transaction).

        std::vector<UNL> unls;
        unls.push_back({0, 1, 2, 6});
        unls.push_back({1, 0, 2, 3, 4});
        unls.push_back({2, 0, 1, 3, 4});
        unls.push_back({3, 1, 2, 4, 5});
        unls.push_back({4, 1, 2, 3, 5});
        unls.push_back({5, 3, 4, 6});
        unls.push_back({6, 0, 5});

        std::vector<int> assignment(unls.size());
        std::iota(assignment.begin(), assignment.end(), 0);
        TrustGraph tg{unls, assignment};
        StreamCollector sc{std::cout};

        for (TrustGraph::ForkInfo const& fi : tg.forkablePairs(0.8))
        {
            std::cout << "Can fork N" << fi.nodeA << " " << unls[fi.nodeA]
                      << " " << fi.nodeB << " N" << unls[fi.nodeB]
                      << " overlap " << fi.overlap << " required "
                      << fi.required << "\n";
        };

        ConsensusParms parms;

        Sim sim(
            parms,
            tg,
            topology(tg, fixed{round<milliseconds>(0.2 * parms.ledgerGRANULARITY)}),
            sc);
        // set prior state
        sim.run(1);

        // All peers see some TX 0
        for (auto& p : sim.peers)
        {
            p.submit(Tx(0));
            // Peers 0,1,2,6 will close the next ledger differently by injecting
            // a non-consensus approved transaciton
            if (p.id < NodeID{3} || p.id == NodeID{6})
            {
                p.txInjections.emplace(p.lastClosedLedger.get().seq(), Tx{42});
            }
        }
        sim.run(4);
        std::cout << "Num Forks: " << sim.forks() << "\n";
        std::cout << "Fully synchronized: " << std::boolalpha
                  << sim.synchronized() << "\n";
    }
};

BEAST_DEFINE_TESTSUITE_MANUAL(ByzantineFailureSim, consensus, ripple);

}  // namespace test
}  // namespace ripple
