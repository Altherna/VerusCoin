/********************************************************************
 * (C) 2019 Michael Toutonghi
 * 
 * Distributed under the MIT software license, see the accompanying
 * file COPYING or http://www.opensource.org/licenses/mit-license.php.
 * 
 * This provides support for PBaaS initialization, notarization, and cross-chain token
 * transactions and enabling liquid or non-liquid tokens across the
 * Verus ecosystem.
 * 
 * 
 */

#ifndef PBAAS_H
#define PBAAS_H

#include <vector>

#include "cc/CCinclude.h"
#include "streams.h"
#include "script/script.h"
#include "amount.h"
#include "pbaas/crosschainrpc.h"

#include <boost/algorithm/string.hpp>

// these are output cryptoconditions for the Verus reserve liquidity system
// VRSC can be proxied to other PBaaS chains and sent back for use with this system
// The assumption that Verus is either the proxy on the PBaaS chain or the native
// coin on Verus enables us to reduce data requirements systemwide

// this is for transaction outputs with a Verus proxy on a PBaaS chain cryptoconditions with these outputs
// must also be funded with the native chain for fees, unless the chain is a Verus reserve chain, in which
// case the fee will be autoconverted from the Verus proxy through the conversion rules of this chain

// bits for currency controls
// if CURRENCY_FUNGIBLE is set, it can be bought and sold automatically through its reserves, and any issuance
// must be backed by some amount of Verus. issuance can include inflation of supply relative to reserves, but
// a reserve over 100% can be used to ensure that inflation never causes the reserve to drop below 100% if that
// is desired.
// If CURRENCY_RESERVE is set, then it can at least be converted from the currency to Verus in order to
// pay rewards in Verus. If the currency is reserve, but not fractional and not fungible, all rewards will
// be automatically converted to Verus for payout, and if reserves are emptied, the chain will stop running unless
// miners and stakers decide to mine and stake it for free.
// If CURRENCY_FRACTIONAL is set, then at 100% or less, the Bancor math formulas will determine conversion
// rates at each mined block, dependent on how many exchanges in either direction are processed in that
// block. All transactions processed get exactly the same price for both exchange to and echange from in any
// given block.
// if CURRENCY_CONVERTSTAKES is set, even stake rewards are required to be automatically converted to reserves.
//  

static const uint32_t PBAAS_VERSION = 1;
static const uint32_t PBAAS_VERSION_INVALID = 0;
static const uint32_t PBAAS_NODESPERNOTARIZATION = 2;       // number of nodes to reference in each notarization
static const int64_t PBAAS_MINNOTARIZATIONOUTPUT = 10000;   // enough for one fee worth to finalization and notarization thread
static const int32_t PBAAS_MINSTARTBLOCKDELTA = 100;        // minimum number of blocks to wait for starting a chain after definition

enum CURRENCY_OPTIONS {
    CURRENCY_FUNGIBLE = 1,
    CURRENCY_FRACTIONAL = 2
};

enum PBAAS_SERVICE_TYPES {
    SERVICE_INVALID = 0,
    SERVICE_MINING = 1,
    SERVICE_STAKING = 2,
    SERVICE_NOTARIZATION = 3,
    SERVICE_NODE = 4,
    SERVICE_ELECTRUM = 5,
    SERVICE_LAST = 5
};

enum CHAIN_OBJECT_TYPES
{
    CHAINOBJ_INVALID = 0,
    CHAINOBJ_HEADER = 1,
    CHAINOBJ_TRANSACTION = 2,
    CHAINOBJ_PROOF = 3,
    CHAINOBJ_OPRETPROOF = 4,
    CHAINOBJ_HEADER_REF = 5,
    CHAINOBJ_TRANSACTION_REF = 6,
    CHAINOBJ_OPRET_REF = 7
};

template <typename SERIALIZABLE>
std::vector<unsigned char> AsVector(SERIALIZABLE &obj)
{
    CDataStream s = CDataStream(SER_NETWORK, PROTOCOL_VERSION);
    s << obj;
    return std::vector<unsigned char>(s.begin(), s.end());
}

template <typename SERIALIZABLE>
void FromVector(const std::vector<unsigned char> &vch, SERIALIZABLE &obj)
{
    CDataStream s(vch, SER_NETWORK, PROTOCOL_VERSION);
    obj.Unserialize(s);
}

