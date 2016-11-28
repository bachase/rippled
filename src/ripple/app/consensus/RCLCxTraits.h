//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2016 Ripple Labs Inc.

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

#ifndef RIPPLE_APP_CONSENSUS_RCLCXTRAITS_H_INCLUDED
#define RIPPLE_APP_CONSENSUS_RCLCXTRAITS_H_INCLUDED

#include <ripple/basics/chrono.h>
#include <ripple/basics/base_uint.h>

#include <ripple/protocol/UintTypes.h>
#include <ripple/protocol/RippleLedgerHash.h>

#include <ripple/app/consensus/RCLCxLedger.h>
#include <ripple/app/ledger/LedgerProposal.h>
#include <ripple/app/consensus/RCLCxTx.h>
#include <ripple/app/consensus/RCLCxCalls.h>


namespace ripple {

// Consensus traits class
// For adapting consensus to RCL
struct RCLCxTraits
{
    using Callback_t = RCLCxCalls;
    using NetTime_t     = NetClock::time_point;
    using Ledger_t     = RCLCxLedger;
    using Pos_t        = LedgerProposal;
    using TxSet_t      = RCLTxSet;
    using MissingTx_t = SHAMapMissingNode;
};
}

#endif
