//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright 2014, Nikolaos D. Bougalis <nikb@bougalis.net>


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
#include <vector>
#include <memory>
#include <string>

namespace ripple {
namespace test {

class CompressedTrie
{
    // a substring of an existing string
    struct Slice
    {
        Slice(std::string const& s) : start{0}, stop{s.size()}, ref{s}
        {
            assert(start <= stop);
        }
        Slice(std::size_t start_, std::size_t stop_, std::string const& r_)
            : start{start_}, stop{stop_}, ref{r_}
        {
        }

        // Return a slice of this over the half-open interval (from,to]
        Slice
        sub(std::size_t from, std::size_t to)
        {
            auto clamp = [&](std::size_t val) {
                return std::min(std::max(start, val), stop);
            };

            return Slice(clamp(from), clamp(to), ref);
        }
        // Return a view of this string start from offset spot.
        Slice
        from(std::size_t spot)
        {
            return sub(spot, stop);
        }

        Slice
        before(std::size_t spot)
        {
            return sub(start, spot);
        }

        bool
        empty() const
        {
            return start >= stop;
        }

        std::size_t
        size() const
        {
            return stop - start;
        }

        // Tie-Breaker for comparisons
        // This is used between sibling children that differ at ref[start]
        char const &
        tieBreaker() const
        {
            return ref[start];
        }

        std::string
        fullStr() const
        {
            return ref.substr(0, stop);
        }

        friend std::ostream&
        operator<<(std::ostream& o, Slice const& s)
        {
            return o << s.ref.substr(s.start, s.stop - s.start);
        }

        friend std::size_t
        firstMismatch(Slice const& a, Slice const& b)
        {
            std::size_t start = std::max(a.start, b.start);
            std::size_t stop = std::min(a.stop, b.stop);

            auto it = std::mismatch(
                          a.ref.begin() + start,
                          a.ref.begin() + stop,
                          b.ref.begin() + start)
                          .first;
            return it - a.ref.begin();
        }

        friend Slice
        combine(Slice const& a, Slice const& b)
        {
            // Return combined slice, using ref from longer slice
            if (a.stop < b.stop)
                return Slice(std::min(a.start, b.start), b.stop, b.ref);

            return Slice(std::min(a.start, b.start), a.stop, a.ref);
        }

        std::size_t start;
        std::size_t stop;
        std::string ref;
    };

    struct Node
    {
        Node() : slice{""}, tipSupport{0}, branchSupport{0}
        {
        }

        Node(std::string const& s) : slice{s}, tipSupport{1}, branchSupport{1}
        {
        }

        Node(Slice s) : slice{std::move(s)}
        {
        }

        Slice slice;
        std::uint32_t tipSupport = 0;
        std::uint32_t branchSupport = 0;

        std::vector<std::unique_ptr<Node>> children;
        Node * parent = nullptr;

        void
        remove(Node const* child)
        {
            children.erase(
                std::remove_if(
                    children.begin(),
                    children.end(),
                    [child](std::unique_ptr<Node> const& curr) {
                        return curr.get() == child;
                    }),
                children.end());
        }

        friend std::ostream&
        operator<<(std::ostream& o, Node const& s)
        {
            return o << s.slice << "(T:" << s.tipSupport
                     << ",B:" << s.branchSupport << ")";
        }
    };

    std::unique_ptr<Node> root;

    void
    incrementBranchSupport(Node * n)
    {
        while (n)
        {
            ++n->branchSupport;
            n = n->parent;
        }
    }

    void
    decrementBranchSupport(Node * n)
    {
        while (n)
        {
            --n->branchSupport;
            n = n->parent;
        }
    }

