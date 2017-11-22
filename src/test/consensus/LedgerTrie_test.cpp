//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2017 Ripple Labs Inc.

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
#include <ripple/beast/unit_test.h>
#include <ripple/consensus/LedgerTrie.h>
#include <test/csf/ledgers.h>
#include <unordered_map>

namespace ripple {
namespace test {

class LedgerTrie_test : public beast::unit_test::suite
{
    beast::Journal j;


    void
    testInsert()
    {
        using namespace csf;
        // Single entry
        {
            LedgerTrie<Ledger> t;
            LedgerHistoryHelper h;
            t.insert(h["abc"]);
            BEAST_EXPECT(t.checkInvariants());
            BEAST_EXPECT(t.tipSupport(h["abc"]) == 1);
            BEAST_EXPECT(t.branchSupport(h["abc"]) == 1);

            t.insert(h["abc"]);
            BEAST_EXPECT(t.checkInvariants());
            BEAST_EXPECT(t.tipSupport(h["abc"]) == 2);
            BEAST_EXPECT(t.branchSupport(h["abc"]) == 2);
        }
        // Suffix of existing (extending tree)
        {
            LedgerTrie<Ledger> t;
            LedgerHistoryHelper h;
            t.insert(h["abc"]);
            BEAST_EXPECT(t.checkInvariants());
            // extend with no siblings
            t.insert(h["abcd"]);
            BEAST_EXPECT(t.checkInvariants());

            BEAST_EXPECT(t.tipSupport(h["abc"]) == 1);
            BEAST_EXPECT(t.branchSupport(h["abc"]) == 2);
            BEAST_EXPECT(t.tipSupport(h["abcd"]) == 1);
            BEAST_EXPECT(t.branchSupport(h["abcd"]) == 1);

            // extend with existing sibling
            t.insert(h["abce"]);
            BEAST_EXPECT(t.tipSupport(h["abc"]) == 1);
            BEAST_EXPECT(t.branchSupport(h["abc"]) == 3);
            BEAST_EXPECT(t.tipSupport(h["abcd"]) == 1);
            BEAST_EXPECT(t.branchSupport(h["abcd"]) == 1);
            BEAST_EXPECT(t.tipSupport(h["abce"]) == 1);
            BEAST_EXPECT(t.branchSupport(h["abce"]) == 1);
        }
        // Prefix of existing node
        {
            LedgerTrie<Ledger> t;
            LedgerHistoryHelper h;
            t.insert(h["abcd"]);
            BEAST_EXPECT(t.checkInvariants());
            // prefix with no siblings
            t.insert(h["abcdf"]);
            BEAST_EXPECT(t.checkInvariants());

            BEAST_EXPECT(t.tipSupport(h["abcd"]) == 1);
            BEAST_EXPECT(t.branchSupport(h["abcd"]) == 2);
            BEAST_EXPECT(t.tipSupport(h["abcdf"]) == 1);
            BEAST_EXPECT(t.branchSupport(h["abcdf"]) == 1);

            // prefix with existing child
            t.insert(h["abc"]);
            BEAST_EXPECT(t.checkInvariants());

            BEAST_EXPECT(t.tipSupport(h["abc"]) == 1);
            BEAST_EXPECT(t.branchSupport(h["abc"]) == 3);
            BEAST_EXPECT(t.tipSupport(h["abcd"]) == 1);
            BEAST_EXPECT(t.branchSupport(h["abcd"]) == 2);
            BEAST_EXPECT(t.tipSupport(h["abcdf"]) == 1);
            BEAST_EXPECT(t.branchSupport(h["abcdf"]) == 1);
        }
        // Suffix + prefix
        {
            LedgerTrie<Ledger> t;
            LedgerHistoryHelper h;
            t.insert(h["abcd"]);
            BEAST_EXPECT(t.checkInvariants());
            t.insert(h["abce"]);
            BEAST_EXPECT(t.checkInvariants());

            BEAST_EXPECT(t.tipSupport(h["abc"]) == 0);
            BEAST_EXPECT(t.branchSupport(h["abc"]) == 2);
            BEAST_EXPECT(t.tipSupport(h["abcd"]) == 1);
            BEAST_EXPECT(t.branchSupport(h["abcd"]) == 1);
            BEAST_EXPECT(t.tipSupport(h["abce"]) == 1);
            BEAST_EXPECT(t.branchSupport(h["abce"]) == 1);
        }
        // Suffix + prefix with existing child
        {
            //  abcd : abcde, abcf

            LedgerTrie<Ledger> t;
            LedgerHistoryHelper h;
            t.insert(h["abcd"]);
            BEAST_EXPECT(t.checkInvariants());
            t.insert(h["abcde"]);
            BEAST_EXPECT(t.checkInvariants());
            t.insert(h["abcf"]);
            BEAST_EXPECT(t.checkInvariants());

            BEAST_EXPECT(t.tipSupport(h["abc"]) == 0);
            BEAST_EXPECT(t.branchSupport(h["abc"]) == 3);
            BEAST_EXPECT(t.tipSupport(h["abcd"]) == 1);
            BEAST_EXPECT(t.branchSupport(h["abcd"]) == 2);
            BEAST_EXPECT(t.tipSupport(h["abcf"]) == 1);
            BEAST_EXPECT(t.branchSupport(h["abcf"]) == 1);
            BEAST_EXPECT(t.tipSupport(h["abcde"]) == 1);
            BEAST_EXPECT(t.branchSupport(h["abcde"]) == 1);
        }

        // Multiple
        {
            LedgerTrie<Ledger> t;
            LedgerHistoryHelper h;
            t.insert(h["ab"],4);
            BEAST_EXPECT(t.tipSupport(h["ab"]) == 4);
            BEAST_EXPECT(t.branchSupport(h["ab"]) == 4);
            BEAST_EXPECT(t.tipSupport(h["a"]) == 0);
            BEAST_EXPECT(t.branchSupport(h["a"]) == 4);

            t.insert(h["abc"],2);
            BEAST_EXPECT(t.tipSupport(h["abc"]) == 2);
            BEAST_EXPECT(t.branchSupport(h["abc"]) == 2);
            BEAST_EXPECT(t.tipSupport(h["ab"]) == 4);
            BEAST_EXPECT(t.branchSupport(h["ab"]) == 6);
            BEAST_EXPECT(t.tipSupport(h["a"]) == 0);
            BEAST_EXPECT(t.branchSupport(h["a"]) == 6);

        }
    }

