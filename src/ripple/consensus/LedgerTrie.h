//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2017 Ripple Labs Inc.

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

#ifndef RIPPLE_APP_CONSENSUS_LEDGERS_TRIE_H_INCLUDED
#define RIPPLE_APP_CONSENSUS_LEDGERS_TRIE_H_INCLUDED

#include <algorithm>
#include <memory>
#include <vector>
#include <ripple/json/json_value.h>

namespace ripple {

/** Ancestry trie of ledgers

    Combination of a compressed trie and merkle-ish tree that maintains
    validation support of recent ledgers based on their ancestry.

    The compressed trie structure comes from recognizing that ledger history
    can be viewed as a string over the alphabet of ledger ids. That is,
    a given ledger with sequence number `seq` defines a length `seq` string,
    with i-th entry equal to the id of the ancestor ledger with sequence
    number i. "Sequence" strings with a common prefix share those ancestor
    ledgers in common. Tracking this ancestry information and relations across
    all validated ledgers is done conveniently in a compressed trie. A node in
    the trie is an ancestor of all its children. If a parent has sequence number
    `seq`, each child has a different ledger starting at `seq+1`. The compression
    comes from the invariant that any non-root node (with 0 tip support) has
    either no children or multiple children. In other words, a non-root
    0-tip-support node can be combined with its single child.

    The merkle-ish property is based on the branch support calculation. Each
    node has a tipSupport, which is the number of current validations for that
    particular ledger. The branch support is the sum of the tip support and
    the branch support of that node's children:
        node->branchSupport = node->tipSupport
                            + sum_(child : node->children) child->branchSupport
    This is analagous to the merkle tree property in which a node's hash is
    the hash of the concatenation of its child node hashes.

    The templated Ledger type represents a ledger which has a unique history.
    It should be lightweight and cheap to copy.

       @code
       // Identifier types that should be equality-comparable and copyable
       struct ID;
       struct Seq;

       struct Ledger
       {
          // The default ledger represents a ledger that prefixes all other ledgers
          // (aka the genesis ledger)
          Ledger();

          Ledger(Ledger &);
          Ledger& operator=(Ledger );

          // Return the sequence number of this ledger
          Seq seq() const;

          // Return the ID of this ledger's ancestor with given sequence number
          // or ID{0} if unknown
          ID
          operator[](Seq s);

       };

       // Return the sequence number of the first possible mismatching ancestor
       Seq
       mismatch(ledgerA, ledgerB);
       @endcode

    The unique history invariant of ledgers requires any ledgers that agree
    on the id of a given sequence number agree on ALL ancestors before that ledger

        @code
        Ledger a,b;
        // For all Seq s:
        if(a[s] == b[s]);
            for(Seq p = 0; p < s; ++p)
                assert(a[p] == b[p]);
        @endcode

    @tparam Ledger A type representing a specific history of ledgers
*/
template <class Ledger>
class LedgerTrie
{
    using Seq = typename Ledger::Seq;
    using ID = typename Ledger::ID;

    // Span of a ledger
    class Span
    {
        // The span is the half-open interval [start,end) of ledger_
        Seq start_{0};
        Seq end_{1};
        Ledger ledger_;

    public:
        Span()
        {
            // Require default ledger to be genesis seq
            assert(ledger_.seq() == start_);
        }

        Span(Ledger ledger)
            : start_{0}, end_{ledger.seq() + Seq{1}}, ledger_{std::move(ledger)}
        {
        }

        Span(Span const& s) = default;
        Span(Span&& s) = default;
        Span&
        operator=(Span const&) = default;
        Span&
        operator=(Span&&) = default;

        Seq
        end() const
        {
            return end_;
        }

        // Return the Span from (spot,end_]
        Span
        from(Seq spot)
        {
            return sub(spot, end_);
        }

        // Return the Span from (start_,spot]
        Span
        before(Seq spot)
        {
            return sub(start_, spot);
        }

        bool
        empty() const
        {
            return start_ == end_;
        }

        //Return the ID of the ledger that starts this span
        ID
        startID() const
        {
            return ledger_[start_];
        }

        // Return the ledger sequence number of the first possible difference
        //  between this span and a given ledger.
        Seq
        diff(Ledger const& o) const
        {
            return clamp(mismatch(ledger_, o));
        }

        std::pair<Seq, ID>
        tip() const
        {
            Seq tipSeq{end_ -Seq{1}};
            return {tipSeq, ledger_[tipSeq]};
        }