template <typename SERIALIZABLE>
uint256 GetHash(SERIALIZABLE obj)
{
    CHashWriter hw(SER_GETHASH, PROTOCOL_VERSION);
    hw << obj;
    return hw.GetHash();
}

// the proof of an opret transaction, which is simply the types of objects and hashes of each
class COpRetProof
{
public:
    uint32_t orIndex;                   // index into the opret objects to begin with
    std::vector<uint8_t>    types;
    std::vector<uint256>    hashes;

    COpRetProof() : orIndex(0), types(0), hashes(0) {}
    COpRetProof(std::vector<uint8_t> &rTypes, std::vector<uint256> &rHashes, uint32_t opretIndex = 0) : types(rTypes), hashes(rHashes), orIndex(opretIndex) {}

    void AddObject(CHAIN_OBJECT_TYPES typeCode, uint256 objHash)
    {
        types.push_back(typeCode);
        hashes.push_back(objHash);
    }

    template <typename CHAINOBJTYPE>
    void AddObject(CHAINOBJTYPE &co, uint256 objHash)
    {
        types.push_back(ObjTypeCode(co));
        hashes.push_back(objHash);
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(orIndex);
        READWRITE(types);
        READWRITE(hashes);
    }
};

// refers to an object stored in a specific position of a specific opret that stores an object array
class COpRetRef
{
public:
    uint256 txid;       // transaction with opret
    uint16_t index;     // index of object in opret

    COpRetRef() : txid(), index(0) {}
    COpRetRef(uint256 txHash, uint16_t objIndex) : txid(txHash), index(objIndex) {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(txid);
        READWRITE(index);
    }
};

class CHeaderRef
{
public:
    uint256 hash;

    CHeaderRef() : hash() {}
    CHeaderRef(uint256 &rHash) : hash(rHash) {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(hash);
    }
};

class CTransactionRef
{
public:
    uint256 hash;

    CTransactionRef() : hash() {}
    CTransactionRef(uint256 &rHash) : hash(rHash) {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(hash);
    }
};

class CBaseChainObject
{
public:
    uint16_t objectType;                    // type of object, such as blockheader, transaction, proof, tokentx, etc.

    CBaseChainObject() : objectType(CHAINOBJ_INVALID) {}
    CBaseChainObject(uint16_t objType) : objectType(objType) {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(objectType);
    }
};

template <typename SERIALIZABLE>
class CChainObject : public CBaseChainObject
{
public:
    SERIALIZABLE object;                    // the actual object

    CChainObject() : CBaseChainObject() {}

    CChainObject(uint16_t objType, SERIALIZABLE &rObject) : CBaseChainObject(objType)
    {
        object = rObject;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(objectType);
        READWRITE(object);
    }

    uint256 GetHash() const
    {
        CHashWriter hw(SER_GETHASH, PROTOCOL_VERSION);

        hw << object;
        return GetHash();
    }
};

// returns a pointer to a base chain object, which can be cast to the
// object type indicated in its objType member
uint256 GetChainObjectHash(const CBaseChainObject &bo);

