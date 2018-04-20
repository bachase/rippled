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
#include <ripple/beast/unit_test.h>
#include <ripple/consensus/LedgerTrie.h>
#include <test/csf/ledgers.h>
#include <unordered_map>
#include <random>

namespace ripple {
namespace test {

class LedgerTrie_test : public beast::unit_test::suite
{
    beast::Journal j;


    void
    testInsert()
    {
        using namespace csf;
        // Single entry by itself
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
        // uncommitted of existing node
        {
            LedgerTrie<Ledger> t;
            LedgerHistoryHelper h;
            t.insert(h["abcd"]);
            BEAST_EXPECT(t.checkInvariants());
            // uncommitted with no siblings
            t.insert(h["abcdf"]);
            BEAST_EXPECT(t.checkInvariants());

            BEAST_EXPECT(t.tipSupport(h["abcd"]) == 1);
            BEAST_EXPECT(t.branchSupport(h["abcd"]) == 2);
            BEAST_EXPECT(t.tipSupport(h["abcdf"]) == 1);
            BEAST_EXPECT(t.branchSupport(h["abcdf"]) == 1);

            // uncommitted with existing child
            t.insert(h["abc"]);
            BEAST_EXPECT(t.checkInvariants());

            BEAST_EXPECT(t.tipSupport(h["abc"]) == 1);
            BEAST_EXPECT(t.branchSupport(h["abc"]) == 3);
            BEAST_EXPECT(t.tipSupport(h["abcd"]) == 1);
            BEAST_EXPECT(t.branchSupport(h["abcd"]) == 2);
            BEAST_EXPECT(t.tipSupport(h["abcdf"]) == 1);
            BEAST_EXPECT(t.branchSupport(h["abcdf"]) == 1);
        }
        // Suffix + uncommitted of existing node
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
        // Suffix + uncommitted with existing child
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

        // Multiple counts
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

        // In trie with = 1 tip support, parent compaction
        {
            LedgerTrie<Ledger> t;
            LedgerHistoryHelper h;
            t.insert(h["ab"]);
            t.insert(h["abc"]);
            t.insert(h["abd"]);
            BEAST_EXPECT(t.checkInvariants());
            t.remove(h["ab"]);
            BEAST_EXPECT(t.checkInvariants());
            BEAST_EXPECT(t.tipSupport(h["abc"]) == 1);
            BEAST_EXPECT(t.tipSupport(h["abd"]) == 1);
            BEAST_EXPECT(t.tipSupport(h["ab"]) == 0);
            BEAST_EXPECT(t.branchSupport(h["ab"]) == 2);

            t.remove(h["abd"]);
            BEAST_EXPECT(t.checkInvariants());

            BEAST_EXPECT(t.tipSupport(h["abc"]) == 1);
            BEAST_EXPECT(t.branchSupport(h["ab"]) == 1);

        }
    }

    void
    testSupport()
    {
        using namespace csf;
        using Seq = Ledger::Seq;


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

        t.remove(h["abc"]);
        BEAST_EXPECT(t.tipSupport(h["a"]) == 0);
        BEAST_EXPECT(t.tipSupport(h["ab"]) == 0);
        BEAST_EXPECT(t.tipSupport(h["abc"]) == 0);
        BEAST_EXPECT(t.tipSupport(h["abe"]) == 1);

        BEAST_EXPECT(t.branchSupport(h["a"]) == 1);
        BEAST_EXPECT(t.branchSupport(h["ab"]) == 1);
        BEAST_EXPECT(t.branchSupport(h["abc"]) == 0);
        BEAST_EXPECT(t.branchSupport(h["abe"]) == 1);

    }

