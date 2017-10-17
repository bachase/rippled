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

#include <ripple/basics/Trace.h>
#include <exception>
#include <iostream>
#if BEAST_LINUX
#include <sys/types.h>
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <unistd.h>
#include <sys/syscall.h>
#endif

namespace ripple {
namespace perf {

Trace::~Trace()
{
    try
    {
        std::lock_guard<std::mutex> lock(mutex_);
        submit();
    }
    catch (std::exception &e)
    {
        std::cerr << "Trace ~ exception:\n" << e.what() << "\n";
        assert(false);
    }
}

Trace::Trace(Trace &&other)
{
    std::lock_guard<std::mutex> lock(other.mutex_);
    type_ = other.type_;
    other.type_ = TraceType::none;
    events_ = std::move(other.events_);
    timers_ = std::move(other.timers_);
}

Trace &
Trace::operator=(Trace &&other)
{
    if (this == &other)
        return *this;
    std::lock_guard<std::mutex> lock(other.mutex_);
    type_ = other.type_;
    other.type_ = TraceType::none;
    events_ = std::move(other.events_);
    timers_ = std::move(other.timers_);
    return *this;
}

void
Trace::add(std::string const &name,
    std::uint64_t const counter,
    EventType const type)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (type_ == TraceType::none)
    {
        lockedOpen(name, counter, TraceType::trace);
    } else
    {
        type_ = TraceType::trace;
        lockedAdd(name, type, counter);
    }
}

void
Trace::start(std::string const &timer,
    std::uint64_t const counter,
    std::chrono::time_point<std::chrono::system_clock> const &tp)
{
    std::lock_guard<std::mutex> lock(mutex_);
    switch (type_)
    {
        case TraceType::none:
            lockedOpen(timer, counter, TraceType::trace, tp);
        case TraceType::trace:
            lockedStart(timer, counter, tp);
            break;
        default:
            assert(false);
    }
}

void
Trace::end(std::string const &timer)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (type_ == TraceType::trace)
        lockedEnd(timer);
}

void
Trace::close()
{
    std::lock_guard<std::mutex> lock(mutex_);
    lockedClose();
}

void
Trace::open(std::string const &name,
    std::uint64_t const counter,
    TraceType const type)
{
    std::lock_guard<std::mutex> lock(mutex_);
    lockedOpen(name, counter, type);
}

//-----------------------------------------------------------------------------

void
Trace::lockedAdd(std::string const &name,
    EventType const type,
    std::uint64_t const counter,
    std::chrono::time_point<std::chrono::system_clock> const &tp)
{
    assert(type_ != TraceType::none);
    if (events_)
    {
        events_->emplace(tp,
            std::make_tuple(name,
                type,
#if BEAST_LINUX
                syscall(SYS_gettid),
#else
                std::this_thread::get_id(),
#endif
                counter));
    }
}

void
Trace::lockedStart(std::string const& timer,
    std::uint64_t const counter,
    std::chrono::time_point<std::chrono::system_clock> const& tp)
{
    assert(type_ == TraceType::trace);
    timers_[timer] = tp;
    lockedAdd(timer, EventType::start, counter, tp);
}

void
Trace::lockedEnd(std::string const& timer)
{
    assert(type_ == TraceType::trace);

    auto start = timers_.find (timer);
    if (start != timers_.end())
    {
        std::chrono::time_point<std::chrono::system_clock> endTime =
            std::chrono::system_clock::now();
        std::uint64_t duration =
            std::chrono::duration_cast<std::chrono::microseconds> (
                endTime - start->second).count();
        timers_.erase (start);
        lockedAdd(timer, EventType::end, duration, endTime);
    }
}

bool
Trace::submit()
{
    if (type_ == TraceType::none)
        return false;

    switch (type_)
    {
        case TraceType::trace:
        {
            assert(events_ && !events_->empty());
            auto now = std::chrono::system_clock::now();
            lockedAdd("END", EventType::generic,
                std::chrono::duration_cast<std::chrono::microseconds>(
                    now - events_->begin()->first).count(), now);
        }
            break;
        case TraceType::trap:
            assert(events_ && !events_->empty());
            break;
        default:
            assert(false);
    }

    gPerfLog->addEvent(std::move(events_));

    return true;
}

void
Trace::lockedClose()
{
    if (!submit())
        return;
    timers_.clear();
    type_ = TraceType::none;
}

void
Trace::lockedOpen(std::string const& name,
    std::uint64_t const counter,
    TraceType const type,
    std::chrono::time_point<std::chrono::system_clock> const& tp)
{
    if (type == TraceType::none)
        return;
    lockedClose();
    type_ = type;
    events_ = std::make_unique<Events>();
    lockedAdd(name, EventType::generic, counter, tp);
    if (type == TraceType::trap)
        lockedClose();
}

} // perf
} // ripple
