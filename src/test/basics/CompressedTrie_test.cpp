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
    /** A span (or segment) of a sequence

    */
    class Span
    {
        std::size_t start_;
        std::size_t end_;
        std::string ref_;
    public:
        Span(std::string s) : start_{0}, end_{s.size()}, ref_{std::move(s)}
        {

        }

        Span() : start_{0}, end_{0}, ref_{}
        {
        }

        std::size_t
        end() const
        {
            return end_;
        }

        // Return a view of this string starting from offset spot.
        Span
        from(std::size_t spot)
        {
            return sub(spot, end_);
        }

        // Return a view of this string ending (before) at offset spot
        Span
        before(std::size_t spot)
        {
            return sub(start_, spot);
        }

        bool
        empty() const
        {
            return start_ >= end_;
        }

        // Tie-Breaker for comparisons
        // This is used between sibling children that differ at ref[start]
        char const &
        tieBreaker() const
        {
            return ref_[start_];
        }

        std::string
        fullStr() const
        {
            return ref_.substr(0, end_);
        }

        std::size_t
        mismatch(std::string const & o) const
        {
            std::size_t mend = std::min(end_, o.size());

            auto it = std::mismatch(
                          ref_.begin() + start_,
                          ref_.begin() + mend,
                          o.begin() + start_)
                          .first;

            return (it - ref_.begin());
        }

    private:
       Span(std::size_t start, std::size_t end, std::string const& r)
            : start_{start}, end_{end}, ref_{r}
        {
            assert(start <= end);
        }

        // Return a span of this over the half-open interval (from,to]
        Span
        sub(std::size_t from, std::size_t to)
        {
            auto clamp = [&](std::size_t val) {
                return std::min(std::max(start_, val), end_);
            };

            return Span(clamp(from), clamp(to), ref_);
        }

        friend std::ostream&
        operator<<(std::ostream& o, Span const& s)
        {
            return o << s.ref_.substr(s.start_, s.end_ - s.start_);
        }

        friend Span
        combine(Span const& a, Span const& b)
        {
            // Return combined span, using ref from longer span
            if (a.end_ < b.end_)
                return Span(std::min(a.start_, b.start_), b.end_, b.ref_);

            return Span(std::min(a.start_, b.start_), a.end_, a.ref_);
        }
    };

    // A node in the trie
    struct Node
    {
        Node() : span{}, tipSupport{0}, branchSupport{0}
        {
        }

        Node(std::string const& s) : span{s}, tipSupport{1}, branchSupport{1}
        {
        }

        Node(Span s) : span{std::move(s)}
        {
        }

        Span span;
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
            return o << s.span << "(T:" << s.tipSupport
                     << ",B:" << s.branchSupport << ")";
        }
    };

    /** Find the node in the trie that represents the longest common prefix with
        `s`.

        @return Pair of the found node and the position in `s` where the prefix
                ends. This might be one past the last position in `s`
                (e.g. s.size()), in which case the prefix matched `s` entirely.
    */
    std::pair<Node*, std::size_t>
    find(std::string const& s) const
    {
        Node* curr = root.get();

        // Root is always defined and is a prefix of all strings
        assert(curr);
        std::size_t pos = curr->span.mismatch(s);

        bool done = false;

        // Continue searching for a better span as long as the current position
        // matches the entire span
        while (!done && pos == curr->span.end())
        {
            done = true;
            // All children spans are disjoint, so we continue if a child
            // has a longer match
            for (std::unique_ptr<Node> const& child : curr->children)
            {
                auto childPos = child->span.mismatch(s);
                if (childPos > pos)
                {
                    done = false;
                    pos = childPos;
                    curr = child.get();
                    break;
                }
            }
        }
        return std::make_pair(curr, pos);
    }

    // The root of the trie. The root is allowed to break the no-single child
    // invariant.
    std::unique_ptr<Node> root;


