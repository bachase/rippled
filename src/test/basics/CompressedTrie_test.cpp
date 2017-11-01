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

    void
    insertImpl(NodePtr& curr, Slice s)
    {
        Slice const& currSlice = curr->slice;
        assert(currSlice.start == s.start);

        auto minStop = std::min(currSlice.stop, s.stop);

        auto it = std::mismatch(
                      currSlice.ref.begin(),
                      currSlice.ref.begin() + minStop,
                      s.ref.begin())
                      .first;
        auto firstDiff = it - currSlice.ref.begin();

        // firstDiff is in start, minStop
        // Matches entire string
        if (firstDiff == minStop)
        {
            // Same string!
            if (currSlice.stop == s.stop)
            {
                curr->tipSupport++;
                incrementBranchSupport(curr);
            }
            else
            {
            }
        }
        else  // less than entire match
        {
        }
    }

public:
    CompressedTrie() : root{std::make_unique<Node>("")}
    {
    }

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

    void
    remove(std::string const& s);

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
            if (firstMismatch(loc->slice, s) <= s.stop)
            {
                return loc->branchSupport;
            }
        }
        return 0;
    }

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
    checkSupport()
    {
        std::stack<NodePtr> nodes;
        nodes.push(root);
        while (!nodes.empty())
        {
            NodePtr curr = nodes.top();
            nodes.pop();
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
    testAdd()
    {
        CompressedTrie t;
        t.insert("abc");
        BEAST_EXPECT(t.checkSupport());
        BEAST_EXPECT(t.branchSupport("ab") == 1);
        BEAST_EXPECT(t.branchSupport("abc") == 1);
        BEAST_EXPECT(!t.branchSupport("abcd") == 0);
        BEAST_EXPECT(t.tipSupport("abc") == 1);

        // 1. Cases
        //
    }

    void
    run()
    {
        testAdd();

        CompressedTrie t;
        t.insert("abc");
        t.dump(std::cout);
        BEAST_EXPECT(t.checkSupport());
        t.insert("a");
        t.dump(std::cout);
        BEAST_EXPECT(t.checkSupport());
        t.insert("bhi");
        t.dump(std::cout);
        BEAST_EXPECT(t.checkSupport());
        t.insert("ad");
        t.dump(std::cout);
        BEAST_EXPECT(t.checkSupport());
        t.insert("abcwz");
        t.dump(std::cout);
        BEAST_EXPECT(t.checkSupport());
        t.insert("abc23");
        t.dump(std::cout);
    }
};

BEAST_DEFINE_TESTSUITE(CompressedTrie, ripple_basics, ripple);

}  // namespace test
}  // namespace ripple
