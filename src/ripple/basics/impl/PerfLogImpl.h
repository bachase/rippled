//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2017 Ripple Labs Inc.

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

#ifndef RIPPLE_BASICS_PERFLOGIMPL_H_INCLUDED
#define RIPPLE_BASICS_PERFLOGIMPL_H_INCLUDED

#include <ripple/beast/core/SystemStats.h>
#include <ripple/app/main/Application.h>
#include <ripple/core/JobTypes.h>
#include <ripple/protocol/TER.h>
#include <ripple/basics/PerfLog.h>
#include <ripple/core/Stoppable.h>
#include <ripple/json/json_value.h>
#include <thread>
#include <chrono>
#include <string>
#include <fstream>
#include <mutex>
#include <condition_variable>
#include <unordered_map>
#include <memory>
#include <atomic>

namespace ripple {
namespace perf {

class TraceMap;
class SharedTraceMap;

class PerfLogImpl
    : public PerfLog, Stoppable
{
public:
    PerfLogImpl(Setup const& setup,
        Stoppable& parent,
        Application& app);
    ~PerfLogImpl() override;
    void rotate() override;

    bool
    logging() const override
    {
        return logging_;
    }

    void
    rpcRunning(std::string const& method) override
    {
        if (!logging())
            return;
        rpc(method, "running");
        rpc("total", "running");
    }

    void
    rpcFinished(std::string const& method) override
    {
        if (!logging())
            return;
        rpc(method, "finished");
        rpc("total", "finished");
    }

    void
    rpcErrored(std::string const& method) override
    {
        if (!logging())
            return;
        rpc(method, "errored");
        rpc("total", "errored");
    }

    void
    jobQueued(JobType const& jt) override
    {
        if (!logging())
            return;
        job(jt, "queued");
        job(jtTOTAL, "queued");
    }

    void
    jobRunning(JobType const& jt) override
    {
        if (!logging())
            return;
        job(jt, "running");
        job(jtTOTAL, "running");
    }

    void
    jobFinished(JobType const& jt) override
    {
        if (!logging())
            return;
        job(jt, "finished");
        job(jtTOTAL, "finished");
    }

    void
    ter(TER const& ter) override
    {
        if (!logging())
            return;
        {
            auto const &v = counters_.ter.find(ter);
            assert(v != counters_.ter.end());
            if (v != counters_.ter.end())
                ++v->second.second;
        }
        {
            auto const &v = counters_.ter.find(tesTOTAL);
            if (v != counters_.ter.end())
                ++v->second.second;
        }
    }

    std::uint64_t
    txq() override
    {
        return setup_.txq;
    }

private:
    struct Fields
    {
        std::vector<std::string> rpc;
        std::unordered_map<std::underlying_type_t<TER>, std::string> ter;
        std::unordered_map<std::underlying_type_t<JobType>, std::string> job;
    };

    struct Counters
    {
        std::unordered_map<std::string,
            std::unique_ptr<Json::StaticString>> fields;

        std::unordered_map<std::underlying_type_t <TER>,
            std::pair<Json::StaticString*, ContainerAtomic<std::uint64_t>>> ter;

        std::unordered_map<std::string,
            std::pair<Json::StaticString*,
                std::unordered_map<Json::StaticString*,
                    ContainerAtomic<std::uint64_t>>>> rpc;

        std::unordered_map<std::underlying_type_t<JobType>,
            std::pair<Json::StaticString*,
                std::unordered_map<Json::StaticString*,
                    ContainerAtomic<std::uint64_t>>>> job;
    };

    Setup const setup_;
    std::atomic<bool> logging_ {false};
    Application& app_;
    bool rotate_ = false;
    bool stop_ = false;
    std::ofstream perfLog_;
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable cond_;
    std::chrono::time_point<std::chrono::system_clock> lastLog_;
    std::string const cHostname_ = beast::getComputerName();
#if BEAST_LINUX || BEAST_MAC || BEAST_BSD
    unsigned int cPid_ = getpid();
#else
    unsigned int cPid_ = 0;
#endif
    std::multimap<std::chrono::time_point<std::chrono::system_clock>,
        std::unique_ptr<Events>> perfEvents_;
    std::mutex eventsMutex_;
    Counters counters_;
    std::atomic<int> workers_ {0};

    // Json field names
    Json::StaticString* ssQueued_;
    Json::StaticString* ssRunning_;
    Json::StaticString* ssFinished_;
    Json::StaticString* ssErrored_;
    Json::StaticString* ssEntries_;
    Json::StaticString* ssCounters_;
    Json::StaticString* ssJobQueue_;
    Json::StaticString* ssTer_;
    Json::StaticString* ssNodeStore_;
    Json::StaticString* ssFetchHits_;
    Json::StaticString* ssFetches_;
    Json::StaticString* ssFetchesSize_;
    Json::StaticString* ssRpc_;
    Json::StaticString* ssTime_;
    Json::StaticString* ssHostname_;
    Json::StaticString* ssPid_;
    Json::StaticString* ssWorkers_;
    Json::StaticString* ssTrace_;
    Json::StaticString* ssType_;
    Json::StaticString* ssName_;
    Json::StaticString* ssTid_;
    Json::StaticString* ssCounter_;
    Json::StaticString* ssEvents_;

    void openLog()
    {
        if (logging())
            perfLog_.open(setup_.perf_log, std::ios::out | std::ios::app);
    }

    void reOpenLog();
    void run();

    void reportHeader(
        std::chrono::time_point<std::chrono::system_clock> const &now,
        Json::Value &report);
    std::string timeString(
        std::chrono::time_point<std::chrono::system_clock> const &tp);
    void report();
    void addEvent(std::unique_ptr<Events> event) override;

    void
    rpc(std::string const& method, std::string const& type)
    {
        auto const& v = counters_.rpc.find(method);
        assert(v != counters_.rpc.end());
        if (v != counters_.rpc.end())
        {
            ++counters_.rpc.at(v->first).second.at(counters_.fields.at(
                type).get());
        }
    }

    void
    job(JobType const& jt, std::string const& type)
    {
        auto const& v = counters_.job.find(jt);
        assert(v != counters_.job.end());
        if (v != counters_.job.end())
        {
            ++counters_.job.at(v->first).second.at(counters_.fields.at(
                type).get());
        }
    }

    void
    setNumberOfThreads(int const workers) override
    {
        if (!logging())
            return;
        workers_ = workers;
    }

    //
    // Stoppable
    //
    void onPrepare() override {}
    void onStart() override;
    // Called when the application begins shutdown.
    void onStop() override;
    // Called when all child Stoppable objects have stopped.
    void onChildrenStopped() override {}
};

class PerfLogTest
    : public PerfLog
{
public:
    PerfLogTest()
    {
        gPerfLog = this;
        gTraceMap = new TraceMap();
        gSharedTraceMap = new SharedTraceMap();
    }

    void rotate() override {}
    bool logging() const override { return false; }
    void addEvent(std::unique_ptr<Events> event) override {}
    void rpcRunning(std::string const &method) override {}
    void rpcFinished(std::string const &method) override {}
    void rpcErrored(std::string const &method) override {}
    void jobQueued(JobType const &jt) override {}
    void jobRunning(JobType const &jt) override {}
    void jobFinished(JobType const &jt) override {}
    void ter(TER const &ter) override {}
    void setNumberOfThreads(int const workers) override {}
    std::uint64_t txq() override { return 0; }
};


} // perf
} // ripple
#endif // RIPPLE_BASICS_PERFLOGIMPL_H_INCLUDED