// returns a pointer to a base chain object, which can be cast to the
// object type indicated in its objType member
template <typename OStream>
CBaseChainObject *RehydrateChainObject(OStream s)
{
    uint8_t objType;

    s >> objType;

    union {
        CChainObject<CBlockHeader> *pNewHeader;
        CChainObject<CTransaction> *pNewTx;
        CChainObject<CMerkleBranch> *pNewProof;
        CChainObject<CHeaderRef> *pNewHeaderRef;
        CChainObject<CTransactionRef> *pNewTxRef;
        CChainObject<COpRetRef> *pNewOpRetRef;
        CBaseChainObject *retPtr;
    };

    retPtr = NULL;

    switch(objType)
    {
        case CHAINOBJ_HEADER:
            pNewHeader = new CChainObject<CBlockHeader>();
            if (pNewHeader)
            {
                s >> pNewHeader->object;
                pNewHeader->objectType = objType;
            }
            break;
        case CHAINOBJ_TRANSACTION:
            pNewTx = new CChainObject<CTransaction>();
            if (pNewTx)
            {
                s >> pNewTx->object;
                pNewTx->objectType = objType;
            }
            break;
        case CHAINOBJ_PROOF:
            pNewProof = new CChainObject<CMerkleBranch>();
            if (pNewProof)
            {
                s >> pNewProof->object;
                pNewProof->objectType = objType;
            }
            break;
        case CHAINOBJ_HEADER_REF:
            pNewHeaderRef = new CChainObject<CHeaderRef>();
            if (pNewHeaderRef)
            {
                s >> pNewHeaderRef->object;
                pNewHeaderRef->objectType = objType;
            }
            break;
        case CHAINOBJ_TRANSACTION_REF:
            pNewTxRef = new CChainObject<CTransactionRef>();
            if (pNewTxRef)
            {
                s >> pNewTxRef->object;
                pNewTxRef->objectType = objType;
            }
            break;
        case CHAINOBJ_OPRET_REF:
            pNewOpRetRef = new CChainObject<COpRetRef>();
            if (pNewOpRetRef)
            {
                s >> pNewOpRetRef->object;
                pNewOpRetRef->objectType = objType;
            }
            break;
    }
    return retPtr;
}

// returns a pointer to a base chain object, which can be cast to the
// object type indicated in its objType member
template <typename OStream>
bool DehydrateChainObject(OStream s, const CBaseChainObject *pobj)
{
    s << pobj->objectType;
    switch(pobj->objectType)
    {
        case CHAINOBJ_HEADER:
            s << *(CChainObject<CBlockHeader> *)pobj;
            return true;

        case CHAINOBJ_TRANSACTION:
            s << *(CChainObject<CTransaction> *)pobj;
            return true;

        case CHAINOBJ_PROOF:
            s << *(CChainObject<CMerkleBranch> *)pobj;
            return true;

        case CHAINOBJ_HEADER_REF:
            s << *(CChainObject<CHeaderRef> *)pobj;
            return true;

        case CHAINOBJ_TRANSACTION_REF:
            s << *(CChainObject<CTransactionRef> *)pobj;
            return true;
    }
    return false;
}

int8_t ObjTypeCode(COpRetProof &obj);

int8_t ObjTypeCode(CBlockHeader &obj);

int8_t ObjTypeCode(CMerkleBranch &obj);

int8_t ObjTypeCode(CTransaction &obj);

int8_t ObjTypeCode(CHeaderRef &obj);

int8_t ObjTypeCode(CTransactionRef &obj);

int8_t ObjTypeCode(COpRetRef &obj);

// this creates an opret script that stores a specific chain object
template <typename SERIALIZABLE>
std::vector<unsigned char> StoreChainObject(const SERIALIZABLE &obj)
{
    CScript vData;
    CDataStream s = CDataStream(SER_NETWORK, PROTOCOL_VERSION);

    s << ObjTypeCode(obj);
    s << obj;

    return std::vector<unsigned char>(s.begin(), s.end());
}

// this adds an opret to a mutable transaction that provides the necessary evidence of a signed, cheating stake transaction
CScript StoreOpRetArray(std::vector<CBaseChainObject *> &objPtrs);

std::vector<CBaseChainObject *> RetrieveOpRetArray(const CScript &opRetScript);

class CNodeData
{
public:
    std::string networkAddress;
    std::string paymentAddress;

    CNodeData() {}
    CNodeData(UniValue &);

    CNodeData(std::string netAddr, std::string paymentAddr) :
        networkAddress(netAddr), paymentAddress(paymentAddr) {}
    
    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(networkAddress);
        READWRITE(paymentAddress);        
    }

    UniValue ToUniValue() const;
};

// This defines the currency characteristics of a PBaaS currency that will be the native coins of a PBaaS chain
class CPBaaSChainDefinition
{
public:
    static const int64_t MIN_PER_BLOCK_NOTARIZATION = 1000000;  // 0.01 VRSC per block notarization minimum
    static const int64_t MIN_BILLING_PERIOD = 480;  // 8 hour minimum billing period for notarization, typically expect days/weeks/months
    static const int64_t DEFAULT_OUTPUT_VALUE = 100000;  // 0.001 VRSC default output value