    Node *
    find(Slice s) const
    {
        // find the node with longest common prefix of slice
        Node * curr = root.get();
        bool done = false;
        while (curr && !done)
        {
            Slice const& currSlice = curr->slice;
            if (s.stop <= currSlice.stop)
            {
                done = true;
            }
            else  // try to recurse on a matching child
            {
                auto it = std::find_if(
                    curr->children.begin(),
                    curr->children.end(),
                    [&](std::unique_ptr<Node> const& child) {
                        return child->slice.ref[child->slice.start] ==
                            s.ref[child->slice.start];
                    });
                if (it != curr->children.end())
                    curr = it->get();
                else
                    done = true;
            }
        }
        return curr;
    }

public:
    CompressedTrie()
    {
    }

    // Insert the given string and increase tip support
    void
    insert(std::string const& sIn)
    {
        Slice s{sIn};
        Node * loc = find(sIn);

        // root is empty
        if(!loc)
        {
            root = std::make_unique<Node>(sIn);
            return;
        }
        // loc is the node with the longest common prefix with sIn

        // determine where that prefix ends
        auto end = firstMismatch(loc->slice, s);

        Slice prefix = s.before(end);
        Slice oldSuffix = loc->slice.from(end);
        Slice newSuffix = s.from(end);
        Node * incrementNode = loc;

        if (!oldSuffix.empty())
        {
            // new is a substring of current
            // e.g. abcdef->..., adding abcd
            // -> abcd->ef->...

            // 1. Create ef node and take children ...
            std::unique_ptr<Node> newNode{std::make_unique<Node>(oldSuffix)};
            newNode->tipSupport = loc->tipSupport;
            newNode->branchSupport = loc->branchSupport;
            // take existing children
            using std::swap;
            swap(newNode->children, loc->children);

            // 2. Turn old abcdef node into abcd and add ef as child
            loc->slice = prefix;
            newNode->parent = loc;
            loc->children.emplace_back(std::move(newNode));
            loc->tipSupport = 0;
            incrementNode = loc;
        }
        if (!newSuffix.empty())
        {
            //  current is a substring of new
            // e.g.  abc->... adding abcde
            // ->   abc-> ...
            //            de

            // Create new child node
            std::unique_ptr<Node> newNode{std::make_unique<Node>(newSuffix)};
            newNode->parent = loc;
            incrementNode = newNode.get();
            loc->children.push_back(std::move(newNode));

        }
        if (incrementNode)
        {
            incrementNode->tipSupport++;
            incrementBranchSupport(incrementNode);
        }
    }

    // Remove the given string/decreasing tip support
    // @return whether it was removed
    bool
    remove(std::string const& sIn)
    {
        Slice s{sIn};
        Node * loc = find(sIn);

        // Find the *exact* matching node
        if (loc)
        {
            // must be exact match to remove
            if (loc->slice.stop == s.stop &&
                (firstMismatch(loc->slice, s) == s.stop) &&
                loc->tipSupport > 0)
            {
                loc->tipSupport--;
                decrementBranchSupport(loc);

                if (loc->tipSupport == 0)
                {
                    if (loc->children.empty())
                    {
                        // this node can be removed
                        loc->parent->remove(loc);
                    }
                    else if (loc->children.size() == 1)
                    {
                        // This node can be combined with its child
                        std::unique_ptr<Node> child =
                            std::move(loc->children.front());
                        // Promote grand-children
                        loc->children.clear();
                        std::swap(loc->children, child->children);
                        loc->tipSupport = child->tipSupport;
                        loc->branchSupport = child->branchSupport;
                        loc->slice = combine(loc->slice, child->slice);
                    }
                }
                return true;
            }
        }
        return false;
    }

    std::uint32_t
    tipSupport(std::string const& sIn) const
    {
        Slice s{sIn};
        Node const * loc = find(sIn);
        if (loc)
        {
            // must be exact match
            if (loc->slice.stop == s.stop &&
                firstMismatch(loc->slice, s) == s.stop)
            {
                return loc->tipSupport;
            }
        }
        return 0;
    }

