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

#include <ripple/basics/impl/PerfLogImpl.h>
#include <ripple/basics/date.h>
#include <ripple/basics/make_lock.h>
#include <ripple/json/to_string.h>
#include <ripple/protocol/JsonFields.h>
#include <ripple/core/JobTypes.h>
#include <ripple/beast/core/CurrentThreadName.h>
#include <ripple/rpc/impl/Handler.h>
#include <ripple/basics/BasicConfig.h>
#include <ripple/basics/Trace.h>
#include <memory>
#include <thread>
#include <mutex>
#include <utility>
#include <iostream>
#include <tuple>
#include <functional>

namespace ripple {
namespace perf {

PerfLog* gPerfLog {nullptr};
TraceMap* gTraceMap;
SharedTraceMap* gSharedTraceMap;

PerfLogImpl::PerfLogImpl (Setup const& setup,
    Stoppable& parent,
    Application& app)
        : Stoppable ("PerfLogImpl", parent)
        , setup_ (setup)
        , logging_ (setup.perf_log.size())
        , app_ (app)
{
    gPerfLog = this;
    gTraceMap = new TraceMap();
    gSharedTraceMap = new SharedTraceMap();
    openLog();

    Fields fields;
    for (auto const& i : ripple::RPC::getHandlerNames())
        fields.rpc.push_back(i);
    fields.rpc.push_back("total");
    fields.job = JobTypes().typeNames();
    fields.ter = terNames();

    for (auto const& i : fields.rpc)
        counters_.fields.emplace(i, std::unique_ptr<Json::StaticString>());
    for (auto const& i : fields.ter)
    {
        counters_.fields.emplace(i.second,
            std::unique_ptr<Json::StaticString>());
    }
    for (auto const& i : fields.job)
    {
        counters_.fields.emplace(i.second,
            std::unique_ptr<Json::StaticString>());
    }
    std::vector<std::string> subFields {
        "queued",
        "running",
        "finished",
        "errored",
        "entries",
        "counters",
        "job_queue",
        "ter",
        "node_store",
        "fetch_hits",
        "fetches",
        "fetches_size",
        "stores",
        "stores_size",
        "rpc",
        "time",
        "hostname",
        "pid",
        "workers",
        "trace",
        "type",
        "name",
        "tid",
        "counter",
        "events",
        "rpc"
    };
    for (auto const& i : subFields)
        counters_.fields.emplace(i, std::unique_ptr<Json::StaticString>());
    for (auto const& i : counters_.fields)
    {
        counters_.fields[i.first] =
            std::make_unique<Json::StaticString>(i.first.c_str());
    }

    ssQueued_      = counters_.fields.at("queued").get();
    ssRunning_     = counters_.fields.at("running").get();
    ssFinished_    = counters_.fields.at("finished").get();
    ssErrored_     = counters_.fields.at("errored").get();
    ssEntries_     = counters_.fields.at("entries").get();
    ssCounters_    = counters_.fields.at("counters").get();
    ssJobQueue_    = counters_.fields.at("job_queue").get();
    ssTer_         = counters_.fields.at("ter").get();
    ssNodeStore_   = counters_.fields.at("node_store").get();
    ssFetchHits_   = counters_.fields.at("fetch_hits").get();
    ssFetches_     = counters_.fields.at("fetches").get();
    ssFetchesSize_ = counters_.fields.at("fetches_size").get();
    ssRpc_         = counters_.fields.at("rpc").get();
    ssTime_        = counters_.fields.at("time").get();
    ssHostname_    = counters_.fields.at("hostname").get();
    ssPid_         = counters_.fields.at("pid").get();
    ssWorkers_     = counters_.fields.at("workers").get();
    ssTrace_       = counters_.fields.at("trace").get();
    ssType_        = counters_.fields.at("type").get();
    ssName_        = counters_.fields.at("name").get();
    ssTid_         = counters_.fields.at("tid").get();
    ssCounter_     = counters_.fields.at("counter").get();
    ssEvents_      = counters_.fields.at("events").get();

    for (auto const& i : fields.ter)
    {
        counters_.ter.emplace(i.first,
            std::make_pair(counters_.fields.at(i.second).get(), 0));
    }
    for (auto const& i : fields.job)
    {
        counters_.job.emplace(i.first,
            std::make_pair(counters_.fields.at(i.second).get(),
                std::unordered_map<Json::StaticString*,
                    ContainerAtomic<std::uint64_t>>{
                    { counters_.fields["queued"].get(), 0 },
                    { counters_.fields["running"].get(), 0 },
                    { counters_.fields["finished"].get(), 0 },
                }
            )
        );
    }
    for (auto const& i : fields.rpc)
    {
        counters_.rpc.emplace(i,
            std::make_pair(counters_.fields.at(i).get(),
                std::unordered_map<Json::StaticString*,
                    ContainerAtomic<std::uint64_t>>{
                    { counters_.fields["errored"].get(), 0 },
                    { counters_.fields["running"].get(), 0 },
                    { counters_.fields["finished"].get(), 0 },
                }
            )
        );
    }
}

PerfLogImpl::~PerfLogImpl()
{
    report();
    if (thread_.joinable())
        thread_.join();
}

void
PerfLogImpl::rotate()
{
    if (!logging())
        return;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        rotate_ = true;
    }
    cond_.notify_one();
}

void
PerfLogImpl::reOpenLog()
{
    if (setup_.perf_log.empty())
        return;
    if (perfLog_.is_open())
        perfLog_.close();
    openLog();
}

void
PerfLogImpl::run()
{
    beast::setCurrentThreadName("perflog");
    lastLog_ = std::chrono::system_clock::now();

    while (true)
    {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            if (stop_)
            {
                stopped();
                return;
            }
            if (rotate_)
                reOpenLog();
            cond_.wait_until (lock,
                lastLog_ + std::chrono::seconds(setup_.log_interval));
        }
        report();
    }
}

