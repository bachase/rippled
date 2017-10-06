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
#ifndef RIPPLE_TEST_CSF_DATA_FRAME_H_INCLUDED
#define RIPPLE_TEST_CSF_DATA_FRAME_H_INCLUDED

#include <tuple>
#include <vector>

template <class... Ts>
struct DataFrame
{
    std::vector<std::string> names;

    using Entry = std::tuple<Ts...>;
    std::vector<Entry> data;

    template <bool...> struct bool_pack;
    template <bool... v>
    using all_true = std::is_same<bool_pack<true, v...>, bool_pack<v..., true>>;

    template <
        class... Str,
        class = std::enable_if_t<
            sizeof...(Ts) == sizeof...(Str) &&
        all_true<std::is_convertible<Str, std::string>{}...>::value>>
    DataFrame(Str&&... s) : names{s...}
    {
    }

    void
    push_back(Ts... ts)
    {
        data.emplace_back(std::make_tuple(ts...));
    }
};


#endif