    std::uint32_t
    branchSupport(std::string const& sIn) const
    {
        Slice s{sIn};
        Node const * loc = find(sIn);
        if (loc)
        {
            // must be prefix match
            // If s is longer than loc->slice, no branch support exists
            if (firstMismatch(loc->slice, s) <= s.stop &&
                s.stop <= loc->slice.stop)
            {
                return loc->branchSupport;
            }
        }
        return 0;
    }

    Slice
    getPreferred()
    {
        Node * curr = root.get();
        // boost::none instead?
        if(!curr)
            return Slice{""};

        bool done = false;
        std::uint32_t prefixSupport = curr ? curr->tipSupport : 0;
        while(curr && !done)
        {
            // If the best child has margin exceeding the latent support
            // continue from that child, otherwise we are done

            Node * best = nullptr;
            std::uint32_t margin = 0;

            if(curr->children.empty())
            {
                // nothing
            }
            else if(curr->children.size() == 1)
            {
                best = curr->children[0].get();
                margin = best->branchSupport;
            }
            else
            {
                // sort placing children with largest branch support in the
                // front, breaking ties with tieBreaker field
                std::partial_sort(
                    curr->children.begin(),
                    curr->children.begin() + 2,
                    curr->children.end(),
                    [](std::unique_ptr<Node> const& a,
                       std::unique_ptr<Node> const& b) {
                        return std::tie(a->branchSupport, a->slice.tieBreaker()) >
                            std::tie(b->branchSupport, b->slice.tieBreaker());
                    });

                best = curr->children[0].get();
                margin = curr->children[0]->branchSupport -
                    curr->children[1]->branchSupport;

                // If best holds the tie-breaker, it has a one larger margin
                // since the second best needs additional branchSupport
                // to overcome the tie
                if (best->slice.tieBreaker() >
                    curr->children[1]->slice.tieBreaker())
                    margin++;
            }

            if (best && ((margin > prefixSupport) || (prefixSupport == 0)))
            {
                prefixSupport += curr->tipSupport;
                curr = best;
            }
            else // current is the best
                done = true;
        }
        return curr->slice;
    }


    // Helpers
    void
    dumpImpl(std::ostream& o, std::unique_ptr<Node> const & curr, int offset) const
    {
        if (curr)
        {
            if (offset > 0)
                o << std::setw(offset) << "|-";

            std::stringstream ss;
            ss << *curr;
            o << ss.str() << std::endl;
            for (std::unique_ptr<Node> const& child : curr->children)
                dumpImpl(o, child, offset + 1 + ss.str().size() + 2);
        }
    }

    void
    dump(std::ostream& o) const
    {
        // DFS
        dumpImpl(o, root, 0);
    }