    uint32_t nVersion;                      // version of this chain definition data structure to allow for extensions (not daemon version)
    std::string name;                       // chain name, maximum 64 characters
    std::string address;                    // non-purchased/converted premine and fee recipient address
    int64_t premine;                        // initial supply that is distributed to the premine output address, but not purchased
    int64_t conversion;                     // factor / 100000000 for conversion of VRSC to coin on launch
    int64_t launchFee;                      // ratio of satoshis to send from contribution to convertible to fee address
    int32_t startBlock;                     // parent chain block # that must kickoff the notarization of block 0, cannot be before this block
    int32_t endBlock;                       // block after which this is considered end-of-lifed
    int32_t eras;                           // number of eras, each vector below should have an entry for each
    std::vector<int64_t> rewards;           // initial reward in each ERA in native coin
    std::vector<int64_t> rewardsDecay;      // decay of rewards during the era - only applies to non-converted values
    std::vector<int32_t> halving;           // number of blocks between halvings
    std::vector<int32_t> eraEnd;            // block number that ends each ERA
    std::vector<int32_t> eraOptions;        // flags to determine fungibility and conversion for each ERA

    int32_t billingPeriod;                  // number of blocks in one billing period
    int64_t notarizationReward;             // default amount per block for notarizations

    std::vector<CNodeData> nodes;           // network nodes

    CPBaaSChainDefinition() : nVersion(PBAAS_VERSION_INVALID) {}

    CPBaaSChainDefinition(const UniValue &obj);

    CPBaaSChainDefinition(const std::vector<unsigned char> &asVector)
    {
        ::FromVector(asVector, *this);
    }

    CPBaaSChainDefinition(const CTransaction &tx, bool validate = false);

    CPBaaSChainDefinition(std::string Name, std::string Address, int64_t Premine, int64_t Conversion, int64_t LaunchFee,
                          int32_t StartBlock, int32_t EndBlock, int32_t chainEras,
                          const std::vector<int64_t> &chainRewards, const std::vector<int64_t> &chainRewardsDecay,
                          const std::vector<int32_t> &chainHalving, const std::vector<int32_t> &chainEraEnd, std::vector<int32_t> &chainCurrencyOptions,
                          int32_t BillingPeriod, int64_t NotaryReward, std::vector<CNodeData> &Nodes) :
                            nVersion(PBAAS_VERSION),
                            name(Name),
                            address(Address),
                            premine(Premine),
                            conversion(Conversion),
                            launchFee(LaunchFee),
                            startBlock(StartBlock),
                            endBlock(EndBlock),
                            eras(chainEras),
                            rewards(chainRewards),
                            rewardsDecay(chainRewardsDecay),
                            halving(chainHalving),
                            eraEnd(chainEraEnd),
                            eraOptions(chainCurrencyOptions),
                            billingPeriod(BillingPeriod),
                            notarizationReward(NotaryReward),
                            nodes(Nodes)
    {
        if (Name.size() > (KOMODO_ASSETCHAIN_MAXLEN - 1))
        {
            Name.resize(KOMODO_ASSETCHAIN_MAXLEN - 1);
        }
        name = Name;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(nVersion);
        READWRITE(name);        
        READWRITE(address);        
        READWRITE(VARINT(premine));
        READWRITE(VARINT(conversion));
        READWRITE(VARINT(launchFee));
        READWRITE(startBlock);
        READWRITE(endBlock);
        READWRITE(eras);
        READWRITE(rewards);
        READWRITE(rewardsDecay);
        READWRITE(halving);
        READWRITE(eraEnd);
        READWRITE(eraOptions);
        READWRITE(billingPeriod);
        READWRITE(VARINT(notarizationReward));
        READWRITE(nodes);
    }

    std::vector<unsigned char> AsVector()
    {
        return ::AsVector(*this);
    }

    static uint160 GetChainID(std::string name);

    uint160 GetChainID()
    {
        return GetChainID(name);
    }

    uint160 GetConditionID(int32_t condition);

    bool IsValid()
    {
        return (nVersion != PBAAS_VERSION_INVALID) && (name.size() && eras > 0) && (eras <= ASSETCHAINS_MAX_ERAS);
    }

    UniValue ToUniValue() const;

    int GetDefinedPort() const;
};

