//------------------------------------------------------------------------------
/*
  This file is part of rippled: https://github.com/ripple/rippled
  Copyright (c) 2012-2016 Ripple Labs Inc.

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
#include <test/jtx.h>
#include <test/jtx/Env.h>
#include <ripple/beast/unit_test.h>
#include <ripple/beast/core/LexicalCast.h>
#include <ripple/protocol/JsonFields.h>
#include <ripple/protocol/SField.h>
#include <boost/filesystem.hpp>
#include <boost/scope_exit.hpp>
#include <ripple/basics/StringUtilities.h>
#include <fstream>

namespace ripple {

extern std::array<char const*,20> ledgerXRPDiscrepancyData;

class Discrepancy_test : public beast::unit_test::suite
{
    // This is a legacy test ported from js/coffee. It starts with
    // a saved ledger, submits a transaction as a blob and then queries
    // that transaction to verify that the net of balance changes match
    // the fee charged.
    void
    testXRPDiscrepancy ()
    {
        testcase ("Discrepancy test : XRP Discrepancy");
        using namespace test::jtx;

        boost::system::error_code ec;
        // create a temporary path to write DB files to and
        // our starting ledger data
        auto path  = boost::filesystem::temp_directory_path(ec);
        BEAST_EXPECTS(!ec, ec.message());
        path /= boost::filesystem::unique_path("%%%%-%%%%-%%%%-%%%%", ec);
        BEAST_EXPECTS(!ec, ec.message());
        boost::filesystem::create_directories(path, ec);
        BEAST_EXPECTS(!ec, ec.message());

        auto ledgerFile = path / "ledgerdata.json";
        std::ofstream o (ledgerFile.string(), std::ios::out | std::ios::trunc);
        for(auto p : ledgerXRPDiscrepancyData)
            o << p;
        o.close();

        {
            BOOST_SCOPE_EXIT( (&path) (&ec) ) {
                boost::filesystem::remove_all(path, ec);
            } BOOST_SCOPE_EXIT_END

            Env env(*this, [&]()
                {
                    auto p = std::make_unique<Config>();
                    test::setupConfigForUnitTests(*p);
                    p->START_LEDGER = ledgerFile.string();
                    p->START_UP = Config::LOAD_FILE;
                    p->legacy("database_path", path.string());
                    return p;
                }());

            Json::Value jrq;
            jrq[jss::id] = 1;
            jrq[jss::tx_blob] =
                "1200002200020000240000124E61D5438D7EA4C680000000000000000000000"
                "000004A50590000000000E5C92828261DBAAC933B6309C6F5C72AF020AFD468"
                "400000000000000A69D4D3E7809B4814C8000000000000000000000000434E5"
                "9000000000041C8BE2C0A6AA17471B9F6D0AF92AAB1C94D5A25732103FC5F96E"
                "A61889691EC7A56FB2B859B600DE68C0255BF580D5C22D02EB97AFCE47447304"
                "5022100D14B60BC6E01E5C19471F87EB00A4BFCA16D039BB91AEE12DA1142E8C"
                "4CAE7C2022020E2809CF24DE2BC0C3DCF1A07C469DB415F880485B2B323E5B5A"
                "A1D9F6F22D48114AFD96601692A6C6416DBA294F0DA684675A824B28314AFD96"
                "601692A6C6416DBA294F0DA684675A824B201123000000000000000000000000"
                "04A50590000000000E5C92828261DBAAC933B6309C6F5C72AF020AFD401E5C92"
                "828261DBAAC933B6309C6F5C72AF020AFD4FF100000000000000000000000000"
                "000000000000000300000000000000000000000004A50590000000000E5C9282"
                "8261DBAAC933B6309C6F5C72AF020AFD401E5C92828261DBAAC933B6309C6F5C"
                "72AF020AFD4FF01A034782E2DBAC4FB82B601CD50421E8EF24F3A00100000000"
                "000000000000000000000000000000000300000000000000000000000004A505"
                "90000000000E5C92828261DBAAC933B6309C6F5C72AF020AFD401E5C92828261"
                "DBAAC933B6309C6F5C72AF020AFD400";
            auto jrr = env.rpc ("json", "submit", to_string(jrq))[jss::result];
            auto hash = jrr[jss::tx_json][jss::hash];
            env.close();
            Json::Value jrq2;
            jrq2[jss::binary] = false;
            jrq2[jss::transaction] = hash;
            jrq2[jss::id] = 3;
            jrr = env.rpc ("json", "tx", to_string(jrq2))[jss::result];
            uint64_t fee { jrr[jss::Fee].asUInt() };
            auto meta = jrr[jss::meta];
            uint64_t sumPrev {0};
            uint64_t sumFinal {0};
            for(auto const& an : meta[sfAffectedNodes.fieldName])
            {
                Json::Value node;
                if(an.isMember(sfCreatedNode.fieldName))
                    node = an[sfCreatedNode.fieldName];
                else if(an.isMember(sfModifiedNode.fieldName))
                    node = an[sfModifiedNode.fieldName];
                else if(an.isMember(sfDeletedNode.fieldName))
                    node = an[sfDeletedNode.fieldName];

                if(node && node[sfLedgerEntryType.fieldName] == "AccountRoot")
                {
                    Json::Value prevFields =
                        node.isMember(sfPreviousFields.fieldName) ?
                        node[sfPreviousFields.fieldName] :
                        node[sfNewFields.fieldName];
                    Json::Value finalFields =
                        node.isMember(sfFinalFields.fieldName) ?
                        node[sfFinalFields.fieldName] :
                        node[sfNewFields.fieldName];
                    if(prevFields)
                        sumPrev += beast::lexicalCastThrow<std::uint64_t>(
                            prevFields[sfBalance.fieldName].asString());
                    if(finalFields)
                        sumFinal += beast::lexicalCastThrow<std::uint64_t>(
                            finalFields[sfBalance.fieldName].asString());
                }
            }
            // the difference in balances (final and prev) should be the
            // fee charged
            BEAST_EXPECT(sumPrev-sumFinal == fee);
        }
    }

public:
    void run ()
    {
        testXRPDiscrepancy ();
    }
};

BEAST_DEFINE_TESTSUITE (Discrepancy, app, ripple);

// STARTUP ledger data used to configure testXRPDiscrepancy
// because of limitations on max char* data size, this is
// split into chunks smaller than 16k bytes
char const* ld_01 = R"LDGER01(
{
    "accepted": true,
    "accountState": [
        {
            "Account": "rE46UhBPrBmWAbuthcEgVL4dQs3khM4fnP",
            "Balance": "113009977",
            "Flags": 0,
            "LedgerEntryType": "AccountRoot",
            "OwnerCount": 4,
            "PreviousTxnID": "EA44F2B2B152BD453158F822034FEB6B6334DAE4CF838113702C248AEC7DF9FF",
            "PreviousTxnLgrSeq": 5204961,
            "Sequence": 40,
            "index": "02351577B355E0FEE2D18335B26FE1B6CF533424DF6E403198151DBD7E28D9A0"
        },
        {
            "Balance": {
                "currency": "JPY",
                "issuer": "rrrrrrrrrrrrrrrrrrrrBZbvji",
                "value": "0.995734367933"
            },
            "Flags": 65536,
            "HighLimit": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "0"
            },
            "HighNode": "0000000000000000",
            "LedgerEntryType": "RippleState",
            "LowLimit": {
                "currency": "JPY",
                "issuer": "ratarRi5YVgBDTHbt7rTPdmCMehH6zge2T",
                "value": "40000"
            },
            "LowNode": "0000000000000000",
            "PreviousTxnID": "B8BC6085A089C9DE96F67E7D452F209285796836761DB6DFE21BFAE38591C610",
            "PreviousTxnLgrSeq": 6198923,
            "index": "035E4A8D4AD8A2A96C555AED16C6D3D6E67026A659998341D2E2980393E3752B"
        },
        {
            "Balance": {
                "currency": "JPY",
                "issuer": "rrrrrrrrrrrrrrrrrrrrBZbvji",
                "value": "0"
            },
            "Flags": 1114112,
            "HighLimit": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "0"
            },
            "HighNode": "0000000000000000",
            "LedgerEntryType": "RippleState",
            "LowLimit": {
                "currency": "JPY",
                "issuer": "rDr83LqpsNJtJ9CouPxwf2pFEhdEuoCM4z",
                "value": "150000"
            },
            "LowNode": "0000000000000000",
            "PreviousTxnID": "0ABE22D9A191DA45F508E384693CCD6CA95926B07D99E1C894E255CE5401F6C0",
            "PreviousTxnLgrSeq": 6226014,
            "index": "03A00CD40E296C363D33515A1F2867D62B7059E7D2DA146993F8AB6A986EFDFD"
        },
        {
            "Flags": 0,
            "Indexes": [
                "035E4A8D4AD8A2A96C555AED16C6D3D6E67026A659998341D2E2980393E3752B",
                "AEA8F9EBE0F130645D376D673A2A95695FC726541C6F4267DD2DD94722D7BF45"
            ],
            "LedgerEntryType": "DirectoryNode",
            "Owner": "ratarRi5YVgBDTHbt7rTPdmCMehH6zge2T",
            "RootIndex": "03E1DBC015C67E43030B88AD52B966C4584A3A95E23AAB5F54F686853E19CD69",
            "index": "03E1DBC015C67E43030B88AD52B966C4584A3A95E23AAB5F54F686853E19CD69"
        },
        {
            "Account": "rajrdNafcXefrq4pYW1YAjMUDoxttSLefM",
            "BookDirectory": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04E1CC6E836AE4000",
            "BookNode": "0000000000000000",
            "Flags": 131072,
            "LedgerEntryType": "Offer",
            "OwnerNode": "0000000000000000",
            "PreviousTxnID": "1ABBD4DEF52440E0A769F41B3972E6F17009F735F2233C8FB02C092AA7E05EE8",
            "PreviousTxnLgrSeq": 6200150,
            "Sequence": 165,
            "TakerGets": "1000000000",
            "TakerPays": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "810"
            },
            "index": "043B696FC70C6D48A30808CE1DC45A8495A3F672FD6544113D2610310290315E"
        },
        {
            "Account": "rpvawRMyKug1gdTCbJWGtHs4yNzHMgcg22",
            "Balance": "77045728291",
            "Flags": 0,
            "LedgerEntryType": "AccountRoot",
            "OwnerCount": 14,
            "PreviousTxnID": "5C3650AC38E269CB9CF56CB9A5C099DEFA4B299F5B19F4F79B04917EBABC2E3F",
            "PreviousTxnLgrSeq": 6212451,
            "Sequence": 124,
            "index": "0796432F52D0AE3389F598AD996C0008B7EDEA3064F63A423E5884048EF9A4F8"
        },
        {
            "Flags": 0,
            "Indexes": [
                "B2490D6B802B0A5CA82C91EADE4504E893B8A20E732BD16F6A0EB43F36191356",
                "B7935B2A2140B443F557B32CA75EC71F64CAD9CF630CAC9D251E2B536980B98B"
            ],
            "LedgerEntryType": "DirectoryNode",
            "Owner": "rw7dJmysoqzguZDYyULBh5HqXdNQikMDtc",
            "RootIndex": "09995E3BA3A62690922DCF73B705AAA63E192712D88ED83FCF15459E0DD6556A",
            "index": "09995E3BA3A62690922DCF73B705AAA63E192712D88ED83FCF15459E0DD6556A"
        },
        {
            "Balance": {
                "currency": "JPY",
                "issuer": "rrrrrrrrrrrrrrrrrrrrBZbvji",
                "value": "4113.600003268848"
            },
            "Flags": 1114112,
            "HighLimit": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "0"
            },
            "HighNode": "0000000000000000",
            "LedgerEntryType": "RippleState",
            "LowLimit": {
                "currency": "JPY",
                "issuer": "rajrdNafcXefrq4pYW1YAjMUDoxttSLefM",
                "value": "1000000"
            },
            "LowNode": "0000000000000000",
            "PreviousTxnID": "88AF273E065D064A154C8D24FBC5891B2670F71814688B44C62B47DAAE717C77",
            "PreviousTxnLgrSeq": 6218862,
            "index": "0AED9B95367D6366D950E58E0884DBA139700217A105D60B3D2616625A0E2F06"
        },
        {
            "Balance": {
                "currency": "JPY",
                "issuer": "rrrrrrrrrrrrrrrrrrrrBZbvji",
                "value": "0"
            },
            "Flags": 65536,
            "HighLimit": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "0"
            },
            "HighNode": "0000000000000000",
            "LedgerEntryType": "RippleState",
            "LowLimit": {
                "currency": "JPY",
                "issuer": "rNruDQQDBM117pzRob2Br211HJrFKxk3tB",
                "value": "200000"
            },
            "LowNode": "0000000000000000",
            "PreviousTxnID": "F0ABD2B67746E06426FDA6DF84CCD25DC17031DEDEE81C0C67330CB6037A553E",
            "PreviousTxnLgrSeq": 5935310,
            "index": "0EFF1D95B5E694B18575969052DDF03A0041064F9B396757DCC0CEA286A8116D"
        },
        {
            "Account": "rHpoggSkNY7puahMUGVafWPZQ5JH8piZVQ",
            "Balance": "1349953110",
            "Flags": 0,
            "LedgerEntryType": "AccountRoot",
            "OwnerCount": 5,
            "PreviousTxnID": "7B4EE05D265ABECAAF9D7EA65BEE6943571F03A77D1CD50AE01192F944C341ED",
            "PreviousTxnLgrSeq": 6226713,
            "Sequence": 4686,
            "index": "0FCD092467A9098926161CCE5331EA35DD2846CE7B98690B68D23B96505AA65F"
        },
        {
            "Account": "rU8axbJNWix3k3LCTXtL8T8LeFtv88ibMe",
            "Balance": "94999916",
            "Flags": 0,
            "LedgerEntryType": "AccountRoot",
            "OwnerCount": 5,
            "PreviousTxnID": "DFB708EB437C75FD9817A36A913B6134E0F3BD40B4593AFABFCDD67A108DD322",
            "PreviousTxnLgrSeq": 5980811,
            "Sequence": 8,
            "index": "1039DF8A54D0620CAE662E8940BA39B1E536DF6B406B508DADD01E4751A147BE"
        },
        {
            "Flags": 0,
            "Indexes": [
                "9991CD45AC741BE86C45242CDD6BF73010C160BBDE4FB45BA28326BE4B3A89FD",
                "B27436F9FC495FFE07D907E69B59BCFA4F5150BABCBC5DE9F5AFCD00A9E99236"
            ],
            "LedgerEntryType": "DirectoryNode",
            "Owner": "rE46UhBPrBmWAbuthcEgVL4dQs3khM4fnP",
            "RootIndex": "109471C1058E0C982E4A0D77152696FDCB0A9EEF62A89F6B33C3DB3CB3D91383",
            "index": "109471C1058E0C982E4A0D77152696FDCB0A9EEF62A89F6B33C3DB3CB3D91383"
        },
        {
            "Balance": {
                "currency": "JPY",
                "issuer": "rrrrrrrrrrrrrrrrrrrrBZbvji",
                "value": "185251.2635928249"
            },
            "Flags": 65536,
)LDGER01";

char const* ld_02 = R"LDGER02(
            "HighLimit": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "0"
            },
            "HighNode": "0000000000000000",
            "LedgerEntryType": "RippleState",
            "LowLimit": {
                "currency": "JPY",
                "issuer": "r38Mwd8s2gFevETqCK8e34JYfWBjLUB2nH",
                "value": "1200000"
            },
            "LowNode": "0000000000000000",
            "PreviousTxnID": "F845F45D1785EE124FD5E61D988D07608F159D8BD85F201FEDC888EFE722C933",
            "PreviousTxnLgrSeq": 6219200,
            "index": "10FD64419C1F9295E2FF339DB45BA2DE5B20D8C2A0E45A3B3C46DBD3C8D41731"
        },
        {
            "Account": "rDr83LqpsNJtJ9CouPxwf2pFEhdEuoCM4z",
            "Balance": "15886743339",
            "Flags": 0,
            "LedgerEntryType": "AccountRoot",
            "OwnerCount": 2,
            "PreviousTxnID": "0ABE22D9A191DA45F508E384693CCD6CA95926B07D99E1C894E255CE5401F6C0",
            "PreviousTxnLgrSeq": 6226014,
            "Sequence": 28,
            "index": "110776482F33D3A7D12305613167A5913C85240B6E889D6826CE5BBD07C36F3E"
        },
        {
            "Account": "rKL5uUYcpSGcsVe2Yen5okfhGvi4J57mcM",
            "Balance": "601534331",
            "Flags": 0,
            "LedgerEntryType": "AccountRoot",
            "OwnerCount": 3,
            "PreviousTxnID": "AE307992AF99A493C21530581BCED85F8EB2B8F5045DC3819FC9107D13DFC58A",
            "PreviousTxnLgrSeq": 6100833,
            "Sequence": 43,
            "index": "11C0AC881056E470829B22A67CD5B51A96DD9B08D0061F60F33BB7F489D6D12B"
        },
        {
            "Flags": 0,
            "Indexes": [
                "224FC7D1465450509CE761CE2AB02133F9E0C3DE6F1D2C7F4290FF33457D1D2E",
                "7051544FA7B0C129F741CB992C9CC769FCC3D605F57DA808536255C7AB147742"
            ],
            "LedgerEntryType": "DirectoryNode",
            "Owner": "r9RR643anesxNCoNkkuYfEfzpxZUeK5Qzw",
            "RootIndex": "13540B3C58E7AB759D8F1E5900AACE37E6B0378E1491ADE83B1A54C48B39AA34",
            "index": "13540B3C58E7AB759D8F1E5900AACE37E6B0378E1491ADE83B1A54C48B39AA34"
        },
        {
            "Account": "rUZjAUwatwbS2WHGYNNwbcv8QvYYq8QLC3",
            "BookDirectory": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB053038D7EA4C68000",
            "BookNode": "0000000000000000",
            "Flags": 131072,
            "LedgerEntryType": "Offer",
            "OwnerNode": "0000000000000000",
            "PreviousTxnID": "3745CD467F5700FB7F22961237DCB6D17F8EEE51163E329BD51C610822A4464A",
            "PreviousTxnLgrSeq": 6032550,
            "Sequence": 576,
            "TakerGets": "100000000",
            "TakerPays": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "1000000"
            },
            "index": "13833879B05DAB4D7E68EE2E1F7F810CED76692BE5B3F99F017C9056A8C65F05"
        },
        {
            "Balance": {
                "currency": "JPY",
                "issuer": "rrrrrrrrrrrrrrrrrrrrBZbvji",
                "value": "75995.00000056954"
            },
            "Flags": 65536,
            "HighLimit": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "0"
            },
            "HighNode": "0000000000000000",
            "LedgerEntryType": "RippleState",
            "LowLimit": {
                "currency": "JPY",
                "issuer": "rpvawRMyKug1gdTCbJWGtHs4yNzHMgcg22",
                "value": "100000"
            },
            "LowNode": "0000000000000000",
            "PreviousTxnID": "5C3650AC38E269CB9CF56CB9A5C099DEFA4B299F5B19F4F79B04917EBABC2E3F",
            "PreviousTxnLgrSeq": 6212451,
            "index": "13CBE718A9792D410BFB2294E46477E7669379ED126079100C3365925AAE1DBC"
        },
        {
            "Balance": {
                "currency": "JPY",
                "issuer": "rrrrrrrrrrrrrrrrrrrrBZbvji",
                "value": "0"
            },
            "Flags": 1114112,
            "HighLimit": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "0"
            },
            "HighNode": "0000000000000000",
            "LedgerEntryType": "RippleState",
            "LowLimit": {
                "currency": "JPY",
                "issuer": "r3Y3Hh7abFiS9sTgCenK2kk2iToRhFfNs6",
                "value": "100000"
            },
            "LowNode": "0000000000000000",
            "PreviousTxnID": "B6666CE556D4274D26629DD0495D056D075F271BDD273706903FDED29D922535",
            "PreviousTxnLgrSeq": 6122449,
            "index": "13E35A054213C6CA2F639631B6F0618A95081F8E958E99B86A625001EFF9B3BC"
        },
        {
            "Account": "rajrdNafcXefrq4pYW1YAjMUDoxttSLefM",
            "BookDirectory": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04E1AD2AF5C0DE000",
            "BookNode": "0000000000000000",
            "Flags": 131072,
            "LedgerEntryType": "Offer",
            "OwnerNode": "0000000000000000",
            "PreviousTxnID": "EFA7F0203C541DD1022C99526910F90B52ADDADA46CDEAD9436BEE90A9615723",
            "PreviousTxnLgrSeq": 6220042,
            "Sequence": 172,
            "TakerGets": "2000000000",
            "TakerPays": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "1510"
            },
            "index": "15483FA685F65E020C876D69BD01FA7DDB05A753C11B32C98494DE114B7943EF"
        },
        {
            "Account": "racJpvgLpaNQGKB8nhKd1gTEVVA1uQWRKs",
            "BookDirectory": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F03E871B540C000",
            "BookNode": "0000000000000000",
            "Flags": 131072,
            "LedgerEntryType": "Offer",
            "OwnerNode": "0000000000000000",
            "PreviousTxnID": "5517C1FD60F5E6F50C490541CCF9601083A5B0E1FBB481F606A55761AC978805",
            "PreviousTxnLgrSeq": 6043197,
            "Sequence": 92,
            "TakerGets": "301000000000",
            "TakerPays": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "331100"
            },
            "index": "181402989B9E8DA57D49E65310E69A63C1117B1B7D89E2A1E96492C033FD4BEE"
        },
        {
            "Flags": 0,
            "Indexes": [
                "1B5DBDA3A421462B06D53691B051031DB9275BAEEF1276CEDB07D0F29CB80279",
                "352C69FE9817C9627073D02C4BB7CB65EDB3A10B6293A2D5BA671DF9E34D7A41"
            ],
            "LedgerEntryType": "DirectoryNode",
            "Owner": "ra64fg3awuMmrXcVjdDbYzTLFGcWKW1FPc",
            "RootIndex": "19044F0D2E3BB0E22889B9EB4D64F1520F2E5E2019568FF96C8F74CC34D49B2B",
            "index": "19044F0D2E3BB0E22889B9EB4D64F1520F2E5E2019568FF96C8F74CC34D49B2B"
        },
        {
            "Balance": {
                "currency": "JPY",
                "issuer": "rrrrrrrrrrrrrrrrrrrrBZbvji",
                "value": "26.78615210063"
            },
            "Flags": 1114112,
            "HighLimit": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "0"
            },
            "HighNode": "0000000000000000",
            "LedgerEntryType": "RippleState",
            "LowLimit": {
                "currency": "JPY",
                "issuer": "ra64fg3awuMmrXcVjdDbYzTLFGcWKW1FPc",
                "value": "1000000000"
            },
            "LowNode": "0000000000000000",
            "PreviousTxnID": "A9B1A9C425A13CDA3D1810454ED1E547CF6BF7768CF310DBDFCE603B84C09EAC",
            "PreviousTxnLgrSeq": 5751204,
            "index": "1B5DBDA3A421462B06D53691B051031DB9275BAEEF1276CEDB07D0F29CB80279"
        },
        {
            "Account": "rho8mvSESSmVPkF4UiyF8pTJBGMcVx2Uv1",
            "Balance": "7585323746",
            "Flags": 0,
            "LedgerEntryType": "AccountRoot",
            "OwnerCount": 33,
            "PreviousTxnID": "E9ECF535C8AE63EC174E546808052659126D73DEECD8430C76B428D882FC5E90",
            "PreviousTxnLgrSeq": 6221033,
            "Sequence": 333,
            "index": "1DCFF62C0D68837A93A4E181374B712C44EF5B4C8D7233F7647335092D81D2D5"
        },
        {
            "Account": "rhTUpdUStwn7wPnzNMjHEfFgQacPC5eop1",
            "Balance": "24162624472",
)LDGER02";

char const* ld_03 = R"LDGER03(
            "Flags": 0,
            "LedgerEntryType": "AccountRoot",
            "OwnerCount": 3,
            "PreviousTxnID": "B8FA447BC272FA1ACE601F1943DAABEFA9A1928E53289CA846B8D581F7E89132",
            "PreviousTxnLgrSeq": 6130142,
            "Sequence": 16,
            "index": "1E1DF6292A8C762DCE50CEF74B517A972E99E4DD28585A84658F1D689CA9C07B"
        },
        {
            "Account": "r47GLMFhJPjshD65J8TJSWZJzM3jPHcJdZ",
            "BookDirectory": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F0E23C9F634C000",
            "BookNode": "0000000000000000",
            "Flags": 131072,
            "LedgerEntryType": "Offer",
            "OwnerNode": "0000000000000000",
            "PreviousTxnID": "0F761AA689D05369FE503CDDD3EA7B2922F00CF1C922D53598BC1E7A6FCD9124",
            "PreviousTxnLgrSeq": 5909039,
            "Sequence": 509,
            "TakerGets": "10000000000",
            "TakerPays": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "39800"
            },
            "index": "1E9215E83CF75C0AC80ABB3F434115E4992981544F009CAF14CAED53DFD79935"
        },
        {
            "Flags": 0,
            "Indexes": [
                "13CBE718A9792D410BFB2294E46477E7669379ED126079100C3365925AAE1DBC",
                "EDFFABC23B617EEE0A9F3C9224AD574AD94C99E35F1BC68500B4BD08C0A4B5C1"
            ],
            "LedgerEntryType": "DirectoryNode",
            "Owner": "rpvawRMyKug1gdTCbJWGtHs4yNzHMgcg22",
            "RootIndex": "1F1F2ABE02F5669F13E576D57FDEF8038328E5B9100A47908ED1C6D4278B58FA",
            "index": "1F1F2ABE02F5669F13E576D57FDEF8038328E5B9100A47908ED1C6D4278B58FA"
        },
        {
            "Balance": {
                "currency": "JPY",
                "issuer": "rrrrrrrrrrrrrrrrrrrrBZbvji",
                "value": "0.00000230386122"
            },
            "Flags": 65536,
            "HighLimit": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "0"
            },
            "HighNode": "0000000000000000",
            "LedgerEntryType": "RippleState",
            "LowLimit": {
                "currency": "JPY",
                "issuer": "rLqAVKdGpJt2XpNiF9QKTpjn3AGTQbc6u4",
                "value": "0"
            },
            "LowNode": "0000000000000000",
            "PreviousTxnID": "A9C4228666022C95A1828310DAFBCD24D7AB87A30A407A297A0A5A52E1056452",
            "PreviousTxnLgrSeq": 6217621,
            "index": "1F5482CD6E2A5CCD6902AA599FE63A635A5263C5D2E59A3C5697D0DD5C760B32"
        },
        {
            "Balance": {
                "currency": "JPY",
                "issuer": "rrrrrrrrrrrrrrrrrrrrBZbvji",
                "value": "891.057671702356"
            },
            "Flags": 1114112,
            "HighLimit": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "0"
            },
            "HighNode": "0000000000000000",
            "LedgerEntryType": "RippleState",
            "LowLimit": {
                "currency": "JPY",
                "issuer": "rKX9Rb3ygKmYYsQfYvv3KXmRuwJ3AoLbLq",
                "value": "110000"
            },
            "LowNode": "0000000000000000",
            "PreviousTxnID": "A007B808B165ACB9135263143AD3C47AED640D60842F25937BB8A428F433B51C",
            "PreviousTxnLgrSeq": 6110463,
            "index": "20E49A1185CBB556D55EDFD054162F5833ABE8889E0B03247F79D1D8FA85F60A"
        },
        {
            "Flags": 0,
            "Indexes": [
                "3DE8A735E9996A3DB3093D85AC36DCEF135777EF6AE3C67337F9D1481FA83BBF",
                "C4CDCC5A64CF564982F17B71F2131A08DDBB6C9A4D041890BC6F763A1E49E05D",
                "23578AAA82674D543D587F948B2DCD33277AB7BCFEDC0E7132146D94EA9DA78C",
                "50C33C456676E0AF7B69397CFEE6612B59F9D294B2C7995023C2B8B748226F4A"
            ],
            "LedgerEntryType": "DirectoryNode",
            "Owner": "rP9tNSggJJGPNzUgtAZxaZmsWq8LGtKzYP",
            "RootIndex": "20F20388A770A3195330652190715D3B2261D4D27EB8FD13121F14A1397AF8F6",
            "index": "20F20388A770A3195330652190715D3B2261D4D27EB8FD13121F14A1397AF8F6"
        },
        {
            "Account": "rNruDQQDBM117pzRob2Br211HJrFKxk3tB",
            "BookDirectory": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F1550F7DCA70000",
            "BookNode": "0000000000000000",
            "Flags": 131072,
            "LedgerEntryType": "Offer",
            "OwnerNode": "0000000000000000",
            "PreviousTxnID": "C2F8DC84909AFFA2C13D00FEC41202104ED8D4159BE0751071AE1939D3C96C30",
            "PreviousTxnLgrSeq": 5368259,
            "Sequence": 21,
            "TakerGets": "200000000",
            "TakerPays": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "1200"
            },
            "index": "2213B6153CF9370D3A9EC3597C5C9AB6BFA3343C4CCA46AB13CD02C25EB7965A"
        },
        {
            "Account": "rwFdyL8LVBYkRu8nwkJNKiScRMZnjcrBu6",
            "Balance": "1550465943",
            "Flags": 0,
            "LedgerEntryType": "AccountRoot",
            "OwnerCount": 4,
            "PreviousTxnID": "A48E3D468A91FFC4607E10F234E4206B99C189D4FD4025664CBF2B34A486F027",
            "PreviousTxnLgrSeq": 6192032,
            "Sequence": 354,
            "index": "224906EABF0AF39D9326101D293EAFDC9F3C4CC0AAA432BD174465D2CBF482F4"
        },
        {
            "Balance": {
                "currency": "JPY",
                "issuer": "rrrrrrrrrrrrrrrrrrrrBZbvji",
                "value": "470055.577692946"
            },
            "Flags": 65536,
            "HighLimit": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "0"
            },
            "HighNode": "0000000000000000",
            "LedgerEntryType": "RippleState",
            "LowLimit": {
                "currency": "JPY",
                "issuer": "r9RR643anesxNCoNkkuYfEfzpxZUeK5Qzw",
                "value": "10000000"
            },
            "LowNode": "0000000000000000",
            "PreviousTxnID": "7B4EE05D265ABECAAF9D7EA65BEE6943571F03A77D1CD50AE01192F944C341ED",
            "PreviousTxnLgrSeq": 6226713,
            "index": "224FC7D1465450509CE761CE2AB02133F9E0C3DE6F1D2C7F4290FF33457D1D2E"
        },
        {
            "Account": "rP9tNSggJJGPNzUgtAZxaZmsWq8LGtKzYP",
            "BookDirectory": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F082BD67AFBC000",
            "BookNode": "0000000000000000",
            "Flags": 131072,
            "LedgerEntryType": "Offer",
            "OwnerNode": "0000000000000000",
            "PreviousTxnID": "92EC5FC57E64AC47AF2EF0014A6BF44BEABEBFFC4ABECDCEB85CD00799C21BAB",
            "PreviousTxnLgrSeq": 5421886,
            "Sequence": 48,
            "TakerGets": "1000000000",
            "TakerPays": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "2300"
            },
            "index": "23578AAA82674D543D587F948B2DCD33277AB7BCFEDC0E7132146D94EA9DA78C"
        },
        {
            "Account": "r3Y3Hh7abFiS9sTgCenK2kk2iToRhFfNs6",
            "Balance": "78726465020",
            "Flags": 0,
            "LedgerEntryType": "AccountRoot",
            "OwnerCount": 58,
            "PreviousTxnID": "B62B6225DE36D1D63A831621F0DE13920DD3DFD88ED3EAB4A2AEFAF15EC730EF",
            "PreviousTxnLgrSeq": 6226618,
            "Sequence": 1569,
            "index": "24D275236A95E6B6C8A165D93191D27DD60068268E33CDA141B7A52C2F3D167C"
        },
        {
            "Account": "rLqAVKdGpJt2XpNiF9QKTpjn3AGTQbc6u4",
            "Balance": "846122113272",
            "Flags": 0,
            "LedgerEntryType": "AccountRoot",
            "OwnerCount": 17,
            "PreviousTxnID": "6F8B6BB82D46B92C70AD1D1FEC160D974C383CCDEB0342EA97C465DDA356B096",
            "PreviousTxnLgrSeq": 6226256,
            "Sequence": 7744,
            "index": "25D041859903879486D4BCC81A1ABAB604547B2454378E16E34A9D659062D635"
        },
        {
            "Balance": {
                "currency": "JPY",
                "issuer": "rrrrrrrrrrrrrrrrrrrrBZbvji",
                "value": "0"
            },
            "Flags": 1114112,
            "HighLimit": {
                "currency": "JPY",
)LDGER03";

char const* ld_04 = R"LDGER04(
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "0"
            },
            "HighNode": "0000000000000000",
            "LedgerEntryType": "RippleState",
            "LowLimit": {
                "currency": "JPY",
                "issuer": "rpNEwuT1D3TkmeTq8tu6nsPgeKe8oWJ9kN",
                "value": "10000000000"
            },
            "LowNode": "0000000000000000",
            "PreviousTxnID": "782A10F2D7F4B9C0BCB0469A41E8C1DFBE30456F7AC7F731A420895C28C35AA2",
            "PreviousTxnLgrSeq": 5670964,
            "index": "25DE74D76DC8AC60511D87EA9EBE12F01240532CEFDA4006FFB1329E1AF19AD0"
        },
        {
            "Account": "rUQwWJBVPBbEQ6CoaoJKeGH8HDWDwysERb",
            "BookDirectory": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04E1AE3F7244E1000",
            "BookNode": "0000000000000000",
            "Flags": 131072,
            "LedgerEntryType": "Offer",
            "OwnerNode": "0000000000000000",
            "PreviousTxnID": "3F20C5A8B1937151F73766DB14D61537646324F4CDEC5E1FCD4FE7012EA60657",
            "PreviousTxnLgrSeq": 6225785,
            "Sequence": 1209,
            "TakerGets": "180000000000",
            "TakerPays": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "136242"
            },
            "index": "26B697285E56D3E89C7FA172314359BD607AB22AED7124C239443FE0DAE7E162"
        },
        {
            "Flags": 0,
            "Indexes": [
                "D67C2598A92B64C2B4D785C9222363B55597CF4480C4A2F1930A0586EA117A5A",
                "82F0B686571941A10925823D930FC2C1197904D250F282E0496B6A85D3D9B5B6",
                "26B697285E56D3E89C7FA172314359BD607AB22AED7124C239443FE0DAE7E162"
            ],
            "LedgerEntryType": "DirectoryNode",
            "Owner": "rUQwWJBVPBbEQ6CoaoJKeGH8HDWDwysERb",
            "RootIndex": "26BB986F7C536283E1A39043A6265B063C2573C529411FD0E13CC3FC0FBC03B6",
            "index": "26BB986F7C536283E1A39043A6265B063C2573C529411FD0E13CC3FC0FBC03B6"
        },
        {
            "Flags": 0,
            "Indexes": [
                "300C6D4FCF7EA0F7F144623370AE7670F85B0433A8DFD5FA91576426BA13B6E3",
                "75373DB421A31947A0533DCCFBF70BD6D5E6B70F14D178EF6D114892156C44B5",
                "A12A9C28748191A8C3B8B386873C26CEC6E3BED4A5D3CDBC6F378FDD631B5696"
            ],
            "LedgerEntryType": "DirectoryNode",
            "Owner": "rM3X3QSr8icjTGpaF52dozhbT2BZSXJQYM",
            "RootIndex": "29C277077947E4DDE7DA1EEA5CC5066BDD083058540E4396D6BD28AB66110EEF",
            "index": "29C277077947E4DDE7DA1EEA5CC5066BDD083058540E4396D6BD28AB66110EEF"
        },
        {
            "Balance": {
                "currency": "JPY",
                "issuer": "rrrrrrrrrrrrrrrrrrrrBZbvji",
                "value": "64.98758259435"
            },
            "Flags": 65536,
            "HighLimit": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "0"
            },
            "HighNode": "0000000000000000",
            "LedgerEntryType": "RippleState",
            "LowLimit": {
                "currency": "JPY",
                "issuer": "rM3X3QSr8icjTGpaF52dozhbT2BZSXJQYM",
                "value": "0"
            },
            "LowNode": "0000000000000000",
            "PreviousTxnID": "B30BDEEC83796B5B205E32F75CF45E309B61F2E7A9B1C34280C62F3DE9EC1629",
            "PreviousTxnLgrSeq": 6226493,
            "index": "300C6D4FCF7EA0F7F144623370AE7670F85B0433A8DFD5FA91576426BA13B6E3"
        },
        {
            "Flags": 0,
            "Indexes": [
                "7CD8FA0B1C81934DF8F5FEFD39323D993A7FA0A1E6D71CE8E49071D7B53F0772",
                "63A89DA746DBCFF5466F2003BDFC1CAE8C0B15A240F912E780EE84C12BF13554"
            ],
            "LedgerEntryType": "DirectoryNode",
            "Owner": "rqb6b8GZn9BTYnhbs4wiMQEoeqw8UnAE5",
            "RootIndex": "30B801CC7101C8DCAE29D84B9161168C7D6B4F1C68A17C5AEB424A20F15A05E0",
            "index": "30B801CC7101C8DCAE29D84B9161168C7D6B4F1C68A17C5AEB424A20F15A05E0"
        },
        {
            "Account": "ra64fg3awuMmrXcVjdDbYzTLFGcWKW1FPc",
            "BookDirectory": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F0E90EDA3944000",
            "BookNode": "0000000000000000",
            "Flags": 131072,
            "LedgerEntryType": "Offer",
            "OwnerNode": "0000000000000000",
            "PreviousTxnID": "8775DC272E3868143B07152C2282729859AEA363EA8BBB45E3C64297C73303CC",
            "PreviousTxnLgrSeq": 5351037,
            "Sequence": 96,
            "TakerGets": "10000000000",
            "TakerPays": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "41000"
            },
            "index": "352C69FE9817C9627073D02C4BB7CB65EDB3A10B6293A2D5BA671DF9E34D7A41"
        },
        {
            "Account": "rajrdNafcXefrq4pYW1YAjMUDoxttSLefM",
            "Balance": "29394016799",
            "Flags": 0,
            "LedgerEntryType": "AccountRoot",
            "OwnerCount": 29,
            "PreviousTxnID": "EFA7F0203C541DD1022C99526910F90B52ADDADA46CDEAD9436BEE90A9615723",
            "PreviousTxnLgrSeq": 6220042,
            "Sequence": 173,
            "index": "382C319316783F5CF97A1BF22A6B6737605ABE6AEBCB44A683D03028356B7761"
        },
        {
            "Account": "rUkPuKD5mEkvnrPcvBeBSqe1m9isAMVX5M",
            "BookDirectory": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F05AF3107A40000",
            "BookNode": "0000000000000000",
            "Flags": 131072,
            "LedgerEntryType": "Offer",
            "OwnerNode": "0000000000000000",
            "PreviousTxnID": "B9C87AAB15D4DC0D9B759813F4E6DC5A6E811D4B28CC0F78C3477496D4EC58FA",
            "PreviousTxnLgrSeq": 5853405,
            "Sequence": 34,
            "TakerGets": "8000000000",
            "TakerPays": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "12800"
            },
            "index": "3D9FF34F845CBF920116593E8C7C7492F1A1B549FF966BB5D54D6DE1D8320721"
        },
        {
            "Balance": {
                "currency": "JPY",
                "issuer": "rrrrrrrrrrrrrrrrrrrrBZbvji",
                "value": "-144.79"
            },
            "Flags": 131072,
            "HighLimit": {
                "currency": "JPY",
                "issuer": "rP9tNSggJJGPNzUgtAZxaZmsWq8LGtKzYP",
                "value": "30000"
            },
            "HighNode": "0000000000000000",
            "LedgerEntryType": "RippleState",
            "LowLimit": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "0"
            },
            "LowNode": "0000000000000000",
            "PreviousTxnID": "7E4B8AF96BFB00DABC77A094B89E5544F6BA99072B6EBB3D3EEA0AA04903FFD4",
            "PreviousTxnLgrSeq": 5361580,
            "index": "3DE8A735E9996A3DB3093D85AC36DCEF135777EF6AE3C67337F9D1481FA83BBF"
        },
        {
            "Flags": 0,
            "Indexes": [
                "D043B6B526F5B9FBC7C2DE1BC2D59291A0C59CB7906153CF0E7DC2F6C80D00C8",
                "E2F373FF3803FFEB2F3EBB805AE20A00A16E7A32E6F51EA49AEA47D4B851AAC5"
            ],
            "LedgerEntryType": "DirectoryNode",
            "Owner": "rHpoggSkNY7puahMUGVafWPZQ5JH8piZVQ",
            "RootIndex": "3ECB504E9C83754902E289261E414BB82C052E022FAF5638C1AE63DA20ADCDA6",
            "index": "3ECB504E9C83754902E289261E414BB82C052E022FAF5638C1AE63DA20ADCDA6"
        },
        {
            "Balance": {
                "currency": "JPY",
                "issuer": "rrrrrrrrrrrrrrrrrrrrBZbvji",
                "value": "6745.37422329"
            },
            "Flags": 1114112,
            "HighLimit": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "0"
            },
            "HighNode": "0000000000000000",
            "LedgerEntryType": "RippleState",
            "LowLimit": {
                "currency": "JPY",
                "issuer": "r34iSwVNKXQZVzqPB8ZEuUwT7dsjQhdaJu",
                "value": "500000"
            },
            "LowNode": "0000000000000000",
            "PreviousTxnID": "DA541B97B45CF0AF3CD7272CEA1F1429E818F8E5B0A3D52CDCDC935E81078E11",
            "PreviousTxnLgrSeq": 5685594,
            "index": "3EE0E409F23D45BF8A95BCDA14AFFF2326877E07C7A40F10F5108298BEBA2A3A"
        },
        {
            "Account": "rpW8wvWYx1SZbYKJVXt9A7rtayPgULa11B",
)LDGER04";

char const* ld_05 = R"LDGER05(
            "Balance": "99999976",
            "Flags": 0,
            "LedgerEntryType": "AccountRoot",
            "OwnerCount": 2,
            "PreviousTxnID": "EA390C127C668344377505A805D4350B795DF697E351DD9153066B5CF4ACF48B",
            "PreviousTxnLgrSeq": 5894710,
            "Sequence": 3,
            "index": "3FEAEF608925363EDDB9D6BE283845EF2CFFA6338A37FFB1F6012ACF98C7366F"
        },
        {
            "Flags": 0,
            "Indexes": [
                "3EE0E409F23D45BF8A95BCDA14AFFF2326877E07C7A40F10F5108298BEBA2A3A",
                "428C78CE704A4A86A44345475EFC3EED9344D75DA2ADC9962E0018B64941364B"
            ],
            "LedgerEntryType": "DirectoryNode",
            "Owner": "r34iSwVNKXQZVzqPB8ZEuUwT7dsjQhdaJu",
            "RootIndex": "4009722C122388EE25E20B5C5E13DE8F8C565767E8D6A6A0FED82489B6BD2CEC",
            "index": "4009722C122388EE25E20B5C5E13DE8F8C565767E8D6A6A0FED82489B6BD2CEC"
        },
        {
            "Account": "r34iSwVNKXQZVzqPB8ZEuUwT7dsjQhdaJu",
            "BookDirectory": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F138A388A43C000",
            "BookNode": "0000000000000000",
            "Flags": 131072,
            "LedgerEntryType": "Offer",
            "OwnerNode": "0000000000000000",
            "PreviousTxnID": "D086B75A27AE18A2322767B216E3CD968FF817DE8B4758E0E82F469B66E9C7B5",
            "PreviousTxnLgrSeq": 5659522,
            "Sequence": 44,
            "TakerGets": "500000000",
            "TakerPays": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "2750"
            },
            "index": "428C78CE704A4A86A44345475EFC3EED9344D75DA2ADC9962E0018B64941364B"
        },
        {
            "Account": "rUxXgX1dZgrEZyj644jsMXXrKEFDMphU75",
            "Balance": "584551302793",
            "Flags": 0,
            "LedgerEntryType": "AccountRoot",
            "OwnerCount": 4,
            "PreviousTxnID": "7B4EE05D265ABECAAF9D7EA65BEE6943571F03A77D1CD50AE01192F944C341ED",
            "PreviousTxnLgrSeq": 6226713,
            "Sequence": 333,
            "index": "43B677CA52A4F76ABF0F77700426CD9A7DE1303A4B0EEE2BFEBA4F1B0E782C97"
        },
        {
            "Account": "rngNbgfn7cT4bHbHJPNoPY12R66a4RMMaa",
            "Balance": "1038043423",
            "Flags": 0,
            "LedgerEntryType": "AccountRoot",
            "OwnerCount": 8,
            "PreviousTxnID": "B38382BBA925B434306DED6928CC634C35EAAEABF3C9C2AA0790590541A1B663",
            "PreviousTxnLgrSeq": 6213507,
            "Sequence": 33,
            "index": "43E3BEB6C7D2A94422C622C8B28D1283AD78336440A8D73C3EDF51058AB7F35F"
        },
        {
            "Flags": 0,
            "Indexes": [
                "86543C222523A608A63B9168171E66CB5DF4DFD94DA8C35BA111739F5908DE95",
                "13833879B05DAB4D7E68EE2E1F7F810CED76692BE5B3F99F017C9056A8C65F05",
                "6F3119C29E3D423B9CCDA9377EBCA770ECFFBC674E5F3809BC2851915293022D",
                "91F178D98C547B5976FACEAB5686A3D55EA0E0071DB4FB50D6E0C65DD62C0A32"
            ],
            "LedgerEntryType": "DirectoryNode",
            "Owner": "rUZjAUwatwbS2WHGYNNwbcv8QvYYq8QLC3",
            "RootIndex": "440BA6821E167C66AB321FED84A87A1562F7DBAAA034512E6083E3A58E0FC75C",
            "index": "440BA6821E167C66AB321FED84A87A1562F7DBAAA034512E6083E3A58E0FC75C"
        },
        {
            "Balance": {
                "currency": "JPY",
                "issuer": "rrrrrrrrrrrrrrrrrrrrBZbvji",
                "value": "0.0050732"
            },
            "Flags": 65536,
            "HighLimit": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "0"
            },
            "HighNode": "0000000000000000",
            "LedgerEntryType": "RippleState",
            "LowLimit": {
                "currency": "JPY",
                "issuer": "ra284e11Q432pmnoKJY9WC77XN8GUsQvYc",
                "value": "100000"
            },
            "LowNode": "0000000000000000",
            "PreviousTxnID": "34304E0706CE538ADBDCF441086FF80AA8FF5485A4A5C702521E50CC5C7A3218",
            "PreviousTxnLgrSeq": 5468714,
            "index": "4628C6B90FDCCD23E451176B826391D115CBCA5A5E30218E1D83C0D447A21538"
        },
        {
            "Flags": 0,
            "Indexes": [
                "13E35A054213C6CA2F639631B6F0618A95081F8E958E99B86A625001EFF9B3BC",
                "81721B602049F4A05B7D01208FBDDCA4183B1BEE6A3540E694E3D71B81057A27",
                "95D87B2D2138AB924A9665DDF2B4C9E8CC4749D2F9CA94741435E8E2AE99675E",
                "B7352CF1A28793675F07C559BD330181CB6669E6A71DA971EBA8A16C342323E7"
            ],
            "LedgerEntryType": "DirectoryNode",
            "Owner": "r3Y3Hh7abFiS9sTgCenK2kk2iToRhFfNs6",
            "RootIndex": "4879D0ED72A2953F0B386714F83D243BE50EFBC57458C330FC8D6DE236DC82AA",
            "index": "4879D0ED72A2953F0B386714F83D243BE50EFBC57458C330FC8D6DE236DC82AA"
        },
        {
            "Account": "rG1JXRtt7VqxwRt4CNASY1KCJ8xZtMAvCy",
            "BookDirectory": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB054038D7EA4C68000",
            "BookNode": "0000000000000000",
            "Flags": 131072,
            "LedgerEntryType": "Offer",
            "OwnerNode": "0000000000000000",
            "PreviousTxnID": "094D0D774070E030D1D8FFC6E3D4C7B066FBF760D024C768836799BB17789863",
            "PreviousTxnLgrSeq": 5720276,
            "Sequence": 55,
            "TakerGets": "1500000000",
            "TakerPays": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "150000000"
            },
            "index": "489B73AC921003479A0CC9725DB0249B15663A25AF29EF6FE737D3BFF02F0FAD"
        },
        {
            "Flags": 0,
            "Indexes": [
                "CE0EDA775D377BDCC77B6F85DA9540EAB77F44D4AE2B12FCC86810545B759CFA",
                "D043B6B526F5B9FBC7C2DE1BC2D59291A0C59CB7906153CF0E7DC2F6C80D00C8",
                "AE39B7DAF9C3C9E5E1C0C6A758F41D22F81B5CE2D44C128F0ECD949B56D67804"
            ],
            "LedgerEntryType": "DirectoryNode",
            "Owner": "razqQKzJRdB4UxFPWf5NEpEG3WMkmwgcXA",
            "RootIndex": "494660EA99CF2FE02C1D8235791BD46F80D84253D413A5C67AF7E276E9C7404F",
            "index": "494660EA99CF2FE02C1D8235791BD46F80D84253D413A5C67AF7E276E9C7404F"
        },
        {
            "Flags": 0,
            "Indexes": [
                "8782F28AC73A79162357EB1FB38E0AA5F55C066F0F2ACC774BBF095B21E07E64",
                "F37871AD76189305B0BA6A652A69C4207C384DA95336418A1A474D938E768BEE",
                "D770FB84E4ED16B67C925F7BAD094E52D48297D6375BAC0A8F30539BADBAC36F",
                "F984915B0302CE07E061BC46C82574C37E49B6BF138C5AF092F779F0EE75C3FF"
            ],
            "LedgerEntryType": "DirectoryNode",
            "Owner": "rhsxr2aAddyCKx5iZctebT4Padxv6iWDxb",
            "RootIndex": "49DA34D0CCDB7AF9A1B5751ECDC647D6379033B0126D645CD16395E302239BAE",
            "index": "49DA34D0CCDB7AF9A1B5751ECDC647D6379033B0126D645CD16395E302239BAE"
        },
        {
            "Account": "rNruDQQDBM117pzRob2Br211HJrFKxk3tB",
            "BookDirectory": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F1550F7DCA70000",
            "BookNode": "0000000000000000",
            "Flags": 131072,
            "LedgerEntryType": "Offer",
            "OwnerNode": "0000000000000000",
            "PreviousTxnID": "F7D79D86CCDBF624C4C7C8AB4D5B7B0A46E5AAB05671DD57AA408A4DA41E0241",
            "PreviousTxnLgrSeq": 5368283,
            "Sequence": 23,
            "TakerGets": "200000000",
            "TakerPays": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "1200"
            },
            "index": "4F68DB8A9E94EB3CD6979892E338288200C0CC370E1CA6AA9FE685D616C5C774"
        },
        {
            "Account": "r47GLMFhJPjshD65J8TJSWZJzM3jPHcJdZ",
            "Balance": "82717045395",
            "Flags": 0,
            "LedgerEntryType": "AccountRoot",
            "OwnerCount": 181,
            "PreviousTxnID": "93DB04343A4FF129CA0D09FDFB44D44EE131804ABD5810619FDB12E0FF9662F6",
            "PreviousTxnLgrSeq": 6219155,
            "Sequence": 1030,
            "index": "4FDD40C1CC2E764B83BEEC16908FCA2CB4967DE8006D116434D01F443C466A86"
        },
        {
            "Account": "rP9tNSggJJGPNzUgtAZxaZmsWq8LGtKzYP",
            "BookDirectory": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F0AA87BEE538000",
            "BookNode": "0000000000000000",
            "Flags": 131072,
            "LedgerEntryType": "Offer",
            "OwnerNode": "0000000000000000",
            "PreviousTxnID": "14F38411C2ECA1DC1A2FC355A23C8E1C0867C6B2481BCDC44596CC2301BC0798",
            "PreviousTxnLgrSeq": 5421892,
            "Sequence": 49,
            "TakerGets": "500000000",
            "TakerPays": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "1500"
            },
            "index": "50C33C456676E0AF7B69397CFEE6612B59F9D294B2C7995023C2B8B748226F4A"
        },
)LDGER05";

char const* ld_06 = R"LDGER06(
        {
            "Account": "ra64fg3awuMmrXcVjdDbYzTLFGcWKW1FPc",
            "Balance": "27307741032",
            "Flags": 0,
            "LedgerEntryType": "AccountRoot",
            "OwnerCount": 4,
            "PreviousTxnID": "A9B1A9C425A13CDA3D1810454ED1E547CF6BF7768CF310DBDFCE603B84C09EAC",
            "PreviousTxnLgrSeq": 5751204,
            "Sequence": 152,
            "index": "50E94EE08F753E63506DACE319B46F6599027F7C3B47EA0D8F46F248B1888ABF"
        },
        {
            "Account": "rUZjAUwatwbS2WHGYNNwbcv8QvYYq8QLC3",
            "Balance": "275259084999",
            "Flags": 0,
            "LedgerEntryType": "AccountRoot",
            "OwnerCount": 6,
            "PreviousTxnID": "DD718391B19612CDD6A458F75AC84C8E33951258B16BF8B2B09CA18DACA3E17B",
            "PreviousTxnLgrSeq": 6184455,
            "Sequence": 582,
            "index": "583FC610667F7CA7664E83F4C7B5C99649F9CA077B5D3E47D5B8C3BE8593AF61"
        },
        {
            "Flags": 0,
            "Indexes": [
                "A58F531945492C5270C9D364632996C152E12516EC235EE7DF1133876E23BBA0",
                "EB7C3A1CD0DB012AD336262CE4E47113F0E59D0F44E18359B9BD788DBD426B7E"
            ],
            "LedgerEntryType": "DirectoryNode",
            "Owner": "rU8axbJNWix3k3LCTXtL8T8LeFtv88ibMe",
            "RootIndex": "59809C2C5B7CBED5BCF67F92880A3353BA3722518850E4AFE09AE5E7B6C88DD6",
            "index": "59809C2C5B7CBED5BCF67F92880A3353BA3722518850E4AFE09AE5E7B6C88DD6"
        },
        {
            "Account": "rKX9Rb3ygKmYYsQfYvv3KXmRuwJ3AoLbLq",
            "Balance": "3871722324",
            "Flags": 0,
            "LedgerEntryType": "AccountRoot",
            "OwnerCount": 51,
            "PreviousTxnID": "091D205E27907336C1E00B56B4DE3FBC6FBDFEB7F1B5A8B9E3595AE544908836",
            "PreviousTxnLgrSeq": 6202077,
            "Sequence": 543,
            "index": "5B1942BA4779C6CF7156B470E3A41F54D8FFFC28B7A503625263E2FA5B6648DB"
        },
        {
            "Flags": 0,
            "Indexes": [
                "E55DB8FB9BEC6D16123EE8BE8434F09035AC7DD2D90A3450A1F7400DFEB214B8",
                "C46FA7924251F67DADC69D6FCB71D4BA2167BE7EA0615078E466FAB236D88BF6",
                "AC579B09EB6B609DBAAB2CCF4AE3F59B2D1F56072A4B0E63001621877E7ADEA0"
            ],
            "LedgerEntryType": "DirectoryNode",
            "Owner": "rngNbgfn7cT4bHbHJPNoPY12R66a4RMMaa",
            "RootIndex": "5BBA82B535F457C5C353B337D394AE664A31176B010CFE947353109D6E7A1E18",
            "index": "5BBA82B535F457C5C353B337D394AE664A31176B010CFE947353109D6E7A1E18"
        },
        {
            "Account": "rho8mvSESSmVPkF4UiyF8pTJBGMcVx2Uv1",
            "BookDirectory": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F071AFD498D0000",
            "BookNode": "0000000000000000",
            "Flags": 131072,
            "LedgerEntryType": "Offer",
            "OwnerNode": "0000000000000000",
            "PreviousTxnID": "97A7CA33D1B35988FA5700DB5D2AE2052CC9A9F25FAE841A809ACE4F3564A2F7",
            "PreviousTxnLgrSeq": 6218795,
            "Sequence": 326,
            "TakerGets": "1000000000",
            "TakerPays": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "2000"
            },
            "index": "5CDE229B1DDC6E52DE881F7DF00942C838DF5AD12AFD6E2780936E823CD02005"
        },
        {
            "ExchangeRate": "5C11C37937E08000",
            "Flags": 0,
            "Indexes": ["B27436F9FC495FFE07D907E69B59BCFA4F5150BABCBC5DE9F5AFCD00A9E99236"],
            "LedgerEntryType": "DirectoryNode",
            "RootIndex": "5EB7785286CB89D3B705046BFA0EDB1082E1116CD9EA35885C11C37937E08000",
            "TakerGetsCurrency": "000000000000000000000000434E590000000000",
            "TakerGetsIssuer": "A034782E2DBAC4FB82B601CD50421E8EF24F3A00",
            "TakerPaysCurrency": "0000000000000000000000000000000000000000",
            "TakerPaysIssuer": "0000000000000000000000000000000000000000",
            "index": "5EB7785286CB89D3B705046BFA0EDB1082E1116CD9EA35885C11C37937E08000"
        },
        {
            "Account": "r47GLMFhJPjshD65J8TJSWZJzM3jPHcJdZ",
            "BookDirectory": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F11B1489AFB4000",
            "BookNode": "0000000000000000",
            "Flags": 131072,
            "LedgerEntryType": "Offer",
            "OwnerNode": "0000000000000000",
            "PreviousTxnID": "116E342AE6F42D6AC2DA595512B28CBC6DEE5CB7850BD88D590A2D0A48D40DD6",
            "PreviousTxnLgrSeq": 5909046,
            "Sequence": 510,
            "TakerGets": "10000000000",
            "TakerPays": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "49800"
            },
            "index": "61A9A18EAF404FC100A4FBC813DBA1F9C0B80AB0DC29790BF7EA3B438BDA0249"
        },
        {
            "Account": "rqb6b8GZn9BTYnhbs4wiMQEoeqw8UnAE5",
            "BookDirectory": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F038D7EA4C68000",
            "BookNode": "0000000000000000",
            "Flags": 131072,
            "LedgerEntryType": "Offer",
            "OwnerNode": "0000000000000000",
            "PreviousTxnID": "41FE05D555FFF2A1F79896A7169C90E8323228CF58A46B26425ADACDC6D3C57A",
            "PreviousTxnLgrSeq": 6129535,
            "Sequence": 529,
            "TakerGets": "1000000000000",
            "TakerPays": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "1000000"
            },
            "index": "63A89DA746DBCFF5466F2003BDFC1CAE8C0B15A240F912E780EE84C12BF13554"
        },
        {
            "Flags": 0,
            "Indexes": [
                "F59533169EAC6639FB94220A952C8459FFCFCF0A1BDC80D7A5AD26DF30CD5757",
                "5CDE229B1DDC6E52DE881F7DF00942C838DF5AD12AFD6E2780936E823CD02005",
                "7FB16A6516304F196127F10ACB771829F45480BC368CB8DBF89266E04E3AE1FD",
                "6E45279F78A5092B5F92C314C1BF4D23936426E0EA724E433992654A80F5DB6B"
            ],
            "LedgerEntryType": "DirectoryNode",
            "Owner": "rho8mvSESSmVPkF4UiyF8pTJBGMcVx2Uv1",
            "RootIndex": "63D0C4B9B699058ABE346C9A0E15283CDF877B7B692F0D2251C6D0C3AC6D409A",
            "index": "63D0C4B9B699058ABE346C9A0E15283CDF877B7B692F0D2251C6D0C3AC6D409A"
        },
        {
            "Account": "rpRzczN3gPxXMRzqMR98twVsH63xATHUb7",
            "BookDirectory": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB050038D7EA4C68000",
            "BookNode": "0000000000000000",
            "Flags": 131072,
            "LedgerEntryType": "Offer",
            "OwnerNode": "0000000000000000",
            "PreviousTxnID": "E244AD4F7BE62435A7AC5648577D0A242DAD02EBA94223EC2688C835DE122634",
            "PreviousTxnLgrSeq": 5293960,
            "Sequence": 472,
            "TakerGets": "1000000000",
            "TakerPays": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "10000"
            },
            "index": "665C464568A1F581501591A6ED36E39B78381679AA7F4B667CDEFE5E347855DB"
        },
        {
            "Account": "rKL5uUYcpSGcsVe2Yen5okfhGvi4J57mcM",
            "BookDirectory": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB054038D7EA4C68000",
            "BookNode": "0000000000000000",
            "Flags": 131072,
            "LedgerEntryType": "Offer",
            "OwnerNode": "0000000000000000",
            "PreviousTxnID": "CA01715F17D67DD1277B5C4C7FFF63AD6F37038CBABFF2108DBF2909186789EC",
            "PreviousTxnLgrSeq": 6099909,
            "Sequence": 41,
            "TakerGets": "50000000",
            "TakerPays": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "5000000"
            },
            "index": "670ECD9A17639A02C6161F3CC8638C22170E504F8E04B6619F9146B24F117262"
        },
        {
            "Flags": 0,
            "Indexes": [
                "25DE74D76DC8AC60511D87EA9EBE12F01240532CEFDA4006FFB1329E1AF19AD0",
                "A946B2416E147206FC3A19504693390DDBDB976F1801BA35AD685448224C83FE"
            ],
            "LedgerEntryType": "DirectoryNode",
            "Owner": "rpNEwuT1D3TkmeTq8tu6nsPgeKe8oWJ9kN",
            "RootIndex": "67C9145705EC06456086C57916581C5870007B0B43F98C423E81312B3FBF6DBD",
            "index": "67C9145705EC06456086C57916581C5870007B0B43F98C423E81312B3FBF6DBD"
        },
        {
            "Account": "rpW8wvWYx1SZbYKJVXt9A7rtayPgULa11B",
            "BookDirectory": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F03DF5966CE2000",
            "BookNode": "0000000000000000",
            "Flags": 131072,
            "LedgerEntryType": "Offer",
            "OwnerNode": "0000000000000000",
            "PreviousTxnID": "EA390C127C668344377505A805D4350B795DF697E351DD9153066B5CF4ACF48B",
            "PreviousTxnLgrSeq": 5894710,
            "Sequence": 2,
            "TakerGets": "50000000",
            "TakerPays": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "54.5"
            },
            "index": "6B79A8D89C4E369336D21ECA23A724A5B1E30DBE2344F66141444165FBE1270F"
        },
)LDGER06";

char const* ld_07 = R"LDGER07(
        {
            "Account": "rho8mvSESSmVPkF4UiyF8pTJBGMcVx2Uv1",
            "BookDirectory": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F0AA87BEE538000",
            "BookNode": "0000000000000000",
            "Flags": 131072,
            "LedgerEntryType": "Offer",
            "OwnerNode": "0000000000000000",
            "PreviousTxnID": "CBC6953AE1013B05B2D2DB6A9F81033982C4A8464E4C279377532D1C5B844462",
            "PreviousTxnLgrSeq": 6218811,
            "Sequence": 328,
            "TakerGets": "1000000000",
            "TakerPays": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "3000"
            },
            "index": "6E45279F78A5092B5F92C314C1BF4D23936426E0EA724E433992654A80F5DB6B"
        },
        {
            "Account": "rUZjAUwatwbS2WHGYNNwbcv8QvYYq8QLC3",
            "BookDirectory": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB054038D7EA4C68000",
            "BookNode": "0000000000000000",
            "Flags": 131072,
            "LedgerEntryType": "Offer",
            "OwnerNode": "0000000000000000",
            "PreviousTxnID": "B5FDE2288D56EEBBEF5D6FDEEEB402A72FE5450C03EC977BA12C4620D975BBE2",
            "PreviousTxnLgrSeq": 6032562,
            "Sequence": 577,
            "TakerGets": "1000000000",
            "TakerPays": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "100000000"
            },
            "index": "6F3119C29E3D423B9CCDA9377EBCA770ECFFBC674E5F3809BC2851915293022D"
        },
        {
            "Flags": 0,
            "Indexes": [
                "AE39B7DAF9C3C9E5E1C0C6A758F41D22F81B5CE2D44C128F0ECD949B56D67804",
                "9991CD45AC741BE86C45242CDD6BF73010C160BBDE4FB45BA28326BE4B3A89FD"
            ],
            "LedgerEntryType": "DirectoryNode",
            "Owner": "rEcnyLQD7LXPqTTRG3cXgzcK1C3TDkuUWb",
            "RootIndex": "70247FEB2AA9A21868A05A1ADB6D4D10C9E425C95827718BF66D2CC7AC37781D",
            "index": "70247FEB2AA9A21868A05A1ADB6D4D10C9E425C95827718BF66D2CC7AC37781D"
        },
        {
            "Account": "r9RR643anesxNCoNkkuYfEfzpxZUeK5Qzw",
            "BookDirectory": "92466F5377C34C5EA957034339321E217A23FA4E27A31D475B050F939563B2B0",
            "BookNode": "0000000000000000",
            "Flags": 0,
            "LedgerEntryType": "Offer",
            "OwnerNode": "0000000000000000",
            "PreviousTxnID": "7B4EE05D265ABECAAF9D7EA65BEE6943571F03A77D1CD50AE01192F944C341ED",
            "PreviousTxnLgrSeq": 6226713,
            "Sequence": 41,
            "TakerGets": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "34501.48722737"
            },
            "TakerPays": "49147417635",
            "index": "7051544FA7B0C129F741CB992C9CC769FCC3D605F57DA808536255C7AB147742"
        },
        {
            "ExchangeRate": "4D0DF90AEBE6D000",
            "Flags": 0,
            "Indexes": ["C5C0D61BA32C097DDCE6C381E1DEC33B36D6BF4C3B5CFCB1174352BC036EA121"],
            "LedgerEntryType": "DirectoryNode",
            "RootIndex": "7254404DF6B7FBFFEF34DC38867A7E7DE610B513997B78804D0DF90AEBE6D000",
            "TakerGetsCurrency": "0000000000000000000000000000000000000000",
            "TakerGetsIssuer": "0000000000000000000000000000000000000000",
            "TakerPaysCurrency": "000000000000000000000000434E590000000000",
            "TakerPaysIssuer": "41C8BE2C0A6AA17471B9F6D0AF92AAB1C94D5A25",
            "index": "7254404DF6B7FBFFEF34DC38867A7E7DE610B513997B78804D0DF90AEBE6D000"
        },
        {
            "Balance": {
                "currency": "JPY",
                "issuer": "rrrrrrrrrrrrrrrrrrrrBZbvji",
                "value": "33.9184096710748"
            },
            "Flags": 1114112,
            "HighLimit": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "0"
            },
            "HighNode": "0000000000000000",
            "LedgerEntryType": "RippleState",
            "LowLimit": {
                "currency": "JPY",
                "issuer": "rwonczT4eRKiEPb3YvcViUxvSxgJuPfngh",
                "value": "1000000"
            },
            "LowNode": "0000000000000000",
            "PreviousTxnID": "0827D5700BA798AB3B248C57C1BF59EAB18B2B47958B547A8D31FED8BFC49D82",
            "PreviousTxnLgrSeq": 6181555,
            "index": "7280EDED4E1FA80C6E5F86D07A70F0E704B1B637F994DC3152FCC7248F5DAB6B"
        },
        {
            "Balance": {
                "currency": "JPY",
                "issuer": "rrrrrrrrrrrrrrrrrrrrBZbvji",
                "value": "1662388.253747577"
            },
            "Flags": 1114112,
            "HighLimit": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "0"
            },
            "HighNode": "0000000000000000",
            "LedgerEntryType": "RippleState",
            "LowLimit": {
                "currency": "JPY",
                "issuer": "racJpvgLpaNQGKB8nhKd1gTEVVA1uQWRKs",
                "value": "10000000"
            },
            "LowNode": "0000000000000000",
            "PreviousTxnID": "0A8FA410E853BF178C24782CEEF2490A7B7BDD02448DB09F86EB5B3B75AFC1BC",
            "PreviousTxnLgrSeq": 6217622,
            "index": "73DB3FF0D87377B82D7946FA4B1FDB1FB5DD92D3C664666CE5B49A2922761CAF"
        },
        {
            "Account": "rG1JXRtt7VqxwRt4CNASY1KCJ8xZtMAvCy",
            "Balance": "1914107398",
            "Flags": 0,
            "LedgerEntryType": "AccountRoot",
            "OwnerCount": 3,
            "PreviousTxnID": "C639AFAF2CAD8BE6E8E9A471BF8AE238CF54C90F8A534590A5AE7045076A4AF9",
            "PreviousTxnLgrSeq": 6086248,
            "Sequence": 58,
            "index": "7466A777B614BFA3B73F4F4172C6801861AA9C362A7D9DBF63AE2F717DF8BA5A"
        },
        {
            "Account": "rKE2TX794t8Aoqe25AvWKWvKi1igXJpBUi",
            "Balance": "19499064732",
            "Flags": 0,
            "LedgerEntryType": "AccountRoot",
            "OwnerCount": 6,
            "PreviousTxnID": "7847F6B456138678879C06B11B1A040FD800F2A930A9E4AC37483CBEE74FB9F6",
            "PreviousTxnLgrSeq": 6168034,
            "Sequence": 132,
            "index": "74AFA3DF3CB2BCD7CBF402957332B0BA0B02BFF35E7A4E34CB457121ADDEB796"
        },
        {
            "Account": "rM3X3QSr8icjTGpaF52dozhbT2BZSXJQYM",
            "BookDirectory": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04E1999C9B1822A51",
            "BookNode": "0000000000000000",
            "Flags": 0,
            "LedgerEntryType": "Offer",
            "OwnerNode": "0000000000000000",
            "PreviousTxnID": "50AB41C744C8F6DDAD77543BF4DBD27705749157D2640FF99BEC2935F8B0E73F",
            "PreviousTxnLgrSeq": 6226664,
            "Sequence": 44671,
            "TakerGets": "1804060700",
            "TakerPays": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "1300"
            },
            "index": "75373DB421A31947A0533DCCFBF70BD6D5E6B70F14D178EF6D114892156C44B5"
        },
        {
            "Balance": {
                "currency": "JPY",
                "issuer": "rrrrrrrrrrrrrrrrrrrrBZbvji",
                "value": "0"
            },
            "Flags": 1114112,
            "HighLimit": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "0"
            },
            "HighNode": "0000000000000000",
            "LedgerEntryType": "RippleState",
            "LowLimit": {
                "currency": "JPY",
                "issuer": "rpRzczN3gPxXMRzqMR98twVsH63xATHUb7",
                "value": "1000"
            },
            "LowNode": "0000000000000000",
            "PreviousTxnID": "D297C64C03B9D67F2EFED22786E6D01409590F43051CCF46E13920D387A50F9B",
            "PreviousTxnLgrSeq": 5284307,
            "index": "7548EDD4EE8582725A58ECB6D7E70A5DED5E05A8A3BB9C2BF8062742CB9B8225"
        },
        {
            "Account": "r47GLMFhJPjshD65J8TJSWZJzM3jPHcJdZ",
            "BookDirectory": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F08CF8BFF0B0000",
            "BookNode": "0000000000000000",
            "Flags": 131072,
            "LedgerEntryType": "Offer",
            "OwnerNode": "0000000000000000",
            "PreviousTxnID": "C55D7B4DBF25D85D7F73FCDCECE50D25F1CA9FFBC0C08E2FBEC87748A8417AE1",
            "PreviousTxnLgrSeq": 5909024,
            "Sequence": 507,
            "TakerGets": "10000000000",
)LDGER07";

char const* ld_08 = R"LDGER08(
            "TakerPays": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "24800"
            },
            "index": "7A599E3DA6A3E67E86CE80B10EA8B3C5C395A6E457C6802D9F51909BA15BB98B"
        },
        {
            "Flags": 0,
            "Indexes": [
                "20E49A1185CBB556D55EDFD054162F5833ABE8889E0B03247F79D1D8FA85F60A",
                "7EFCC8EE289C60DB11F776D5B6DC86CDE231B8D8AE8A77D75952C59693B42760"
            ],
            "LedgerEntryType": "DirectoryNode",
            "Owner": "rKX9Rb3ygKmYYsQfYvv3KXmRuwJ3AoLbLq",
            "RootIndex": "7B8E28304B2493FDFE5F3C8A1070008A96E13E71C1AE22EBD83E40B95A64C1D3",
            "index": "7B8E28304B2493FDFE5F3C8A1070008A96E13E71C1AE22EBD83E40B95A64C1D3"
        },
        {
            "Balance": {
                "currency": "JPY",
                "issuer": "rrrrrrrrrrrrrrrrrrrrBZbvji",
                "value": "606.215249584"
            },
            "Flags": 65536,
            "HighLimit": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "0"
            },
            "HighNode": "0000000000000000",
            "LedgerEntryType": "RippleState",
            "LowLimit": {
                "currency": "JPY",
                "issuer": "rqb6b8GZn9BTYnhbs4wiMQEoeqw8UnAE5",
                "value": "1000000"
            },
            "LowNode": "0000000000000000",
            "PreviousTxnID": "C02241EC013815C87FB902EC8CA5567FB9B6347AE5556A63A5CAA7691C3D4C2D",
            "PreviousTxnLgrSeq": 6193047,
            "index": "7CD8FA0B1C81934DF8F5FEFD39323D993A7FA0A1E6D71CE8E49071D7B53F0772"
        },
        {
            "Account": "rhTUpdUStwn7wPnzNMjHEfFgQacPC5eop1",
            "BookDirectory": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB0502386F01BB51C00",
            "BookNode": "0000000000000000",
            "Flags": 131072,
            "LedgerEntryType": "Offer",
            "OwnerNode": "0000000000000000",
            "PreviousTxnID": "1C003624AFD8951C581F68AC6DC439EC88DAB4BC7C3A5CC58A6E163D23A74202",
            "PreviousTxnLgrSeq": 6096454,
            "Sequence": 13,
            "TakerGets": "5000000000",
            "TakerPays": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "499999.5"
            },
            "index": "7D76318C279034B7ACE1231F16BF345DF8FCF6EC40E99E17091AD56B19867AE3"
        },
        {
            "Account": "rajrdNafcXefrq4pYW1YAjMUDoxttSLefM",
            "BookDirectory": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04E1FF973CAFA8000",
            "BookNode": "0000000000000000",
            "Flags": 131072,
            "LedgerEntryType": "Offer",
            "OwnerNode": "0000000000000000",
            "PreviousTxnID": "8913F8FA5C9B313BC248C04D6CAFD404C9C4CA66ADCB24724ABCC0DBEA424822",
            "PreviousTxnLgrSeq": 6200392,
            "Sequence": 167,
            "TakerGets": "2000000000",
            "TakerPays": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "1800"
            },
            "index": "7D7874A4A96EB03A227365EABA10C637681BCCDFCDE4ADB6B73B4CB7F1FD1142"
        },
        {
            "Account": "rhsxr2aAddyCKx5iZctebT4Padxv6iWDxb",
            "Balance": "27987195720",
            "Flags": 0,
            "LedgerEntryType": "AccountRoot",
            "OwnerCount": 7,
            "PreviousTxnID": "474A1C0B4B28BE23C7D472C14C0C33E7B774CF946528A6B97AFD4E10F50A324C",
            "PreviousTxnLgrSeq": 6224257,
            "Sequence": 294,
            "index": "7E7EBE111CB117C19F55CB87A1166D3235D32605AD29F5EFF795D84962FE4D5A"
        },
        {
            "Account": "rKX9Rb3ygKmYYsQfYvv3KXmRuwJ3AoLbLq",
            "BookDirectory": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB051038D7EA4C68000",
            "BookNode": "0000000000000000",
            "Flags": 131072,
            "LedgerEntryType": "Offer",
            "OwnerNode": "0000000000000000",
            "PreviousTxnID": "9A55BE44192ACF0481CF285F9116A298CA45DF0893B06C43A463A119558E7680",
            "PreviousTxnLgrSeq": 5885228,
            "Sequence": 359,
            "TakerGets": "1000000000",
            "TakerPays": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "100000"
            },
            "index": "7EFCC8EE289C60DB11F776D5B6DC86CDE231B8D8AE8A77D75952C59693B42760"
        },
        {
            "Account": "rho8mvSESSmVPkF4UiyF8pTJBGMcVx2Uv1",
            "BookDirectory": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F038D7EA4C68000",
            "BookNode": "0000000000000000",
            "Flags": 131072,
            "LedgerEntryType": "Offer",
            "OwnerNode": "0000000000000000",
            "PreviousTxnID": "E1969FBC9CA4BC4D40CBD7DBD7430D757565CD8621B79D623D216CFCA9E079D6",
            "PreviousTxnLgrSeq": 6218803,
            "Sequence": 327,
            "TakerGets": "1000000000",
            "TakerPays": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "1000"
            },
            "index": "7FB16A6516304F196127F10ACB771829F45480BC368CB8DBF89266E04E3AE1FD"
        },
        {
            "Flags": 0,
            "Indexes": [
                "03A00CD40E296C363D33515A1F2867D62B7059E7D2DA146993F8AB6A986EFDFD",
                "F56F107800DF0AB585BC31684011DADD77C9A2516A2E47901EDCB0D9A1A0D5E0"
            ],
            "LedgerEntryType": "DirectoryNode",
            "Owner": "rDr83LqpsNJtJ9CouPxwf2pFEhdEuoCM4z",
            "RootIndex": "80D03B23034F453458C82E5DB54BBB6502DC5AB160730900F42EF085F435E0EF",
            "index": "80D03B23034F453458C82E5DB54BBB6502DC5AB160730900F42EF085F435E0EF"
        },
        {
            "Account": "r38Mwd8s2gFevETqCK8e34JYfWBjLUB2nH",
            "BookDirectory": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB0572386F26F286980",
            "BookNode": "0000000000000000",
            "Flags": 131072,
            "LedgerEntryType": "Offer",
            "OwnerNode": "0000000000000000",
            "PreviousTxnID": "D3711277C4631A7EBDB26174F750AC497CE4373EBEC6E850191A334FCB3BD26C",
            "PreviousTxnLgrSeq": 6040077,
            "Sequence": 379,
            "TakerGets": "10000000000",
            "TakerPays": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "9999999990000"
            },
            "index": "814E0164B20ADE0618011D698E1E78106AC43FF000A55C3CC976F4F917262182"
        },
        {
            "Account": "r3Y3Hh7abFiS9sTgCenK2kk2iToRhFfNs6",
            "BookDirectory": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F03E871B5391EE0",
            "BookNode": "0000000000000000",
            "Flags": 131072,
            "LedgerEntryType": "Offer",
            "OwnerNode": "0000000000000000",
            "PreviousTxnID": "F150B1C3020D737EB5F85929DBFDB3D35C610B37360662D474627BCFC5A55F76",
            "PreviousTxnLgrSeq": 6091011,
            "Sequence": 1050,
            "TakerGets": "20000000000",
            "TakerPays": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "21999.99999"
            },
            "index": "81721B602049F4A05B7D01208FBDDCA4183B1BEE6A3540E694E3D71B81057A27"
        },
        {
            "Account": "rUQwWJBVPBbEQ6CoaoJKeGH8HDWDwysERb",
            "BookDirectory": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04E1AF627C1335000",
            "BookNode": "0000000000000000",
            "Flags": 131072,
            "LedgerEntryType": "Offer",
            "OwnerNode": "0000000000000000",
            "PreviousTxnID": "8365072E3453F38E6782BBB98D991AF5C1C3445480FEB8096725F62842349EC5",
            "PreviousTxnLgrSeq": 6219342,
            "Sequence": 1166,
            "TakerGets": "150000000000",
            "TakerPays": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "113835"
            },
            "index": "82F0B686571941A10925823D930FC2C1197904D250F282E0496B6A85D3D9B5B6"
        },
        {
            "Account": "r47GLMFhJPjshD65J8TJSWZJzM3jPHcJdZ",
            "BookDirectory": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F0A964B516E4000",
            "BookNode": "0000000000000000",
            "Flags": 131072,
            "LedgerEntryType": "Offer",
            "OwnerNode": "0000000000000000",
            "PreviousTxnID": "1D1C9CA6C7432D3F6EC520DE130CC1F156C3E732BF11173B6EBD9FC0865ED485",
            "PreviousTxnLgrSeq": 5909028,
            "Sequence": 508,
)LDGER08";

char const* ld_09 = R"LDGER09(
            "TakerGets": "10000000000",
            "TakerPays": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "29800"
            },
            "index": "8476DF4803ECF5DB945DA2FC41CEC6DFB6D50B829E8577267699920525DDBD5B"
        },
        {
            "Balance": {
                "currency": "JPY",
                "issuer": "rrrrrrrrrrrrrrrrrrrrBZbvji",
                "value": "0"
            },
            "Flags": 1114112,
            "HighLimit": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "0"
            },
            "HighNode": "0000000000000000",
            "LedgerEntryType": "RippleState",
            "LowLimit": {
                "currency": "JPY",
                "issuer": "rUZjAUwatwbS2WHGYNNwbcv8QvYYq8QLC3",
                "value": "500000"
            },
            "LowNode": "0000000000000000",
            "PreviousTxnID": "8FA939835832F9D7564128E2D34CAC97543EDFAAFE8C369A237C4692401491C4",
            "PreviousTxnLgrSeq": 5969968,
            "index": "86543C222523A608A63B9168171E66CB5DF4DFD94DA8C35BA111739F5908DE95"
        },
        {
            "Flags": 0,
            "Indexes": [
                "8E12E77B98570C4D5A9CBBB296A526068EB47ACBB34FB81F56EE05514D687D55",
                "489B73AC921003479A0CC9725DB0249B15663A25AF29EF6FE737D3BFF02F0FAD"
            ],
            "LedgerEntryType": "DirectoryNode",
            "Owner": "rG1JXRtt7VqxwRt4CNASY1KCJ8xZtMAvCy",
            "RootIndex": "86C3B827468223D6F49386643D141A747BCDE0C863BF46002DFFE2174E0AB2F6",
            "index": "86C3B827468223D6F49386643D141A747BCDE0C863BF46002DFFE2174E0AB2F6"
        },
        {
            "Balance": {
                "currency": "JPY",
                "issuer": "rrrrrrrrrrrrrrrrrrrrBZbvji",
                "value": "8060.67354263549"
            },
            "Flags": 1114112,
            "HighLimit": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "0"
            },
            "HighNode": "0000000000000000",
            "LedgerEntryType": "RippleState",
            "LowLimit": {
                "currency": "JPY",
                "issuer": "rhsxr2aAddyCKx5iZctebT4Padxv6iWDxb",
                "value": "1000000"
            },
            "LowNode": "0000000000000000",
            "PreviousTxnID": "474A1C0B4B28BE23C7D472C14C0C33E7B774CF946528A6B97AFD4E10F50A324C",
            "PreviousTxnLgrSeq": 6224257,
            "index": "8782F28AC73A79162357EB1FB38E0AA5F55C066F0F2ACC774BBF095B21E07E64"
        },
        {
            "Account": "rNruDQQDBM117pzRob2Br211HJrFKxk3tB",
            "Balance": "396135019885",
            "Flags": 0,
            "LedgerEntryType": "AccountRoot",
            "OwnerCount": 4,
            "PreviousTxnID": "F0ABD2B67746E06426FDA6DF84CCD25DC17031DEDEE81C0C67330CB6037A553E",
            "PreviousTxnLgrSeq": 5935310,
            "Sequence": 60,
            "index": "87CCB134D4AAD9BD28308EE263F89FE0889C60097E6AB0425CD7F21A222F1B87"
        },
        {
            "Balance": {
                "currency": "JPY",
                "issuer": "rrrrrrrrrrrrrrrrrrrrBZbvji",
                "value": "3000.000000097762"
            },
            "Flags": 1114112,
            "HighLimit": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "0"
            },
            "HighNode": "0000000000000000",
            "LedgerEntryType": "RippleState",
            "LowLimit": {
                "currency": "JPY",
                "issuer": "rwFdyL8LVBYkRu8nwkJNKiScRMZnjcrBu6",
                "value": "10000"
            },
            "LowNode": "0000000000000000",
            "PreviousTxnID": "CA4AF74074DA98195310591220FFA9536F13C7030E909366DD9553B8BC1A48A7",
            "PreviousTxnLgrSeq": 6060610,
            "index": "886DFEEC874F4ED640DA896FEE1C8D690F34D478F84C51877AF326E431753F16"
        },
        {
            "Flags": 0,
            "Indexes": [
                "7548EDD4EE8582725A58ECB6D7E70A5DED5E05A8A3BB9C2BF8062742CB9B8225",
                "665C464568A1F581501591A6ED36E39B78381679AA7F4B667CDEFE5E347855DB",
                "D70DB4444E22FC2FB6087B24897F7AB63D05E60C1CFFE764072D84463DD5C8A0"
            ],
            "LedgerEntryType": "DirectoryNode",
            "Owner": "rpRzczN3gPxXMRzqMR98twVsH63xATHUb7",
            "RootIndex": "88B553F99C55E23BBA1F3F8079271C38264D861FC518022F195F89C1F2CCC108",
            "index": "88B553F99C55E23BBA1F3F8079271C38264D861FC518022F195F89C1F2CCC108"
        },
        {
            "Balance": {
                "currency": "JPY",
                "issuer": "rrrrrrrrrrrrrrrrrrrrBZbvji",
                "value": "0"
            },
            "Flags": 1114112,
            "HighLimit": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "0"
            },
            "HighNode": "0000000000000000",
            "LedgerEntryType": "RippleState",
            "LowLimit": {
                "currency": "JPY",
                "issuer": "rpW8wvWYx1SZbYKJVXt9A7rtayPgULa11B",
                "value": "100000"
            },
            "LowNode": "0000000000000000",
            "PreviousTxnID": "C3F1E0C57A24595344ACFD2C6A633F4F375B52FF49B90DEC6863693AA9AFF738",
            "PreviousTxnLgrSeq": 5894665,
            "index": "8BF4D2FB788EF310C2B52498DC38DCA195CDAFA7C608B48EFBA2CACC96988D3A"
        },
        {
            "Account": "ra284e11Q432pmnoKJY9WC77XN8GUsQvYc",
            "BookDirectory": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F071AFD498D0000",
            "BookNode": "0000000000000000",
            "Flags": 131072,
            "LedgerEntryType": "Offer",
            "OwnerNode": "0000000000000000",
            "PreviousTxnID": "1936C067CA71ED1F2C8C058817DD4CD831CA7BE8E921525323B30B30218C00FB",
            "PreviousTxnLgrSeq": 6122985,
            "Sequence": 75,
            "TakerGets": "500000000",
            "TakerPays": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "1000"
            },
            "index": "8C3B2102834932190981192CC03CA723ABB0BFAA1CA2881D16A6FF47C8A75159"
        },
        {
            "Balance": {
                "currency": "JPY",
                "issuer": "rrrrrrrrrrrrrrrrrrrrBZbvji",
                "value": "0"
            },
            "Flags": 65536,
            "HighLimit": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "0"
            },
            "HighNode": "0000000000000000",
            "LedgerEntryType": "RippleState",
            "LowLimit": {
                "currency": "JPY",
                "issuer": "rG1JXRtt7VqxwRt4CNASY1KCJ8xZtMAvCy",
                "value": "30000000000"
            },
            "LowNode": "0000000000000000",
            "PreviousTxnID": "FFB583AFD8B424CF0DE05CCF9934935356998D186BFB2FC7497BC90DA7932688",
            "PreviousTxnLgrSeq": 5720244,
            "index": "8E12E77B98570C4D5A9CBBB296A526068EB47ACBB34FB81F56EE05514D687D55"
        },
        {
            "Flags": 0,
            "Indexes": [
                "886DFEEC874F4ED640DA896FEE1C8D690F34D478F84C51877AF326E431753F16",
                "DD9129C14E714E872D366AE7386874E02DDDE12FAFC43CB4A1FD6782E25B55A2"
            ],
            "LedgerEntryType": "DirectoryNode",
            "Owner": "rwFdyL8LVBYkRu8nwkJNKiScRMZnjcrBu6",
            "RootIndex": "906DA47DC58F59EBDAB48E91B8C0B25DDFE52877BD8196E670D5DD96149439F2",
            "index": "906DA47DC58F59EBDAB48E91B8C0B25DDFE52877BD8196E670D5DD96149439F2"
        },
        {
            "Account": "rUZjAUwatwbS2WHGYNNwbcv8QvYYq8QLC3",
            "BookDirectory": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB056038D7EA4C68000",
            "BookNode": "0000000000000000",
            "Flags": 131072,
            "LedgerEntryType": "Offer",
            "OwnerNode": "0000000000000000",
            "PreviousTxnID": "6CBB344CFBCC0EA1F3DE27CA9F955D3065EA5A930093888E41060449BD80A74E",
            "PreviousTxnLgrSeq": 6032581,
)LDGER09";

char const* ld_10 = R"LDGER10(
            "Sequence": 578,
            "TakerGets": "2000000000",
            "TakerPays": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "20000000000"
            },
            "index": "91F178D98C547B5976FACEAB5686A3D55EA0E0071DB4FB50D6E0C65DD62C0A32"
        },
        {
            "ExchangeRate": "5B050F939563B2B0",
            "Flags": 0,
            "Indexes": ["7051544FA7B0C129F741CB992C9CC769FCC3D605F57DA808536255C7AB147742"],
            "LedgerEntryType": "DirectoryNode",
            "RootIndex": "92466F5377C34C5EA957034339321E217A23FA4E27A31D475B050F939563B2B0",
            "TakerGetsCurrency": "0000000000000000000000004A50590000000000",
            "TakerGetsIssuer": "E5C92828261DBAAC933B6309C6F5C72AF020AFD4",
            "TakerPaysCurrency": "0000000000000000000000000000000000000000",
            "TakerPaysIssuer": "0000000000000000000000000000000000000000",
            "index": "92466F5377C34C5EA957034339321E217A23FA4E27A31D475B050F939563B2B0"
        },
        {
            "Account": "r38Mwd8s2gFevETqCK8e34JYfWBjLUB2nH",
            "Balance": "650402873648",
            "Flags": 0,
            "LedgerEntryType": "AccountRoot",
            "OwnerCount": 35,
            "PreviousTxnID": "129C8EC2C115CA794D443CC0EA17593FE4497A134D01543999C6DF2FEDA44DF8",
            "PreviousTxnLgrSeq": 6224477,
            "Sequence": 723,
            "index": "9436B21B69807F45C69F8D45981E1D59FE3BE95ECAE56DA6625BA661E07EB9FA"
        },
        {
            "Account": "rpNEwuT1D3TkmeTq8tu6nsPgeKe8oWJ9kN",
            "Balance": "61304248589",
            "Flags": 0,
            "LedgerEntryType": "AccountRoot",
            "OwnerCount": 2,
            "PreviousTxnID": "3B245FD46C9CF5102AE8A6236DE12BB0327E2F19E4BEF74950CF32E522A8E9A5",
            "PreviousTxnLgrSeq": 6092921,
            "Sequence": 16,
            "index": "9459567CFCF1A561761A3DEEED489638AA9EC4D66AD4F730D5A59A3F4B904485"
        },
        {
            "Balance": {
                "currency": "JPY",
                "issuer": "rrrrrrrrrrrrrrrrrrrrBZbvji",
                "value": "0"
            },
            "Flags": 1114112,
            "HighLimit": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "0"
            },
            "HighNode": "0000000000000000",
            "LedgerEntryType": "RippleState",
            "LowLimit": {
                "currency": "JPY",
                "issuer": "rKL5uUYcpSGcsVe2Yen5okfhGvi4J57mcM",
                "value": "500000"
            },
            "LowNode": "0000000000000000",
            "PreviousTxnID": "5B70ED270612CE2D8FCCACF9516CFD81F4E51F680CA0AE8BFA03DBDD327E686E",
            "PreviousTxnLgrSeq": 6100690,
            "index": "95D79D06AD590C176743857C5FA02EAD10C714E21BC7C6799EFCD6ECDE68E148"
        },
        {
            "Account": "r3Y3Hh7abFiS9sTgCenK2kk2iToRhFfNs6",
            "BookDirectory": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB05011C37937D8DEE0",
            "BookNode": "0000000000000000",
            "Flags": 131072,
            "LedgerEntryType": "Offer",
            "OwnerNode": "0000000000000000",
            "PreviousTxnID": "8F717C3F4DF084D600C1D594983FD292F66FC7B29DE9DE2B3101C0CB5A6B2988",
            "PreviousTxnLgrSeq": 6123065,
            "Sequence": 1103,
            "TakerGets": "20000000000",
            "TakerPays": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "999999.9999"
            },
            "index": "95D87B2D2138AB924A9665DDF2B4C9E8CC4749D2F9CA94741435E8E2AE99675E"
        },
        {
            "Flags": 0,
            "Indexes": [
                "E88430B715DFD7B2D62A24F002657558407EB429568257BBF942C1FD0E6C3CD3",
                "3D9FF34F845CBF920116593E8C7C7492F1A1B549FF966BB5D54D6DE1D8320721",
                "A967C6187ECA7BCC94BFD904A52284C9C331400FF0448B449686F9DFCDC5C5BE"
            ],
            "LedgerEntryType": "DirectoryNode",
            "Owner": "rUkPuKD5mEkvnrPcvBeBSqe1m9isAMVX5M",
            "RootIndex": "996E9BAA2DA36D592BE2FE131E30CFEF998475F7C657A55477333FB0E9B75132",
            "index": "996E9BAA2DA36D592BE2FE131E30CFEF998475F7C657A55477333FB0E9B75132"
        },
        {
            "Balance": {
                "currency": "CNY",
                "issuer": "rrrrrrrrrrrrrrrrrrrrBZbvji",
                "value": "260.5529101001182"
            },
            "Flags": 65536,
            "HighLimit": {
                "currency": "CNY",
                "issuer": "rEcnyLQD7LXPqTTRG3cXgzcK1C3TDkuUWb",
                "value": "0"
            },
            "HighNode": "0000000000000000",
            "LedgerEntryType": "RippleState",
            "LowLimit": {
                "currency": "CNY",
                "issuer": "rE46UhBPrBmWAbuthcEgVL4dQs3khM4fnP",
                "value": "50000"
            },
            "LowNode": "0000000000000000",
            "PreviousTxnID": "EA44F2B2B152BD453158F822034FEB6B6334DAE4CF838113702C248AEC7DF9FF",
            "PreviousTxnLgrSeq": 5204961,
            "index": "9991CD45AC741BE86C45242CDD6BF73010C160BBDE4FB45BA28326BE4B3A89FD"
        },
        {
            "Account": "rwonczT4eRKiEPb3YvcViUxvSxgJuPfngh",
            "BookDirectory": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F04CBD15E726000",
            "BookNode": "0000000000000000",
            "Flags": 131072,
            "LedgerEntryType": "Offer",
            "OwnerNode": "0000000000000000",
            "PreviousTxnID": "45C6552573FB7FC8AA0D83F9B13BECF82E1AB9FC45C0F45C74EFDB2AD26687C4",
            "PreviousTxnLgrSeq": 6020760,
            "Sequence": 16,
            "TakerGets": "4000000000",
            "TakerPays": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "5400"
            },
            "index": "9F9DABE91A4BDEE0B743753A30207D4D3AE4BD5CA94201A04062079C3F42FEA4"
        },
        {
            "Account": "rM3X3QSr8icjTGpaF52dozhbT2BZSXJQYM",
            "BookDirectory": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04E1A2DE143BFDD2B",
            "BookNode": "0000000000000000",
            "Flags": 0,
            "LedgerEntryType": "Offer",
            "OwnerNode": "0000000000000000",
            "PreviousTxnID": "E535A11DDC4CD6516D5C336993175C715C96DCEF4A923D710715B7679B76D94A",
            "PreviousTxnLgrSeq": 6226513,
            "Sequence": 44627,
            "TakerGets": "1764196200",
            "TakerPays": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "1300"
            },
            "index": "A12A9C28748191A8C3B8B386873C26CEC6E3BED4A5D3CDBC6F378FDD631B5696"
        },
        {
            "Account": "rajrdNafcXefrq4pYW1YAjMUDoxttSLefM",
            "BookDirectory": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04E1E8DA789118000",
            "BookNode": "0000000000000000",
            "Flags": 131072,
            "LedgerEntryType": "Offer",
            "OwnerNode": "0000000000000000",
            "PreviousTxnID": "ED4BE68BEC02EFCA2093C963F39C1B9E10DBE0FC8525998E0A5DE60A0B9BFC5B",
            "PreviousTxnLgrSeq": 6200156,
            "Sequence": 166,
            "TakerGets": "1000000000",
            "TakerPays": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "860"
            },
            "index": "A20B9C6B254C8AD6C301F21AD0E98A523B0F394F45CB498205C11FF90FC05824"
        },
        {
            "Balance": {
                "currency": "JPY",
                "issuer": "rrrrrrrrrrrrrrrrrrrrBZbvji",
                "value": "6.27500000000001"
            },
            "Flags": 65536,
            "HighLimit": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "0"
            },
            "HighNode": "0000000000000000",
            "LedgerEntryType": "RippleState",
            "LowLimit": {
                "currency": "JPY",
                "issuer": "rU8axbJNWix3k3LCTXtL8T8LeFtv88ibMe",
                "value": "0"
            },
            "LowNode": "0000000000000000",
            "PreviousTxnID": "5EA36E5E0BBCFDCD27C5C6B637D759638654412E0E14D5925ADC0A5246D5B9E8",
            "PreviousTxnLgrSeq": 5871532,
            "index": "A58F531945492C5270C9D364632996C152E12516EC235EE7DF1133876E23BBA0"
        },
        {
)LDGER10";

char const* ld_11 = R"LDGER11(
            "Flags": 0,
            "Indexes": [
                "7280EDED4E1FA80C6E5F86D07A70F0E704B1B637F994DC3152FCC7248F5DAB6B",
                "9F9DABE91A4BDEE0B743753A30207D4D3AE4BD5CA94201A04062079C3F42FEA4"
            ],
            "LedgerEntryType": "DirectoryNode",
            "Owner": "rwonczT4eRKiEPb3YvcViUxvSxgJuPfngh",
            "RootIndex": "A5F17A6148AC6A5559261CB4076FBD7DAFFD140A8F216134502AA47405E2D91B",
            "index": "A5F17A6148AC6A5559261CB4076FBD7DAFFD140A8F216134502AA47405E2D91B"
        },
        {
            "Account": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
            "Balance": "126876546",
            "Flags": 0,
            "LedgerEntryType": "AccountRoot",
            "OwnerCount": 1,
            "PreviousTxnID": "0A4045FD93AB9F90EA70D7EB847380B77108A686BB6ECF63A31758FD235BDFF3",
            "PreviousTxnLgrSeq": 6226211,
            "Sequence": 489,
            "index": "A5F37C05FBED611F326E48E6F0D14C6BBAC664CE14ACF4FCC0E959FD60330716"
        },
        {
            "Account": "rLqAVKdGpJt2XpNiF9QKTpjn3AGTQbc6u4",
            "BookDirectory": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04E1AE4DFF8F32000",
            "BookNode": "0000000000000000",
            "Flags": 131072,
            "LedgerEntryType": "Offer",
            "OwnerNode": "0000000000000000",
            "PreviousTxnID": "B8E1D67D4D32C77B7F9B06FE51491D9E359F07D752ECD0C4FF88D08D972BDB06",
            "PreviousTxnLgrSeq": 6224905,
            "Sequence": 7740,
            "TakerGets": "2000000000000",
            "TakerPays": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "1514000"
            },
            "index": "A6AD2AF00F8E45C44F00E7AC5F0D9619A0D91CB26959E654AADA3F8095311949"
        },
        {
            "Account": "rKL5uUYcpSGcsVe2Yen5okfhGvi4J57mcM",
            "BookDirectory": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB052071AFD498D0000",
            "BookNode": "0000000000000000",
            "Flags": 131072,
            "LedgerEntryType": "Offer",
            "OwnerNode": "0000000000000000",
            "PreviousTxnID": "AE307992AF99A493C21530581BCED85F8EB2B8F5045DC3819FC9107D13DFC58A",
            "PreviousTxnLgrSeq": 6100833,
            "Sequence": 42,
            "TakerGets": "100000000",
            "TakerPays": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "200000"
            },
            "index": "A8386F14133CC2ED2A44977209F791F1537F5612AF205F506F6EC9A2AEBF56D1"
        },
        {
            "Account": "r9RR643anesxNCoNkkuYfEfzpxZUeK5Qzw",
            "Balance": "929877732754",
            "Flags": 0,
            "LedgerEntryType": "AccountRoot",
            "OwnerCount": 5,
            "PreviousTxnID": "7B4EE05D265ABECAAF9D7EA65BEE6943571F03A77D1CD50AE01192F944C341ED",
            "PreviousTxnLgrSeq": 6226713,
            "Sequence": 42,
            "index": "A88F72E46968AF5C3E8017793039B56DD0D12085CC0DC8809423991772FEB0C2"
        },
        {
            "Account": "rpNEwuT1D3TkmeTq8tu6nsPgeKe8oWJ9kN",
            "BookDirectory": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F06502C0DC41000",
            "BookNode": "0000000000000000",
            "Flags": 131072,
            "LedgerEntryType": "Offer",
            "OwnerNode": "0000000000000000",
            "PreviousTxnID": "4FD5704C041F9517E9F96117B900024543188541148183614585F2693CFC6CB0",
            "PreviousTxnLgrSeq": 5691507,
            "Sequence": 15,
            "TakerGets": "30000000000",
            "TakerPays": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "53310"
            },
            "index": "A946B2416E147206FC3A19504693390DDBDB976F1801BA35AD685448224C83FE"
        },
        {
            "Account": "rUkPuKD5mEkvnrPcvBeBSqe1m9isAMVX5M",
            "BookDirectory": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F04281BDA632000",
            "BookNode": "0000000000000000",
            "Flags": 131072,
            "LedgerEntryType": "Offer",
            "OwnerNode": "0000000000000000",
            "PreviousTxnID": "604BA63201838ED9ACE56C2529D1D416163F9AF32AFBDFCDAB1929481340746F",
            "PreviousTxnLgrSeq": 5913165,
            "Sequence": 45,
            "TakerGets": "10000000000",
            "TakerPays": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "11700"
            },
            "index": "A967C6187ECA7BCC94BFD904A52284C9C331400FF0448B449686F9DFCDC5C5BE"
        },
        {
            "Flags": 0,
            "Indexes": [
                "AE15437DF9B98E2DB40B6C06C313ADF45B9E38B700A4C3462AFB61DBC5FFAFB8",
                "7D76318C279034B7ACE1231F16BF345DF8FCF6EC40E99E17091AD56B19867AE3"
            ],
            "LedgerEntryType": "DirectoryNode",
            "Owner": "rhTUpdUStwn7wPnzNMjHEfFgQacPC5eop1",
            "RootIndex": "AA823F672B8FE1254F55411AB338B2FC7F2C812E09347ED2F27FB7F804B74654",
            "index": "AA823F672B8FE1254F55411AB338B2FC7F2C812E09347ED2F27FB7F804B74654"
        },
        {
            "Account": "rngNbgfn7cT4bHbHJPNoPY12R66a4RMMaa",
            "BookDirectory": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F22D10C4ECC8000",
            "BookNode": "0000000000000000",
            "Flags": 131072,
            "LedgerEntryType": "Offer",
            "OwnerNode": "0000000000000000",
            "PreviousTxnID": "86EC4C57765777A2557DA123CAE1C2C8BE8BE3FBF1893FFC1049B6C93043FFD6",
            "PreviousTxnLgrSeq": 5928241,
            "Sequence": 27,
            "TakerGets": "150000000",
            "TakerPays": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "1470"
            },
            "index": "AC579B09EB6B609DBAAB2CCF4AE3F59B2D1F56072A4B0E63001621877E7ADEA0"
        },
        {
            "Flags": 0,
            "Indexes": [
                "1F5482CD6E2A5CCD6902AA599FE63A635A5263C5D2E59A3C5697D0DD5C760B32",
                "A6AD2AF00F8E45C44F00E7AC5F0D9619A0D91CB26959E654AADA3F8095311949"
            ],
            "LedgerEntryType": "DirectoryNode",
            "Owner": "rLqAVKdGpJt2XpNiF9QKTpjn3AGTQbc6u4",
            "RootIndex": "ACD18A96B451994A3DBFCD3744E4D796943BFE5EBD82AE2E62682608F615DC7B",
            "index": "ACD18A96B451994A3DBFCD3744E4D796943BFE5EBD82AE2E62682608F615DC7B"
        },
        {
            "Account": "rwonczT4eRKiEPb3YvcViUxvSxgJuPfngh",
            "Balance": "71397725290",
            "Flags": 0,
            "LedgerEntryType": "AccountRoot",
            "OwnerCount": 4,
            "PreviousTxnID": "0827D5700BA798AB3B248C57C1BF59EAB18B2B47958B547A8D31FED8BFC49D82",
            "PreviousTxnLgrSeq": 6181555,
            "Sequence": 30,
            "index": "ADB0EB1E453657BD55C05F487888A2BC2D71623E1887F751770648B39D271B97"
        },
        {
            "Balance": {
                "currency": "JPY",
                "issuer": "rrrrrrrrrrrrrrrrrrrrBZbvji",
                "value": "2058.500001172999"
            },
            "Flags": 1114112,
            "HighLimit": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "0"
            },
            "HighNode": "0000000000000000",
            "LedgerEntryType": "RippleState",
            "LowLimit": {
                "currency": "JPY",
                "issuer": "rhTUpdUStwn7wPnzNMjHEfFgQacPC5eop1",
                "value": "50000"
            },
            "LowNode": "0000000000000000",
            "PreviousTxnID": "B8FA447BC272FA1ACE601F1943DAABEFA9A1928E53289CA846B8D581F7E89132",
            "PreviousTxnLgrSeq": 6130142,
            "index": "AE15437DF9B98E2DB40B6C06C313ADF45B9E38B700A4C3462AFB61DBC5FFAFB8"
        },
        {
            "Account": "rUkPuKD5mEkvnrPcvBeBSqe1m9isAMVX5M",
            "Balance": "19762621460",
            "Flags": 0,
            "LedgerEntryType": "AccountRoot",
            "OwnerCount": 5,
            "PreviousTxnID": "0AD853FC456DC4D8D807764BCB21DE3AD6E94A4DD4AB8C01214075699F6509CD",
            "PreviousTxnLgrSeq": 6183335,
            "Sequence": 50,
            "index": "AE17DB7081B9CE22EEC1E5CA080D7EC96C0683D0FE32A2C8C93E99AB9559E8F3"
        },
        {
            "Balance": {
                "currency": "CNY",
                "issuer": "rrrrrrrrrrrrrrrrrrrrBZbvji",
                "value": "-292.7422499377242"
            },
            "Flags": 131072,
            "HighLimit": {
                "currency": "CNY",
                "issuer": "rEcnyLQD7LXPqTTRG3cXgzcK1C3TDkuUWb",
)LDGER11";

char const* ld_12 = R"LDGER12(
                "value": "3012"
            },
            "HighNode": "0000000000000000",
            "LedgerEntryType": "RippleState",
            "LowLimit": {
                "currency": "CNY",
                "issuer": "razqQKzJRdB4UxFPWf5NEpEG3WMkmwgcXA",
                "value": "0"
            },
            "LowNode": "0000000000000000",
            "PreviousTxnID": "4240A0D061602F2DD5924243365A6CB35EFDD8B2991810C609648770E9B374A4",
            "PreviousTxnLgrSeq": 6218734,
            "index": "AE39B7DAF9C3C9E5E1C0C6A758F41D22F81B5CE2D44C128F0ECD949B56D67804"
        },
        {
            "Account": "ratarRi5YVgBDTHbt7rTPdmCMehH6zge2T",
            "BookDirectory": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04E1BB60F053F8000",
            "BookNode": "0000000000000000",
            "Flags": 131072,
            "LedgerEntryType": "Offer",
            "OwnerNode": "0000000000000000",
            "PreviousTxnID": "831CF6C924F7A7E509D0D41AA2C3EE7D174B622D311D8DD4194D2BF6580014EB",
            "PreviousTxnLgrSeq": 6200265,
            "Sequence": 125,
            "TakerGets": "9100000000",
            "TakerPays": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "7098"
            },
            "index": "AEA8F9EBE0F130645D376D673A2A95695FC726541C6F4267DD2DD94722D7BF45"
        },
        {
            "Balance": {
                "currency": "JPY",
                "issuer": "rrrrrrrrrrrrrrrrrrrrBZbvji",
                "value": "176594.1099885048"
            },
            "Flags": 1114112,
            "HighLimit": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "0"
            },
            "HighNode": "0000000000000000",
            "LedgerEntryType": "RippleState",
            "LowLimit": {
                "currency": "JPY",
                "issuer": "rw7dJmysoqzguZDYyULBh5HqXdNQikMDtc",
                "value": "100000"
            },
            "LowNode": "0000000000000000",
            "PreviousTxnID": "2F99153B1E3979AAB6F4C82B1E700102F65426AB9CFEDB62E3B12E15A0A97E38",
            "PreviousTxnLgrSeq": 6226460,
            "index": "B2490D6B802B0A5CA82C91EADE4504E893B8A20E732BD16F6A0EB43F36191356"
        },
        {
            "Account": "rE46UhBPrBmWAbuthcEgVL4dQs3khM4fnP",
            "BookDirectory": "5EB7785286CB89D3B705046BFA0EDB1082E1116CD9EA35885C11C37937E08000",
            "BookNode": "0000000000000000",
            "Flags": 0,
            "LedgerEntryType": "Offer",
            "OwnerNode": "0000000000000000",
            "PreviousTxnID": "EA44F2B2B152BD453158F822034FEB6B6334DAE4CF838113702C248AEC7DF9FF",
            "PreviousTxnLgrSeq": 5204961,
            "Sequence": 7,
            "TakerGets": {
                "currency": "CNY",
                "issuer": "rEcnyLQD7LXPqTTRG3cXgzcK1C3TDkuUWb",
                "value": "24.65999991311782"
            },
            "TakerPays": "1232999987",
            "index": "B27436F9FC495FFE07D907E69B59BCFA4F5150BABCBC5DE9F5AFCD00A9E99236"
        },
        {
            "Account": "ra284e11Q432pmnoKJY9WC77XN8GUsQvYc",
            "Balance": "526898859",
            "Flags": 0,
            "LedgerEntryType": "AccountRoot",
            "OwnerCount": 7,
            "PreviousTxnID": "972B7E0B2C10896874F1669FBF87CAF1D40936F6CFAEBB20C7412864A36CD796",
            "PreviousTxnLgrSeq": 6203295,
            "Sequence": 87,
            "index": "B62382AC9103B1B0BA4B7A27392CE0DD1AECB65415FF2CEF6F7AD84BB32ED3D7"
        },
        {
            "Account": "r3Y3Hh7abFiS9sTgCenK2kk2iToRhFfNs6",
            "BookDirectory": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F05543DF724A9EA",
            "BookNode": "0000000000000000",
            "Flags": 131072,
            "LedgerEntryType": "Offer",
            "OwnerNode": "0000000000000000",
            "PreviousTxnID": "C0BFFBCE8B8556957B6A5E2845B2C6550E201826BEFD765C6B397E8C151F8665",
            "PreviousTxnLgrSeq": 6091042,
            "Sequence": 1053,
            "TakerGets": "30000000000",
            "TakerPays": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "44999.99999"
            },
            "index": "B7352CF1A28793675F07C559BD330181CB6669E6A71DA971EBA8A16C342323E7"
        },
        {
            "Account": "rw7dJmysoqzguZDYyULBh5HqXdNQikMDtc",
            "BookDirectory": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F039696F3392000",
            "BookNode": "0000000000000000",
            "Flags": 131072,
            "LedgerEntryType": "Offer",
            "OwnerNode": "0000000000000000",
            "PreviousTxnID": "61F049742C05CC472A7857840477B826C74DE61796BD0B7A235A15F9BE2B0B1C",
            "PreviousTxnLgrSeq": 5990006,
            "Sequence": 514,
            "TakerGets": "37974000000",
            "TakerPays": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "38353.74"
            },
            "index": "B7935B2A2140B443F557B32CA75EC71F64CAD9CF630CAC9D251E2B536980B98B"
        },
        {
            "ExchangeRate": "4E1999C9B1822A51",
            "Flags": 0,
            "Indexes": ["75373DB421A31947A0533DCCFBF70BD6D5E6B70F14D178EF6D114892156C44B5"],
            "LedgerEntryType": "DirectoryNode",
            "RootIndex": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04E1999C9B1822A51",
            "TakerGetsCurrency": "0000000000000000000000000000000000000000",
            "TakerGetsIssuer": "0000000000000000000000000000000000000000",
            "TakerPaysCurrency": "0000000000000000000000004A50590000000000",
            "TakerPaysIssuer": "E5C92828261DBAAC933B6309C6F5C72AF020AFD4",
            "index": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04E1999C9B1822A51"
        },
        {
            "ExchangeRate": "4E1A1F7606E0BC3C",
            "Flags": 0,
            "Indexes": ["F984915B0302CE07E061BC46C82574C37E49B6BF138C5AF092F779F0EE75C3FF"],
            "LedgerEntryType": "DirectoryNode",
            "RootIndex": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04E1A1F7606E0BC3C",
            "TakerGetsCurrency": "0000000000000000000000000000000000000000",
            "TakerGetsIssuer": "0000000000000000000000000000000000000000",
            "TakerPaysCurrency": "0000000000000000000000004A50590000000000",
            "TakerPaysIssuer": "E5C92828261DBAAC933B6309C6F5C72AF020AFD4",
            "index": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04E1A1F7606E0BC3C"
        },
        {
            "ExchangeRate": "4E1A2DE143BFDD2B",
            "Flags": 0,
            "Indexes": ["A12A9C28748191A8C3B8B386873C26CEC6E3BED4A5D3CDBC6F378FDD631B5696"],
            "LedgerEntryType": "DirectoryNode",
            "RootIndex": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04E1A2DE143BFDD2B",
            "TakerGetsCurrency": "0000000000000000000000000000000000000000",
            "TakerGetsIssuer": "0000000000000000000000000000000000000000",
            "TakerPaysCurrency": "0000000000000000000000004A50590000000000",
            "TakerPaysIssuer": "E5C92828261DBAAC933B6309C6F5C72AF020AFD4",
            "index": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04E1A2DE143BFDD2B"
        },
        {
            "ExchangeRate": "4E1AD2AF5C0DE000",
            "Flags": 0,
            "Indexes": ["15483FA685F65E020C876D69BD01FA7DDB05A753C11B32C98494DE114B7943EF"],
            "LedgerEntryType": "DirectoryNode",
            "RootIndex": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04E1AD2AF5C0DE000",
            "TakerGetsCurrency": "0000000000000000000000000000000000000000",
            "TakerGetsIssuer": "0000000000000000000000000000000000000000",
            "TakerPaysCurrency": "0000000000000000000000004A50590000000000",
            "TakerPaysIssuer": "E5C92828261DBAAC933B6309C6F5C72AF020AFD4",
            "index": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04E1AD2AF5C0DE000"
        },
        {
            "ExchangeRate": "4E1AE3F7244E1000",
            "Flags": 0,
            "Indexes": ["26B697285E56D3E89C7FA172314359BD607AB22AED7124C239443FE0DAE7E162"],
            "LedgerEntryType": "DirectoryNode",
            "RootIndex": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04E1AE3F7244E1000",
            "TakerGetsCurrency": "0000000000000000000000000000000000000000",
            "TakerGetsIssuer": "0000000000000000000000000000000000000000",
            "TakerPaysCurrency": "0000000000000000000000004A50590000000000",
            "TakerPaysIssuer": "E5C92828261DBAAC933B6309C6F5C72AF020AFD4",
            "index": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04E1AE3F7244E1000"
        },
        {
            "ExchangeRate": "4E1AE4DFF8F32000",
            "Flags": 0,
            "Indexes": ["A6AD2AF00F8E45C44F00E7AC5F0D9619A0D91CB26959E654AADA3F8095311949"],
            "LedgerEntryType": "DirectoryNode",
            "RootIndex": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04E1AE4DFF8F32000",
            "TakerGetsCurrency": "0000000000000000000000000000000000000000",
            "TakerGetsIssuer": "0000000000000000000000000000000000000000",
            "TakerPaysCurrency": "0000000000000000000000004A50590000000000",
            "TakerPaysIssuer": "E5C92828261DBAAC933B6309C6F5C72AF020AFD4",
            "index": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04E1AE4DFF8F32000"
        },
        {
            "ExchangeRate": "4E1AEA1C83351F08",
            "Flags": 0,
            "Indexes": ["D770FB84E4ED16B67C925F7BAD094E52D48297D6375BAC0A8F30539BADBAC36F"],
            "LedgerEntryType": "DirectoryNode",
            "RootIndex": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04E1AEA1C83351F08",
            "TakerGetsCurrency": "0000000000000000000000000000000000000000",
)LDGER12";

char const* ld_13 = R"LDGER13(
            "TakerGetsIssuer": "0000000000000000000000000000000000000000",
            "TakerPaysCurrency": "0000000000000000000000004A50590000000000",
            "TakerPaysIssuer": "E5C92828261DBAAC933B6309C6F5C72AF020AFD4",
            "index": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04E1AEA1C83351F08"
        },
        {
            "ExchangeRate": "4E1AF627C1335000",
            "Flags": 0,
            "Indexes": ["82F0B686571941A10925823D930FC2C1197904D250F282E0496B6A85D3D9B5B6"],
            "LedgerEntryType": "DirectoryNode",
            "RootIndex": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04E1AF627C1335000",
            "TakerGetsCurrency": "0000000000000000000000000000000000000000",
            "TakerGetsIssuer": "0000000000000000000000000000000000000000",
            "TakerPaysCurrency": "0000000000000000000000004A50590000000000",
            "TakerPaysIssuer": "E5C92828261DBAAC933B6309C6F5C72AF020AFD4",
            "index": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04E1AF627C1335000"
        },
        {
            "ExchangeRate": "4E1BACF6B6CCE000",
            "Flags": 0,
            "Indexes": ["DFDB7E0EC2F3FDD188747CB63EAE1FACDD46AB4E6A2DCF97AD61E0A178656420"],
            "LedgerEntryType": "DirectoryNode",
            "RootIndex": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04E1BACF6B6CCE000",
            "TakerGetsCurrency": "0000000000000000000000000000000000000000",
            "TakerGetsIssuer": "0000000000000000000000000000000000000000",
            "TakerPaysCurrency": "0000000000000000000000004A50590000000000",
            "TakerPaysIssuer": "E5C92828261DBAAC933B6309C6F5C72AF020AFD4",
            "index": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04E1BACF6B6CCE000"
        },
        {
            "ExchangeRate": "4E1BB60F053F8000",
            "Flags": 0,
            "Indexes": ["AEA8F9EBE0F130645D376D673A2A95695FC726541C6F4267DD2DD94722D7BF45"],
            "LedgerEntryType": "DirectoryNode",
            "RootIndex": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04E1BB60F053F8000",
            "TakerGetsCurrency": "0000000000000000000000000000000000000000",
            "TakerGetsIssuer": "0000000000000000000000000000000000000000",
            "TakerPaysCurrency": "0000000000000000000000004A50590000000000",
            "TakerPaysIssuer": "E5C92828261DBAAC933B6309C6F5C72AF020AFD4",
            "index": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04E1BB60F053F8000"
        },
        {
            "ExchangeRate": "4E1BF960279E0408",
            "Flags": 0,
            "Indexes": ["F37871AD76189305B0BA6A652A69C4207C384DA95336418A1A474D938E768BEE"],
            "LedgerEntryType": "DirectoryNode",
            "RootIndex": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04E1BF960279E0408",
            "TakerGetsCurrency": "0000000000000000000000000000000000000000",
            "TakerGetsIssuer": "0000000000000000000000000000000000000000",
            "TakerPaysCurrency": "0000000000000000000000004A50590000000000",
            "TakerPaysIssuer": "E5C92828261DBAAC933B6309C6F5C72AF020AFD4",
            "index": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04E1BF960279E0408"
        },
        {
            "ExchangeRate": "4E1CC6E836AE4000",
            "Flags": 0,
            "Indexes": ["043B696FC70C6D48A30808CE1DC45A8495A3F672FD6544113D2610310290315E"],
            "LedgerEntryType": "DirectoryNode",
            "RootIndex": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04E1CC6E836AE4000",
            "TakerGetsCurrency": "0000000000000000000000000000000000000000",
            "TakerGetsIssuer": "0000000000000000000000000000000000000000",
            "TakerPaysCurrency": "0000000000000000000000004A50590000000000",
            "TakerPaysIssuer": "E5C92828261DBAAC933B6309C6F5C72AF020AFD4",
            "index": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04E1CC6E836AE4000"
        },
        {
            "ExchangeRate": "4E1E8DA789118000",
            "Flags": 0,
            "Indexes": ["A20B9C6B254C8AD6C301F21AD0E98A523B0F394F45CB498205C11FF90FC05824"],
            "LedgerEntryType": "DirectoryNode",
            "RootIndex": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04E1E8DA789118000",
            "TakerGetsCurrency": "0000000000000000000000000000000000000000",
            "TakerGetsIssuer": "0000000000000000000000000000000000000000",
            "TakerPaysCurrency": "0000000000000000000000004A50590000000000",
            "TakerPaysIssuer": "E5C92828261DBAAC933B6309C6F5C72AF020AFD4",
            "index": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04E1E8DA789118000"
        },
        {
            "ExchangeRate": "4E1FF973CAFA8000",
            "Flags": 0,
            "Indexes": ["7D7874A4A96EB03A227365EABA10C637681BCCDFCDE4ADB6B73B4CB7F1FD1142"],
            "LedgerEntryType": "DirectoryNode",
            "RootIndex": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04E1FF973CAFA8000",
            "TakerGetsCurrency": "0000000000000000000000000000000000000000",
            "TakerGetsIssuer": "0000000000000000000000000000000000000000",
            "TakerPaysCurrency": "0000000000000000000000004A50590000000000",
            "TakerPaysIssuer": "E5C92828261DBAAC933B6309C6F5C72AF020AFD4",
            "index": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04E1FF973CAFA8000"
        },
        {
            "ExchangeRate": "4E2386F26F5CDB64",
            "Flags": 0,
            "Indexes": ["F56F107800DF0AB585BC31684011DADD77C9A2516A2E47901EDCB0D9A1A0D5E0"],
            "LedgerEntryType": "DirectoryNode",
            "RootIndex": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04E2386F26F5CDB64",
            "TakerGetsCurrency": "0000000000000000000000000000000000000000",
            "TakerGetsIssuer": "0000000000000000000000000000000000000000",
            "TakerPaysCurrency": "0000000000000000000000004A50590000000000",
            "TakerPaysIssuer": "E5C92828261DBAAC933B6309C6F5C72AF020AFD4",
            "index": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04E2386F26F5CDB64"
        },
        {
            "ExchangeRate": "4F038D7EA4C68000",
            "Flags": 0,
            "Indexes": [
                "63A89DA746DBCFF5466F2003BDFC1CAE8C0B15A240F912E780EE84C12BF13554",
                "7FB16A6516304F196127F10ACB771829F45480BC368CB8DBF89266E04E3AE1FD"
            ],
            "LedgerEntryType": "DirectoryNode",
            "RootIndex": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F038D7EA4C68000",
            "TakerGetsCurrency": "0000000000000000000000000000000000000000",
            "TakerGetsIssuer": "0000000000000000000000000000000000000000",
            "TakerPaysCurrency": "0000000000000000000000004A50590000000000",
            "TakerPaysIssuer": "E5C92828261DBAAC933B6309C6F5C72AF020AFD4",
            "index": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F038D7EA4C68000"
        },
        {
            "ExchangeRate": "4F039696F3392000",
            "Flags": 0,
            "Indexes": ["B7935B2A2140B443F557B32CA75EC71F64CAD9CF630CAC9D251E2B536980B98B"],
            "LedgerEntryType": "DirectoryNode",
            "RootIndex": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F039696F3392000",
            "TakerGetsCurrency": "0000000000000000000000000000000000000000",
            "TakerGetsIssuer": "0000000000000000000000000000000000000000",
            "TakerPaysCurrency": "0000000000000000000000004A50590000000000",
            "TakerPaysIssuer": "E5C92828261DBAAC933B6309C6F5C72AF020AFD4",
            "index": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F039696F3392000"
        },
        {
            "ExchangeRate": "4F03A8C7901E6000",
            "Flags": 0,
            "Indexes": ["CE398D77F49EB6B6C995539F6B6F48660660BA4C7C08AE5F633BB2A493E62C06"],
            "LedgerEntryType": "DirectoryNode",
            "RootIndex": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F03A8C7901E6000",
            "TakerGetsCurrency": "0000000000000000000000000000000000000000",
            "TakerGetsIssuer": "0000000000000000000000000000000000000000",
            "TakerPaysCurrency": "0000000000000000000000004A50590000000000",
            "TakerPaysIssuer": "E5C92828261DBAAC933B6309C6F5C72AF020AFD4",
            "index": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F03A8C7901E6000"
        },
        {
            "ExchangeRate": "4F03DF5966CE2000",
            "Flags": 0,
            "Indexes": ["6B79A8D89C4E369336D21ECA23A724A5B1E30DBE2344F66141444165FBE1270F"],
            "LedgerEntryType": "DirectoryNode",
            "RootIndex": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F03DF5966CE2000",
            "TakerGetsCurrency": "0000000000000000000000000000000000000000",
            "TakerGetsIssuer": "0000000000000000000000000000000000000000",
            "TakerPaysCurrency": "0000000000000000000000004A50590000000000",
            "TakerPaysIssuer": "E5C92828261DBAAC933B6309C6F5C72AF020AFD4",
            "index": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F03DF5966CE2000"
        },
        {
            "ExchangeRate": "4F03E871B5391EE0",
            "Flags": 0,
            "Indexes": ["81721B602049F4A05B7D01208FBDDCA4183B1BEE6A3540E694E3D71B81057A27"],
            "LedgerEntryType": "DirectoryNode",
            "RootIndex": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F03E871B5391EE0",
            "TakerGetsCurrency": "0000000000000000000000000000000000000000",
            "TakerGetsIssuer": "0000000000000000000000000000000000000000",
            "TakerPaysCurrency": "0000000000000000000000004A50590000000000",
            "TakerPaysIssuer": "E5C92828261DBAAC933B6309C6F5C72AF020AFD4",
            "index": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F03E871B5391EE0"
        },
        {
            "ExchangeRate": "4F03E871B540C000",
            "Flags": 0,
            "Indexes": [
                "181402989B9E8DA57D49E65310E69A63C1117B1B7D89E2A1E96492C033FD4BEE",
                "D9EC3E44A4235F4B89BCB8C34BF39850A01449BBB4F70722A5E03FB71EF4EA57"
            ],
            "LedgerEntryType": "DirectoryNode",
            "RootIndex": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F03E871B540C000",
            "TakerGetsCurrency": "0000000000000000000000000000000000000000",
            "TakerGetsIssuer": "0000000000000000000000000000000000000000",
            "TakerPaysCurrency": "0000000000000000000000004A50590000000000",
            "TakerPaysIssuer": "E5C92828261DBAAC933B6309C6F5C72AF020AFD4",
            "index": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F03E871B540C000"
        },
        {
            "ExchangeRate": "4F04281BDA632000",
            "Flags": 0,
            "Indexes": ["A967C6187ECA7BCC94BFD904A52284C9C331400FF0448B449686F9DFCDC5C5BE"],
            "LedgerEntryType": "DirectoryNode",
            "RootIndex": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F04281BDA632000",
            "TakerGetsCurrency": "0000000000000000000000000000000000000000",
            "TakerGetsIssuer": "0000000000000000000000000000000000000000",
            "TakerPaysCurrency": "0000000000000000000000004A50590000000000",
            "TakerPaysIssuer": "E5C92828261DBAAC933B6309C6F5C72AF020AFD4",
            "index": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F04281BDA632000"
        },
        {
            "ExchangeRate": "4F04CBD15E726000",
            "Flags": 0,
            "Indexes": ["9F9DABE91A4BDEE0B743753A30207D4D3AE4BD5CA94201A04062079C3F42FEA4"],
            "LedgerEntryType": "DirectoryNode",
            "RootIndex": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F04CBD15E726000",
            "TakerGetsCurrency": "0000000000000000000000000000000000000000",
            "TakerGetsIssuer": "0000000000000000000000000000000000000000",
            "TakerPaysCurrency": "0000000000000000000000004A50590000000000",
)LDGER13";

char const* ld_14 = R"LDGER14(
            "TakerPaysIssuer": "E5C92828261DBAAC933B6309C6F5C72AF020AFD4",
            "index": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F04CBD15E726000"
        },
        {
            "ExchangeRate": "4F04F94AE6AF8000",
            "Flags": 0,
            "Indexes": ["EB7C3A1CD0DB012AD336262CE4E47113F0E59D0F44E18359B9BD788DBD426B7E"],
            "LedgerEntryType": "DirectoryNode",
            "RootIndex": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F04F94AE6AF8000",
            "TakerGetsCurrency": "0000000000000000000000000000000000000000",
            "TakerGetsIssuer": "0000000000000000000000000000000000000000",
            "TakerPaysCurrency": "0000000000000000000000004A50590000000000",
            "TakerPaysIssuer": "E5C92828261DBAAC933B6309C6F5C72AF020AFD4",
            "index": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F04F94AE6AF8000"
        },
        {
            "ExchangeRate": "4F05543DF724A9EA",
            "Flags": 0,
            "Indexes": ["B7352CF1A28793675F07C559BD330181CB6669E6A71DA971EBA8A16C342323E7"],
            "LedgerEntryType": "DirectoryNode",
            "RootIndex": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F05543DF724A9EA",
            "TakerGetsCurrency": "0000000000000000000000000000000000000000",
            "TakerGetsIssuer": "0000000000000000000000000000000000000000",
            "TakerPaysCurrency": "0000000000000000000000004A50590000000000",
            "TakerPaysIssuer": "E5C92828261DBAAC933B6309C6F5C72AF020AFD4",
            "index": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F05543DF724A9EA"
        },
        {
            "ExchangeRate": "4F05AF3107A40000",
            "Flags": 0,
            "Indexes": ["3D9FF34F845CBF920116593E8C7C7492F1A1B549FF966BB5D54D6DE1D8320721"],
            "LedgerEntryType": "DirectoryNode",
            "RootIndex": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F05AF3107A40000",
            "TakerGetsCurrency": "0000000000000000000000000000000000000000",
            "TakerGetsIssuer": "0000000000000000000000000000000000000000",
            "TakerPaysCurrency": "0000000000000000000000004A50590000000000",
            "TakerPaysIssuer": "E5C92828261DBAAC933B6309C6F5C72AF020AFD4",
            "index": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F05AF3107A40000"
        },
        {
            "ExchangeRate": "4F0649CE3D40A000",
            "Flags": 0,
            "Indexes": ["C46FA7924251F67DADC69D6FCB71D4BA2167BE7EA0615078E466FAB236D88BF6"],
            "LedgerEntryType": "DirectoryNode",
            "RootIndex": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F0649CE3D40A000",
            "TakerGetsCurrency": "0000000000000000000000000000000000000000",
            "TakerGetsIssuer": "0000000000000000000000000000000000000000",
            "TakerPaysCurrency": "0000000000000000000000004A50590000000000",
            "TakerPaysIssuer": "E5C92828261DBAAC933B6309C6F5C72AF020AFD4",
            "index": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F0649CE3D40A000"
        },
        {
            "ExchangeRate": "4F06502C0DC41000",
            "Flags": 0,
            "Indexes": ["A946B2416E147206FC3A19504693390DDBDB976F1801BA35AD685448224C83FE"],
            "LedgerEntryType": "DirectoryNode",
            "RootIndex": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F06502C0DC41000",
            "TakerGetsCurrency": "0000000000000000000000000000000000000000",
            "TakerGetsIssuer": "0000000000000000000000000000000000000000",
            "TakerPaysCurrency": "0000000000000000000000004A50590000000000",
            "TakerPaysIssuer": "E5C92828261DBAAC933B6309C6F5C72AF020AFD4",
            "index": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F06502C0DC41000"
        },
        {
            "ExchangeRate": "4F071AFD498D0000",
            "Flags": 0,
            "Indexes": [
                "8C3B2102834932190981192CC03CA723ABB0BFAA1CA2881D16A6FF47C8A75159",
                "5CDE229B1DDC6E52DE881F7DF00942C838DF5AD12AFD6E2780936E823CD02005"
            ],
            "LedgerEntryType": "DirectoryNode",
            "RootIndex": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F071AFD498D0000",
            "TakerGetsCurrency": "0000000000000000000000000000000000000000",
            "TakerGetsIssuer": "0000000000000000000000000000000000000000",
            "TakerPaysCurrency": "0000000000000000000000004A50590000000000",
            "TakerPaysIssuer": "E5C92828261DBAAC933B6309C6F5C72AF020AFD4",
            "index": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F071AFD498D0000"
        },
        {
            "ExchangeRate": "4F07A25028A59C00",
            "Flags": 0,
            "Indexes": ["D70DB4444E22FC2FB6087B24897F7AB63D05E60C1CFFE764072D84463DD5C8A0"],
            "LedgerEntryType": "DirectoryNode",
            "RootIndex": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F07A25028A59C00",
            "TakerGetsCurrency": "0000000000000000000000000000000000000000",
            "TakerGetsIssuer": "0000000000000000000000000000000000000000",
            "TakerPaysCurrency": "0000000000000000000000004A50590000000000",
            "TakerPaysIssuer": "E5C92828261DBAAC933B6309C6F5C72AF020AFD4",
            "index": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F07A25028A59C00"
        },
        {
            "ExchangeRate": "4F07D0E36A818000",
            "Flags": 0,
            "Indexes": ["C4CDCC5A64CF564982F17B71F2131A08DDBB6C9A4D041890BC6F763A1E49E05D"],
            "LedgerEntryType": "DirectoryNode",
            "RootIndex": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F07D0E36A818000",
            "TakerGetsCurrency": "0000000000000000000000000000000000000000",
            "TakerGetsIssuer": "0000000000000000000000000000000000000000",
            "TakerPaysCurrency": "0000000000000000000000004A50590000000000",
            "TakerPaysIssuer": "E5C92828261DBAAC933B6309C6F5C72AF020AFD4",
            "index": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F07D0E36A818000"
        },
        {
            "ExchangeRate": "4F082BD67AFBC000",
            "Flags": 0,
            "Indexes": ["23578AAA82674D543D587F948B2DCD33277AB7BCFEDC0E7132146D94EA9DA78C"],
            "LedgerEntryType": "DirectoryNode",
            "RootIndex": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F082BD67AFBC000",
            "TakerGetsCurrency": "0000000000000000000000000000000000000000",
            "TakerGetsIssuer": "0000000000000000000000000000000000000000",
            "TakerPaysCurrency": "0000000000000000000000004A50590000000000",
            "TakerPaysIssuer": "E5C92828261DBAAC933B6309C6F5C72AF020AFD4",
            "index": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F082BD67AFBC000"
        },
        {
            "ExchangeRate": "4F08CF8BFF0B0000",
            "Flags": 0,
            "Indexes": ["7A599E3DA6A3E67E86CE80B10EA8B3C5C395A6E457C6802D9F51909BA15BB98B"],
            "LedgerEntryType": "DirectoryNode",
            "RootIndex": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F08CF8BFF0B0000",
            "TakerGetsCurrency": "0000000000000000000000000000000000000000",
            "TakerGetsIssuer": "0000000000000000000000000000000000000000",
            "TakerPaysCurrency": "0000000000000000000000004A50590000000000",
            "TakerPaysIssuer": "E5C92828261DBAAC933B6309C6F5C72AF020AFD4",
            "index": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F08CF8BFF0B0000"
        },
        {
            "ExchangeRate": "4F0A964B516E4000",
            "Flags": 0,
            "Indexes": ["8476DF4803ECF5DB945DA2FC41CEC6DFB6D50B829E8577267699920525DDBD5B"],
            "LedgerEntryType": "DirectoryNode",
            "RootIndex": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F0A964B516E4000",
            "TakerGetsCurrency": "0000000000000000000000000000000000000000",
            "TakerGetsIssuer": "0000000000000000000000000000000000000000",
            "TakerPaysCurrency": "0000000000000000000000004A50590000000000",
            "TakerPaysIssuer": "E5C92828261DBAAC933B6309C6F5C72AF020AFD4",
            "index": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F0A964B516E4000"
        },
        {
            "ExchangeRate": "4F0AA87BEE538000",
            "Flags": 0,
            "Indexes": [
                "50C33C456676E0AF7B69397CFEE6612B59F9D294B2C7995023C2B8B748226F4A",
                "6E45279F78A5092B5F92C314C1BF4D23936426E0EA724E433992654A80F5DB6B"
            ],
            "LedgerEntryType": "DirectoryNode",
            "RootIndex": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F0AA87BEE538000",
            "TakerGetsCurrency": "0000000000000000000000000000000000000000",
            "TakerGetsIssuer": "0000000000000000000000000000000000000000",
            "TakerPaysCurrency": "0000000000000000000000004A50590000000000",
            "TakerPaysIssuer": "E5C92828261DBAAC933B6309C6F5C72AF020AFD4",
            "index": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F0AA87BEE538000"
        },
        {
            "ExchangeRate": "4F0C5D0AA3D18000",
            "Flags": 0,
            "Indexes": ["BD619F6D141E357F21E05A20AB14F180409DFB3190F638C2F3F8358AF84FBFD0"],
            "LedgerEntryType": "DirectoryNode",
            "RootIndex": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F0C5D0AA3D18000",
            "TakerGetsCurrency": "0000000000000000000000000000000000000000",
            "TakerGetsIssuer": "0000000000000000000000000000000000000000",
            "TakerPaysCurrency": "0000000000000000000000004A50590000000000",
            "TakerPaysIssuer": "E5C92828261DBAAC933B6309C6F5C72AF020AFD4",
            "index": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F0C5D0AA3D18000"
        },
        {
            "ExchangeRate": "4F0E23C9F634C000",
            "Flags": 0,
            "Indexes": ["1E9215E83CF75C0AC80ABB3F434115E4992981544F009CAF14CAED53DFD79935"],
            "LedgerEntryType": "DirectoryNode",
            "RootIndex": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F0E23C9F634C000",
            "TakerGetsCurrency": "0000000000000000000000000000000000000000",
            "TakerGetsIssuer": "0000000000000000000000000000000000000000",
            "TakerPaysCurrency": "0000000000000000000000004A50590000000000",
            "TakerPaysIssuer": "E5C92828261DBAAC933B6309C6F5C72AF020AFD4",
            "index": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F0E23C9F634C000"
        },
        {
            "ExchangeRate": "4F0E90EDA3944000",
            "Flags": 0,
            "Indexes": ["352C69FE9817C9627073D02C4BB7CB65EDB3A10B6293A2D5BA671DF9E34D7A41"],
            "LedgerEntryType": "DirectoryNode",
            "RootIndex": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F0E90EDA3944000",
            "TakerGetsCurrency": "0000000000000000000000000000000000000000",
            "TakerGetsIssuer": "0000000000000000000000000000000000000000",
            "TakerPaysCurrency": "0000000000000000000000004A50590000000000",
            "TakerPaysIssuer": "E5C92828261DBAAC933B6309C6F5C72AF020AFD4",
            "index": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F0E90EDA3944000"
        },
        {
            "ExchangeRate": "4F11B1489AFB4000",
            "Flags": 0,
            "Indexes": ["61A9A18EAF404FC100A4FBC813DBA1F9C0B80AB0DC29790BF7EA3B438BDA0249"],
            "LedgerEntryType": "DirectoryNode",
            "RootIndex": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F11B1489AFB4000",
            "TakerGetsCurrency": "0000000000000000000000000000000000000000",
            "TakerGetsIssuer": "0000000000000000000000000000000000000000",
            "TakerPaysCurrency": "0000000000000000000000004A50590000000000",
            "TakerPaysIssuer": "E5C92828261DBAAC933B6309C6F5C72AF020AFD4",
            "index": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F11B1489AFB4000"
)LDGER14";

char const* ld_15 = R"LDGER15(
        },
        {
            "ExchangeRate": "4F138A388A43C000",
            "Flags": 0,
            "Indexes": ["428C78CE704A4A86A44345475EFC3EED9344D75DA2ADC9962E0018B64941364B"],
            "LedgerEntryType": "DirectoryNode",
            "RootIndex": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F138A388A43C000",
            "TakerGetsCurrency": "0000000000000000000000000000000000000000",
            "TakerGetsIssuer": "0000000000000000000000000000000000000000",
            "TakerPaysCurrency": "0000000000000000000000004A50590000000000",
            "TakerPaysIssuer": "E5C92828261DBAAC933B6309C6F5C72AF020AFD4",
            "index": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F138A388A43C000"
        },
        {
            "ExchangeRate": "4F1550F7DCA70000",
            "Flags": 0,
            "Indexes": [
                "2213B6153CF9370D3A9EC3597C5C9AB6BFA3343C4CCA46AB13CD02C25EB7965A",
                "4F68DB8A9E94EB3CD6979892E338288200C0CC370E1CA6AA9FE685D616C5C774"
            ],
            "LedgerEntryType": "DirectoryNode",
            "RootIndex": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F1550F7DCA70000",
            "TakerGetsCurrency": "0000000000000000000000000000000000000000",
            "TakerGetsIssuer": "0000000000000000000000000000000000000000",
            "TakerPaysCurrency": "0000000000000000000000004A50590000000000",
            "TakerPaysIssuer": "E5C92828261DBAAC933B6309C6F5C72AF020AFD4",
            "index": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F1550F7DCA70000"
        },
        {
            "ExchangeRate": "4F22D10C4ECC8000",
            "Flags": 0,
            "Indexes": ["AC579B09EB6B609DBAAB2CCF4AE3F59B2D1F56072A4B0E63001621877E7ADEA0"],
            "LedgerEntryType": "DirectoryNode",
            "RootIndex": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F22D10C4ECC8000",
            "TakerGetsCurrency": "0000000000000000000000000000000000000000",
            "TakerGetsIssuer": "0000000000000000000000000000000000000000",
            "TakerPaysCurrency": "0000000000000000000000004A50590000000000",
            "TakerPaysIssuer": "E5C92828261DBAAC933B6309C6F5C72AF020AFD4",
            "index": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F22D10C4ECC8000"
        },
        {
            "ExchangeRate": "50038D7EA4C68000",
            "Flags": 0,
            "Indexes": ["665C464568A1F581501591A6ED36E39B78381679AA7F4B667CDEFE5E347855DB"],
            "LedgerEntryType": "DirectoryNode",
            "RootIndex": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB050038D7EA4C68000",
            "TakerGetsCurrency": "0000000000000000000000000000000000000000",
            "TakerGetsIssuer": "0000000000000000000000000000000000000000",
            "TakerPaysCurrency": "0000000000000000000000004A50590000000000",
            "TakerPaysIssuer": "E5C92828261DBAAC933B6309C6F5C72AF020AFD4",
            "index": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB050038D7EA4C68000"
        },
        {
            "ExchangeRate": "5011C37937D8DEE0",
            "Flags": 0,
            "Indexes": ["95D87B2D2138AB924A9665DDF2B4C9E8CC4749D2F9CA94741435E8E2AE99675E"],
            "LedgerEntryType": "DirectoryNode",
            "RootIndex": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB05011C37937D8DEE0",
            "TakerGetsCurrency": "0000000000000000000000000000000000000000",
            "TakerGetsIssuer": "0000000000000000000000000000000000000000",
            "TakerPaysCurrency": "0000000000000000000000004A50590000000000",
            "TakerPaysIssuer": "E5C92828261DBAAC933B6309C6F5C72AF020AFD4",
            "index": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB05011C37937D8DEE0"
        },
        {
            "ExchangeRate": "502386F01BB51C00",
            "Flags": 0,
            "Indexes": ["7D76318C279034B7ACE1231F16BF345DF8FCF6EC40E99E17091AD56B19867AE3"],
            "LedgerEntryType": "DirectoryNode",
            "RootIndex": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB0502386F01BB51C00",
            "TakerGetsCurrency": "0000000000000000000000000000000000000000",
            "TakerGetsIssuer": "0000000000000000000000000000000000000000",
            "TakerPaysCurrency": "0000000000000000000000004A50590000000000",
            "TakerPaysIssuer": "E5C92828261DBAAC933B6309C6F5C72AF020AFD4",
            "index": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB0502386F01BB51C00"
        },
        {
            "ExchangeRate": "51038D7EA4C68000",
            "Flags": 0,
            "Indexes": ["7EFCC8EE289C60DB11F776D5B6DC86CDE231B8D8AE8A77D75952C59693B42760"],
            "LedgerEntryType": "DirectoryNode",
            "RootIndex": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB051038D7EA4C68000",
            "TakerGetsCurrency": "0000000000000000000000000000000000000000",
            "TakerGetsIssuer": "0000000000000000000000000000000000000000",
            "TakerPaysCurrency": "0000000000000000000000004A50590000000000",
            "TakerPaysIssuer": "E5C92828261DBAAC933B6309C6F5C72AF020AFD4",
            "index": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB051038D7EA4C68000"
        },
        {
            "ExchangeRate": "51071AFD498D0000",
            "Flags": 0,
            "Indexes": ["EDFFABC23B617EEE0A9F3C9224AD574AD94C99E35F1BC68500B4BD08C0A4B5C1"],
            "LedgerEntryType": "DirectoryNode",
            "RootIndex": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB051071AFD498D0000",
            "TakerGetsCurrency": "0000000000000000000000000000000000000000",
            "TakerGetsIssuer": "0000000000000000000000000000000000000000",
            "TakerPaysCurrency": "0000000000000000000000004A50590000000000",
            "TakerPaysIssuer": "E5C92828261DBAAC933B6309C6F5C72AF020AFD4",
            "index": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB051071AFD498D0000"
        },
        {
            "ExchangeRate": "52071AFD498D0000",
            "Flags": 0,
            "Indexes": ["A8386F14133CC2ED2A44977209F791F1537F5612AF205F506F6EC9A2AEBF56D1"],
            "LedgerEntryType": "DirectoryNode",
            "RootIndex": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB052071AFD498D0000",
            "TakerGetsCurrency": "0000000000000000000000000000000000000000",
            "TakerGetsIssuer": "0000000000000000000000000000000000000000",
            "TakerPaysCurrency": "0000000000000000000000004A50590000000000",
            "TakerPaysIssuer": "E5C92828261DBAAC933B6309C6F5C72AF020AFD4",
            "index": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB052071AFD498D0000"
        },
        {
            "ExchangeRate": "521A4A42C3568000",
            "Flags": 0,
            "Indexes": ["EEA663B7BD24612B8103830279E62392ED83F1564196331EA1D364CC1187F8E3"],
            "LedgerEntryType": "DirectoryNode",
            "RootIndex": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB0521A4A42C3568000",
            "TakerGetsCurrency": "0000000000000000000000000000000000000000",
            "TakerGetsIssuer": "0000000000000000000000000000000000000000",
            "TakerPaysCurrency": "0000000000000000000000004A50590000000000",
            "TakerPaysIssuer": "E5C92828261DBAAC933B6309C6F5C72AF020AFD4",
            "index": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB0521A4A42C3568000"
        },
        {
            "ExchangeRate": "53038D7EA4C68000",
            "Flags": 0,
            "Indexes": [
                "13833879B05DAB4D7E68EE2E1F7F810CED76692BE5B3F99F017C9056A8C65F05",
                "DD9129C14E714E872D366AE7386874E02DDDE12FAFC43CB4A1FD6782E25B55A2"
            ],
            "LedgerEntryType": "DirectoryNode",
            "RootIndex": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB053038D7EA4C68000",
            "TakerGetsCurrency": "0000000000000000000000000000000000000000",
            "TakerGetsIssuer": "0000000000000000000000000000000000000000",
            "TakerPaysCurrency": "0000000000000000000000004A50590000000000",
            "TakerPaysIssuer": "E5C92828261DBAAC933B6309C6F5C72AF020AFD4",
            "index": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB053038D7EA4C68000"
        },
        {
            "ExchangeRate": "53071AFD498D0000",
            "Flags": 0,
            "Indexes": ["EAC21ABB5EE92F88C9FE210F5DF00B96F1EDA1892DBDD98EEF64FF12130D73B8"],
            "LedgerEntryType": "DirectoryNode",
            "RootIndex": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB053071AFD498D0000",
            "TakerGetsCurrency": "0000000000000000000000000000000000000000",
            "TakerGetsIssuer": "0000000000000000000000000000000000000000",
            "TakerPaysCurrency": "0000000000000000000000004A50590000000000",
            "TakerPaysIssuer": "E5C92828261DBAAC933B6309C6F5C72AF020AFD4",
            "index": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB053071AFD498D0000"
        },
        {
            "ExchangeRate": "54038D7EA4C68000",
            "Flags": 0,
            "Indexes": [
                "489B73AC921003479A0CC9725DB0249B15663A25AF29EF6FE737D3BFF02F0FAD",
                "6F3119C29E3D423B9CCDA9377EBCA770ECFFBC674E5F3809BC2851915293022D",
                "670ECD9A17639A02C6161F3CC8638C22170E504F8E04B6619F9146B24F117262"
            ],
            "LedgerEntryType": "DirectoryNode",
            "RootIndex": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB054038D7EA4C68000",
            "TakerGetsCurrency": "0000000000000000000000000000000000000000",
            "TakerGetsIssuer": "0000000000000000000000000000000000000000",
            "TakerPaysCurrency": "0000000000000000000000004A50590000000000",
            "TakerPaysIssuer": "E5C92828261DBAAC933B6309C6F5C72AF020AFD4",
            "index": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB054038D7EA4C68000"
        },
        {
            "ExchangeRate": "56038D7EA4C68000",
            "Flags": 0,
            "Indexes": ["91F178D98C547B5976FACEAB5686A3D55EA0E0071DB4FB50D6E0C65DD62C0A32"],
            "LedgerEntryType": "DirectoryNode",
            "RootIndex": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB056038D7EA4C68000",
            "TakerGetsCurrency": "0000000000000000000000000000000000000000",
            "TakerGetsIssuer": "0000000000000000000000000000000000000000",
            "TakerPaysCurrency": "0000000000000000000000004A50590000000000",
            "TakerPaysIssuer": "E5C92828261DBAAC933B6309C6F5C72AF020AFD4",
            "index": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB056038D7EA4C68000"
        },
        {
            "ExchangeRate": "572386F26F286980",
            "Flags": 0,
            "Indexes": ["814E0164B20ADE0618011D698E1E78106AC43FF000A55C3CC976F4F917262182"],
            "LedgerEntryType": "DirectoryNode",
            "RootIndex": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB0572386F26F286980",
            "TakerGetsCurrency": "0000000000000000000000000000000000000000",
            "TakerGetsIssuer": "0000000000000000000000000000000000000000",
            "TakerPaysCurrency": "0000000000000000000000004A50590000000000",
            "TakerPaysIssuer": "E5C92828261DBAAC933B6309C6F5C72AF020AFD4",
            "index": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB0572386F26F286980"
        },
        {
            "Account": "rqb6b8GZn9BTYnhbs4wiMQEoeqw8UnAE5",
            "Balance": "52926947549",
            "Flags": 0,
            "LedgerEntryType": "AccountRoot",
            "OwnerCount": 9,
            "PreviousTxnID": "B05CCFEA9993A64E63BEC6E091A4F944219675CC83680B4E0F62A80BA819D4F1",
            "PreviousTxnLgrSeq": 6220958,
            "Sequence": 540,
)LDGER15";

char const* ld_16 = R"LDGER16(
            "index": "BD3C577B91B0A927B7E6509ECC4F00B828C6690071D7E505DB6E67BE2508A4B1"
        },
        {
            "Account": "r47GLMFhJPjshD65J8TJSWZJzM3jPHcJdZ",
            "BookDirectory": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F0C5D0AA3D18000",
            "BookNode": "0000000000000000",
            "Flags": 131072,
            "LedgerEntryType": "Offer",
            "OwnerNode": "0000000000000000",
            "PreviousTxnID": "68D48B5E0C1A96E531460E2937A1FCD224D69BB264F8113A7637765369E669CE",
            "PreviousTxnLgrSeq": 5964819,
            "Sequence": 609,
            "TakerGets": "10000000000",
            "TakerPays": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "34800"
            },
            "index": "BD619F6D141E357F21E05A20AB14F180409DFB3190F638C2F3F8358AF84FBFD0"
        },
        {
            "Balance": {
                "currency": "JPY",
                "issuer": "rrrrrrrrrrrrrrrrrrrrBZbvji",
                "value": "2.8599251461938"
            },
            "Flags": 1114112,
            "HighLimit": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "0"
            },
            "HighNode": "0000000000000000",
            "LedgerEntryType": "RippleState",
            "LowLimit": {
                "currency": "JPY",
                "issuer": "rKE2TX794t8Aoqe25AvWKWvKi1igXJpBUi",
                "value": "100000"
            },
            "LowNode": "0000000000000000",
            "PreviousTxnID": "04B49FAED92C8E181D83601DEE3E3B2075116DEE040C3ECC366F210D275E8920",
            "PreviousTxnLgrSeq": 6114147,
            "index": "BDA23C732CCE9C0FEBC31D6F60CE781D949D7E67264AF02E59E0752160CBA6B5"
        },
        {
            "Account": "rngNbgfn7cT4bHbHJPNoPY12R66a4RMMaa",
            "BookDirectory": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F0649CE3D40A000",
            "BookNode": "0000000000000000",
            "Flags": 131072,
            "LedgerEntryType": "Offer",
            "OwnerNode": "0000000000000000",
            "PreviousTxnID": "573AB62B77C2888339B4A10C1AF175420E05131C7DC18378B4AD2B56E2A37ED5",
            "PreviousTxnLgrSeq": 5928315,
            "Sequence": 28,
            "TakerGets": "395000000",
            "TakerPays": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "699.15"
            },
            "index": "C46FA7924251F67DADC69D6FCB71D4BA2167BE7EA0615078E466FAB236D88BF6"
        },
        {
            "Account": "rP9tNSggJJGPNzUgtAZxaZmsWq8LGtKzYP",
            "BookDirectory": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F07D0E36A818000",
            "BookNode": "0000000000000000",
            "Flags": 131072,
            "LedgerEntryType": "Offer",
            "OwnerNode": "0000000000000000",
            "PreviousTxnID": "F03F5BA8C7A1914FEE3C778BC3651D35106DBB7180103AFB9FF5BCE758D8E4BE",
            "PreviousTxnLgrSeq": 5421881,
            "Sequence": 47,
            "TakerGets": "1000000000",
            "TakerPays": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "2200"
            },
            "index": "C4CDCC5A64CF564982F17B71F2131A08DDBB6C9A4D041890BC6F763A1E49E05D"
        },
        {
            "Account": "r34iSwVNKXQZVzqPB8ZEuUwT7dsjQhdaJu",
            "Balance": "5465847287",
            "Flags": 0,
            "LedgerEntryType": "AccountRoot",
            "OwnerCount": 13,
            "PreviousTxnID": "9C77C302968777182FF7062474400D0B648770AF6632A840A5CC02BF9C24F87E",
            "PreviousTxnLgrSeq": 6079073,
            "Sequence": 51,
            "index": "C4D0B9E929B282F0EC5D95317A322FD73B69D81C37870F3163D18EDDE60B208B"
        },
        {
            "Account": "rUxXgX1dZgrEZyj644jsMXXrKEFDMphU75",
            "BookDirectory": "7254404DF6B7FBFFEF34DC38867A7E7DE610B513997B78804D0DF90AEBE6D000",
            "BookNode": "0000000000000000",
            "Flags": 131072,
            "LedgerEntryType": "Offer",
            "OwnerNode": "0000000000000000",
            "PreviousTxnID": "7B4EE05D265ABECAAF9D7EA65BEE6943571F03A77D1CD50AE01192F944C341ED",
            "PreviousTxnLgrSeq": 6226713,
            "Sequence": 332,
            "TakerGets": "80085788757",
            "TakerPays": {
                "currency": "CNY",
                "issuer": "razqQKzJRdB4UxFPWf5NEpEG3WMkmwgcXA",
                "value": "3149.774071895922"
            },
            "index": "C5C0D61BA32C097DDCE6C381E1DEC33B36D6BF4C3B5CFCB1174352BC036EA121"
        },
        {
            "Flags": 0,
            "Indexes": [
                "73DB3FF0D87377B82D7946FA4B1FDB1FB5DD92D3C664666CE5B49A2922761CAF",
                "181402989B9E8DA57D49E65310E69A63C1117B1B7D89E2A1E96492C033FD4BEE"
            ],
            "LedgerEntryType": "DirectoryNode",
            "Owner": "racJpvgLpaNQGKB8nhKd1gTEVVA1uQWRKs",
            "RootIndex": "CDF3B24EAEB907165B3146F2DC449447AA055433BBDE5BF46D1D40186AEEC67E",
            "index": "CDF3B24EAEB907165B3146F2DC449447AA055433BBDE5BF46D1D40186AEEC67E"
        },
        {
            "Balance": {
                "currency": "CNY",
                "issuer": "rrrrrrrrrrrrrrrrrrrrBZbvji",
                "value": "-8683.393920324141"
            },
            "Flags": 2228224,
            "HighLimit": {
                "currency": "CNY",
                "issuer": "rUxXgX1dZgrEZyj644jsMXXrKEFDMphU75",
                "value": "50000"
            },
            "HighNode": "0000000000000000",
            "LedgerEntryType": "RippleState",
            "LowLimit": {
                "currency": "CNY",
                "issuer": "razqQKzJRdB4UxFPWf5NEpEG3WMkmwgcXA",
                "value": "0"
            },
            "LowNode": "0000000000000000",
            "PreviousTxnID": "7B4EE05D265ABECAAF9D7EA65BEE6943571F03A77D1CD50AE01192F944C341ED",
            "PreviousTxnLgrSeq": 6226713,
            "index": "CE0EDA775D377BDCC77B6F85DA9540EAB77F44D4AE2B12FCC86810545B759CFA"
        },
        {
            "Account": "r38Mwd8s2gFevETqCK8e34JYfWBjLUB2nH",
            "BookDirectory": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F03A8C7901E6000",
            "BookNode": "0000000000000000",
            "Flags": 131072,
            "LedgerEntryType": "Offer",
            "OwnerNode": "0000000000000000",
            "PreviousTxnID": "923FF7190E32D147B68CAB603A4AE0D7243A52AC8D67FD2FFFA894DCEA0C9C13",
            "PreviousTxnLgrSeq": 5981342,
            "Sequence": 222,
            "TakerGets": "1000000000",
            "TakerPays": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "1030"
            },
            "index": "CE398D77F49EB6B6C995539F6B6F48660660BA4C7C08AE5F633BB2A493E62C06"
        },
        {
            "Account": "ratarRi5YVgBDTHbt7rTPdmCMehH6zge2T",
            "Balance": "9197315729",
            "Flags": 0,
            "LedgerEntryType": "AccountRoot",
            "OwnerCount": 2,
            "PreviousTxnID": "831CF6C924F7A7E509D0D41AA2C3EE7D174B622D311D8DD4194D2BF6580014EB",
            "PreviousTxnLgrSeq": 6200265,
            "Sequence": 126,
            "index": "CE3DAF0DBCEFC32AC06F28027AF9F93E47BAC333E575EC97E11F2FF6B6E505D2"
        },
        {
            "Balance": {
                "currency": "CNY",
                "issuer": "rrrrrrrrrrrrrrrrrrrrBZbvji",
                "value": "-31.2365570758485"
            },
            "Flags": 2228224,
            "HighLimit": {
                "currency": "CNY",
                "issuer": "rHpoggSkNY7puahMUGVafWPZQ5JH8piZVQ",
                "value": "200000"
            },
            "HighNode": "0000000000000000",
            "LedgerEntryType": "RippleState",
            "LowLimit": {
                "currency": "CNY",
                "issuer": "razqQKzJRdB4UxFPWf5NEpEG3WMkmwgcXA",
                "value": "0"
            },
            "LowNode": "0000000000000000",
            "PreviousTxnID": "7B4EE05D265ABECAAF9D7EA65BEE6943571F03A77D1CD50AE01192F944C341ED",
            "PreviousTxnLgrSeq": 6226713,
            "index": "D043B6B526F5B9FBC7C2DE1BC2D59291A0C59CB7906153CF0E7DC2F6C80D00C8"
        },
        {
            "Account": "rpRzczN3gPxXMRzqMR98twVsH63xATHUb7",
            "Balance": "1741748232",
)LDGER16";

char const* ld_17 = R"LDGER17(
            "Flags": 0,
            "LedgerEntryType": "AccountRoot",
            "OwnerCount": 24,
            "PreviousTxnID": "AE90B6ED7C1CA8446FF474E2E1845407484D5295D02058C6D83EB605E52E1BB9",
            "PreviousTxnLgrSeq": 6095561,
            "Sequence": 670,
            "index": "D2B91C0E88F8199A8DE78C51A69C5F619C2A5C470ABEA9DB622093348F65DDF7"
        },
        {
            "Flags": 0,
            "Indexes": [
                "0AED9B95367D6366D950E58E0884DBA139700217A105D60B3D2616625A0E2F06",
                "DFDB7E0EC2F3FDD188747CB63EAE1FACDD46AB4E6A2DCF97AD61E0A178656420",
                "7D7874A4A96EB03A227365EABA10C637681BCCDFCDE4ADB6B73B4CB7F1FD1142",
                "043B696FC70C6D48A30808CE1DC45A8495A3F672FD6544113D2610310290315E",
                "A20B9C6B254C8AD6C301F21AD0E98A523B0F394F45CB498205C11FF90FC05824",
                "15483FA685F65E020C876D69BD01FA7DDB05A753C11B32C98494DE114B7943EF"
            ],
            "LedgerEntryType": "DirectoryNode",
            "Owner": "rajrdNafcXefrq4pYW1YAjMUDoxttSLefM",
            "RootIndex": "D5BA82A7E30DA85BF2F00DBDF078B506666C0B3ADFF546077A84707544E22010",
            "index": "D5BA82A7E30DA85BF2F00DBDF078B506666C0B3ADFF546077A84707544E22010"
        },
        {
            "Account": "rw7dJmysoqzguZDYyULBh5HqXdNQikMDtc",
            "Balance": "118983668175",
            "Flags": 0,
            "LedgerEntryType": "AccountRoot",
            "OwnerCount": 11,
            "PreviousTxnID": "C6A3D0BB56C1E94874930A49136CAEFFB4D768333602E365BEBAB13FA940C752",
            "PreviousTxnLgrSeq": 6226097,
            "Sequence": 614,
            "index": "D5CF81EB5A80D4378161B7525B5CAF9C74535FE91FADB3504FE9EA79B706C3D2"
        },
        {
            "Balance": {
                "currency": "JPY",
                "issuer": "rrrrrrrrrrrrrrrrrrrrBZbvji",
                "value": "246635.0669598848"
            },
            "Flags": 65536,
            "HighLimit": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "0"
            },
            "HighNode": "0000000000000000",
            "LedgerEntryType": "RippleState",
            "LowLimit": {
                "currency": "JPY",
                "issuer": "rUQwWJBVPBbEQ6CoaoJKeGH8HDWDwysERb",
                "value": "0"
            },
            "LowNode": "0000000000000000",
            "PreviousTxnID": "EB3296B9D1451629F17CC0C0B2C3A8C3F0A68B981019B7A3AA5BF11FDAC79E5A",
            "PreviousTxnLgrSeq": 6225565,
            "index": "D67C2598A92B64C2B4D785C9222363B55597CF4480C4A2F1930A0586EA117A5A"
        },
        {
            "Flags": 0,
            "Indexes": [
                "10FD64419C1F9295E2FF339DB45BA2DE5B20D8C2A0E45A3B3C46DBD3C8D41731",
                "814E0164B20ADE0618011D698E1E78106AC43FF000A55C3CC976F4F917262182",
                "CE398D77F49EB6B6C995539F6B6F48660660BA4C7C08AE5F633BB2A493E62C06"
            ],
            "LedgerEntryType": "DirectoryNode",
            "Owner": "r38Mwd8s2gFevETqCK8e34JYfWBjLUB2nH",
            "RootIndex": "D6A5DB367F231F2A707A5886718C55CAB8BE2808B94AF19B6E9049185E00D073",
            "index": "D6A5DB367F231F2A707A5886718C55CAB8BE2808B94AF19B6E9049185E00D073"
        },
        {
            "Account": "rHqtzHk6nWaBtJ1srDGaKaJU6kEGPDWiLy",
            "Balance": "2000666792",
            "Flags": 0,
            "LedgerEntryType": "AccountRoot",
            "OwnerCount": 2,
            "PreviousTxnID": "F4B1B237715C92BE7DE9D7FD1736A88D571FCEF632F8196DFE05A9B6AAFBD2A2",
            "PreviousTxnLgrSeq": 6088179,
            "Sequence": 3,
            "index": "D70A3113A9F3264F0D3FAB748BE86F3BCF16E28BCD2CA79EF32E2AEB4D2F9C7C"
        },
        {
            "Account": "rpRzczN3gPxXMRzqMR98twVsH63xATHUb7",
            "BookDirectory": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F07A25028A59C00",
            "BookNode": "0000000000000000",
            "Flags": 131072,
            "LedgerEntryType": "Offer",
            "OwnerNode": "0000000000000000",
            "PreviousTxnID": "FDD813BBC931B030AA4293A527F154F464B569C0589650D26EDE2D278F69D820",
            "PreviousTxnLgrSeq": 5362941,
            "Sequence": 477,
            "TakerGets": "100000000",
            "TakerPays": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "214.879"
            },
            "index": "D70DB4444E22FC2FB6087B24897F7AB63D05E60C1CFFE764072D84463DD5C8A0"
        },
        {
            "Account": "rhsxr2aAddyCKx5iZctebT4Padxv6iWDxb",
            "BookDirectory": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04E1AEA1C83351F08",
            "BookNode": "0000000000000000",
            "Flags": 0,
            "LedgerEntryType": "Offer",
            "OwnerNode": "0000000000000000",
            "PreviousTxnID": "DA4356B2DDE054F0653F4240B73BABB28CE49135023308670E1ADDD07582792E",
            "PreviousTxnLgrSeq": 6221082,
            "Sequence": 291,
            "TakerGets": "13200000000",
            "TakerPays": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "10000"
            },
            "index": "D770FB84E4ED16B67C925F7BAD094E52D48297D6375BAC0A8F30539BADBAC36F"
        },
        {
            "Account": "razqQKzJRdB4UxFPWf5NEpEG3WMkmwgcXA",
            "Balance": "3329219182",
            "Flags": 0,
            "LedgerEntryType": "AccountRoot",
            "OwnerCount": 0,
            "PreviousTxnID": "EF8860A3032AE9EF7CC9A973B6872CEBE1596391F7D69385212AB234629CF420",
            "PreviousTxnLgrSeq": 6226290,
            "Sequence": 333,
            "index": "D9A4529146AB12ABD244CCC0ED0523CF5C6BA97043999AB27C1D4EB567929069"
        },
        {
            "Account": "rKE2TX794t8Aoqe25AvWKWvKi1igXJpBUi",
            "BookDirectory": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F03E871B540C000",
            "BookNode": "0000000000000000",
            "Flags": 131072,
            "LedgerEntryType": "Offer",
            "OwnerNode": "0000000000000000",
            "PreviousTxnID": "3DD6735C464CCF483DBA50183005DD1A49C5DAE068C5DC022CAA40EF58247BF4",
            "PreviousTxnLgrSeq": 6132338,
            "Sequence": 131,
            "TakerGets": "19000000000",
            "TakerPays": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "20900"
            },
            "index": "D9EC3E44A4235F4B89BCB8C34BF39850A01449BBB4F70722A5E03FB71EF4EA57"
        },
        {
            "Flags": 0,
            "Indexes": [
                "7CD8FA0B1C81934DF8F5FEFD39323D993A7FA0A1E6D71CE8E49071D7B53F0772",
                "73DB3FF0D87377B82D7946FA4B1FDB1FB5DD92D3C664666CE5B49A2922761CAF",
                "20E49A1185CBB556D55EDFD054162F5833ABE8889E0B03247F79D1D8FA85F60A",
                "AE15437DF9B98E2DB40B6C06C313ADF45B9E38B700A4C3462AFB61DBC5FFAFB8",
                "B2490D6B802B0A5CA82C91EADE4504E893B8A20E732BD16F6A0EB43F36191356",
                "7548EDD4EE8582725A58ECB6D7E70A5DED5E05A8A3BB9C2BF8062742CB9B8225",
                "13E35A054213C6CA2F639631B6F0618A95081F8E958E99B86A625001EFF9B3BC",
                "3DE8A735E9996A3DB3093D85AC36DCEF135777EF6AE3C67337F9D1481FA83BBF",
                "E2F373FF3803FFEB2F3EBB805AE20A00A16E7A32E6F51EA49AEA47D4B851AAC5",
                "86543C222523A608A63B9168171E66CB5DF4DFD94DA8C35BA111739F5908DE95",
                "1B5DBDA3A421462B06D53691B051031DB9275BAEEF1276CEDB07D0F29CB80279",
                "3EE0E409F23D45BF8A95BCDA14AFFF2326877E07C7A40F10F5108298BEBA2A3A",
                "E55DB8FB9BEC6D16123EE8BE8434F09035AC7DD2D90A3450A1F7400DFEB214B8",
                "03A00CD40E296C363D33515A1F2867D62B7059E7D2DA146993F8AB6A986EFDFD",
                "4628C6B90FDCCD23E451176B826391D115CBCA5A5E30218E1D83C0D447A21538",
                "0EFF1D95B5E694B18575969052DDF03A0041064F9B396757DCC0CEA286A8116D",
                "13CBE718A9792D410BFB2294E46477E7669379ED126079100C3365925AAE1DBC",
                "035E4A8D4AD8A2A96C555AED16C6D3D6E67026A659998341D2E2980393E3752B",
                "F3AB294F8D27E388A172458CBDBC9D00FB5909CF20B35E97753CE8E927AE5B6B",
                "E88430B715DFD7B2D62A24F002657558407EB429568257BBF942C1FD0E6C3CD3",
                "1F5482CD6E2A5CCD6902AA599FE63A635A5263C5D2E59A3C5697D0DD5C760B32",
                "D67C2598A92B64C2B4D785C9222363B55597CF4480C4A2F1930A0586EA117A5A",
                "300C6D4FCF7EA0F7F144623370AE7670F85B0433A8DFD5FA91576426BA13B6E3",
                "224FC7D1465450509CE761CE2AB02133F9E0C3DE6F1D2C7F4290FF33457D1D2E",
                "F59533169EAC6639FB94220A952C8459FFCFCF0A1BDC80D7A5AD26DF30CD5757",
                "A58F531945492C5270C9D364632996C152E12516EC235EE7DF1133876E23BBA0",
                "BDA23C732CCE9C0FEBC31D6F60CE781D949D7E67264AF02E59E0752160CBA6B5",
                "10FD64419C1F9295E2FF339DB45BA2DE5B20D8C2A0E45A3B3C46DBD3C8D41731",
                "25DE74D76DC8AC60511D87EA9EBE12F01240532CEFDA4006FFB1329E1AF19AD0",
                "0AED9B95367D6366D950E58E0884DBA139700217A105D60B3D2616625A0E2F06",
                "E315F11E126E041EC24AEA0EBC62DCA4B39AC7F1A08854843C0E0F7BFE2FE086",
                "95D79D06AD590C176743857C5FA02EAD10C714E21BC7C6799EFCD6ECDE68E148",
                "8BF4D2FB788EF310C2B52498DC38DCA195CDAFA7C608B48EFBA2CACC96988D3A",
                "8782F28AC73A79162357EB1FB38E0AA5F55C066F0F2ACC774BBF095B21E07E64",
                "886DFEEC874F4ED640DA896FEE1C8D690F34D478F84C51877AF326E431753F16",
                "7280EDED4E1FA80C6E5F86D07A70F0E704B1B637F994DC3152FCC7248F5DAB6B",
                "8E12E77B98570C4D5A9CBBB296A526068EB47ACBB34FB81F56EE05514D687D55"
            ],
            "LedgerEntryType": "DirectoryNode",
            "Owner": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
            "RootIndex": "DD8763F37822A3129919DA194DC31D9A9FA5BEA547E233B32E4573F0E60D46D3",
            "index": "DD8763F37822A3129919DA194DC31D9A9FA5BEA547E233B32E4573F0E60D46D3"
        },
        {
            "Account": "rwFdyL8LVBYkRu8nwkJNKiScRMZnjcrBu6",
            "BookDirectory": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB053038D7EA4C68000",
            "BookNode": "0000000000000000",
            "Flags": 131072,
            "LedgerEntryType": "Offer",
            "OwnerNode": "0000000000000000",
            "PreviousTxnID": "472F2591748F8BFBD8F2185A2875AB67C07F21EC5A7114A87D3F5FADDE61B3C8",
)LDGER17";

char const* ld_18 = R"LDGER18(
            "PreviousTxnLgrSeq": 6060770,
            "Sequence": 339,
            "TakerGets": "12000000000",
            "TakerPays": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "120000000"
            },
            "index": "DD9129C14E714E872D366AE7386874E02DDDE12FAFC43CB4A1FD6782E25B55A2"
        },
        {
            "Flags": 0,
            "Indexes": [
                "CE0EDA775D377BDCC77B6F85DA9540EAB77F44D4AE2B12FCC86810545B759CFA",
                "C5C0D61BA32C097DDCE6C381E1DEC33B36D6BF4C3B5CFCB1174352BC036EA121"
            ],
            "LedgerEntryType": "DirectoryNode",
            "Owner": "rUxXgX1dZgrEZyj644jsMXXrKEFDMphU75",
            "RootIndex": "DF601BD8AC2BE88A2856B08FF830BDBFA24BA810C3E7E0421BAB018F1F202492",
            "index": "DF601BD8AC2BE88A2856B08FF830BDBFA24BA810C3E7E0421BAB018F1F202492"
        },
        {
            "Account": "rajrdNafcXefrq4pYW1YAjMUDoxttSLefM",
            "BookDirectory": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04E1BACF6B6CCE000",
            "BookNode": "0000000000000000",
            "Flags": 131072,
            "LedgerEntryType": "Offer",
            "OwnerNode": "0000000000000000",
            "PreviousTxnID": "591AAF22552D9C6F18551DFAD48CFCD5999ACA997109F60C9D8EB4B09969D73A",
            "PreviousTxnLgrSeq": 6217765,
            "Sequence": 169,
            "TakerGets": "3000000000",
            "TakerPays": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "2337"
            },
            "index": "DFDB7E0EC2F3FDD188747CB63EAE1FACDD46AB4E6A2DCF97AD61E0A178656420"
        },
        {
            "Balance": {
                "currency": "JPY",
                "issuer": "rrrrrrrrrrrrrrrrrrrrBZbvji",
                "value": "729117.2080619572"
            },
            "Flags": 1114112,
            "HighLimit": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "0"
            },
            "HighNode": "0000000000000000",
            "LedgerEntryType": "RippleState",
            "LowLimit": {
                "currency": "JPY",
                "issuer": "rHpoggSkNY7puahMUGVafWPZQ5JH8piZVQ",
                "value": "1000000"
            },
            "LowNode": "0000000000000000",
            "PreviousTxnID": "7B4EE05D265ABECAAF9D7EA65BEE6943571F03A77D1CD50AE01192F944C341ED",
            "PreviousTxnLgrSeq": 6226713,
            "index": "E2F373FF3803FFEB2F3EBB805AE20A00A16E7A32E6F51EA49AEA47D4B851AAC5"
        },
        {
            "Flags": 0,
            "Indexes": [
                "F3AB294F8D27E388A172458CBDBC9D00FB5909CF20B35E97753CE8E927AE5B6B",
                "BD619F6D141E357F21E05A20AB14F180409DFB3190F638C2F3F8358AF84FBFD0",
                "7A599E3DA6A3E67E86CE80B10EA8B3C5C395A6E457C6802D9F51909BA15BB98B",
                "8476DF4803ECF5DB945DA2FC41CEC6DFB6D50B829E8577267699920525DDBD5B",
                "1E9215E83CF75C0AC80ABB3F434115E4992981544F009CAF14CAED53DFD79935",
                "61A9A18EAF404FC100A4FBC813DBA1F9C0B80AB0DC29790BF7EA3B438BDA0249"
            ],
            "LedgerEntryType": "DirectoryNode",
            "Owner": "r47GLMFhJPjshD65J8TJSWZJzM3jPHcJdZ",
            "RootIndex": "E2F685D85F7147C15FE3CF2FCD1E98B33321A81DAD65F9B40CF78F0C50ECF9C2",
            "index": "E2F685D85F7147C15FE3CF2FCD1E98B33321A81DAD65F9B40CF78F0C50ECF9C2"
        },
        {
            "Balance": {
                "currency": "JPY",
                "issuer": "rrrrrrrrrrrrrrrrrrrrBZbvji",
                "value": "0"
            },
            "Flags": 65536,
            "HighLimit": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "0"
            },
            "HighNode": "0000000000000000",
            "LedgerEntryType": "RippleState",
            "LowLimit": {
                "currency": "JPY",
                "issuer": "rHqtzHk6nWaBtJ1srDGaKaJU6kEGPDWiLy",
                "value": "100000"
            },
            "LowNode": "0000000000000000",
            "PreviousTxnID": "8437C82132958BA0F5CABC2660F6EDE3A17D346BA2C2C66C4A2A63940B475028",
            "PreviousTxnLgrSeq": 6037169,
            "index": "E315F11E126E041EC24AEA0EBC62DCA4B39AC7F1A08854843C0E0F7BFE2FE086"
        },
        {
            "Balance": {
                "currency": "JPY",
                "issuer": "rrrrrrrrrrrrrrrrrrrrBZbvji",
                "value": "734.8351996792"
            },
            "Flags": 65536,
            "HighLimit": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "0"
            },
            "HighNode": "0000000000000000",
            "LedgerEntryType": "RippleState",
            "LowLimit": {
                "currency": "JPY",
                "issuer": "rngNbgfn7cT4bHbHJPNoPY12R66a4RMMaa",
                "value": "0"
            },
            "LowNode": "0000000000000000",
            "PreviousTxnID": "B38382BBA925B434306DED6928CC634C35EAAEABF3C9C2AA0790590541A1B663",
            "PreviousTxnLgrSeq": 6213507,
            "index": "E55DB8FB9BEC6D16123EE8BE8434F09035AC7DD2D90A3450A1F7400DFEB214B8"
        },
        {
            "Balance": {
                "currency": "JPY",
                "issuer": "rrrrrrrrrrrrrrrrrrrrBZbvji",
                "value": "0"
            },
            "Flags": 1114112,
            "HighLimit": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "0"
            },
            "HighNode": "0000000000000000",
            "LedgerEntryType": "RippleState",
            "LowLimit": {
                "currency": "JPY",
                "issuer": "rUkPuKD5mEkvnrPcvBeBSqe1m9isAMVX5M",
                "value": "30000"
            },
            "LowNode": "0000000000000000",
            "PreviousTxnID": "EE7E64699B17EA029E1A97E01E4D999E11F30508E3C76692BC96A385489731FB",
            "PreviousTxnLgrSeq": 5912520,
            "index": "E88430B715DFD7B2D62A24F002657558407EB429568257BBF942C1FD0E6C3CD3"
        },
        {
            "Flags": 0,
            "Indexes": [
                "95D79D06AD590C176743857C5FA02EAD10C714E21BC7C6799EFCD6ECDE68E148",
                "670ECD9A17639A02C6161F3CC8638C22170E504F8E04B6619F9146B24F117262",
                "A8386F14133CC2ED2A44977209F791F1537F5612AF205F506F6EC9A2AEBF56D1"
            ],
            "LedgerEntryType": "DirectoryNode",
            "Owner": "rKL5uUYcpSGcsVe2Yen5okfhGvi4J57mcM",
            "RootIndex": "E8EC275F1868780D9AB340ABC92AD04F2C32A28AD4844ADC5A922F32B7CD5711",
            "index": "E8EC275F1868780D9AB340ABC92AD04F2C32A28AD4844ADC5A922F32B7CD5711"
        },
        {
            "Account": "rNruDQQDBM117pzRob2Br211HJrFKxk3tB",
            "BookDirectory": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB053071AFD498D0000",
            "BookNode": "0000000000000000",
            "Flags": 131072,
            "LedgerEntryType": "Offer",
            "OwnerNode": "0000000000000000",
            "PreviousTxnID": "C224BB8B0520AC4A95F91A336A9737B93D51B36A64575111BBA127A3048E2406",
            "PreviousTxnLgrSeq": 5888108,
            "Sequence": 55,
            "TakerGets": "20000000000",
            "TakerPays": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "400000000"
            },
            "index": "EAC21ABB5EE92F88C9FE210F5DF00B96F1EDA1892DBDD98EEF64FF12130D73B8"
        },
        {
            "Account": "rU8axbJNWix3k3LCTXtL8T8LeFtv88ibMe",
            "BookDirectory": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04F04F94AE6AF8000",
            "BookNode": "0000000000000000",
            "Flags": 131072,
            "LedgerEntryType": "Offer",
            "OwnerNode": "0000000000000000",
            "PreviousTxnID": "9B2D8C0057682122E2C1A0A010D342483BACD8031ABE32C5219228FEB9749011",
            "PreviousTxnLgrSeq": 5977360,
            "Sequence": 6,
            "TakerGets": "5000000",
            "TakerPays": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "7"
            },
            "index": "EB7C3A1CD0DB012AD336262CE4E47113F0E59D0F44E18359B9BD788DBD426B7E"
        },
        {
            "Account": "rUQwWJBVPBbEQ6CoaoJKeGH8HDWDwysERb",
)LDGER18";

char const* ld_19 = R"LDGER19(
            "Balance": "188214736897",
            "Flags": 0,
            "LedgerEntryType": "AccountRoot",
            "OwnerCount": 98,
            "PreviousTxnID": "3F20C5A8B1937151F73766DB14D61537646324F4CDEC5E1FCD4FE7012EA60657",
            "PreviousTxnLgrSeq": 6225785,
            "Sequence": 1210,
            "index": "EDBABAFEB654B744DC3A2016AD9DBB982B066AE0F066770F7A5772FFC7EC7A01"
        },
        {
            "Account": "rpvawRMyKug1gdTCbJWGtHs4yNzHMgcg22",
            "BookDirectory": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB051071AFD498D0000",
            "BookNode": "0000000000000000",
            "Flags": 131072,
            "LedgerEntryType": "Offer",
            "OwnerNode": "0000000000000000",
            "PreviousTxnID": "75CD28A43E6126730FDCEEEB1F7C61DC16CEF16C94ED350602CE82C61B74CE75",
            "PreviousTxnLgrSeq": 6158962,
            "Sequence": 110,
            "TakerGets": "1000000000",
            "TakerPays": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "200000"
            },
            "index": "EDFFABC23B617EEE0A9F3C9224AD574AD94C99E35F1BC68500B4BD08C0A4B5C1"
        },
        {
            "Flags": 0,
            "Indexes": [
                "8BF4D2FB788EF310C2B52498DC38DCA195CDAFA7C608B48EFBA2CACC96988D3A",
                "6B79A8D89C4E369336D21ECA23A724A5B1E30DBE2344F66141444165FBE1270F"
            ],
            "LedgerEntryType": "DirectoryNode",
            "Owner": "rpW8wvWYx1SZbYKJVXt9A7rtayPgULa11B",
            "RootIndex": "EE48C14546B7C612B21DE35A68BB789C408873C80E9618276509B5D57999F68E",
            "index": "EE48C14546B7C612B21DE35A68BB789C408873C80E9618276509B5D57999F68E"
        },
        {
            "Account": "rHqtzHk6nWaBtJ1srDGaKaJU6kEGPDWiLy",
            "BookDirectory": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB0521A4A42C3568000",
            "BookNode": "0000000000000000",
            "Flags": 131072,
            "LedgerEntryType": "Offer",
            "OwnerNode": "0000000000000000",
            "PreviousTxnID": "392941C246612FB2E39A739AF2CD9CA579887603ABB0E133363F7E463DD41197",
            "PreviousTxnLgrSeq": 6037294,
            "Sequence": 2,
            "TakerGets": "1800000000",
            "TakerPays": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "13320000"
            },
            "index": "EEA663B7BD24612B8103830279E62392ED83F1564196331EA1D364CC1187F8E3"
        },
        {
            "Account": "rP9tNSggJJGPNzUgtAZxaZmsWq8LGtKzYP",
            "Balance": "5450015202",
            "Flags": 0,
            "LedgerEntryType": "AccountRoot",
            "OwnerCount": 7,
            "PreviousTxnID": "14F38411C2ECA1DC1A2FC355A23C8E1C0867C6B2481BCDC44596CC2301BC0798",
            "PreviousTxnLgrSeq": 5421892,
            "Sequence": 50,
            "index": "EFD3FA07AA23E6E31044D892B96EDD04F9B44C74D560052D23B6EA1F50576F11"
        },
        {
            "Account": "rM3X3QSr8icjTGpaF52dozhbT2BZSXJQYM",
            "Balance": "47155126935",
            "Flags": 0,
            "LedgerEntryType": "AccountRoot",
            "OwnerCount": 76,
            "PreviousTxnID": "1D60D1F0674A73FE6F7CB00B4B37091485EFD0F722581AC42E73808F5EF1A78E",
            "PreviousTxnLgrSeq": 6226680,
            "Sequence": 44678,
            "index": "F13BE615EDDC53504C862D741B0E1DD42B90AF5C2C4FB1F077B5C2C0BC0F41EB"
        },
        {
            "Flags": 0,
            "Indexes": [
                "E315F11E126E041EC24AEA0EBC62DCA4B39AC7F1A08854843C0E0F7BFE2FE086",
                "EEA663B7BD24612B8103830279E62392ED83F1564196331EA1D364CC1187F8E3"
            ],
            "LedgerEntryType": "DirectoryNode",
            "Owner": "rHqtzHk6nWaBtJ1srDGaKaJU6kEGPDWiLy",
            "RootIndex": "F2788CAC79254B13457F8FCC5CF1E6E14FCB2BC59338CEA51A87378718EF0F3E",
            "index": "F2788CAC79254B13457F8FCC5CF1E6E14FCB2BC59338CEA51A87378718EF0F3E"
        },
        {
            "Flags": 0,
            "Indexes": [
                "BDA23C732CCE9C0FEBC31D6F60CE781D949D7E67264AF02E59E0752160CBA6B5",
                "D9EC3E44A4235F4B89BCB8C34BF39850A01449BBB4F70722A5E03FB71EF4EA57"
            ],
            "LedgerEntryType": "DirectoryNode",
            "Owner": "rKE2TX794t8Aoqe25AvWKWvKi1igXJpBUi",
            "RootIndex": "F32472FB2BDA436B0509DC0658D495D9ABC117492C8FDFC88BAAEE4E628AC19F",
            "index": "F32472FB2BDA436B0509DC0658D495D9ABC117492C8FDFC88BAAEE4E628AC19F"
        },
        {
            "Account": "rhsxr2aAddyCKx5iZctebT4Padxv6iWDxb",
            "BookDirectory": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04E1BF960279E0408",
            "BookNode": "0000000000000000",
            "Flags": 0,
            "LedgerEntryType": "Offer",
            "OwnerNode": "0000000000000000",
            "PreviousTxnID": "D789142820ED0A9D9E3910CB4D45F77C65F6491AC8669348E977AB455445A5CB",
            "PreviousTxnLgrSeq": 6198642,
            "Sequence": 277,
            "TakerGets": "12700000000",
            "TakerPays": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "10000"
            },
            "index": "F37871AD76189305B0BA6A652A69C4207C384DA95336418A1A474D938E768BEE"
        },
        {
            "Balance": {
                "currency": "JPY",
                "issuer": "rrrrrrrrrrrrrrrrrrrrBZbvji",
                "value": "-22000.25001382842"
            },
            "Flags": 2228224,
            "HighLimit": {
                "currency": "JPY",
                "issuer": "r47GLMFhJPjshD65J8TJSWZJzM3jPHcJdZ",
                "value": "25000"
            },
            "HighNode": "0000000000000000",
            "LedgerEntryType": "RippleState",
            "LowLimit": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "0"
            },
            "LowNode": "0000000000000000",
            "PreviousTxnID": "7915CA40239D2A29BDAFE3E538D03C986774DFB76256C9576265813348B806D6",
            "PreviousTxnLgrSeq": 6022612,
            "index": "F3AB294F8D27E388A172458CBDBC9D00FB5909CF20B35E97753CE8E927AE5B6B"
        },
        {
            "Account": "rDr83LqpsNJtJ9CouPxwf2pFEhdEuoCM4z",
            "BookDirectory": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04E2386F26F5CDB64",
            "BookNode": "0000000000000000",
            "Flags": 131072,
            "LedgerEntryType": "Offer",
            "OwnerNode": "0000000000000000",
            "PreviousTxnID": "76F1FC27E8A46430AA13C80179079941314B745E78AB229272E57F58D4A5CE4F",
            "PreviousTxnLgrSeq": 6179747,
            "Sequence": 23,
            "TakerGets": "15237000000",
            "TakerPays": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "15236.99999"
            },
            "index": "F56F107800DF0AB585BC31684011DADD77C9A2516A2E47901EDCB0D9A1A0D5E0"
        },
        {
            "Balance": {
                "currency": "JPY",
                "issuer": "rrrrrrrrrrrrrrrrrrrrBZbvji",
                "value": "7140.000001580002"
            },
            "Flags": 1114112,
            "HighLimit": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "0"
            },
            "HighNode": "0000000000000000",
            "LedgerEntryType": "RippleState",
            "LowLimit": {
                "currency": "JPY",
                "issuer": "rho8mvSESSmVPkF4UiyF8pTJBGMcVx2Uv1",
                "value": "1000000"
            },
            "LowNode": "0000000000000000",
            "PreviousTxnID": "E9ECF535C8AE63EC174E546808052659126D73DEECD8430C76B428D882FC5E90",
            "PreviousTxnLgrSeq": 6221033,
            "index": "F59533169EAC6639FB94220A952C8459FFCFCF0A1BDC80D7A5AD26DF30CD5757"
        },
        {
            "Account": "rhsxr2aAddyCKx5iZctebT4Padxv6iWDxb",
            "BookDirectory": "BCF012C63E83DAF510C7B6B27FE1045CF913B0CF94049AB04E1A1F7606E0BC3C",
            "BookNode": "0000000000000000",
            "Flags": 0,
            "LedgerEntryType": "Offer",
            "OwnerNode": "0000000000000000",
            "PreviousTxnID": "C0E5CE0CDCAD33E9F9D179336E10473E2C1186E52ED5FD2676699FE736D42D8E",
            "PreviousTxnLgrSeq": 6221097,
            "Sequence": 292,
            "TakerGets": "6800000000",
            "TakerPays": {
                "currency": "JPY",
                "issuer": "rMAz5ZnK73nyNUL4foAvaxdreczCkG3vA6",
                "value": "5000"
            },
)LDGER19";

char const* ld_20 = R"LDGER20(
            "index": "F984915B0302CE07E061BC46C82574C37E49B6BF138C5AF092F779F0EE75C3FF"
        },
        {
            "Account": "racJpvgLpaNQGKB8nhKd1gTEVVA1uQWRKs",
            "Balance": "5117962383026",
            "Flags": 0,
            "LedgerEntryType": "AccountRoot",
            "OwnerCount": 3,
            "PreviousTxnID": "96CB1D70009B4FB614DDA58E9A00B67208BAD4AB534862B475329C812A53F3A8",
            "PreviousTxnLgrSeq": 6221359,
            "Sequence": 141,
            "index": "FBB0B2D07AAFB7E1C5371307A77830BD60C3E832AAD793E6C29A939859EC410E"
        },
        {
            "Account": "rEcnyLQD7LXPqTTRG3cXgzcK1C3TDkuUWb",
            "Balance": "99965683",
            "Flags": 0,
            "LedgerEntryType": "AccountRoot",
            "OwnerCount": 3,
            "PreviousTxnID": "89A5C8F3277B3EDF092EDE574EBC101A395730DC42E3B0F32E628FBCDF93644B",
            "PreviousTxnLgrSeq": 3636220,
            "Sequence": 378,
            "index": "FC8BED166F71FE4E547CA588C6580C2521AA5C5432DB2C268717AF9E483B39F5"
        },
        {
            "Flags": 0,
            "Indexes": [
                "4628C6B90FDCCD23E451176B826391D115CBCA5A5E30218E1D83C0D447A21538",
                "8C3B2102834932190981192CC03CA723ABB0BFAA1CA2881D16A6FF47C8A75159"
            ],
            "LedgerEntryType": "DirectoryNode",
            "Owner": "ra284e11Q432pmnoKJY9WC77XN8GUsQvYc",
            "RootIndex": "FDC4DBB9EDD7FB2C612E13256124C3B2A79DB0D27F34F90154D8094DA40A7780",
            "index": "FDC4DBB9EDD7FB2C612E13256124C3B2A79DB0D27F34F90154D8094DA40A7780"
        },
        {
            "Flags": 0,
            "Indexes": [
                "0EFF1D95B5E694B18575969052DDF03A0041064F9B396757DCC0CEA286A8116D",
                "4F68DB8A9E94EB3CD6979892E338288200C0CC370E1CA6AA9FE685D616C5C774",
                "2213B6153CF9370D3A9EC3597C5C9AB6BFA3343C4CCA46AB13CD02C25EB7965A",
                "EAC21ABB5EE92F88C9FE210F5DF00B96F1EDA1892DBDD98EEF64FF12130D73B8"
            ],
            "LedgerEntryType": "DirectoryNode",
            "Owner": "rNruDQQDBM117pzRob2Br211HJrFKxk3tB",
            "RootIndex": "FF821221CD4F45E00F7262244FF6FD971D1CBBA4741AA031A3865B2E8574B5FF",
            "index": "FF821221CD4F45E00F7262244FF6FD971D1CBBA4741AA031A3865B2E8574B5FF"
        }
    ],
    "account_hash": "E4506440FF330BB4C85B3025A18EC329032E09DDA0768E6B63E2676A2D869CD1",
    "close_time": 451530870,
    "close_time_human": "2014-Apr-23 01:14:30",
    "close_time_resolution": 10,
    "closed": true,
    "hash": "04665ADEF09DB9AC5F8E140309BADF7B711838108CE5D3574466DD14D9033DA4",
    "ledger_hash": "04665ADEF09DB9AC5F8E140309BADF7B711838108CE5D3574466DD14D9033DA4",
    "ledger_index": "6226713",
    "parent_hash": "3992A93EEBA15D876AEDA421F501EBE64F512C96AA7FF15F9EB83C39AFF245E1",
    "seqNum": "6226713",
    "totalCoins": "99999995008771466",
    "total_coins": "99999995008771466",
    "transaction_hash": "5F6BAA14CC721D715ED25A1C35720A7A32E052F1D6C10AC61690718E9399A2BB",
    "transactions": []
}

)LDGER20";

std::array<char const*,20> ledgerXRPDiscrepancyData ={{
    ld_01, ld_02, ld_03, ld_04, ld_05,
    ld_06, ld_07, ld_08, ld_09, ld_10,
    ld_11, ld_12, ld_13, ld_14, ld_15,
    ld_16, ld_17, ld_18, ld_19, ld_20
}};

}  // ripple