    bool
    checkInvariants() const
    {
        std::stack<Node const *> nodes;
        nodes.push(root.get());
        while (!nodes.empty())
        {
            Node const * curr = nodes.top();
            nodes.pop();
            if(!curr)
                continue;

            // Node with 0 tip support must have multiple children
            if (curr->tipSupport == 0 && curr->children.size() < 2)
                return false;

            // branchSupport = tipSupport + sum(child->branchSupport)
            std::size_t support = curr->tipSupport;
            for (auto const& child : curr->children)
            {
                support += child->branchSupport;
                nodes.push(child.get());
            }
            if (support != curr->branchSupport)
                return false;
        }
        return true;
    }
};

class CompressedTrie_test : public beast::unit_test::suite
{
private:
public:
    void
    testInsert()
    {
        // Single entry
        {
            CompressedTrie t;
            t.insert("abc");
            BEAST_EXPECT(t.checkInvariants());
            BEAST_EXPECT(t.tipSupport("abc") == 1);
            BEAST_EXPECT(t.branchSupport("abc") == 1);

            t.insert("abc");
            BEAST_EXPECT(t.checkInvariants());
            BEAST_EXPECT(t.tipSupport("abc") == 2);
            BEAST_EXPECT(t.branchSupport("abc") == 2);
        }
        // Suffix of existing (extending tree)
        {
            CompressedTrie t;
            t.insert("abc");
            BEAST_EXPECT(t.checkInvariants());
            // extend with no siblings
            t.insert("abcd");
            BEAST_EXPECT(t.checkInvariants());

            BEAST_EXPECT(t.tipSupport("abc") == 1);
            BEAST_EXPECT(t.branchSupport("abc") == 2);
            BEAST_EXPECT(t.tipSupport("abcd") == 1);
            BEAST_EXPECT(t.branchSupport("abcd") == 1);

            //extend with existing sibling
            t.insert("abce");
            BEAST_EXPECT(t.tipSupport("abc") == 1);
            BEAST_EXPECT(t.branchSupport("abc") == 3);
            BEAST_EXPECT(t.tipSupport("abcd") == 1);
            BEAST_EXPECT(t.branchSupport("abcd") == 1);
            BEAST_EXPECT(t.tipSupport("abce") == 1);
            BEAST_EXPECT(t.branchSupport("abce") == 1);

        }
        // Prefix of existing node
        {
            CompressedTrie t;
            t.insert("abcd");
            BEAST_EXPECT(t.checkInvariants());
            // prefix with no siblings
            t.insert("abcdf");
            BEAST_EXPECT(t.checkInvariants());

            BEAST_EXPECT(t.tipSupport("abcd") == 1);
            BEAST_EXPECT(t.branchSupport("abcd") == 2);
            BEAST_EXPECT(t.tipSupport("abcdf") == 1);
            BEAST_EXPECT(t.branchSupport("abcdf") == 1);

            // prefix with existing child
            t.insert("abc");
            BEAST_EXPECT(t.checkInvariants());

            BEAST_EXPECT(t.tipSupport("abc") == 1);
            BEAST_EXPECT(t.branchSupport("abc") == 3);
            BEAST_EXPECT(t.tipSupport("abcd") == 1);
            BEAST_EXPECT(t.branchSupport("abcd") == 2);
            BEAST_EXPECT(t.tipSupport("abcdf") == 1);
            BEAST_EXPECT(t.branchSupport("abcdf") == 1);
        }
        // Suffix + prefix
        {
            CompressedTrie t;
            t.insert("abcd");
            BEAST_EXPECT(t.checkInvariants());
            t.insert("abce");
            BEAST_EXPECT(t.checkInvariants());

            BEAST_EXPECT(t.tipSupport("abc") == 0);
            BEAST_EXPECT(t.branchSupport("abc") == 2);
            BEAST_EXPECT(t.tipSupport("abcd") == 1);
            BEAST_EXPECT(t.branchSupport("abcd") == 1);
            BEAST_EXPECT(t.tipSupport("abce") == 1);
            BEAST_EXPECT(t.branchSupport("abce") == 1);
        }
        // Suffix + prefix with existing child
        {
            //  abcd : abcde, abcf

            CompressedTrie t;
            t.insert("abcd");
            BEAST_EXPECT(t.checkInvariants());
            t.insert("abcde");
            BEAST_EXPECT(t.checkInvariants());
            t.insert("abcf");
            BEAST_EXPECT(t.checkInvariants());

            BEAST_EXPECT(t.tipSupport("abc") == 0);
            BEAST_EXPECT(t.branchSupport("abc") == 3);
            BEAST_EXPECT(t.tipSupport("abcd") == 1);
            BEAST_EXPECT(t.branchSupport("abcd") == 2);
            BEAST_EXPECT(t.tipSupport("abcf") == 1);
            BEAST_EXPECT(t.branchSupport("abcf") == 1);
            BEAST_EXPECT(t.tipSupport("abcde") == 1);
            BEAST_EXPECT(t.branchSupport("abcde") == 1);
        }
    }

