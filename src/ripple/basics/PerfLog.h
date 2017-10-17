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

#ifndef RIPPLE_BASICS_PERFLOG_H_INCLUDED
#define RIPPLE_BASICS_PERFLOG_H_INCLUDED

#include <ripple/protocol/TER.h>
#include <ripple/core/Job.h>
#include <ripple/beast/core/PlatformConfig.h>
#include <string>
#include <memory>
#include <mutex>
#include <map>
#include <tuple>
#include <thread>

namespace ripple {
namespace perf {

enum class EventType
{
    generic, start, end
};

using Event = std::tuple<std::string,
    EventType,
#if BEAST_LINUX
    int,
#else
    std::thread::id,
#endif
    std::uint64_t>;

using Events = std::multimap<
    std::chrono::time_point<std::chrono::system_clock>,
    Event>;

/**
 * This class logs performance data, and also maintains configuration options
 * for benchmarking.
 */
class PerfLog
{
public:
    struct Setup
    {
        std::string perf_log;
        unsigned int log_interval {1};
        std::uint64_t txq {100};
    };

    virtual ~PerfLog()
    {}

    /** Re-open logfile for RPC "logrotate". */
    virtual void rotate() = 0;
    virtual bool logging() const = 0;
    virtual void addEvent(std::unique_ptr<Events> event) = 0;
    virtual void rpcRunning(std::string const &method) = 0;
    virtual void rpcFinished(std::string const &method) = 0;
    virtual void rpcErrored(std::string const &method) = 0;
    virtual void jobQueued(JobType const &jt) = 0;
    virtual void jobRunning(JobType const &jt) = 0;
    virtual void jobFinished(JobType const &jt) = 0;
    virtual void ter(TER const &ter) = 0;
    virtual void setNumberOfThreads(int const workers) = 0;
    virtual std::uint64_t txq() = 0;
};

//------------------------------------------------------------------------------

// All Trace objects need to find this.
extern PerfLog* gPerfLog;

} //perf

class Section;
class Stoppable;
class Application;

namespace perf {
/** Build PerfLog::Setup from a config section. */
PerfLog::Setup setup_PerfLog(Section const& section);

void make_PerfLogTest();
std::unique_ptr<PerfLog> make_PerfLog(
    perf::PerfLog::Setup const& setup,
    Stoppable& parent,
    Application& app);

class Trace;
class TraceMap
{
    std::unordered_map<std::string, std::unique_ptr<Trace>> map_;
    std::unique_ptr<Trace> dummy_ {nullptr};
    void insert(std::string const& name);
public:
    TraceMap();
    std::unique_ptr<Trace> const& get(std::string const& name);
};

class SharedTraceMap
{
    std::unordered_map<std::string, std::shared_ptr<Trace>> map_;
    std::shared_ptr<Trace> dummy_ {nullptr};
    void insert(std::string const& name);
public:
    SharedTraceMap();
    std::shared_ptr<Trace> const& get(std::string const& name);
};

extern TraceMap* gTraceMap;
extern SharedTraceMap* gSharedTraceMap;

} // perf
} // ripple

#endif // RIPPLE_BASICS_PERFLOG_H_INCLUDED