    void
    testGetPreferred()
    {
        using namespace csf;
        using Seq = Ledger::Seq;
        // Empty
        {
            LedgerTrie<Ledger> t;
            LedgerHistoryHelper h;
            BEAST_EXPECT(t.getPreferred(Seq{0}).id == h[""].id());
            BEAST_EXPECT(t.getPreferred(Seq{2}).id == h[""].id());
        }
        // Single node no children
        {
            LedgerTrie<Ledger> t;
            LedgerHistoryHelper h;
            t.insert(h["abc"]);
            BEAST_EXPECT(t.getPreferred(Seq{3}).id == h["abc"].id());
        }
        // Single node smaller child support
        {
            LedgerTrie<Ledger> t;
            LedgerHistoryHelper h;
            t.insert(h["abc"]);
            t.insert(h["abcd"]);
            BEAST_EXPECT(t.getPreferred(Seq{3}).id == h["abc"].id());
            BEAST_EXPECT(t.getPreferred(Seq{4}).id == h["abc"].id());
        }
        // Single node larger child
        {
            LedgerTrie<Ledger> t;
            LedgerHistoryHelper h;
            t.insert(h["abc"]);
            t.insert(h["abcd"],2);
            BEAST_EXPECT(t.getPreferred(Seq{3}).id == h["abcd"].id());
            BEAST_EXPECT(t.getPreferred(Seq{4}).id == h["abcd"].id());
        }
        // Single node smaller children support
        {
            LedgerTrie<Ledger> t;
            LedgerHistoryHelper h;
            t.insert(h["abc"]);
            t.insert(h["abcd"]);
            t.insert(h["abce"]);
            BEAST_EXPECT(t.getPreferred(Seq{3}).id == h["abc"].id());
            BEAST_EXPECT(t.getPreferred(Seq{4}).id == h["abc"].id());

            t.insert(h["abc"]);
            BEAST_EXPECT(t.getPreferred(Seq{3}).id == h["abc"].id());
            BEAST_EXPECT(t.getPreferred(Seq{4}).id == h["abc"].id());
        }
        // Single node larger children
        {
            LedgerTrie<Ledger> t;
            LedgerHistoryHelper h;
            t.insert(h["abc"]);
            t.insert(h["abcd"],2);
            t.insert(h["abce"]);
            BEAST_EXPECT(t.getPreferred(Seq{3}).id == h["abc"].id());
            BEAST_EXPECT(t.getPreferred(Seq{4}).id == h["abc"].id());

            t.insert(h["abcd"]);
            BEAST_EXPECT(t.getPreferred(Seq{3}).id == h["abcd"].id());
            BEAST_EXPECT(t.getPreferred(Seq{4}).id == h["abcd"].id());
        }
        // Tie-breaker by id
        {
            LedgerTrie<Ledger> t;
            LedgerHistoryHelper h;
            t.insert(h["abcd"],2);
            t.insert(h["abce"],2);

            BEAST_EXPECT(h["abce"].id() > h["abcd"].id());
            BEAST_EXPECT(t.getPreferred(Seq{4}).id == h["abce"].id());

            t.insert(h["abcd"]);
            BEAST_EXPECT(h["abce"].id() > h["abcd"].id());
            BEAST_EXPECT(t.getPreferred(Seq{4}).id == h["abcd"].id());
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
            BEAST_EXPECT(t.getPreferred(Seq{3}).id == h["abce"].id());
            BEAST_EXPECT(t.getPreferred(Seq{4}).id == h["abce"].id());

            // Switch support from abce to abcd, tie-breaker now needed
            t.insert(h["abcd"]);
            t.remove(h["abce"]);
            BEAST_EXPECT(t.getPreferred(Seq{3}).id == h["abc"].id());
            BEAST_EXPECT(t.getPreferred(Seq{4}).id == h["abc"].id());
        }

        // Single node larger grand child
        {
            LedgerTrie<Ledger> t;
            LedgerHistoryHelper h;
            t.insert(h["abc"]);
            t.insert(h["abcd"],2);
            t.insert(h["abcde"],4);
            BEAST_EXPECT(t.getPreferred(Seq{3}).id == h["abcde"].id());
            BEAST_EXPECT(t.getPreferred(Seq{4}).id == h["abcde"].id());
            BEAST_EXPECT(t.getPreferred(Seq{5}).id == h["abcde"].id());
        }

        // Too much uncommitted support from competing branches
        {
            LedgerTrie<Ledger> t;
            LedgerHistoryHelper h;
            t.insert(h["abc"]);
            t.insert(h["abcde"],2);
            t.insert(h["abcfg"],2);
            // 'de' and 'fg' are tied without 'abc' vote
            BEAST_EXPECT(t.getPreferred(Seq{3}).id == h["abc"].id());
            BEAST_EXPECT(t.getPreferred(Seq{4}).id == h["abc"].id());
            BEAST_EXPECT(t.getPreferred(Seq{5}).id == h["abc"].id());

            t.insert(h["abcd"]);
            t.remove(h["abc"]);

            // 'de' branch has 3 votes to 2, so earlier sequences see it as
            // preferred
            BEAST_EXPECT(t.getPreferred(Seq{3}).id == h["abcde"].id());
            BEAST_EXPECT(t.getPreferred(Seq{4}).id == h["abcde"].id());

            // However, if you validated a ledger with Seq 5, potentially on
            // a different branch, you do not yet know if they chose abcd
            // or abcf because of you, so abc remains preferred
            BEAST_EXPECT(t.getPreferred(Seq{5}).id == h["abc"].id());
        }

        // Changing largestSeq perspective changes preferred branch
        {
            /** Build the tree below with initial tip support annotated
                   A
                  / \
               B(1)  C(1)
              /  |   |
             H   D   F(1)
                 |
                 E(2)
                 |
                 G
            */
            LedgerTrie<Ledger> t;
            LedgerHistoryHelper h;
            t.insert(h["ab"]);
            t.insert(h["ac"]);
            t.insert(h["acf"]);
            t.insert(h["abde"],2);

            // B has more branch support
            BEAST_EXPECT(t.getPreferred(Seq{1}).id == h["ab"].id());
            BEAST_EXPECT(t.getPreferred(Seq{2}).id == h["ab"].id());
            // But if you last validated D,F or E, you do not yet know
            // if someone used that validation to commit to B or C
            BEAST_EXPECT(t.getPreferred(Seq{3}).id == h["a"].id());
            BEAST_EXPECT(t.getPreferred(Seq{4}).id == h["a"].id());

            /** One of E advancing to G doesn't change anything
                   A
                  / \
               B(1)  C(1)
              /  |   |
             H   D   F(1)
                 |
                 E(1)
                 |
                 G(1)
            */
            t.insert(h["abdeg"]);
            t.remove(h["abde"]);
            BEAST_EXPECT(t.getPreferred(Seq{1}).id == h["ab"].id());
            BEAST_EXPECT(t.getPreferred(Seq{2}).id == h["ab"].id());
            BEAST_EXPECT(t.getPreferred(Seq{3}).id == h["a"].id());
            BEAST_EXPECT(t.getPreferred(Seq{4}).id == h["a"].id());
            BEAST_EXPECT(t.getPreferred(Seq{5}).id == h["a"].id());

            /** C advancing to H does advance the seq 3 preferred ledger
                   A
                  / \
               B(1)  C
              /  |   |
             H(1)D   F(1)
                 |
                 E(1)
                 |
                 G(1)
            */
            t.insert(h["abh"]);
            t.remove(h["ac"]);
            BEAST_EXPECT(t.getPreferred(Seq{1}).id == h["ab"].id());
            BEAST_EXPECT(t.getPreferred(Seq{2}).id == h["ab"].id());
            BEAST_EXPECT(t.getPreferred(Seq{3}).id == h["ab"].id());
            BEAST_EXPECT(t.getPreferred(Seq{4}).id == h["a"].id());
            BEAST_EXPECT(t.getPreferred(Seq{5}).id == h["a"].id());

            /** F advancing to E also moves the preferred ledger forward
                   A
                  / \
               B(1)  C
              /  |   |
             H(1)D   F
                 |
                 E(2)
                 |
                 G(1)
            */
            t.insert(h["abde"]);
            t.remove(h["acf"]);
            BEAST_EXPECT(t.getPreferred(Seq{1}).id == h["abde"].id());
            BEAST_EXPECT(t.getPreferred(Seq{2}).id == h["abde"].id());
            BEAST_EXPECT(t.getPreferred(Seq{3}).id == h["abde"].id());
            BEAST_EXPECT(t.getPreferred(Seq{4}).id == h["ab"].id());
            BEAST_EXPECT(t.getPreferred(Seq{5}).id == h["ab"].id());

        }
    }