    void
    testRemove()
    {
        // Not in trie
        {
            CompressedTrie t;
            t.insert("abc");

            BEAST_EXPECT(!t.remove("ab"));
            BEAST_EXPECT(t.checkInvariants());
            BEAST_EXPECT(!t.remove("a"));
            BEAST_EXPECT(t.checkInvariants());
        }
        // In trie but with 0 tip support
        {
            CompressedTrie t;
            t.insert("abcd");
            t.insert("abce");

            BEAST_EXPECT(t.tipSupport("abc") == 0);
            BEAST_EXPECT(t.branchSupport("abc") == 2);
            BEAST_EXPECT(!t.remove("abc"));
            BEAST_EXPECT(t.checkInvariants());
            BEAST_EXPECT(t.tipSupport("abc") == 0);
            BEAST_EXPECT(t.branchSupport("abc") == 2);

        }
        // In trie with > 1 tip support
        {
            CompressedTrie t;
            t.insert("abc");
            t.insert("abc");

            BEAST_EXPECT(t.tipSupport("abc") == 2);
            BEAST_EXPECT(t.remove("abc"));
            BEAST_EXPECT(t.checkInvariants());
            BEAST_EXPECT(t.tipSupport("abc") == 1);
        }
        // In trie with = 1 tip support, no children
        {
            CompressedTrie t;
            t.insert("ab");
            t.insert("abc");

            BEAST_EXPECT(t.tipSupport("ab") == 1);
            BEAST_EXPECT(t.branchSupport("ab") == 2);
            BEAST_EXPECT(t.tipSupport("abc") == 1);
            BEAST_EXPECT(t.branchSupport("abc") == 1);

            BEAST_EXPECT(t.remove("abc"));
            BEAST_EXPECT(t.checkInvariants());
            BEAST_EXPECT(t.tipSupport("ab") == 1);
            BEAST_EXPECT(t.branchSupport("ab") == 1);
            BEAST_EXPECT(t.tipSupport("abc") == 0);
            BEAST_EXPECT(t.branchSupport("abc") == 0);
        }
        // In trie with = 1 tip support, 1 child
        {
            CompressedTrie t;
            t.insert("ab");
            t.insert("abc");
            t.insert("abcd");

            BEAST_EXPECT(t.tipSupport("abc") == 1);
            BEAST_EXPECT(t.branchSupport("abc") == 2);
            BEAST_EXPECT(t.tipSupport("abcd") == 1);
            BEAST_EXPECT(t.branchSupport("abcd") == 1);

            BEAST_EXPECT(t.remove("abc"));
            BEAST_EXPECT(t.checkInvariants());
            BEAST_EXPECT(t.tipSupport("abc") == 0);
            BEAST_EXPECT(t.branchSupport("abc") == 1);
            BEAST_EXPECT(t.tipSupport("abcd") == 1);
            BEAST_EXPECT(t.branchSupport("abcd") == 1);

        }
        // In trie with = 1 tip support, > 1 children
        {
            CompressedTrie t;
            t.insert("ab");
            t.insert("abc");
            t.insert("abcd");
            t.insert("abce");

            BEAST_EXPECT(t.tipSupport("abc") == 1);
            BEAST_EXPECT(t.branchSupport("abc") == 3);

            BEAST_EXPECT(t.remove("abc"));
            BEAST_EXPECT(t.checkInvariants());
            BEAST_EXPECT(t.tipSupport("abc") == 0);
            BEAST_EXPECT(t.branchSupport("abc") == 2);
        }
    }

