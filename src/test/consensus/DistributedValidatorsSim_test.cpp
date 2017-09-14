//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2016 Ripple Labs Inc.

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

#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <algorithm>
#include <sstream>
#include <fstream>
#include <string>
#include <thread>
#include <mutex>

namespace ripple {
namespace test {

/** In progress simulations for diversifying and distributing validators
*/
class DistributedValidatorsSim_test : public beast::unit_test::suite
{
    std::mutex mutex;

    void
    completeTrustCompleteConnectNormalDelay(
            std::size_t numPeers,
            std::chrono::milliseconds delay = std::chrono::milliseconds(200),
            bool printHeaders = false)
    {
        using namespace csf;
        using namespace std::chrono;

        // Initialize persistent collector logs specific to this method
        std::string const prefix =
                "DistributedValidatorsSim_"
                "completeTrustCompleteConnectNormalDelay";
        std::fstream
                txLog(prefix + "_tx.csv", std::ofstream::app),
                ledgerLog(prefix + "_ledger.csv", std::ofstream::app);

        // title
        {
          std::lock_guard<std::mutex> guard(mutex);
          log << prefix << "(" << numPeers << "," << delay.count() << ")"
              << std::endl;
        }

        // number of peers, UNLs, connections
        BEAST_EXPECT(numPeers >= 1);

        ConsensusParms const parms;
        Sim sim;
        PeerGroup peers = sim.createGroup(numPeers);

        // complete trust graph
        peers.trust(peers);

        // complete connect graph with fixed delay
        //    - delay for a given message; avg in ms, stddev in ms
        SimDuration avgConnectionDelay = 200ms;
        SimDuration stddevConnectionDelay = 60ms;
        std::normal_distribution<double> delayDistr(
                avgConnectionDelay.count(),
                stddevConnectionDelay.count());
        DurationDistribution delayGen{randomDuration(delayDistr, sim.rng)};
        peers.connect(peers, delayGen);

        // Initialize collectors to track statistics to report
        TxCollector txCollector;
        LedgerCollector ledgerCollector;
        auto colls = makeCollectors(txCollector, ledgerCollector);
        sim.collectors.add(colls);

        // Initial round to set prior state
        sim.run(1);

        // Initialize timers
        HeartbeatTimer heart(sim.scheduler);

        // Run for 10 minues, submitting 100 tx/second
        std::chrono::nanoseconds const simDuration = 10min;
        std::chrono::nanoseconds const quiet = 10s;
        Rate const avgSubmissionRate{100, 1000ms};
        std::exponential_distribution<double> submissionInterval(
                avgSubmissionRate.ratio());

        // txs, start/stop/step, target
        auto peerSelector = makeSelector(peers.begin(),
                                     peers.end(),
                                     std::vector<double>(numPeers, 1.),
                                     sim.rng);
        auto txSubmitter = makeSubmitter(submissionInterval,
                                     sim.scheduler.now() + quiet,
                                     sim.scheduler.now() + simDuration - quiet,
                                     peerSelector,
                                     sim.scheduler,
                                     sim.rng);

        // run simulation for given duration
        heart.start();
        sim.run(simDuration);

        //BEAST_EXPECT(sim.branches() == 1);
        //BEAST_EXPECT(sim.synchronized());

        {
          std::lock_guard<std::mutex> guard(mutex);
          log << std::right;
          log << "| Peers: "<< std::setw(2) << peers.size();
          log << " | Duration: " << std::setw(6)
              << duration_cast<milliseconds>(simDuration).count() << " ms";
          log << " | Branches: " << std::setw(1) << sim.branches();
          log << " | Synchronized: " << std::setw(1)
              << (sim.synchronized() ? "Y" : "N");
          log << " |" << std::endl;
  
          txCollector.report(simDuration, log, true);
          ledgerCollector.report(simDuration, log, false);
  
          std::string const tag = "{"
             "\"numPeers\":" +std::to_string(numPeers) + ","
             "\"delay\":" + std::to_string(delay.count()) + "}";
  
          txCollector.csv(simDuration, txLog, tag, printHeaders);
          ledgerCollector.csv(simDuration, ledgerLog, tag, printHeaders);
  
          log << std::endl;
        }
    }