    void
    testRemove()
    {
        using namespace csf;
        // Not in trie
        {
            LedgerTrie<Ledger> t;
            LedgerHistoryHelper h;
            t.insert(h["abc"]);

            BEAST_EXPECT(!t.remove(h["ab"]));
            BEAST_EXPECT(t.checkInvariants());
            BEAST_EXPECT(!t.remove(h["a"]));
            BEAST_EXPECT(t.checkInvariants());
        }
        // In trie but with 0 tip support
        {
            LedgerTrie<Ledger> t;
            LedgerHistoryHelper h;
            t.insert(h["abcd"]);
            t.insert(h["abce"]);

            BEAST_EXPECT(t.tipSupport(h["abc"]) == 0);
            BEAST_EXPECT(t.branchSupport(h["abc"]) == 2);
            BEAST_EXPECT(!t.remove(h["abc"]));
            BEAST_EXPECT(t.checkInvariants());
            BEAST_EXPECT(t.tipSupport(h["abc"]) == 0);
            BEAST_EXPECT(t.branchSupport(h["abc"]) == 2);
        }
        // In trie with > 1 tip support
        {
            LedgerTrie<Ledger> t;
            LedgerHistoryHelper h;
            t.insert(h["abc"],2);

            BEAST_EXPECT(t.tipSupport(h["abc"]) == 2);
            BEAST_EXPECT(t.remove(h["abc"]));
            BEAST_EXPECT(t.checkInvariants());
            BEAST_EXPECT(t.tipSupport(h["abc"]) == 1);

            t.insert(h["abc"], 1);
            BEAST_EXPECT(t.tipSupport(h["abc"]) == 2);
            BEAST_EXPECT(t.remove(h["abc"], 2));
            BEAST_EXPECT(t.checkInvariants());
            BEAST_EXPECT(t.tipSupport(h["abc"]) == 0);

            t.insert(h["abc"], 3);
            BEAST_EXPECT(t.tipSupport(h["abc"]) == 3);
            BEAST_EXPECT(t.remove(h["abc"], 300));
            BEAST_EXPECT(t.checkInvariants());
            BEAST_EXPECT(t.tipSupport(h["abc"]) == 0);

        }
        // In trie with = 1 tip support, no children
        {
            LedgerTrie<Ledger> t;
            LedgerHistoryHelper h;
            t.insert(h["ab"]);
            t.insert(h["abc"]);

            BEAST_EXPECT(t.tipSupport(h["ab"]) == 1);
            BEAST_EXPECT(t.branchSupport(h["ab"]) == 2);
            BEAST_EXPECT(t.tipSupport(h["abc"]) == 1);
            BEAST_EXPECT(t.branchSupport(h["abc"]) == 1);

            BEAST_EXPECT(t.remove(h["abc"]));
            BEAST_EXPECT(t.checkInvariants());
            BEAST_EXPECT(t.tipSupport(h["ab"]) == 1);
            BEAST_EXPECT(t.branchSupport(h["ab"]) == 1);
            BEAST_EXPECT(t.tipSupport(h["abc"]) == 0);
            BEAST_EXPECT(t.branchSupport(h["abc"]) == 0);
        }
        // In trie with = 1 tip support, 1 child
        {
            LedgerTrie<Ledger> t;
            LedgerHistoryHelper h;
            t.insert(h["ab"]);
            t.insert(h["abc"]);
            t.insert(h["abcd"]);

            BEAST_EXPECT(t.tipSupport(h["abc"]) == 1);
            BEAST_EXPECT(t.branchSupport(h["abc"]) == 2);
            BEAST_EXPECT(t.tipSupport(h["abcd"]) == 1);
            BEAST_EXPECT(t.branchSupport(h["abcd"]) == 1);

            BEAST_EXPECT(t.remove(h["abc"]));
            BEAST_EXPECT(t.checkInvariants());
            BEAST_EXPECT(t.tipSupport(h["abc"]) == 0);
            BEAST_EXPECT(t.branchSupport(h["abc"]) == 1);
            BEAST_EXPECT(t.tipSupport(h["abcd"]) == 1);
            BEAST_EXPECT(t.branchSupport(h["abcd"]) == 1);
        }
        // In trie with = 1 tip support, > 1 children
        {
            LedgerTrie<Ledger> t;
            LedgerHistoryHelper h;
            t.insert(h["ab"]);
            t.insert(h["abc"]);
            t.insert(h["abcd"]);
            t.insert(h["abce"]);

            BEAST_EXPECT(t.tipSupport(h["abc"]) == 1);
            BEAST_EXPECT(t.branchSupport(h["abc"]) == 3);

            BEAST_EXPECT(t.remove(h["abc"]));
            BEAST_EXPECT(t.checkInvariants());
            BEAST_EXPECT(t.tipSupport(h["abc"]) == 0);
            BEAST_EXPECT(t.branchSupport(h["abc"]) == 2);
        }
    }

