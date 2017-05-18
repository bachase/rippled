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
#ifndef RIPPLE_TEST_CSF_VALIDATION_H_INCLUDED
#define RIPPLE_TEST_CSF_VALIDATION_H_INCLUDED

#include <boost/optional.hpp>
#include <memory>
#include <ripple/basics/tagged_integer.h>
#include <test/csf/Ledger.h>
#include <utility>

namespace ripple {
namespace test {
namespace csf {

struct NodeTag;
using NodeID = tagged_integer<std::uint32_t, NodeTag>;

using NodeKey =  std::pair<NodeID, std::uint32_t>;

/** Validation of a specific ledger.

    Represents a ledger validated by a specific node in the network.
*/
class Validation
{
    Ledger::ID ledgerID_{0};
    Ledger::Seq seq_{0};

    NetClock::time_point signTime_;
    NetClock::time_point seenTime_;
    NodeKey key_;
    NodeID nodeID_{0};
    bool trusted_ = true;
    boost::optional<std::uint32_t> loadFee_;
  
public:
    Validation(Ledger::ID id,
        Ledger::Seq seq,
        NetClock::time_point sign,
        NetClock::time_point seen,
        NodeKey key,
        NodeID nodeID,
        bool trusted,
        boost::optional<std::uint32_t> loadFee = boost::none)
        : ledgerID_{id}
        , seq_{seq}
        , signTime_{sign}
        , seenTime_{seen}
        , key_{key}
        , nodeID_{nodeID}
        , trusted_{trusted}
        , loadFee_{loadFee}
    {
    }

    Ledger::ID
    ledgerID() const
    {
        return ledgerID_;
    }

    Ledger::Seq
    seq() const
    {
        return seq_;
    }

    NetClock::time_point
    signTime() const
    {
        return signTime_;
    }

    NetClock::time_point
    seenTime() const
    {
        return seenTime_;
    }

    NodeKey
    key() const
    {
        return key_;
    }

    NodeID
    nodeID() const
    {
        return nodeID_;
    }

    bool
    trusted() const
    {
        return trusted_;
    }

    boost::optional<std::uint32_t>
    loadFee() const
    {
        return loadFee_;
    }

    Validation const&
    unwrap() const
    {
        return *this;
    }

    auto
    asTie() const
    {
        return std::tie(ledgerID_, seq_, signTime_, seenTime_, key_, nodeID_,
            trusted_, loadFee_);
    }
    bool
    operator==(Validation const& o) const
    {
        return asTie() == o.asTie();
    }

    bool
    operator<(Validation const& o) const
    {
        return asTie() < o.asTie();
    }

    void
    setTrusted()
    {
        trusted_ = true;
    }

    void
    setSeen(NetClock::time_point seen)
    {
        seenTime_ = seen;
    }
};

}  // ripple
}  // test
}  // csf
#endif