void
PerfLogImpl::reportHeader (
    std::chrono::time_point<std::chrono::system_clock> const& now,
    Json::Value& report)
{
    report[*ssTime_] = timeString(now);
    report[*ssHostname_] = cHostname_;
    report[*ssPid_] = cPid_;
    report[*ssEntries_] = Json::Value (Json::arrayValue);
}

std::string
PerfLogImpl::timeString (
    std::chrono::time_point<std::chrono::system_clock> const& tp)
{
    auto tpu = ::date::floor<std::chrono::microseconds> (tp);
    auto dp = ::date::floor<::date::days> (tpu);
    auto ymd = ::date::year_month_day (dp);
    auto time = ::date::make_time (tpu - dp);
    std::stringstream ss;
    ss << ymd << 'T' << time << 'Z';
    return ss.str();
}

void
PerfLogImpl::report()
{
    if (setup_.perf_log.empty())
        return;

    Json::Value report;
    auto now = std::chrono::system_clock::now();

    if (now >= lastLog_ + std::chrono::seconds(setup_.log_interval))
    {
        lastLog_ = now;

        reportHeader (now, report);
        if (workers_)
            report[*ssWorkers_] = workers_.load();
        auto& entry = report[*counters_.fields.at("entries").get()].append(
            Json::objectValue);
        entry[*ssType_] = "counters";
        Json::Value jq(Json::objectValue);
        for (auto const& i : counters_.job)
            jq[*i.second.first] = Json::Value(i.second.second);
        entry[*ssJobQueue_] = jq;
        Json::Value ter(Json::objectValue);
        for (auto const& i : counters_.ter)
            ter[*i.second.first] = Json::Value(i.second.second);
        entry[*ssTer_] = ter;
        Json::Value rpc(Json::objectValue);
        for (auto const& i : counters_.rpc)
            rpc[*i.second.first] = Json::Value(i.second.second);
        entry[*ssRpc_] = rpc;
    }

    std::multimap<std::chrono::time_point<std::chrono::system_clock>,
        std::unique_ptr<Events>> perfEvents;
    {
        std::lock_guard<std::mutex> lock (eventsMutex_);
        if (perfEvents_.size())
            perfEvents = std::move(perfEvents_);
    }

    if (perfEvents.size())
    {
        if (report.type() == Json::nullValue)
            reportHeader (now, report);

        for (auto& trace : perfEvents)
        {
            assert(trace.second);
            if (!trace.second)
                continue;
            auto& entry = report[*ssEntries_].append (Json::objectValue);
            entry[*ssType_] = "trace";
            entry[*ssEvents_] = Json::arrayValue;
            entry[*ssTime_] = timeString(trace.first);

            auto& events = entry[*ssEvents_];
            for (auto& e : *trace.second)
            {
                Json::Value je = Json::objectValue;
                je[*ssTime_] = timeString (e.first);
                je[*ssName_] = std::get<0> (e.second);
                switch (std::get<1> (e.second))
                {
                    case EventType::generic:
                        je[*ssType_] = "generic";
                        break;
                    case EventType::start:
                        je[*ssType_] = "start";
                        break;
                    case EventType::end:
                        je[*ssType_] = "end";
                }
#if BEAST_LINUX
                je[*ssTid_] = std::get<2> (e.second);
#else
                std::stringstream ss;
                ss << std::get<2> (e.second);
                je[*ssTid_] = ss.str();
#endif
                je[*ssCounter_] = std::get<3>(e.second);
                events.append(je);
            }
        }
    }

    if (report.type() != Json::nullValue)
        perfLog_ << Json::to_string (report) << std::endl;
}