    private:
        Span(Seq start, Seq end, Ledger const& l)
            : start_{start}, end_{end}, ledger_{l}
        {
            assert(start <= end);
        }

        Seq
        clamp(Seq val) const
        {
            return std::min(std::max(start_, val), end_);
        };

        // Return a span of this over the half-open interval [from,to)
        Span
        sub(Seq from, Seq to)
        {
            return Span(clamp(from), clamp(to), ledger_);
        }

        friend std::ostream&
        operator<<(std::ostream& o, Span const& s)
        {
            return o << s.tip().second << "(" << s.start_ << "," << s.end_
                     << "]";
        }

        friend Span
        merge(Span const& a, Span const& b)
        {
            // Return combined span, using ledger_ from higher sequence span
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
        Node* parent = nullptr;

        void
        erase(Node const* child)
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

        Json::Value
        getJson() const
        {
            Json::Value res;
            res["id"] = to_string(span.tip().second);
            res["seq"] = static_cast<std::uint32_t>(span.tip().first);
            res["tipSupport"] = tipSupport;
            res["branchSupport"] = branchSupport;
            if(!children.empty())
            {
                Json::Value &cs = (res["children"] = Json::arrayValue);
                for(auto const & child : children)
                {
                    cs.append(child->getJson());
                }
            }
            return res;
        }
    };

    // The root of the trie. The root is allowed to break the no-single child
    // invariant.
    std::unique_ptr<Node> root;

