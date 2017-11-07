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
    public:
        std::size_t start;
        std::size_t stop;
        std::string ref;

        Span(std::size_t start_, std::size_t stop_, std::string const& r_)
            : start{start_}, stop{stop_}, ref{r_}
        {
            assert(start <= stop);
        }

        // Return a span of this over the half-open interval (from,to]
        Span
        sub(std::size_t from, std::size_t to)
        {
            auto clamp = [&](std::size_t val) {
                return std::min(std::max(start, val), stop);
            };

            return Span(clamp(from), clamp(to), ref);
        }

    public:

        Span(std::string s) : start{0}, stop{s.size()}, ref{std::move(s)}
        {

        }

        // Return a view of this string starting from offset spot.
        Span
        from(std::size_t spot)
        {
            return sub(spot, stop);
        }

        // Return a view of this string ending (before) at offset spot
        Span
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

        enum MatchType
        {
            None,      // The span has no overlap
            PrefixOf,  // The span is a prefix of the object
            SuffixOf,  // The span is a suffix of the object
            SuffixedBy,// The object fully matches the span, but has remaining data
            Full   // The span fully matches the object and has no remaining data
        };

        struct MatchResult
        {
            MatchType type;
            std::size_t pos; // The last matching position
        };

        std::size_t
        mismatch(std::string const & o) const
        {
            std::size_t mstop = std::min(stop, o.size());

            auto it = std::mismatch(
                          ref.begin() + start,
                          ref.begin() + mstop,
                          o.begin() + start)
                          .first;

            return (it - ref.begin());
        }

        friend std::ostream&
        operator<<(std::ostream& o, Span const& s)
        {
            return o << s.ref.substr(s.start, s.stop - s.start);
        }

        friend std::size_t
        firstMismatch(Span const& a, Span const& b)
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

        friend Span
        combine(Span const& a, Span const& b)
        {
            // Return combined span, using ref from longer span
            if (a.stop < b.stop)
                return Span(std::min(a.start, b.start), b.stop, b.ref);

            return Span(std::min(a.start, b.start), a.stop, a.ref);
        }
    };

    struct Node
    {
        Node() : span{""}, tipSupport{0}, branchSupport{0}
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

    // Find the node that has the longest prefix in common with Span
    std::pair<Node *, std::size_t>
    find(std::string const & s) const
    {
        Node * curr = root.get();

        // Root is always defined and is a prefix of all strings
        assert(curr);
        std::size_t pos = curr->span.mismatch(s);

        bool done = false;

        // Continue searching for a better span if the current position
        // matches the entire span
        while(!done && pos == curr->span.stop && pos < s.size())
        {
            done = true;
            // All children spans are disjoint, so we continue with the first
            // child that has a longer match
            for(std::unique_ptr<Node> const & child : curr->children)
            {
                auto childPos = child->span.mismatch(s);
                if(childPos > pos)
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

public:
    CompressedTrie() : root{std::make_unique<Node>()}
    {
    }

    // Insert the given string and increase tip support
    void
    insert(std::string const & s)
    {
        Node * loc;
        std::size_t pos;
        std::tie(loc,pos) = find(s);

        // determine where that prefix ends
        Span sTmp{s};
        Span prefix = sTmp.before(pos);
        Span oldSuffix = loc->span.from(pos);
        Span newSuffix = sTmp.from(pos);
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
            loc->span = prefix;
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
    remove(std::string const & s)
    {
        // Cannot remove an empty span
        if(s.empty())
            return false;

        Node * loc;
        std::size_t pos;
        std::tie(loc,pos) = find(s);

        // Find the *exact* matching node
        if (loc)
        {
            // must be exact match to remove
            if (loc->span.stop == s.size() &&
                pos == s.size() &&
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
                        loc->span = combine(loc->span, child->span);
                    }
                }
                return true;
            }
        }
        return false;
    }

    std::uint32_t
    tipSupport(std::string const & s) const
    {
        Node const * loc;
        std::size_t pos;
        std::tie(loc,pos) = find(s);

        if (loc)
        {
            // must be exact match
            if (loc->span.stop == s.size() && pos == s.size())
            {
                return loc->tipSupport;
            }
        }
        return 0;
    }

    std::uint32_t
    branchSupport(std::string const & s) const
    {
        Node const * loc;
        std::size_t pos;
        std::tie(loc,pos) = find(s);

        if (loc)
        {
            // must be prefix match
            // If s is longer than loc->span, no branch support exists
            if (pos <= s.size() &&
                s.size() <= loc->span.stop)
            {
                return loc->branchSupport;
            }
        }
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
                        return std::tie(a->branchSupport, a->span.tieBreaker()) >
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