    void
    completeTrustScaleFreeConnectNormalDelay(
            std::size_t numPeers,
            std::chrono::milliseconds delay = std::chrono::milliseconds(200),
            bool printHeaders = false)
    {
        using namespace csf;
        using namespace std::chrono;

        // Initialize persistent collector logs specific to this method
        std::string const prefix =
                "DistributedValidatorsSim_"
                "completeTrustScaleFreeConnectNormalDelay";
        std::fstream
                txLog(prefix + "_tx.csv", std::ofstream::app),
                ledgerLog(prefix + "_ledger.csv", std::ofstream::app);

        // title
        {
          std::lock_guard<std::mutex> guard(mutex);
          log << prefix << "(" << numPeers << "," << delay.count() << ")"
              << std::endl;
        }

        // number of peers, UNLs, connections
        int const numCNLs    = std::max(int(1.00 * numPeers), 1);
        int const minCNLSize = std::max(int(0.25 * numCNLs),  1);
        int const maxCNLSize = std::max(int(0.50 * numCNLs),  1);
        BEAST_EXPECT(numPeers >= 1);
        BEAST_EXPECT(numCNLs >= 1);
        BEAST_EXPECT(1 <= minCNLSize
                && minCNLSize <= maxCNLSize
                && maxCNLSize <= numPeers);

        ConsensusParms const parms;
        Sim sim;
        PeerGroup peers = sim.createGroup(numPeers);

        // complete trust graph
        peers.trust(peers);

        // scale-free connect graph with fixed delay
        SimDuration const avgConnectionDelay = 200ms;
        SimDuration const stddevConnectionDelay = 60ms;
        std::normal_distribution<double> delayDistr(
                avgConnectionDelay.count(),
                stddevConnectionDelay.count());
        DurationDistribution delayGen{randomDuration(delayDistr, sim.rng)};
        std::vector<double> const ranks =
                sample(peers.size(), PowerLawDistribution{1, 3}, sim.rng);
        randomRankedConnect(peers, ranks, numCNLs,
                std::uniform_int_distribution<>{minCNLSize, maxCNLSize},
                sim.rng, delayGen);

        // Initialize collectors to track statistics to report
        TxCollector txCollector;
        LedgerCollector ledgerCollector;
        auto colls = makeCollectors(txCollector, ledgerCollector);
        sim.collectors.add(colls);

        // Initial round to set prior state
        sim.run(1);

        // Initialize timers
        HeartbeatTimer heart(sim.scheduler);

        // Run for 10 minues, submitting 100 tx/second
        std::chrono::nanoseconds const simDuration = 10min;
        std::chrono::nanoseconds const quiet = 10s;
        Rate const avgSubmissionRate{100, 1000ms};
        std::exponential_distribution<double> submissionInterval(
                avgSubmissionRate.ratio());

        // txs, start/stop/step, target
        auto peerSelector = makeSelector(peers.begin(),
                                     peers.end(),
                                     ranks,
                                     sim.rng);
        auto txSubmitter = makeSubmitter(submissionInterval,
                                     sim.scheduler.now() + quiet,
                                     sim.scheduler.now() + simDuration - quiet,
                                     peerSelector,
                                     sim.scheduler,
                                     sim.rng);

        // run simulation for given duration
        heart.start();
        sim.run(simDuration);

        //BEAST_EXPECT(sim.branches() == 1);
        //BEAST_EXPECT(sim.synchronized());

        {
          std::lock_guard<std::mutex> guard(mutex);
          log << std::right;
          log << "| Peers: "<< std::setw(2) << peers.size();
          log << " | Duration: " << std::setw(6)
              << duration_cast<milliseconds>(simDuration).count() << " ms";
          log << " | Branches: " << std::setw(1) << sim.branches();
          log << " | Synchronized: " << std::setw(1)
              << (sim.synchronized() ? "Y" : "N");
          log << " |" << std::endl;
  
          txCollector.report(simDuration, log, true);
          ledgerCollector.report(simDuration, log, false);
  
          std::string const tag = "{"
             "\"numPeers\":" +std::to_string(numPeers) + ","
             "\"delay\":" + std::to_string(delay.count()) + "}";
  
          txCollector.csv(simDuration, txLog, tag, printHeaders);
          ledgerCollector.csv(simDuration, ledgerLog, tag, printHeaders);
  
          log << std::endl;
        }
    }

    void
    ringGrouped(std::size_t numGroups, std::size_t peersPerGroup)
    {
        log << "DistributedValidatorsSim_test::ringGrouped not implemented"
            << std::endl;
    }

    void
    run() override
    {
        std::string defaultArgs = "4 5 100 200";
        std::string args = arg().empty() ? defaultArgs : arg();
        std::stringstream argStream(args);

        int maxThreads = 0;
        int maxNumValidators = 0;
        std::vector<std::chrono::milliseconds> delays;

        int delayCount(200);
        argStream >> maxThreads;
        argStream >> maxNumValidators;
        while(argStream >> delayCount)
            delays.emplace_back(delayCount);

        std::chrono::milliseconds delay(delayCount);

        log << "DistributedValidatorsSim: 1 to " << maxNumValidators << " Peers"
            << " on " << maxThreads << " threads." << std::endl;

        /**
         * Simulate with N = 1 to N
         *  completeTrustCompleteConnectNormalDelay
         *      - complete trust graph
         *      - complete network connectivity
         *      - normal delay for network links
         *  completeTrustScaleFreeConnectNormalDelay
         *      - scale-free trust graph
         *      - complete network connectivity
         *      - normal delay for network links
         */

        for(auto f : {
                &DistributedValidatorsSim_test
                        ::completeTrustCompleteConnectNormalDelay,
                &DistributedValidatorsSim_test
                        ::completeTrustScaleFreeConnectNormalDelay})
        {
            bool printHeaders = true;
            std::vector<std::thread> threads(maxThreads);
            for (auto delay : delays)
            {
                for (int i = 1; i <= maxNumValidators;)
                {
                    for (int j = 0; j < maxThreads && i <= maxNumValidators;
                         i++, j++)
                    {
                        threads[j] = std::thread(f, this,
                                i, delay, printHeaders);
                        printHeaders = false;
                    }
                    for (int k = 0; k < maxThreads; k++)
                        if (threads[k].joinable())
                            threads[k].join();
                }
            }
        }

    }

};

BEAST_DEFINE_TESTSUITE_MANUAL(DistributedValidatorsSim, consensus, ripple);

}  // namespace test
}  // namespace ripple