void
PerfLogImpl::addEvent(std::unique_ptr<Events> event)
{
    if (!logging())
        return;
    if (event)
    {
        std::lock_guard<std::mutex> lock (eventsMutex_);
        perfEvents_.emplace(event->begin()->first, std::move(event));
    }
}

void
PerfLogImpl::onStart()
{
    if (logging())
        thread_ = std::thread (&PerfLogImpl::run, this);
}

void
PerfLogImpl::onStop()
{
    if (thread_.joinable())
    {
        {
            std::lock_guard<std::mutex> lock (mutex_);
            stop_ = true;
        }
        cond_.notify_one();
    }
    else
    {
        stopped();
    }
}

PerfLog::Setup
setup_PerfLog(Section const& section)
{
    PerfLog::Setup setup;
    set (setup.perf_log, "perf_log", section);
    set (setup.log_interval, "log_interval", section);
    set (setup.txq, "txq", section);
    return setup;
}

void
make_PerfLogTest()
{
    new PerfLogTest();
}

void
TraceMap::insert(std::string const& name)
{
    assert(map_.emplace(name, makeTrace()).second);
}

std::unique_ptr<PerfLog>
make_PerfLog(PerfLog::Setup const& setup,
    Stoppable& parent,
    Application& app)
{
    return std::make_unique<PerfLogImpl>(setup, parent, app);
}

TraceMap::TraceMap() {}

std::unique_ptr<Trace> const&
TraceMap::get(std::string const &name)
{
    auto it = map_.find(name);
    if (it != map_.end())
    {
        return it->second;
    }
    else
    {
        assert(false);
        return dummy_;
    }
}

SharedTraceMap::SharedTraceMap()
{
    insert("consensus");
}

void
SharedTraceMap::insert(std::string const& name)
{
    assert(map_.emplace(name, sharedTrace()).second);
}

std::shared_ptr<Trace> const&
SharedTraceMap::get(std::string const &name)
{
    auto it = map_.find(name);
    if (it != map_.end())
    {
        return it->second;
    }
    else
    {
        assert(false);
        return dummy_;
    }
}

} // perf
} // ripple
