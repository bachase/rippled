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
#include <deque>
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

    struct Node;

    using NodePtr = std::shared_ptr<Node>;

    struct Node
    {
        Node(std::string const& s) : slice{s}, tipSupport{1}, branchSupport{1}
        {
        }

        Node(Slice s) : slice{std::move(s)}
        {
        }

        Slice slice;
        std::uint32_t tipSupport = 0;
        std::uint32_t branchSupport = 0;

        std::deque<NodePtr> children;
        NodePtr parent;

        void
        remove(NodePtr const& child)
        {
            children.erase(
                std::remove(children.begin(), children.end(), child),
                children.end());
        }
        friend std::ostream&
        operator<<(std::ostream& o, Node const& s)
        {
            return o << s.slice << "(T:" << s.tipSupport
                     << ",B:" << s.branchSupport << ")";
        }
    };

    NodePtr root;

    void
    incrementBranchSupport(NodePtr n)
    {
        while (n)
        {
            ++n->branchSupport;
            n = n->parent;
        }
    }

    void
    decrementBranchSupport(NodePtr n)
    {
        while (n)
        {
            --n->branchSupport;
            n = n->parent;
        }
    }

    NodePtr
    find(Slice s)
    {
        // find the node with longest common prefix of slice
        NodePtr curr = root;
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
                    [&](NodePtr const& child) {
                        return child->slice.ref[child->slice.start] ==
                            s.ref[child->slice.start];
                    });
                if (it != curr->children.end())
                    curr = *it;
                else
                    done = true;
            }
        }
        return curr;
    }

public:
    CompressedTrie() : root{std::make_unique<Node>("")}
    {
    }

    // Insert the given string and increase tip support
    void
    insert(std::string const& sIn)
    {
        Slice s{sIn};
        NodePtr loc = find(sIn);
        // loc is the node with the longest common prefix with sIn

        // determine where that prefix ends
        auto end = firstMismatch(loc->slice, s);

        Slice prefix = s.before(end);
        Slice oldSuffix = loc->slice.from(end);
        Slice newSuffix = s.from(end);
        NodePtr incrementNode = loc;

        if (!oldSuffix.empty())
        {
            // new is a substring of current
            // e.g. abcdef->..., adding abcd
            // -> abcd->ef->...

            // 1. Create ef node and take children ...
            NodePtr newNode{std::make_shared<Node>(oldSuffix)};
            newNode->tipSupport = loc->tipSupport;
            newNode->branchSupport = loc->branchSupport;
            // take existing children
            using std::swap;
            swap(newNode->children, loc->children);

            // 2. Turn old abcdef node into abcd and add ef as child
            loc->slice = prefix;
            newNode->parent = loc;
            loc->children.push_back(newNode);
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
            NodePtr newNode{std::make_shared<Node>(newSuffix)};
            newNode->parent = loc;
            loc->children.push_back(newNode);
            incrementNode = newNode;
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
        NodePtr loc = find(sIn);

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
                        NodePtr child = loc->children.front();
                        // Promote grand-children
                        loc->children.clear();
                        for (NodePtr const& grandChild : child->children)
                            loc->children.push_back(grandChild);
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
    tipSupport(std::string const& sIn)
    {
        Slice s{sIn};
        NodePtr loc = find(sIn);
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
    branchSupport(std::string const& sIn)
    {
        Slice s{sIn};
        NodePtr loc = find(sIn);
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

    // Helpers
    void
    dumpImpl(std::ostream& o, NodePtr const& curr, int offset) const
    {
        if (curr)
        {
            if (offset > 0)
                o << std::setw(offset) << "|-";

            std::stringstream ss;
            ss << *curr;
            o << ss.str() << std::endl;
            for (NodePtr const& child : curr->children)
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
    checkInvariants()
    {
        std::stack<NodePtr> nodes;
        nodes.push(root);
        while (!nodes.empty())
        {
            NodePtr curr = nodes.top();
            nodes.pop();

            // Node with 0 tip support must have multiple children
            if (curr->tipSupport == 0 && curr->children.size() < 2)
                return false;

            // branchSupport = tipSupport + sum(child->branchSupport)
            std::size_t support = curr->tipSupport;
            for (auto const& child : curr->children)
            {
                support += child->branchSupport;
                nodes.push(child);
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
    run()
    {
        testInsert();
        testRemove();
        testTipAndBranchSupport();

    }
};  // namespace test

BEAST_DEFINE_TESTSUITE(CompressedTrie, ripple_basics, ripple);

}  // namespace test
}  // namespace ripple
