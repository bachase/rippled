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

#ifndef RIPPLE_BASICS_TRACE_H
#define RIPPLE_BASICS_TRACE_H

#include <ripple/basics/PerfLog.h>
#include <boost/optional.hpp>
#include <boost/none.hpp>
#include <memory>

namespace ripple {
namespace perf {

enum class TraceType { none = 0, trace, trap };

class Trace
{
public:
    Trace() = default;

    Trace(std::string const &name,
        std::uint64_t const counter = 0,
        TraceType const type = TraceType::trace)
    {
        lockedOpen(name, counter, type);
    }

    ~Trace();
    Trace(Trace &&other);
    Trace &operator=(Trace &&other);

    explicit operator bool() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return type_ != TraceType::none;
    }

    void add(std::string const &name,
        std::uint64_t const counter = 0,
        EventType const type = EventType::generic);
    void start(std::string const &timer,
        std::uint64_t const counter = 0,
        std::chrono::time_point<std::chrono::system_clock> const &tp =
            std::chrono::system_clock::now());
    void end(std::string const &timer);
    void close();
    void open(std::string const &name,
        std::uint64_t const counter = 0,
        TraceType const type = TraceType::trace);

private:
    TraceType type_{TraceType::none};
    std::unique_ptr<Events> events_;
    std::unordered_map<std::string,
        std::chrono::time_point<std::chrono::system_clock>> timers_;
    mutable std::mutex mutex_;

    // These functions are called with mutex_ locked already as necessary.
    void lockedAdd(std::string const &name,
        EventType const type,
        std::uint64_t const counter,
        std::chrono::time_point<std::chrono::system_clock> const &tp =
        std::chrono::system_clock::now());
    void lockedStart(std::string const &timer,
        std::uint64_t const counter = 0,
        std::chrono::time_point<std::chrono::system_clock> const &tp =
        std::chrono::system_clock::now());
    void lockedEnd(std::string const &timer);
    bool submit();
    void lockedClose();
    void lockedOpen(std::string const &name,
        std::uint64_t const counter = 0,
        TraceType const type = TraceType::trace,
        std::chrono::time_point<std::chrono::system_clock> const &tp =
        std::chrono::system_clock::now());
};

inline std::unique_ptr<Trace>
makeTrace()
{
    if (gPerfLog->logging())
        return std::make_unique<Trace>();
    else
        return nullptr;
}

inline std::unique_ptr<Trace>
makeTrace(std::string const &name,
    std::uint64_t const counter=0)
{
    if (gPerfLog->logging())
        return std::make_unique<Trace>(name, counter, TraceType::trace);
    else
        return nullptr;
}

inline std::shared_ptr<Trace>
sharedTrace()
{
    if (gPerfLog->logging())
        return std::make_shared<Trace>();
    else
        return nullptr;
}

inline std::shared_ptr<Trace>
sharedTrace(std::string const &name,
    std::uint64_t const counter=0)
{
    if (gPerfLog->logging())
        return std::make_shared<Trace>(name, counter, TraceType::trace);
    else
        return nullptr;
}

inline void
trap(std::string const &name, std::uint64_t const counter = 0)
{
    Trace(name, counter, TraceType::trap);
}

template <class T>
void
open(T const& trace,
    std::string const& name,
    std::uint64_t const counter=0)
{
    if (trace)
        trace->open(name, counter, TraceType::trace);
}

template <class T>
void
close(T const& trace)
{
    if (trace)
        trace->close();
}

template <class T>
void
add(T const& trace,
    std::string const& name,
    std::uint64_t const counter=0)
{
    if (trace)
        trace->add(name, counter, EventType::generic);
}

template <class T>
void
start(T const& trace,
    std::string const& timer,
    std::uint64_t const counter=0,
    std::chrono::time_point<std::chrono::system_clock> const& tp =
    std::chrono::system_clock::now())
{
    if (trace)
        trace->start(timer, counter, tp);
}

template <class T>
void
end(T const& trace, std::string const& timer)
{
    if (trace)
        trace->end(timer);
}

} // perf
} // ripple
#endif // RIPPLE_BASICS_TRACE_H
