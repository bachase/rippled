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
#include <ripple/basics/UnorderedContainers.h>
#include <ripple/beast/unit_test.h>
#include <memory>
#include <set>
#include <test/csf/Validation.h>
#include <test/csf/ledgers.h>

namespace ripple {
namespace test {
namespace csf {

/** Ancestry trie of ledgers

    Combination of a compressed trie and merkle-ish tree that maintains
    validation support of recent ledgers based on their ancestry.

    The compressed trie structure comes from recognizing that ledger history
    can be viewed as a string over the alphabet of ledger hashes. That is,
    a given ledger with sequence number `seq` defines a length `seq` string,
    with i-th entry equal to the hash of the ancestor ledger with sequence number
    i. "Sequence" strings with a common prefix share those ancestor ledgers in
    common. Tracking this ancestry information and relations across all validated
    ledgers is done conveniently in a compressed trie. A node in the trie is
    an ancestor of all its children. If a parent has sequence number `seq`, each
    child has a different ledger starting at `seq+1`. The compression comes
    from the invariant that any non-root node (with 0 tip support) has either no
    children or multiple children. In other words, a non-root 0-tip-support node
    can be combined with its single child.

    The merkle-ish property is based on the branch support calculation. Each
    node has a tipSupport, which is the number of current validations for that
    particular ledger. The branch support is the sum of the tip support and
    the branch support of that node's children:
        node->branchSupport = node->tipSupport
                            + sum_(child : node->children) child->branchSupport
    This is analagous to the merkle tree property in which a nodes hash is
    the hash of the concatenation of its child node hashes.
*/
class LedgerTrie
{
    // Span of ledger history
    class Span
    {
        // The span is the half-open interval [start,end) of ledger_
        Ledger::Seq start_;
        Ledger::Seq end_;
        Ledger const ledger_;
    public:
        Span(Ledger s)
            : start_{0}
            , end_{ledger_.seq() + Ledger::Seq{1}}
            , ledger_{std::move(s)}
        {

        }

        Span() : start_{0}, end_{0}, ledger_{}
        {
        }

        Ledger::Seq
        end() const
        {
            return end_;
        }

        // Return a view of this string starting from offset spot.
        Span
        from(Ledger::Seq spot)
        {
            return sub(spot, end_);
        }

        // Return a view of this string ending (before) at offset spot
        Span
        before(Ledger::Seq spot)
        {
            return sub(start_, spot);
        }

        bool
        empty() const
        {
            return start_ >= end_;
        }

        Ledger::ID
        id() const
        {
            return ledger_.id();
        }

        Ledger::Seq
        mismatch(Ledger const & o) const
        {
            // TODO : Find the first mismatching Ledger sequence
            assert(false);
            return Ledger::Seq{0};
        }

    private:
       Span(Ledger::Seq start, Ledger::Seq end, Ledger const & l)
            : start_{start}, end_{end}, ledger_{l}
        {
            assert(start <= end);
        }

        // Return a span of this over the half-open interval (from,to]
        Span
        sub(Ledger::Seq from, Ledger::Seq to)
        {
            auto clamp = [&](Ledger::Seq val) {
                return std::min(std::max(start_, val), end_);
            };

            return Span(clamp(from), clamp(to), ledger_);
        }

        friend std::ostream&
        operator<<(std::ostream& o, Span const& s)
        {
            return o << s.ledger_.id();
        }

        friend Span
        combine(Span const& a, Span const& b)
        {
            // Return combined span, using ledger_ from longer span
            if (a.end_ < b.end_)
                return Span(std::min(a.start_, b.start_), b.end_, b.ledger_);

            return Span(std::min(a.start_, b.start_), a.end_, a.ledger_);
        }
    };

    // A node in the trie
    struct Node
    {
        Node() : span{}, tipSupport{0}, branchSupport{0}
        {
        }

        Node(Ledger const& l) : span{l}, tipSupport{1}, branchSupport{1}
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

    /** Find the node in the trie that represents the longest common ancetry
        with the given ledger.

        @return Pair of the found node and the sequence number of the first
                ledger difference.
    */
    std::pair<Node*, Ledger::Seq>
    find(Ledger const& ledger) const
    {
        Node* curr = root.get();

        // Root is always defined and is a prefix of all strings
        assert(curr);
        Ledger::Seq pos = curr->span.mismatch(ledger);

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
    LedgerTrie() : root{std::make_unique<Node>()}
    {
    }

    /** Insert and increment the support for the given ledger.
    */
    void
    insert(Ledger const & ledger)
    {
        Node* loc;
        Ledger::Seq pos;
        std::tie(loc, pos) = find(ledger);

        // There is always a place to insert
        assert(loc);

        Span sTmp{ledger};
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

    /** Decreasing tip support for a ledger, compressing if it is the
        final node

        @return Whether any ledger with tip support existed
    */
    bool
    remove(Ledger const & s)
    {
        Node* loc;
        Ledger::Seq pos;
        std::tie(loc, pos) = find(s);

        // Cannot remove root
        if (loc && loc != root.get())
        {
            // Must be exact match with tip support
            if (pos == loc->span.end() && pos > s.seq() && loc->tipSupport > 0)
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
    tipSupport(Ledger const& ledger) const
    {
        Node const* loc;
        Ledger::Seq pos;
        std::tie(loc, pos) = find(ledger);

        // Exact match
        if (loc && pos == loc->span.end() && pos > ledger.seq())
            return loc->tipSupport;
        return 0;
    }

    std::uint32_t
    branchSupport(Ledger const & ledger) const
    {
        Node const * loc;
        Ledger::Seq pos;
        std::tie(loc,pos) = find(ledger);

        // Prefix or exact match
        if (loc && pos <= ledger.seq() && s.seq() < loc->span.end())
            return loc->branchSupport;
        return 0;
    }

    Ledger::ID
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
                // front, breaking ties with the ledger ID
                std::partial_sort(
                    curr->children.begin(),
                    curr->children.begin() + 2,
                    curr->children.end(),
                    [](std::unique_ptr<Node> const& a,
                       std::unique_ptr<Node> const& b) {
                        return std::tie(a->branchSupport, a->span.id()) >
                            std::tie(b->branchSupport, b->span.id());
                    });

                best = curr->children[0].get();
                margin = curr->children[0]->branchSupport -
                    curr->children[1]->branchSupport;

                // If best holds the tie-breaker, it has a one larger margin
                // since the second best needs additional branchSupport
                // to overcome the tie
                if (best->span.id() > curr->children[1]->span.id())
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
        return curr->span.id();
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


}  // namespace csf


class ValidationsTree_test : public beast::unit_test::suite
{
    beast::Journal j;

    void
    run() override
    {

    }
};

BEAST_DEFINE_TESTSUITE(ValidationsTree, consensus, ripple);
}  // namespace test
}  // namespace ripple