    void
    testTipAndBranchSupport()
    {
        CompressedTrie t;
        BEAST_EXPECT(t.tipSupport("a") == 0);
        BEAST_EXPECT(t.tipSupport("adfdf") == 0);
        BEAST_EXPECT(t.branchSupport("a") == 0);
        BEAST_EXPECT(t.branchSupport("adfdf") == 0);

        t.insert("abc");
        BEAST_EXPECT(t.tipSupport("a") == 0);
        BEAST_EXPECT(t.tipSupport("ab") == 0);
        BEAST_EXPECT(t.tipSupport("abc") == 1);
        BEAST_EXPECT(t.tipSupport("abcd") == 0);
        BEAST_EXPECT(t.branchSupport("a") == 1);
        BEAST_EXPECT(t.branchSupport("ab") == 1);
        BEAST_EXPECT(t.branchSupport("abc") == 1);
        BEAST_EXPECT(t.branchSupport("abcd") == 0);

        t.insert("abe");
        BEAST_EXPECT(t.tipSupport("a") == 0);
        BEAST_EXPECT(t.tipSupport("ab") == 0);
        BEAST_EXPECT(t.tipSupport("abc") == 1);
        BEAST_EXPECT(t.tipSupport("abe") == 1);

        BEAST_EXPECT(t.branchSupport("a") == 2);
        BEAST_EXPECT(t.branchSupport("ab") == 2);
        BEAST_EXPECT(t.branchSupport("abc") == 1);
        BEAST_EXPECT(t.branchSupport("abe") == 1);
    }


    void
    testGetPreferred()
    {

        // Empty
        {
            CompressedTrie t;
            BEAST_EXPECT(t.getPreferred().empty());
        }
        // Single node no children
        {
            CompressedTrie t;
            t.insert("abc");
            BEAST_EXPECT(t.getPreferred().fullStr() == "abc");
        }
        // Single node smaller child support
        {
            CompressedTrie t;
            t.insert("abc");
            t.insert("abcd");
            BEAST_EXPECT(t.getPreferred().fullStr() == "abc");

            t.insert("abc");
            BEAST_EXPECT(t.getPreferred().fullStr() == "abc");
        }
        // Single node larger child
        {
            CompressedTrie t;
            t.insert("abc");
            t.insert("abcd");
            t.insert("abcd");
            BEAST_EXPECT(t.getPreferred().fullStr() == "abcd");
        }
        // Single node smaller children support
        {
            CompressedTrie t;
            t.insert("abc");
            t.insert("abcd");
            t.insert("abce");
            BEAST_EXPECT(t.getPreferred().fullStr() == "abc");

            t.insert("abc");
            BEAST_EXPECT(t.getPreferred().fullStr() == "abc");
        }
        // Single node larger children
        {
            CompressedTrie t;
            t.insert("abc");
            t.insert("abcd");
            t.insert("abcd");
            t.insert("abce");
            BEAST_EXPECT(t.getPreferred().fullStr() == "abc");
            t.insert("abcd");
            BEAST_EXPECT(t.getPreferred().fullStr() == "abcd");

        }
        // Tie-breaker
        {
            CompressedTrie t;
            t.insert("abcd");
            t.insert("abcd");
            t.insert("abce");
            t.insert("abce");
            BEAST_EXPECT(t.getPreferred().fullStr() == "abce");

            t.insert("abcd");
            BEAST_EXPECT(t.getPreferred().fullStr() == "abcd");
        }

        // Tie-breaker not needed
        {
            CompressedTrie t;
            t.insert("abc");
            t.insert("abcd");
            t.insert("abce");
            t.insert("abce");
            // abce only has a margin of 1, but it owns the tie-breaker
            BEAST_EXPECT(t.getPreferred().fullStr() == "abce");

            t.remove("abc");
            t.insert("abcd");
            BEAST_EXPECT(t.getPreferred().fullStr() == "abce");
        }

        // Single node larger grand child
        {
            CompressedTrie t;
            t.insert("abc");
            t.insert("abcd");
            t.insert("abcd");
            t.insert("abcde");
            t.insert("abcde");
            t.insert("abcde");
            BEAST_EXPECT(t.getPreferred().fullStr() == "abcde");
        }
    }
    void
    run()
    {
        testInsert();
        testRemove();
        testTipAndBranchSupport();
        testGetPreferred();

    }
};  // namespace test

BEAST_DEFINE_TESTSUITE(CompressedTrie, ripple_basics, ripple);

}  // namespace test
}  // namespace ripple
