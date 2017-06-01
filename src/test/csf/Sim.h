//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2017 Ripple Labs Inc

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

#ifndef RIPPLE_TEST_CSF_SIM_H_INCLUDED
#define RIPPLE_TEST_CSF_SIM_H_INCLUDED

#include <test/csf/BasicNetwork.h>
#include <test/csf/Scheduler.h>
#include <test/csf/Peer.h>
#include <test/csf/UNL.h>

namespace ripple {
namespace test {
namespace csf {

class Sim
{
public:

    LedgerOracle oracle;
	Scheduler scheduler;
    BasicNetwork<Peer*> net;
    std::vector<Peer> peers;

    /** Create a simulator for the given trust graph and network topology.

        Create a simulator for consensus over the given trust graph and connect
        the network links between nodes based on the provided topology.

        Topology is is a functor with signature

               boost::optional<std::chrono::duration> (NodeId i, NodeId j)

        that returns the delay sending messages from node i to node j.

        In general, this network graph is distinct from the trust graph, but
        users can use adaptors to present a TrustGraph as a Topology by
        specifying the delay between nodes.

        @param g The trust graph between peers.
        @param top The network topology between peers.
        @param parms Consensus parameters to use in the simulation

    */
    template <class Topology>
    Sim(ConsensusParms parms, TrustGraph const& g, Topology const& top)
        : net{scheduler}
    {
        peers.reserve(g.numPeers());
        for (std::uint32_t i = 0; i < g.numPeers(); ++i)
            peers.emplace_back(i, scheduler, oracle, net, g.unl(i));

        for (std::uint32_t i = 0; i < peers.size(); ++i)
        {
            for (std::uint32_t j = 0; j < peers.size(); ++j)
            {
                if (i != j)
                {
                    auto d = top(i, j);
                    if (d)
                    {
                        net.connect(&peers[i], &peers[j], *d);
                    }
                }
            }
        }
    }

    /** Run consensus protocol to generate the provided number of ledgers.

        Has each peer run consensus until it creates `ledgers` more ledgers.

        @param ledgers The number of additional ledgers to create
    */
    void
    run(int ledgers);

    /** Check whether all peers in the network are synchronized.

        Nodes in the network are synchronized if they share the same last
        fully validated and last generated ledger.
    */
    bool
    synchronized() const;

    /** Calculate the number of forks in the network.

        A fork occurs if two peers have fullyValidatedLedgers that are not on
        the same chain of ledgers.
    */
    std::size_t
    forks() const;

};

}  // csf
}  // test
}  // ripple

#endif
