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

        std::mt19937_64 rng;

        auto tg = TrustGraph::makeRandomRanked(
            N,
            numUNLs,
            PowerLawDistribution{1, 3},
            std::uniform_int_distribution<>{minUNLSize, maxUNLSize},
            rng);

        TxCollector txCollector;
        LedgerCollector ledgerCollector;
        ConsensusParms parms;
        auto colls = collectors(txCollector, ledgerCollector);

        Sim sim{
            parms,
            tg,
            topology(
                tg, fixed{round<milliseconds>(0.2 * parms.ledgerGRANULARITY)}),
            colls};

        // Initial round to set prior state
        sim.run(1);

        // Run for 10 minues, submitting 100 tx/second
        std::chrono::nanoseconds simDuration = 10min;
        std::chrono::nanoseconds quiet = 10s;
        Rate rate{100, 1000ms};

        // txs, start/stop/step, target
        auto txSubmitter = submitter(Fixed{rate},
                          sim.scheduler.now() + quiet,
                          sim.scheduler.now() + (simDuration - quiet),
                          sim.peers.front(),
                          sim.scheduler,
                          rng);

        // run simulation for given duration
        sim.run(simDuration);

        BEAST_EXPECT(sim.forks() == 1);
        BEAST_EXPECT(sim.synchronized());

        // TODO: Clean up this formatting mess!!

        log << "Peers: " << sim.peers.size() << std::endl;
        log << "Simulated Duration: "
            << duration_cast<milliseconds>(simDuration).count()
            << " ms" << std::endl;
        log << "Forks: " << sim.forks() << std::endl;
        log << "Synchronized: " << (sim.synchronized() ? "Y" : "N")
            << std::endl;
        log << std::endl;

        auto perSec = [&simDuration](std::size_t count)
        {
            return double(count)/duration_cast<seconds>(simDuration).count();
        };

        auto fmtS = [](SimDuration dur)
        {
            return duration_cast<duration<float>>(dur).count();
        };

        log << "Transaction Statistics" << std::endl;

        log << std::left
            << std::setw(11) << "Name" <<  "|"
            << std::setw(7) << "Count" <<  "|"
            << std::setw(7) << "Per Sec" <<  "|"
            << std::setw(15) << "Latency (sec)"
            << std::right
            << std::setw(7) << "10-ile"
            << std::setw(7) << "50-ile"
            << std::setw(7) << "90-ile"
            << std::left
            << std::endl;

        log << std::setw(11) << std::setfill('-') << "-" <<  "|"
            << std::setw(7) << std::setfill('-') << "-" <<  "|"
            << std::setw(7) << std::setfill('-') << "-" <<  "|"
            << std::setw(36) << std::setfill('-') << "-"
            << std::endl;
        log << std::setfill(' ');

        log << std::left <<
            std::setw(11) << "Submit " << "|"
            << std::right
            << std::setw(7) << txCollector.submitted << "|"
            << std::setw(7) << std::setprecision(2) << perSec(txCollector.submitted) << "|"
            << std::setw(36) << "" << std::endl;

        log << std::left
            << std::setw(11) << "Accept " << "|"
            << std::right
            << std::setw(7) << txCollector.accepted << "|"
            << std::setw(7) << std::setprecision(2) << perSec(txCollector.accepted) << "|"
            << std::setw(15) << std::left << "From Submit" << std::right
            << std::setw(7) << std::setprecision(2) << fmtS(txCollector.submitToAccept.percentile(0.1f))
            << std::setw(7) << std::setprecision(2) << fmtS(txCollector.submitToAccept.percentile(0.5f))
            << std::setw(7) << std::setprecision(2) << fmtS(txCollector.submitToAccept.percentile(0.9f))
            << std::endl;

        log << std::left
            << std::setw(11) << "Validate " << "|"
            << std::right
            << std::setw(7) << txCollector.validated << "|"
            << std::setw(7) << std::setprecision(2) << perSec(txCollector.validated) << "|"
            << std::setw(15) << std::left << "From Submit" << std::right
            << std::setw(7) << std::setprecision(2) << fmtS(txCollector.submitToValidate.percentile(0.1f))
            << std::setw(7) << std::setprecision(2) << fmtS(txCollector.submitToValidate.percentile(0.5f))
            << std::setw(7) << std::setprecision(2) << fmtS(txCollector.submitToValidate.percentile(0.9f))
            << std::endl;

        log << std::left
            << std::setw(11) << "Orphan" << "|"
            << std::right
            << std::setw(7) << txCollector.orphaned() << "|"
            << std::setw(7) << "" << "|"
            << std::setw(36) << std::endl;

        log << std::left
            << std::setw(11) << "Unvalidated" << "|"
            << std::right
            << std::setw(7) << txCollector.unvalidated() << "|"
            << std::setw(7) << "" << "|"
            << std::setw(43) << std::endl;

        log << std::endl;
        log << std::left << "Ledger statistics " << std::endl;

        log << std::left
            << std::setw(11) << "Name" <<  "|"
            << std::setw(7)  << "Count" <<  "|"
            << std::setw(7)  << "Per Sec" <<  "|"
            << std::setw(15) << "Latency (sec)"
            << std::right
            << std::setw(7) << "10-ile"
            << std::setw(7) << "50-ile"
            << std::setw(7) << "90-ile"
            << std::left
            << std::endl;

         log << std::left
            << std::setw(11) << "Accept " << "|"
            << std::right
            << std::setw(7) << ledgerCollector.accepted << "|"
            << std::setw(7) << std::setprecision(2) << perSec(ledgerCollector.accepted) << "|"
            << std::setw(15) << std::left << "From Accept" << std::right
            << std::setw(7) << std::setprecision(2) << fmtS(ledgerCollector.acceptToAccept.percentile(0.1f))
            << std::setw(7) << std::setprecision(2) << fmtS(ledgerCollector.acceptToAccept.percentile(0.5f))
            << std::setw(7) << std::setprecision(2) << fmtS(ledgerCollector.acceptToAccept.percentile(0.9f))
            << std::endl;

        log << std::left
            << std::setw(11) << "Validate " << "|"
            << std::right
            << std::setw(7) << ledgerCollector.fullyValidated << "|"
            << std::setw(7) << std::setprecision(2) << perSec(ledgerCollector.fullyValidated) << "|"
            << std::setw(15) << std::left << "From Validate " << std::right
            << std::setw(7) << std::setprecision(2) << fmtS(ledgerCollector.fullyValidToFullyValid.percentile(0.1f))
            << std::setw(7) << std::setprecision(2) << fmtS(ledgerCollector.fullyValidToFullyValid.percentile(0.5f))
            << std::setw(7) << std::setprecision(2) << fmtS(ledgerCollector.fullyValidToFullyValid.percentile(0.9f))
            << std::endl;

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
