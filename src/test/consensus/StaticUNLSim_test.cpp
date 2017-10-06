//-------------------------------x-----------------------------------------------
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
#include <test/csf/qos.h>
#include <test/csf/random.h>
#include <utility>

namespace ripple {
namespace test {

namespace csf {

inline
std::ostream & operator<<(std::ostream & os, std::chrono::milliseconds const & ms)
{
    return os << ms.count();
}
namespace detail {

void
vectorPrint(std::ostream& out, std::vector<std::string> const& vs)
{
    bool doComma = false;
    for (auto const& s : vs)
    {
        if (doComma)
            out << ",";
        out << s;
        doComma = true;
    }
}

template <typename Type, unsigned N, unsigned Last>
struct tuple_printer
{
    static void
    print(std::ostream& out, const Type& ts)
    {
        out << std::get<N>(ts) << ", ";
        tuple_printer<Type, N + 1, Last>::print(out, ts);
    }
};

template <typename Type, unsigned N>
struct tuple_printer<Type, N, N>
{
    static void
    print(std::ostream& out, const Type& ts)
    {
        out << std::get<N>(ts);
    }
};

template <class... Ts>
void
tuplePrint(std::ostream& out, std::tuple<Ts...> const& ts)
{
    tuple_printer<std::tuple<Ts...>, 0, sizeof...(Ts) - 1>::print(out, ts);
}
}  // namespace detail


template <class T>
struct Parm
{
    std::string const name;
    T const t;
};

template<class T>
Parm<T>
parm(std::string const & n, T const & t)
{
    return Parm<T>{n, t};
}

template <class... Ts>
struct Parms
{
    std::vector<std::string> names;
    std::tuple<Ts...> parms;

    Parms(Parm<Ts>... ps) : names{ps.name...}, parms{ps.t...} {}

    friend
    inline
    std::ostream& operator<<(std::ostream& out, Parms const & p)
    {
        detail::vectorPrint(out, p.names);
        out << ",";
        detail::tuplePrint(out, p.parms);
        return out;
    }
};

template <class... Ts>
Parms<Ts...>
parms(Parm<Ts> ...ps)
{
    return Parms<Ts...>(ps...);
}

class StreamingWriter
{
    std::ofstream out;
    bool printHeader = true;

public:
    StreamingWriter(std::string const & s) : out{s}
    {
    }


    template <class ... Ps, class ... Ts>
    void
    write(Parms<Ps...> const & parms, DataFrame<Ts...> const & df)
    {
        if(printHeader)
        {
            detail::vectorPrint(out, parms.names);
            out << ",";
            detail::vectorPrint(out, df.names);
            out << "\n";

        }
        printHeader = false;

        for(auto const & row : df.data)
        {
            detail::tuplePrint(out, parms.parms);
            out << ",";
            detail::tuplePrint(out, row);
            out << "\n";
        }
    }
};

// Study a
class StaticUNLSim_test : public beast::unit_test::suite
{
    void
    run() override
    {
        using namespace std::chrono;
        using namespace csf;

        // Constant Parameters
        std::chrono::nanoseconds const activeDuration = 3min;

        // Tx submission rate
        Rate const rate{10, 1000ms};

        // Output files
        StreamingWriter fProgressOut("forwardProgress.csv");
        StreamingWriter txProgressOut("txProgress.csv");
        StreamingWriter acceptLedgersOut("acceptLedgers.csv");
        StreamingWriter fullValidationsOut("fullValidations.csv");

        // Number of trials for random stagger start
        for (std::uint32_t trial = 0; trial < 50; ++trial)
        {
            // Fixed messaging delay
            for (milliseconds const delay :
                 {100ms, 200ms, 400ms, 800ms, 1600ms, 3200ms, 6400ms, 12800ms})
            //{3200ms})
            {
                // Network size
                for (std::uint32_t const size : {5, 10, 15, 20})
                //{5})
                {
                    // Warmup phase when connection still fast
                    for (milliseconds const warmupPhase : {0s, 30s})
                    {
                        Sim sim;
                        PeerGroup network = sim.createGroup(size);

                        std::chrono::nanoseconds const txWarmup = 0s;

                        std::chrono::nanoseconds const cooldown =
                            std::max<std::chrono::nanoseconds>(10 * delay, 10s);
                        std::chrono::nanoseconds const simDuration =
                            txWarmup + activeDuration + cooldown;
                        // Delayed start to let net
                        auto const fastUntil =
                            sim.scheduler.now() + warmupPhase;
                        // Use a fast time during the warmup phase
                        network.trustAndConnect(
                            network, [&sim, delay, fastUntil]() {
                                if (sim.scheduler.now() < fastUntil)
                                    return 200ms;
                                else
                                    return delay;
                            });

                        TxProgressCollector txProgress{size};
                        ForwardProgressCollector fProgress{size};
                        BranchCollector branchCollector;
                        StreamCollector sc{std::cout, false};
                        JumpCollector jumps;
                        auto coll = makeCollectors(
                            txProgress,
                            fProgress,
                            jumps,
                            branchCollector);  //, sc);
                        auto windowed = makeStartAfter(
                            sim.scheduler.now() + warmupPhase, coll);
                        sim.collectors.add(windowed);

                        // txs, start/stop/step, target
                        auto peerSelector = makeSelector(
                            network.begin(),
                            network.end(),
                            std::vector<double>(size, 1.),
                            sim.rng);

                        auto txSubmitter = makeSubmitter(
                            ConstantDistribution{rate.inv()},
                            sim.scheduler.now() + txWarmup,
                            sim.scheduler.now() + txWarmup + activeDuration,
                            peerSelector,
                            sim.scheduler,
                            sim.rng);

                        // run simulation for given duration

                        // stagger the initial start times
                        std::uniform_int_distribution<std::int64_t> randMS{
                            0, 1000};

                        for (Peer* p : network)
                        {
                            sim.scheduler.in(
                                milliseconds{randMS(sim.rng)},
                                [p]() { p->start(); });
                        }
                        sim.scheduler.step_for(simDuration);

                        fProgress.finalize(sim.scheduler.now());

                        BEAST_EXPECT(jumps.closeJumps.empty());
                        BEAST_EXPECT(jumps.fullyValidatedJumps.empty());
                        BEAST_EXPECT(sim.branches() == 1);
                        BEAST_EXPECT(sim.synchronized());

                        {
                            auto parameters = parms(
                                parm("Trial", trial),
                                parm("Delay", delay),
                                parm("Peers", size),
                                parm("Warmup", warmupPhase));

                            std::cout << parameters << std::endl;

                            fProgressOut.write(
                                parameters, fProgress.toDataFrame());
                            txProgressOut.write(
                                parameters, txProgress.toDataFrame());
                            acceptLedgersOut.write(
                                parameters, branchCollector.acceptLedgers);
                            fullValidationsOut.write(
                                parameters, branchCollector.fullValidations);
                        }
                    }
                }
            }
        }
        // bandwidth/messages vs network Size and delay
        // Smeared initial state?
    }
};

BEAST_DEFINE_TESTSUITE_MANUAL(StaticUNLSim, consensus, ripple);

}  // namespace csf
}  // namespace test
}  // namespace ripple
