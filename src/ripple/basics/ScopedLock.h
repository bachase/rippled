//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Portions of this file are from JUCE.
    Copyright (c) 2013 - Raw Material Software Ltd.
    Please visit http://www.juce.com

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

#ifndef RIPPLE_BASICS_SCOPEDLOCK_H_INCLUDED
#define RIPPLE_BASICS_SCOPEDLOCK_H_INCLUDED

#include <ripple/basics/Trace.h>
#include <string>
#include <cstdint>
#include <memory>

namespace ripple
{

//==============================================================================
/**
    Automatically unlocks and re-locks a mutex object.

    This is the reverse of a std::lock_guard object - instead of locking the mutex
    for the lifetime of this object, it unlocks it.

    Make sure you don't try to unlock mutexes that aren't actually locked!

    e.g. @code

    std::mutex mut;

    for (;;)
    {
        std::lock_guard<std::mutex> myScopedLock{mut};
        // mut is now locked

        ... do some stuff with it locked ..

        while (xyz)
        {
            ... do some stuff with it locked ..

            GenericScopedUnlock<std::mutex> unlocker{mut};

            // mut is now unlocked for the remainder of this block,
            // and re-locked at the end.

            ...do some stuff with it unlocked ...
        }  // mut gets locked here.

    }  // mut gets unlocked here
    @endcode

*/
template <class MutexType>
class GenericScopedUnlock
{
    MutexType& lock_;
    std::shared_ptr<perf::Trace> trace_;
    std::string name_;
    std::uint64_t counter_;
public:
    /** Creates a GenericScopedUnlock.

        As soon as it is created, this will unlock the CriticalSection, and
        when the ScopedLock object is deleted, the CriticalSection will
        be re-locked.

        Make sure this object is created and deleted by the same thread,
        otherwise there are no guarantees what will happen! Best just to use it
        as a local stack object, rather than creating one with the new() operator.
    */
    explicit GenericScopedUnlock (MutexType& lock) noexcept
        : lock_ (lock)
    {
        lock.unlock();
    }

    GenericScopedUnlock (MutexType& lock,
        std::shared_ptr<perf::Trace>& trace,
        std::string const& name, std::uint64_t const counter)
            : lock_ (lock)
            , trace_ (trace)
            , name_ (name)
            , counter_ (counter)
    {
        lock.unlock();
        if (trace_)
            trace_->close();
    }

    GenericScopedUnlock (GenericScopedUnlock const&) = delete;
    GenericScopedUnlock& operator= (GenericScopedUnlock const&) = delete;

    /** Destructor.

        The CriticalSection will be unlocked when the destructor is called.

        Make sure this object is created and deleted by the same thread,
        otherwise there are no guarantees what will happen!
    */
    ~GenericScopedUnlock() noexcept(false)
    {
        if (trace_)
        {
            trace_->open(name_, counter_);
            lock_.lock();
            trace_->add("locked");
        } else
        {
            lock_.lock();
        }
    }
};

} // ripple
#endif