    void
    testTipAndBranchSupport()
    {
        using namespace csf;
        LedgerTrie<Ledger> t;
        LedgerHistoryHelper h;
        BEAST_EXPECT(t.tipSupport(h["a"]) == 0);
        BEAST_EXPECT(t.tipSupport(h["axy"]) == 0);
        BEAST_EXPECT(t.branchSupport(h["a"]) == 0);
        BEAST_EXPECT(t.branchSupport(h["axy"]) == 0);

        t.insert(h["abc"]);
        BEAST_EXPECT(t.tipSupport(h["a"]) == 0);
        BEAST_EXPECT(t.tipSupport(h["ab"]) == 0);
        BEAST_EXPECT(t.tipSupport(h["abc"]) == 1);
        BEAST_EXPECT(t.tipSupport(h["abcd"]) == 0);
        BEAST_EXPECT(t.branchSupport(h["a"]) == 1);
        BEAST_EXPECT(t.branchSupport(h["ab"]) == 1);
        BEAST_EXPECT(t.branchSupport(h["abc"]) == 1);
        BEAST_EXPECT(t.branchSupport(h["abcd"]) == 0);

        t.insert(h["abe"]);
        BEAST_EXPECT(t.tipSupport(h["a"]) == 0);
        BEAST_EXPECT(t.tipSupport(h["ab"]) == 0);
        BEAST_EXPECT(t.tipSupport(h["abc"]) == 1);
        BEAST_EXPECT(t.tipSupport(h["abe"]) == 1);

        BEAST_EXPECT(t.branchSupport(h["a"]) == 2);
        BEAST_EXPECT(t.branchSupport(h["ab"]) == 2);
        BEAST_EXPECT(t.branchSupport(h["abc"]) == 1);
        BEAST_EXPECT(t.branchSupport(h["abe"]) == 1);
    }