    template <class T, class F>
    static void
    cartesianProduct4(std::vector<T> const& iter, F&& callback)
    {
        for (auto const& a : iter)
            for (auto const& b : iter)
                for (auto const& c : iter)
                    for (auto const& d : iter)
                        callback(a, b, c, d);
    };

    void
    testMarkInvalid()
    {
        using namespace csf;
        using Seq = Ledger::Seq;

        // Internal span invalid
        {
            LedgerTrie<Ledger> t;
            LedgerHistoryHelper h;
            t.insert(h["abcd"]);
            BEAST_EXPECT(t.getPreferred(Seq{4}).id == h["abcd"].id());
            t.markInvalid(h["abcd"]);
            BEAST_EXPECT(t.getPreferred(Seq{4}).id == h["abc"].id());
            t.markInvalid(h["abc"]);
            BEAST_EXPECT(t.getPreferred(Seq{4}).id == h["ab"].id());
            t.markInvalid(h["ab"]);
            BEAST_EXPECT(t.getPreferred(Seq{4}).id == h["a"].id());
            t.markInvalid(h["a"]);
            BEAST_EXPECT(t.getPreferred(Seq{4}).id == h[""].id());
        }

        // Single child invalid
        {
            LedgerTrie<Ledger> t;
            LedgerHistoryHelper h;
            t.insert(h["a"]);
            BEAST_EXPECT(t.getPreferred(Seq{1}).id == h["a"].id());
            t.markInvalid(h["a"]);
            BEAST_EXPECT(t.getPreferred(Seq{1}).id == h[""].id());
        }

        // Two children, none valid
        {
            LedgerTrie<Ledger> t;
            LedgerHistoryHelper h;
            t.insert(h["ab"]);
            t.insert(h["ac"]);
            BEAST_EXPECT(t.getPreferred(Seq{2}).id == h["ac"].id());
            t.markInvalid(h["ab"]);
            t.markInvalid(h["ac"]);
            BEAST_EXPECT(t.getPreferred(Seq{2}).id == h["a"].id());
        }

        // Two children, second-best valid, best valid
        {
            LedgerTrie<Ledger> t;
            LedgerHistoryHelper h;
            t.insert(h["ab"]);
            t.insert(h["ac"]);
            BEAST_EXPECT(t.getPreferred(Seq{2}).id == h["ac"].id());
            t.markInvalid(h["ab"]);
            BEAST_EXPECT(t.getPreferred(Seq{2}).id == h["ac"].id());
        }

        // Two children, best invalid, second best valid
        {
            LedgerTrie<Ledger> t;
            LedgerHistoryHelper h;
            t.insert(h["ab"]);
            t.insert(h["ac"]);
            BEAST_EXPECT(t.getPreferred(Seq{2}).id == h["ac"].id());
            t.markInvalid(h["ac"]);
            BEAST_EXPECT(t.getPreferred(Seq{2}).id == h["ab"].id());
        }

        // Exhaustive multi-children
        // Given 4 siblings, tests all combinations of relative
        // weights and valid/invalid
        //     G
        //  / / \ \
        // A  B  C D
        std::vector<std::uint32_t> const counts = {1,2,3,4};
        std::vector<bool> const valids = {true, false};
        cartesianProduct4(
            counts,
            [&](std::uint32_t countA,
                std::uint32_t countB,
                std::uint32_t countC,
                std::uint32_t countD) {
            cartesianProduct4(
                valids,
                [&](bool validA, bool validB, bool validC, bool validD) {
                    LedgerTrie<Ledger> t;
                    LedgerHistoryHelper h;
                    std::map<std::string, std::uint32_t> counts = {
                        {"a", countA}, {"b", countB}, {"c", countC},
                        {"d", countD}};

                    for(auto it : counts)
                        t.insert(h[it.first], it.second);

                    auto markInvalid = [&](bool const isValid, std::string s) {
                        if (!isValid)
                        {
                            counts.erase(s);
                            t.markInvalid(h[s]);
                        }
                    };

                    markInvalid(validA, "a");
                    markInvalid(validB, "b");
                    markInvalid(validC, "c");
                    markInvalid(validD, "d");
                    auto it = std::max_element(
                        counts.begin(), counts.end(), [](auto& a, auto& b) {
                            return std::make_pair(a.second, a.first) <
                                std::make_pair(b.second, b.first);
                        });
                    if (it != counts.end())
                        BEAST_EXPECT(
                            t.getPreferred(Seq{1}).id == h[it->first].id());
                    else
                        BEAST_EXPECT(t.getPreferred(Seq{1}).id == h[""].id());

                });
        });

        // Invalid retained as branch advances
        {
            LedgerTrie<Ledger> t;
            LedgerHistoryHelper h;
            t.insert(h["ab"]);
            t.insert(h["ac"], 2);

            BEAST_EXPECT(t.getPreferred(Seq{2}).id == h["ac"].id());
            t.markInvalid(h["ac"]);
            BEAST_EXPECT(t.getPreferred(Seq{2}).id == h["ab"].id());

            t.insert(h["acd"]);
            t.remove(h["ac"]);
            BEAST_EXPECT(t.getPreferred(Seq{2}).id == h["ab"].id());
            t.insert(h["acd"]);
            t.remove(h["ac"]);
            BEAST_EXPECT(t.getPreferred(Seq{2}).id == h["ab"].id());
        }
        // Invalid retained if intermediate support removed
        {
            LedgerTrie<Ledger> t;
            LedgerHistoryHelper h;
            t.insert(h["ab"]);
            t.insert(h["abcd"],2);
            BEAST_EXPECT(t.getPreferred(Seq{4}).id == h["abcd"].id());

            t.markInvalid(h["abc"]);
            BEAST_EXPECT(t.getPreferred(Seq{4}).id == h["ab"].id());

            t.remove(h["ab"]);
            BEAST_EXPECT(t.getPreferred(Seq{4}).id == h["ab"].id());
        }

        // Invalid lost if branch completely abandoned then recovered
        {
            LedgerTrie<Ledger> t;
            LedgerHistoryHelper h;
            t.insert(h["ab"]);
            t.insert(h["abc"],2);
            BEAST_EXPECT(t.getPreferred(Seq{2}).id == h["abc"].id());

            t.markInvalid(h["abc"]);
            BEAST_EXPECT(t.getPreferred(Seq{2}).id == h["ab"].id());

            t.remove(h["abc"],2);
            BEAST_EXPECT(t.getPreferred(Seq{2}).id == h["ab"].id());

            t.insert(h["abc"],2);
            BEAST_EXPECT(t.getPreferred(Seq{2}).id == h["abc"].id());
        }

        // Invalid eliminates tie-breaker advantage
        {
            LedgerTrie<Ledger> t;
            LedgerHistoryHelper h;
            t.insert(h["abc"]);
            t.insert(h["abcd"]);
            t.insert(h["abce"],2);
            // abce only has a margin of 1, but it owns the tie-breaker
            BEAST_EXPECT(h["abce"].id() > h["abcd"].id());
            BEAST_EXPECT(t.getPreferred(Seq{3}).id == h["abce"].id());
            BEAST_EXPECT(t.getPreferred(Seq{4}).id == h["abce"].id());

            t.markInvalid(h["abce"]);
            BEAST_EXPECT(t.getPreferred(Seq{3}).id == h["abc"].id());
            BEAST_EXPECT(t.getPreferred(Seq{4}).id == h["abc"].id());

            // Switch support from abce to abcd, abcd now has enough
            // margin since the one abce node is invalid
            t.insert(h["abcd"]);
            t.remove(h["abce"]);
            BEAST_EXPECT(t.getPreferred(Seq{3}).id == h["abcd"].id());
            BEAST_EXPECT(t.getPreferred(Seq{4}).id == h["abcd"].id());
        }

        // Invalid doesn't impact last good sequence view
        {
            // Setting the following ledgers as invalid shouldn't change the
            // preferred ledger based on the last locally valided sequence

            for (std::string const invalid :
                {"", "ac", "acf", "abh", "abd", "ab"})
            {
                /** Build the tree below with initial tip support annotated
                       A
                      / \
                   B(1)  C(1)
                  /  |   |
                 H   D   F(1)
                     |
                     E(2)
                     |
                     G
                */
                LedgerTrie<Ledger> t;
                LedgerHistoryHelper h;
                t.insert(h["ab"]);
                t.insert(h["ac"]);
                t.insert(h["acf"]);
                t.insert(h["abde"], 2);

                if(!invalid.empty())
                    t.markInvalid(h[invalid]);

                // B has more branch support
                if(invalid != "ab")
                {
                    BEAST_EXPECT(t.getPreferred(Seq{1}).id == h["ab"].id());
                    BEAST_EXPECT(t.getPreferred(Seq{2}).id == h["ab"].id());
                }
                else //unless B is invalid
                {
                    BEAST_EXPECT(t.getPreferred(Seq{1}).id == h["ac"].id());
                    BEAST_EXPECT(t.getPreferred(Seq{2}).id == h["ac"].id());
                }
                // But if you last validated D,F or E, you do not yet know
                // if someone used that validation to commit to B or C
                BEAST_EXPECT(t.getPreferred(Seq{3}).id == h["a"].id());
                BEAST_EXPECT(t.getPreferred(Seq{4}).id == h["a"].id());
            }
        }
    }