    /** Find the node in the trie that represents the longest common ancetry
        with the given ledger.

        @return Pair of the found node and the sequence number of the first
                ledger difference.
    */
    std::pair<Node*, Seq>
    find(Ledger const& ledger) const
    {
        Node* curr = root.get();

        // Root is always defined and is in common with all ledgers
        assert(curr);
        Seq pos = curr->span.diff(ledger);

        bool done = false;

        // Continue searching for a better span as long as the current position
        // matches the entire span
        while (!done && pos == curr->span.end())
        {
            done = true;
            // Find the child with the longest ancestry match
            for (std::unique_ptr<Node> const& child : curr->children)
            {
                auto childPos = child->span.diff(ledger);
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

    void
    dumpImpl(std::ostream& o, std::unique_ptr<Node> const& curr, int offset)
        const
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

public:
    LedgerTrie() : root{std::make_unique<Node>()}
    {
    }

    /** Insert and increment the support for the given ledger.

        @param ledger A ledger and its ancestry
        @param count The count of support for this ledger
     */
    void
    insert(Ledger const& ledger, std::uint32_t count = 1)
    {
        Node* loc;
        Seq diffSeq;
        std::tie(loc, diffSeq) = find(ledger);

        // There is always a place to insert
        assert(loc);

        Span lTmp{ledger};
        Span prefix = lTmp.before(diffSeq);
        Span oldSuffix = loc->span.from(diffSeq);
        Span newSuffix = lTmp.from(diffSeq);
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
            for(std::unique_ptr<Node> & child : newNode->children)
                child->parent = newNode.get();

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

        incNode->tipSupport += count;
        while (incNode)
        {
            incNode->branchSupport += count;
            incNode = incNode->parent;
        }
    }

    /** Decrease tip support for a ledger, compressing if possible.

        @param ledger The ledger history to remove
        @param count The amount of tip support to remove

        @return Whether a node was erased as a result
    */
    bool
    remove(Ledger const& ledger, std::uint32_t count = 1)
    {
        Node* loc;
        Seq diffSeq;
        std::tie(loc, diffSeq) = find(ledger);

        // Cannot erase root
        if (loc && loc != root.get())
        {
            // Must be exact match with tip support
            if (diffSeq == loc->span.end() && diffSeq > ledger.seq() &&
                loc->tipSupport > 0)
            {
                count = std::min(count, loc->tipSupport);
                loc->tipSupport -= count;

                Node* decNode = loc;
                while (decNode)
                {
                    decNode->branchSupport -= count;
                    decNode = decNode->parent;
                }

                if (loc->tipSupport == 0)
                {
                    if (loc->children.empty())
                    {
                        // this node can be erased
                        loc->parent->erase(loc);
                    }
                    else if (loc->children.size() == 1)
                    {
                        // This node can be combined with its child
                        std::unique_ptr<Node> child =
                            std::move(loc->children.front());
                        child->span = merge(loc->span, child->span);
                        child->parent = loc->parent;
                        loc->parent->children.emplace_back(std::move(child));
                        loc->parent->erase(loc);
                    }
                }
                return true;
            }
        }
        return false;
    }

    /** Return count of support for the specific ledger.
     */
    std::uint32_t
    tipSupport(Ledger const& ledger) const
    {
        Node const* loc;
        Seq diffSeq;
        std::tie(loc, diffSeq) = find(ledger);

        // Exact match
        if (loc && diffSeq == loc->span.end() && diffSeq > ledger.seq())
            return loc->tipSupport;
        return 0;
    }

    /** Return the count of branch support for the specific ledger
     */
    std::uint32_t
    branchSupport(Ledger const& ledger) const
    {
        Node const* loc;
        Seq diffSeq;
        std::tie(loc, diffSeq) = find(ledger);

        // Check that ledger is is an exact match or proper
        // prefix of loc
        if (loc && diffSeq > ledger.seq() &&
            ledger.seq() < loc->span.end())
        {
            return loc->branchSupport;
        }
        return 0;
    }

    /** Return the preferred ledger ID

        The preferred ledger is used to determine the working ledger
        for consensus amongst competing alternatives. In this case, tip
        support represents the count of validators most recently working on
        a particular ledger and branch support is the count of validators
        working on a ledger or one if its descendents.

        The preferred ledger is found by walking the ledger trie, choosing the
        child with the most branch support and continuing as long as any
        change in minority support *can not* change which child has the
        most branch support. Ties between siblings are broken using the highest
        ledger ID.
    */
    std::pair<Seq,ID>
    getPreferred()
    {
        Node* curr = root.get();

        bool done = false;
        std::uint32_t prefixSupport = curr->tipSupport;
        while (curr && !done)
        {
            // If the best child has margin exceeding the prefix support
            // continue from that child, otherwise we are done

            Node* best = nullptr;
            std::uint32_t margin = 0;

            if (curr->children.size() == 1)
            {
                best = curr->children[0].get();
                margin = best->branchSupport;
            }
            else if (!curr->children.empty())
            {
                // Sort placing children with largest branch support in the
                // front, breaking ties with the span's starting ID then tip
                std::partial_sort(
                    curr->children.begin(),
                    curr->children.begin() + 2,
                    curr->children.end(),
                    [](std::unique_ptr<Node> const& a,
                       std::unique_ptr<Node> const& b) {
                        return std::tie(a->branchSupport, a->span.startID()) >
                            std::tie(b->branchSupport, b->span.startID());
                    });

                best = curr->children[0].get();
                margin = curr->children[0]->branchSupport -
                    curr->children[1]->branchSupport;

                // If best holds the tie-breaker, gets one larger margin
                // since the second best needs additional branchSupport
                // to overcome the tie
                if (best->span.startID() > curr->children[1]->span.startID())
                    margin++;
            }

            if (best && ((margin > prefixSupport) || (prefixSupport == 0)))
            {
                // Prefix support is all the support not on the branch we
                // are moving to
                //       curr
                //     /  |  \
                //    A   B  best
                // At curr, the prefix support already includes the tip support
                // of curr and its ancestors, along with the branch support of
                // any of its siblings that are inconsistent.
                //
                // The additional prefix suppport that is carried to best is
                //   A->branchSupport + B->branchSupport + best->tipSupport
                // This is the amount of support that has not yet voted
                // on a descendent of best, or has voted on a conflicting
                // descendent and will switch to best in the future. This means
                // that they may support an arbitrary descendent of best.
                //
                // The calculation is simplified using
                //     A->branchSupport+B->branchSupport
                //               =  curr->branchSupport - best->branchSupport
                //                                      - curr->tipSupport
                //
                // This will not overflow by definition of the above quantities
                prefixSupport += (curr->branchSupport - best->branchSupport
                                 - curr->tipSupport) + best->tipSupport;

                curr = best;
            }
            else  // current is the best
                done = true;
        }
        return curr->span.tip();
    }

    /** Dump an ascii representation of the trie to the stream
     */
    void
    dump(std::ostream& o) const
    {
        dumpImpl(o, root, 0);
    }

    /** Dump JON representation of trie state
    */
    Json::Value
    getJson() const
    {
        return root->getJson();
    }

    /** Check the compressed trie and support invariants.
     */
    bool
    checkInvariants() const
    {
        std::stack<Node const*> nodes;
        nodes.push(root.get());
        while (!nodes.empty())
        {
            Node const* curr = nodes.top();
            nodes.pop();
            if (!curr)
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
                if(child->parent != curr)
                    return false;

                support += child->branchSupport;
                nodes.push(child.get());
            }
            if (support != curr->branchSupport)
                return false;
        }
        return true;
    }
};

}  // namespace ripple
#endif