public:
    CompressedTrie() : root{std::make_unique<Node>()}
    {
    }

    /** Insert the given item into the trie.
    */
    void
    insert(std::string const& s)
    {
        Node* loc;
        std::size_t pos;
        std::tie(loc, pos) = find(s);
        // There is always a place to insert
        assert(loc);

        Span sTmp{s};
        Span prefix = sTmp.before(pos);
        Span oldSuffix = loc->span.from(pos);
        Span newSuffix = sTmp.from(pos);
        Node* incNode = loc;

        if (!oldSuffix.empty())
        {
            // new is a prefix of current
            // e.g. abcdef->..., adding abcd
            //    becomes abcd->ef->...

            // Create oldSuffix node that takes over loc
            std::unique_ptr<Node> newNode{std::make_unique<Node>(oldSuffix)};
            newNode->tipSupport = loc->tipSupport;
            newNode->branchSupport = loc->branchSupport;
            using std::swap;
            swap(newNode->children, loc->children);

            // Loc truncates to prefix and newNode is its child
            loc->span = prefix;
            newNode->parent = loc;
            loc->children.emplace_back(std::move(newNode));
            loc->tipSupport = 0;
        }
        if (!newSuffix.empty())
        {
            //  current is a substring of new
            // e.g.  abc->... adding abcde
            // ->   abc->  ...
            //          -> de

            std::unique_ptr<Node> newNode{std::make_unique<Node>(newSuffix)};
            newNode->parent = loc;
            // increment support starting from the new node
            incNode = newNode.get();
            loc->children.push_back(std::move(newNode));
        }

        incNode->tipSupport++;
        while (incNode)
        {
            ++incNode->branchSupport;
            incNode = incNode->parent;
        }
    }

    // Decreasing tip support
    // @return whether it was removed (last tip support)
    bool
    remove(std::string const & s)
    {
        // Cannot remove empty element
        if(s.empty())
            return false;

        Node* loc;
        std::size_t pos;
        std::tie(loc, pos) = find(s);

        if (loc)
        {
            // Must be exact match with tip support
            if (pos == loc->span.end() && pos == s.size() &&
                loc->tipSupport > 0)
            {
                loc->tipSupport--;

                Node * decNode = loc;
                while (decNode)
                {
                    --decNode->branchSupport;
                    decNode = decNode->parent;
                }

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
                        loc->span = combine(loc->span, child->span);
                    }
                }
                return true;
            }
        }
        return false;
    }

    std::uint32_t
    tipSupport(std::string const& s) const
    {
        Node const* loc;
        std::size_t pos;
        std::tie(loc, pos) = find(s);

        // Exact match
        if (loc && pos == loc->span.end() && pos == s.size())
            return loc->tipSupport;
        return 0;
    }

    std::uint32_t
    branchSupport(std::string const & s) const
    {
        Node const * loc;
        std::size_t pos;
        std::tie(loc,pos) = find(s);

        // Prefix or exact match
        if (loc && pos <= s.size() && s.size() <= loc->span.end())
            return loc->branchSupport;
        return 0;
    }

    std::string
    getPreferred()
    {
        Node * curr = root.get();

        bool done = false;
        std::uint32_t prefixSupport = curr->tipSupport;
        while(curr && !done)
        {
            // If the best child has margin exceeding the prefix  support
            // continue from that child, otherwise we are done

            Node * best = nullptr;
            std::uint32_t margin = 0;

            if(curr->children.size() == 1)
            {
                best = curr->children[0].get();
                margin = best->branchSupport;
            }
            else if (!curr->children.empty())
            {
                // sort placing children with largest branch support in the
                // front, breaking ties with tieBreaker field
                std::partial_sort(
                    curr->children.begin(),
                    curr->children.begin() + 2,
                    curr->children.end(),
                    [](std::unique_ptr<Node> const& a,
                       std::unique_ptr<Node> const& b) {
                        return std::tie(
                                   a->branchSupport, a->span.tieBreaker()) >
                            std::tie(b->branchSupport, b->span.tieBreaker());
                    });

                best = curr->children[0].get();
                margin = curr->children[0]->branchSupport -
                    curr->children[1]->branchSupport;

                // If best holds the tie-breaker, it has a one larger margin
                // since the second best needs additional branchSupport
                // to overcome the tie
                if (best->span.tieBreaker() >
                    curr->children[1]->span.tieBreaker())
                    margin++;
            }

            if (best && ((margin > prefixSupport) || (prefixSupport == 0)))
            {
                prefixSupport += best->tipSupport;
                curr = best;
            }
            else // current is the best
                done = true;
        }
        return curr->span.fullStr();
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
            // unless it is the root node
            if (curr != root.get() && curr->tipSupport == 0 &&
                curr->children.size() < 2)
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
        using namespace std::string_literals;
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
        using namespace std::string_literals;
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
        using namespace std::string_literals;
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
        using namespace std::string_literals;
        // Empty
        {
            CompressedTrie t;
            BEAST_EXPECT(t.getPreferred().empty());
        }
        // Single node no children
        {
            CompressedTrie t;
            t.insert("abc");
            BEAST_EXPECT(t.getPreferred() == "abc");
        }
        // Single node smaller child support
        {
            CompressedTrie t;
            t.insert("abc");
            t.insert("abcd");
            BEAST_EXPECT(t.getPreferred() == "abc");

            t.insert("abc");
            BEAST_EXPECT(t.getPreferred() == "abc");
        }
        // Single node larger child
        {
            CompressedTrie t;
            t.insert("abc");
            t.insert("abcd");
            t.insert("abcd");
            BEAST_EXPECT(t.getPreferred() == "abcd");
        }
        // Single node smaller children support
        {
            CompressedTrie t;
            t.insert("abc");
            t.insert("abcd");
            t.insert("abce");
            BEAST_EXPECT(t.getPreferred() == "abc");

            t.insert("abc");
            BEAST_EXPECT(t.getPreferred() == "abc");
        }
        // Single node larger children
        {
            CompressedTrie t;
            t.insert("abc");
            t.insert("abcd");
            t.insert("abcd");
            t.insert("abce");
            BEAST_EXPECT(t.getPreferred() == "abc");
            t.insert("abcd");
            BEAST_EXPECT(t.getPreferred() == "abcd");

        }
        // Tie-breaker
        {
            CompressedTrie t;
            t.insert("abcd");
            t.insert("abcd");
            t.insert("abce");
            t.insert("abce");
            BEAST_EXPECT(t.getPreferred() == "abce");

            t.insert("abcd");
            BEAST_EXPECT(t.getPreferred() == "abcd");
        }

        // Tie-breaker not needed
        {
            CompressedTrie t;
            t.insert("abc");
            t.insert("abcd");
            t.insert("abce");
            t.insert("abce");
            // abce only has a margin of 1, but it owns the tie-breaker
            BEAST_EXPECT(t.getPreferred() == "abce");

            t.remove("abc");
            t.insert("abcd");
            BEAST_EXPECT(t.getPreferred() == "abce");
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
            t.insert("abcde");
            BEAST_EXPECT(t.getPreferred() == "abcde");
        }
    }


    void
    testRootRelated()
    {
        using namespace std::string_literals;
        // Since the root is a special node that breaks the no-single child
        // invariant, do some tests that exercise it.

        CompressedTrie t;
        BEAST_EXPECT(!t.remove(""));
        BEAST_EXPECT(t.branchSupport("") == 0);
        BEAST_EXPECT(t.tipSupport("") == 0);

        t.insert("a");
        BEAST_EXPECT(t.checkInvariants());
        BEAST_EXPECT(t.branchSupport("") == 1);
        BEAST_EXPECT(t.tipSupport("") == 0);

        t.insert("e");
        BEAST_EXPECT(t.checkInvariants());
        BEAST_EXPECT(t.branchSupport("") == 2);
        BEAST_EXPECT(t.tipSupport("") == 0);

        BEAST_EXPECT(t.remove("e"));
        BEAST_EXPECT(t.checkInvariants());
        BEAST_EXPECT(t.branchSupport("") == 1);
        BEAST_EXPECT(t.tipSupport("") == 0);
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
};  // namespace test

BEAST_DEFINE_TESTSUITE(CompressedTrie, ripple_basics, ripple);

}  // namespace test
}  // namespace ripple