    void
    testRootRelated()
    {
        using namespace csf;
        using Seq = Ledger::Seq;
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
    testStress()
    {
        using namespace csf;
        LedgerTrie<Ledger> t;
        LedgerHistoryHelper h;

        // Test quasi-randomly add/remove supporting for different ledgers
        // from a branching history.

        // Ledgers have sequence 1,2,3,4
        std::uint32_t const depth = 4;
        // Each ledger has 4 possible children
        std::uint32_t const width  = 4;

        std::uint32_t const iterations = 10000;

        // Use explicit seed to have same results for CI
        std::mt19937 gen{ 42 };
        std::uniform_int_distribution<> depthDist(0, depth-1);
        std::uniform_int_distribution<> widthDist(0, width-1);
        std::uniform_int_distribution<> flip(0, 1);
        for(std::uint32_t i = 0; i < iterations; ++i)
        {
            // pick a random ledger history
            std::string curr = "";
            char depth = depthDist(gen);
            char offset = 0;
            for(char d = 0; d < depth; ++d)
            {
                char a = offset + widthDist(gen);
                curr += a;
                offset = (a + 1) * width;
            }

            // 50-50 to add remove
            if(flip(gen) == 0)
                t.insert(h[curr]);
            else
                t.remove(h[curr]);
            if(!BEAST_EXPECT(t.checkInvariants()))
                return;
        }
    }

    void
    run()
    {
        testInsert();
        testRemove();
        testSupport();
        testGetPreferred();
        testMarkInvalid();
        testRootRelated();
        testStress();
    }
};

BEAST_DEFINE_TESTSUITE(LedgerTrie, consensus, ripple);
}  // namespace test
}  // namespace ripple
