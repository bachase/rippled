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
#if 0
namespace ripple {
namespace test {
namespace csf {

/** Ancestry tree of validated ledgers

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
    child has a different ledger at `seq+1`.


    The merkle-ish property is based on the branch support calculation. Each
    node has a tipSupport, which is the number of current validations for that
    particular ledger. The branch support is the sum of the tip support and
    the branch support of the nodes children:
        node->branchSupport = node->tipSupport
                            + sum_(child : node->children) child->branchSupport
    This is analagous to the merkle tree property in which a nodes hash is
    the hash of the concatenation of its child node hashes.

*/
class ValidationTree
{
    struct Node;
    using NodePtr = std::shared_ptr<Node>;

    struct Comparator
    {
        using is_transparent = std::true_type;

        bool
        operator()(NodePtr const& a, NodePtr const& b) const;

        bool
        operator()(NodePtr const& a, Ledger::ID const& b) const;

        bool
        operator()(Ledger::ID const& b, NodePtr const& a) const;
    };


    // Use SeqStr to be a (beginSeq,endSeq) and handle to ledger that covers that
    // range
    // Just do mismatch when getting common prefix?
    //  Worry about optimizing later

    /** Represents a ledger history sequence string which maps
        sequence numbers to the alphabet of ledger ids.

    */
    struct SeqStr
    {
        SeqStr(Ledger::Seq start_, Ledger::Seq stop_, Ledger ledger_)
            : start{start_}, stop{stop_}, ledger{ledger_}
        {
        }


        /** Return the sequence number of the last ledger in common
            Assuming ledgers up to and including agreeThrough are already
            in common.
        */
        Ledger::Seq
        commonPrefix(Ledger::Seq agreeThrough,  SeqStr const & other ) const
        {
            Ledger::Seq stop = std::min(ledger_.seq(), other.ledger_.seq());

            if(agreeThrough == stop)
                return agreeThrough;

            // We can do a binary search because
            // We have two ranges
            (s, ledger_.seq())
            (s,other.ledger_.seq())

        }

        Ledger::Seq const start;
        Ledger::Seq const stop;
        Ledger const ledger;
    };



    // Corresponds to the inclusive range of ledgers with sequence numbers
    //  (parent-> + 1, seq)
    // and corresponding hashes given by SeqStr.
    struct Node
    {
        SeqStr seqStr;

        std::uint16_t tipSupport = 0;
        std::uint32_t branchSupport = 0;

        NodePtr parent;

        // TODO: Consider flat_set; these are orderd by operator> below
        std::set<NodePtr, Comparator> children;

        Node(SeqStr seqStr_): seqStr(seqStr_)
        {
        }

        inline friend bool
        operator>(Node const& a, Node const& b)
        {
            return std::tie(a.branchSupport, a.id) >
                std::tie(b.branchSupport, b.id);
        }
    };

    NodePtr root;

    void
    incrementBranchSupport(NodePtr curr)
    {
        while(curr)
        {
            curr->branchSupport++;
            curr = curr->parent;
        }
    }

    void
    addImpl(NodePtr & curr, SeqStr seqStr)
    {
        // Pre-condition curr.seq() <= ledger.seq()
        SeqStr const & nodeSeqStr= curr->seqStr;

        assert(nodeSeqStr.stop <= seqStr.stop);

        if(curr->id == ledger.id())
        {
            curr->tipSupport++;
            incrementBranchSupport(curr);
        }
        else
        {

            auto commonPrefix = [&](NodePtr const & other)
            {
                auto minSeq = std::min(ledger.seq(), other->seq);

                // child represents (parent.seq() + 1 , child.seq())
                // if ledger.seq() < child.seq(), need
                if(ledger.seq() <= other->seq)
                {
                    // proper chain
//                    ledger[other->seq] == other->id;
                }
                else // other is longer than ledger
                {
                }

            };
            auto it = std::find_if(
                curr->children.begin(),
                curr->children.end(),
                [&](NodePtr const& child) { return false;});
            // No common prefix
            if(it == curr->children.end())
            {
                NodePtr newNode{
                    std::make_shared<Node>(ledger.id(), ledger.seq())};
                newNode->tipSupport = 1;
                newNode->branchSupport = 1;
                newNode->parent = curr;

                curr->children.emplace(std::move(newNode));
                incrementBranchSupport(curr);
            }
            // Common prefix with a child
            else
            {
                // split child at the common spot
            }
        }
    }
public:
    ValidationTree()
        : root{std::make_shared<Node>(SeqStr(Ledger::Seq{0}, Ledger::Seq{0}, Ledger{}))}
    {
    }

    // Return the ledger with the most support
    Ledger::ID
    getPreferred() const
    {
        assert (root.get() != nullptr);

        Node const * preferred = root.get();
        std::uint32_t latentSupport = preferred->tipSupport;
        bool done = false;

        while (!done && !preferred->children.empty())
        {
            auto it = preferred->children.begin();
            Node const * best = it->get();

            std::uint32_t margin = best->branchSupport;

            ++it;
            if (it != preferred->children.end())
            {
                Node const * nextBest = it->get();
                margin = margin - nextBest->branchSupport;
                if ((best->id > nextBest->id) && margin > 1)
                    margin = margin - 1;
            }

            if (margin > latentSupport)
            {
                preferred = best;
                latentSupport = latentSupport + preferred->tipSupport;
            }
            else
                done = true;
        }

        return preferred->id;
    }

    void
    add(Ledger const & ledger)
    {
        addImpl(root, SeqStr{Ledger::Seq{0},ledger.seq(), ledger});
    }

    void
    remove(Ledger const & ledger);

    void
    update(Ledger const & prior, Ledger const & curr);

    void
    dump(std::ostream & o);
};


}  // namespace csf


class ValidationsTree_test : public beast::unit_test::suite
{
    beast::Journal j;

    void
    run() override
    {
        using namespace csf;
        using namespace std::chrono;

        LedgerOracle oracle;
        Ledger genesis;

        Ledger curr = oracle.accept(
            genesis,
            TxSetType{},
            genesis.closeTimeResolution(),
            genesis.closeTime() + 3s);
        // 5 validators
        //ValidationTree tree(genesis, validators...);

        //
        // No validations yet -> take best
        // 1 validation take it
        // 2+ validation take majority, ties broken by id

        // Once fully validate
        // Switch to tree mode, but shouldn't change majority yet
        // Test 1, 2+ majority for next level of validations

        // Test more complex branching
        // Test advancing ledger separately?

        //BEAST_EXPECT(tree.getPreferred().id() == genesis.id());

    }
};

BEAST_DEFINE_TESTSUITE(ValidationsTree, consensus, ripple);
}  // namespace test
}  // namespace ripple
#endif