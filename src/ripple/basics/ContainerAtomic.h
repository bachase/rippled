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

#ifndef RIPPLED_CONTAINERATOMIC_H
#define RIPPLED_CONTAINERATOMIC_H

#include <atomic>
#include <cassert>
#include <stdexcept>
#include <sstream>
#include <string>

namespace ripple {
namespace perf {

template <class T>
class ContainerAtomic
{
    std::atomic<T> val_ {0};

    std::string
    what(std::string const& msg, T const val)
    {
        std::stringstream ss;
        ss << msg << ": " << val;
        return ss.str();
    }


public:
    ContainerAtomic() noexcept {}
    ContainerAtomic(T const val) noexcept { val_ = val; }
    ~ContainerAtomic() {}

    ContainerAtomic(ContainerAtomic const& orig)
    {
        if (orig.load())
        {
            assert(false);
            throw std::out_of_range(what("Can't copy non-zero ContainerAtomic",
                orig.load()));
        }
    }

    ContainerAtomic(ContainerAtomic&& orig)
    {
        if (orig.load())
        {
            assert(false);
            throw std::out_of_range(what("Can't move non-zero ContainerAtomic",
                orig.load()));
        }
    }

    ContainerAtomic&
    operator=(ContainerAtomic const& orig)
    {
        if (orig.load())
            throw std::out_of_range(what(
                "Can't copy assign non-zero ContainerAtomic", orig.load()));
        return *this;
    }

    ContainerAtomic&
    operator=(ContainerAtomic&& orig)
    {
        if (orig.load())
            throw std::out_of_range(what(
                "Can't move assign non-zero ContainerAtomic", orig.load()));
        return *this;
    }

    bool
    compare_exchange_strong(T& expected,
        T val,
        std::memory_order sync=std::memory_order_seq_cst) noexcept
    {
        return val_.compare_exchange_strong(expected, val, sync);
    }

    bool
    compare_exchange_strong(T& expected,
        T val,
        std::memory_order success,
        std::memory_order failure) noexcept
    {
        return val_.compare_exchange_strong(expected, val, success, failure);
    }

    bool
    compare_exchange_weak(T& expected,
        T val,
        std::memory_order sync=std::memory_order_seq_cst) noexcept
    {
        return val_.compare_exchange_weak(expected, val, sync);
    }

    bool
    compare_exchange_weak(T& expected,
        T val,
        std::memory_order success,
        std::memory_order failure) noexcept
    {
        return val_.compare_exchange_weak(expected, val, success, failure);
    }

    T
    exchange(T val,
        std::memory_order sync=std::memory_order_seq_cst) noexcept
    {
        return val_.exchange(val, sync);
    }

    bool
    is_lock_free() noexcept
    {
        return val_.is_lock_free();
    }

    T
    load(std::memory_order sync=std::memory_order_seq_cst) const noexcept
    {
        return val_.load(sync);
    }

    operator
    T() const noexcept
    {
        return val_;
    }

    T
    operator=(T val) noexcept
    {
        return val_ = val;
    }

    void
    store(T val, std::memory_order sync=std::memory_order_seq_cst) noexcept
    {
        val_.store(val, sync);
    }

    T
    fetch_add(T val, std::memory_order sync=std::memory_order_seq_cst) noexcept
    {
        return val_.fetch_add(val, sync);
    }

    T fetch_and(T val, std::memory_order sync=std::memory_order_seq_cst)
    noexcept
    {
        return val_.fetch_and(val, sync);
    }

    T fetch_or(T val, std::memory_order sync=std::memory_order_seq_cst) noexcept
    {
        return val_.fetch_or(val, sync);
    }

    T fetch_sub(T val, std::memory_order sync=std::memory_order_seq_cst)
    noexcept
    {
        return val_.fetch_sub(val, sync);
    }

    T fetch_xor(T val, std::memory_order sync=std::memory_order_seq_cst)
    noexcept
    {
        return fetch_xor(val, sync);
    }

    T
    operator--() noexcept
    {
        return --val_;
    }

    T
    operator--(int) noexcept
    {
        return val_--;
    }

    T
    operator+=(T val) noexcept
    {
        return val_ += val;
    }

    T
    operator-=(T val) noexcept
    {
        return val_ -= val;
    }

    T
    operator&=(T val) noexcept
    {
        return val_ &= val;
    }

    T
    operator|=(T val) noexcept
    {
        return val_ |= val;
    }

    T
    operator^=(T val) noexcept
    {
        return val_ ^= val;
    }

    T
    operator++() noexcept
    {
        return ++val_;
    }

    T
    operator++(int) noexcept
    {
        return val_++;
    }

};

void foofunc();

} // perf
} // ripple
#endif // RIPPLED_CONTAINERATOMIC_H