// This data structure is used on an output that provides proof of stake validation for other crypto conditions
// with rate limited spends based on a PoS contest
class CPoSSelector
{
public:
    uint32_t nBits;                         // PoS difficulty target
    uint32_t nTargetSpacing;                // number of 1/1000ths of a block between selections (e.g. 1 == 1000 selections per block)

    CPoSSelector(uint32_t bits, uint32_t TargetSpacing)
    {
        nBits = bits; 
        nTargetSpacing = TargetSpacing;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(nBits);
        READWRITE(nTargetSpacing);
    }

    CPoSSelector(const std::vector<unsigned char> &asVector)
    {
        FromVector(asVector, *this);
    }

    std::vector<unsigned char> AsVector()
    {
        return ::AsVector(*this);
    }

    bool IsValid()
    {
        return nBits != 0;
    }
};

// Additional data for an output pool used for a PBaaS chain's reward for service, such as mining, staking, node or electrum service
class CServiceReward
{
public:
    uint16_t serviceType;                   // type of service
    CAmount nPerReward;                     // amount to pay per reward

    CServiceReward() : serviceType(SERVICE_INVALID) {}

    CServiceReward(PBAAS_SERVICE_TYPES ServiceType, CAmount PerReward)
    {
        serviceType = ServiceType; 
        nPerReward = PerReward;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(serviceType);
        READWRITE(VARINT(nPerReward));
    }

    CServiceReward(const std::vector<unsigned char> &asVector)
    {
        FromVector(asVector, *this);
    }

    std::vector<unsigned char> AsVector()
    {
        return ::AsVector(*this);
    }

    bool IsValid()
    {
        return serviceType != SERVICE_INVALID;
    }
};

class CCrossChainProof
{
public:
    uint256 notarizationRoot;               // notarization txid used as the root of proof
    CMerkleBranch branch;                   // proof of the transaction on the other chain and import, transaction is stored separately
    int32_t opretIndex;                     // index of the tx in the opret

    CCrossChainProof() : opretIndex(-1) {}
    CCrossChainProof(uint256 &rootTxId, uint256 &nTX, MerkleBranch &b);
    CCrossChainProof(uint256 &rootTxId, MerkleBranch &b);  // this will locate notarization transactions

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(notarizationRoot);
        READWRITE(branch);
        READWRITE(opretIndex);
    }

    bool IsValid()
    {
        return !notarizationRoot.IsNull();
    }
};

// This is used on a PBaaS chain to manage reserves of VRSC. type of reserve is assumed to be $VRSC
class CReserveOutput
{
public:
    CAmount nValue;                         // amount of reserve coin in this output
    CScript scrOut;                         // output script, spend is validated as with a normal bitcoin spend

    CReserveOutput(const std::vector<unsigned char> &asVector)
    {
        FromVector(asVector, *this);
    }

    CReserveOutput(const CAmount value, const CScript &rScrOut)
    {
        nValue = value; 
        scrOut = rScrOut;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(VARINT(nValue));
        READWRITE(*(CScriptBase*)(&scrOut));
    }

    std::vector<unsigned char> AsVector()
    {
        return ::AsVector(*this);
    }

    bool IsValid()
    {
        return scrOut.size() != 0;
    }
};

// import $VRSC from another chain transaction
class CReserveImport
{
public:
    CAmount nValue;                         // amount of proxy coin in this output
    CCrossChainProof transferProof;

    CReserveImport(const std::vector<unsigned char> &asVector)
    {
        FromVector(asVector, *this);
    }

    CReserveImport(const CAmount value, CCrossChainProof &rTransferProof)
    {
        nValue = value; 
        transferProof = rTransferProof;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(VARINT(nValue));
        READWRITE(transferProof);
    }

    std::vector<unsigned char> AsVector()
    {
        return ::AsVector(*this);
    }

    bool IsValid()
    {
        return nValue != 0 && transferProof.IsValid();
    }
};

// send $VRSC or VRSCTOKEN to another chain
class CReserveExport
{
public:
    CAmount nValue;
    uint256 chainDest;

    CReserveExport(const std::vector<unsigned char> &asVector)
    {
        FromVector(asVector, *this);
    }