    void
    testGetPreferred()
    {
        using namespace csf;
        // Empty
        {
            LedgerTrie<Ledger> t;
            LedgerHistoryHelper h;
            BEAST_EXPECT(t.getPreferred().second == Ledger::ID{0});
        }
        // Single node no children
        {
            LedgerTrie<Ledger> t;
            LedgerHistoryHelper h;
            t.insert(h["abc"]);
            BEAST_EXPECT(t.getPreferred().second == h["abc"].id());
        }
        // Single node smaller child support
        {
            LedgerTrie<Ledger> t;
            LedgerHistoryHelper h;
            t.insert(h["abc"]);
            t.insert(h["abcd"]);
            BEAST_EXPECT(t.getPreferred().second == h["abc"].id());

            t.insert(h["abc"]);
            BEAST_EXPECT(t.getPreferred().second == h["abc"].id());
        }
        // Single node larger child
        {
            LedgerTrie<Ledger> t;
            LedgerHistoryHelper h;
            t.insert(h["abc"]);
            t.insert(h["abcd"],2);
            BEAST_EXPECT(t.getPreferred().second == h["abcd"].id());
        }
        // Single node smaller children support
        {
            LedgerTrie<Ledger> t;
            LedgerHistoryHelper h;
            t.insert(h["abc"]);
            t.insert(h["abcd"]);
            t.insert(h["abce"]);
            BEAST_EXPECT(t.getPreferred().second == h["abc"].id());

            t.insert(h["abc"]);
            BEAST_EXPECT(t.getPreferred().second == h["abc"].id());
        }
        // Single node larger children
        {
            LedgerTrie<Ledger> t;
            LedgerHistoryHelper h;
            t.insert(h["abc"]);
            t.insert(h["abcd"],2);
            t.insert(h["abce"]);
            BEAST_EXPECT(t.getPreferred().second == h["abc"].id());
            t.insert(h["abcd"]);
            BEAST_EXPECT(t.getPreferred().second == h["abcd"].id());
        }
        // Tie-breaker by id
        {
            LedgerTrie<Ledger> t;
            LedgerHistoryHelper h;
            t.insert(h["abcd"],2);
            t.insert(h["abce"],2);

            BEAST_EXPECT(h["abce"].id() > h["abcd"].id());
            BEAST_EXPECT(t.getPreferred().second == h["abce"].id());

            t.insert(h["abcd"]);
            BEAST_EXPECT(h["abce"].id() > h["abcd"].id());
            BEAST_EXPECT(t.getPreferred().second == h["abcd"].id());
        }

        // Tie-breaker not needed
        {
            LedgerTrie<Ledger> t;
            LedgerHistoryHelper h;
            t.insert(h["abc"]);
            t.insert(h["abcd"]);
            t.insert(h["abce"],2);
            // abce only has a margin of 1, but it owns the tie-breaker
            BEAST_EXPECT(h["abce"].id() > h["abcd"].id());
            BEAST_EXPECT(t.getPreferred().second == h["abce"].id());

            // Switch support from abce to abcd, tie-breaker now needed
            t.remove(h["abce"]);
            t.insert(h["abcd"]);
            BEAST_EXPECT(t.getPreferred().second == h["abc"].id());
        }

        // Single node larger grand child
        {
            LedgerTrie<Ledger> t;
            LedgerHistoryHelper h;
            t.insert(h["abc"]);
            t.insert(h["abcd"],2);
            t.insert(h["abcde"],4);
            BEAST_EXPECT(t.getPreferred().second == h["abcde"].id());
        }

        // Too much prefix support from competing branches
        {
            LedgerTrie<Ledger> t;
            LedgerHistoryHelper h;
            t.insert(h["abc"]);
            t.insert(h["abcde"],2);
            t.insert(h["abcfg"],2);
            // 'de' and 'fg' are tied without 'abc' vote
            BEAST_EXPECT(t.getPreferred().second == h["abc"].id());
            t.remove(h["abc"]);
            t.insert(h["abcd"]);
            // 'de' branch has 3 votes to 2, but not enough suport for 'e'
            // since the node on 'd' and the 2 on 'fg' could go in a
            // different direction
            BEAST_EXPECT(t.getPreferred().second == h["abcd"].id());
        }
    }

    void
    testRootRelated()
    {
        using namespace csf;
        // Since the root is a special node that breaks the no-single child
        // invariant, do some tests that exercise it.

        LedgerTrie<Ledger> t;
        LedgerHistoryHelper h;
        BEAST_EXPECT(!t.remove(h[""]));
        BEAST_EXPECT(t.branchSupport(h[""]) == 0);
        BEAST_EXPECT(t.tipSupport(h[""]) == 0);

        t.insert(h["a"]);
        BEAST_EXPECT(t.checkInvariants());
        BEAST_EXPECT(t.branchSupport(h[""]) == 1);
        BEAST_EXPECT(t.tipSupport(h[""]) == 0);

        t.insert(h["e"]);
        BEAST_EXPECT(t.checkInvariants());
        BEAST_EXPECT(t.branchSupport(h[""]) == 2);
        BEAST_EXPECT(t.tipSupport(h[""]) == 0);

        BEAST_EXPECT(t.remove(h["e"]));
        BEAST_EXPECT(t.checkInvariants());
        BEAST_EXPECT(t.branchSupport(h[""]) == 1);
        BEAST_EXPECT(t.tipSupport(h[""]) == 0);
    }

    void
    run()
    {
        testInsert();
        testRemove();
        testTipAndBranchSupport();
        testGetPreferred();
        testRootRelated();
    }
};

BEAST_DEFINE_TESTSUITE(LedgerTrie, consensus, ripple);
}  // namespace test
}  // namespace ripple
