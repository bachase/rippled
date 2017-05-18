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
#include <BeastConfig.h>
#include <test/csf/Ledger.h>

#include <sstream>

namespace ripple {
namespace test {
namespace csf {

Ledger::Instance const Ledger::genesis;
hash_map<Ledger::Instance, Ledger::ID> Ledger::instances;
Ledger::ID Ledger::nextUniqueID{1};

Json::Value
Ledger::getJson() const
{
    Json::Value res(Json::objectValue);
    res["seq"] = static_cast<Seq::value_type>(seq());
    return res;
}

Ledger
Ledger::close(TxSetType const& txs,
    NetClock::duration closeTimeResolution,
    NetClock::time_point const& consensusCloseTime,
    bool closeTimeAgree) const
{
    Instance next(*instance_);
    next.txs.insert(txs.begin(), txs.end());
    next.seq = seq() + 1;
    next.closeTimeResolution = closeTimeResolution;
    next.closeTime = effCloseTime(
        consensusCloseTime, closeTimeResolution, instance_->parentCloseTime);
    next.closeTimeAgree = closeTimeAgree;
    next.parentCloseTime = closeTime();
    next.parentID = id();
    auto it = instances.find(next);
    if (it == instances.end())
        it = instances.emplace(next, nextUniqueID++).first;
    return Ledger(it->second, &(it->first));
}

}  // namespace csf
}  // namespace test
}  // namespace ripple