    CReserveExport(const CAmount value, const uint256 &rChainDest)
    {
        nValue = value; 
        chainDest = rChainDest;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(VARINT(nValue));
        READWRITE(chainDest);
    }

    std::vector<unsigned char> AsVector()
    {
        return ::AsVector(*this);
    }

    bool IsValid()
    {
        return nValue != 0 && !chainDest.IsNull();
    }
};

// convert from $VRSC to fractional reserve coin or vice versa. coinID determines which``
class CReserveExchange
{
public:
    CScript scrOut;                         // output script for resulting coinbase output
    CAmount nLimit;                         // lowest or highest price to sell or buy coin output, may fail if including this tx in block makes price out of range
    uint32_t nValidBefore;                  // if not filled in this block, mine tx, but refund input
    uint256 chainID;                        // currently supports convert from or to reserve according to conversion rules, this is ouput type

    CReserveExchange(const std::vector<unsigned char> &asVector)
    {
        FromVector(asVector, *this);
    }

    CReserveExchange(const CScript &rScrOut, const CAmount Limit, uint32_t ValidBefore, uint256 ChainID)
    {
        scrOut = rScrOut;
        nLimit = Limit;
        nValidBefore = ValidBefore;
        chainID = ChainID;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(*(CScriptBase*)(&scrOut));
        READWRITE(VARINT(nLimit));
        READWRITE(nValidBefore);
        READWRITE(chainID);
    }

    std::vector<unsigned char> AsVector()
    {
        return ::AsVector(*this);
    }

    bool IsValid()
    {
        // this needs an actual check
        return nValidBefore != 0 && scrOut.size() != 0 && !chainID.IsNull();
    }
};

class CRPCChainData
{
public:
    CPBaaSChainDefinition chainDefinition;  // chain information for the specific chain
    std::string     rpcHost;                // host of the chain's daemon
    int32_t         rpcPort;                // port of the chain's daemon
    std::string     rpcUserPass;            // user and password for this daemon

    CRPCChainData() {}
    CRPCChainData(CPBaaSChainDefinition &chainDef, std::string host, int32_t port, std::string userPass) :
        chainDefinition(chainDef), rpcHost{host}, rpcPort(port), rpcUserPass(userPass) {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(chainDefinition);
        READWRITE(rpcHost);
        READWRITE(rpcPort);
        READWRITE(rpcUserPass);
    }

    std::vector<unsigned char> AsVector()
    {
        return ::AsVector(*this);
    }

    bool IsValid()
    {
        return chainDefinition.IsValid();
    }

    uint160 GetChainID()
    {
        return chainDefinition.GetChainID();
    }
};

// Each merge mined chain gets an entry that includes information required to connect to a live daemon
// for that block, cross notarize, and validate notarizations.
class CPBaaSMergeMinedChainData : public CRPCChainData
{
public:
    static const uint32_t MAX_MERGE_CHAINS = 15;
    CBlock          block;                  // full block to submit upon winning header

    CPBaaSMergeMinedChainData() {}
    CPBaaSMergeMinedChainData(CPBaaSChainDefinition &chainDef, std::string host, int32_t port, std::string userPass, CBlock &blk) :
        CRPCChainData(chainDef, host, port, userPass), block(blk) {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(chainDefinition);
        READWRITE(rpcHost);
        READWRITE(rpcPort);
        READWRITE(rpcUserPass);
        READWRITE(block);
    }

    std::vector<unsigned char> AsVector()
    {
        return ::AsVector(*this);
    }
};

class CConnectedChains
{
protected:
    CPBaaSMergeMinedChainData *GetChainInfo(uint160 chainID);

public:
    std::map<uint160, CPBaaSMergeMinedChainData> mergeMinedChains;
    std::map<arith_uint256, CPBaaSMergeMinedChainData *> mergeMinedTargets;

    std::string notaryChainVersion;             // is Verus or VRSCTEST running and PBaaS compatible?
    int32_t notaryChainHeight;                  // is Verus or VRSCTEST running and PBaaS compatible?
    CRPCChainData notaryChain;                  // notary chain information

    CPBaaSChainDefinition thisChain;

    bool dirty;
    bool lastSubmissionFailed;                  // if we submit a failed block, make another
    std::map<arith_uint256, CBlockHeader> qualifiedHeaders;

    CCriticalSection cs_mergemining;
    CSemaphore sem_submitthread;

    CConnectedChains() : sem_submitthread(0), dirty(0), lastSubmissionFailed(0) {}

    arith_uint256 LowestTarget()
    {
        if (mergeMinedTargets.size())
        {
            return mergeMinedTargets.begin()->first;
        }
        else
        {
            return arith_uint256(0);
        }
    }

    void SubmissionThread();
    static void SubmissionThreadStub();
    std::vector<std::pair<std::string, UniValue>> SubmitQualifiedBlocks();

    bool QueueNewBlockHeader(CBlockHeader &bh);

    bool AddMergedBlock(CPBaaSMergeMinedChainData &blkData);
    bool RemoveMergedBlock(uint160 chainID);
    bool GetChainInfo(uint160 chainID, CRPCChainData &rpcChainData);
    uint32_t PruneOldChains(uint32_t pruneBefore);
    uint32_t CombineBlocks(CBlockHeader &bh);

    CRPCChainData &NotaryChain()
    {
        return notaryChain;
    }

    CPBaaSChainDefinition &ThisChain()
    {
        return thisChain;
    }

    bool CheckVerusPBaaSAvailable(UniValue &chainInfo, UniValue &chainDef);
    bool CheckVerusPBaaSAvailable();      // may use RPC to call Verus
    bool IsVerusPBaaSAvailable();
};

template <typename TOBJ>
CTxOut MakeCC1of1Vout(uint8_t evalcode, CAmount nValue, CPubKey pk, std::vector<CTxDestination> vDest, TOBJ &obj)
{
    CTxOut vout;
    CC *payoutCond = MakeCCcond1(evalcode, pk);
    vout = CTxOut(nValue, CCPubKey(payoutCond));
    cc_free(payoutCond);

    std::vector<std::vector<unsigned char>> vvch({::AsVector(obj)});
    COptCCParams vParams = COptCCParams(COptCCParams::VERSION_V2, evalcode, 1, 1, vDest, vvch);

    // add the object to the end of the script
    vout.scriptPubKey << vParams.AsVector() << OP_DROP;
    return(vout);
}

template <typename TOBJ>
CTxOut MakeCC1of2Vout(uint8_t evalcode, CAmount nValue, CPubKey pk1, CPubKey pk2, TOBJ &obj)
{
    CTxOut vout;
    CC *payoutCond = MakeCCcond1of2(evalcode, pk1, pk2);
    vout = CTxOut(nValue,CCPubKey(payoutCond));
    cc_free(payoutCond);

    std::vector<CPubKey> vpk({pk1, pk2});
    std::vector<std::vector<unsigned char>> vvch({::AsVector(obj)});
    COptCCParams vParams = COptCCParams(COptCCParams::VERSION_V2, evalcode, 1, 1, vpk, vvch);

    // add the object to the end of the script
    vout.scriptPubKey << vParams.AsVector() << OP_DROP;
    return(vout);
}

bool IsVerusActive();

// used to export coins from one chain to another, if they are not native, they are represented on the other
// chain as tokens
bool ValidateChainExport(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, uint32_t nIn);

// used to validate import of coins from one chain to another. if they are not native and are supported,
// they are represented o the chain as tokens
bool ValidateChainImport(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, uint32_t nIn);

// used to validate a specific service reward based on the spending transaction
bool ValidateServiceReward(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, uint32_t nIn);

// used as a proxy token output for a reserve currency on its fractional reserve chain
bool ValidateReserveOutput(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, uint32_t nIn);

// used to convert a fractional reserve currency into its reserve and back 
bool ValidateReserveExchange(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, uint32_t nIn);

// used for distribution of premine
bool ValidatePremineOutput(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, uint32_t nIn);

bool ValidateChainDefinition(struct CCcontract_info *cp, Eval* eval, const CTransaction &tx, uint32_t nIn);
bool IsChainDefinitionInput(const CScript &scriptSig);

bool GetCCParams(const CScript &scr, COptCCParams &ccParams);

bool SetThisChain(UniValue &chainDefinition);

extern CConnectedChains ConnectedChains;
extern uint160 ASSETCHAINS_CHAINID;

#endif
