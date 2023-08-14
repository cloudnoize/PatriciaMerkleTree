#include<iostream>
#include <string>
#include<map>
#include<set>
#include<memory>
#include<optional>
#include <cassert>
#include <cstring>
#include <queue>
#include <iomanip>
 /*
 TODOs- 
  - Key should be binary.
  - Basic optimizations.
  - Support Deletions.
  - Merkle Proofs
  - versioned imp - the Node array bool branches[16] = {false}; will be uint64 array that will hold the version of the child node.
    The version will be a postfix/prefix of the key,

 */


std::string basicHash(const std::string &input) {
    unsigned long long hash[4] = {5381, 0, 12345678, 87654321};

    for (int j = 0; j < 4; ++j) {
        for (char c : input) {
            hash[j] = ((hash[j] << 5) + hash[j]) + c + j; 
        }
    }

    // Convert hashes to string
    std::string result;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 8; j++) {
            result += static_cast<char>((hash[i] >> (j * 8)) & 0xFF);
        }
    }

    return result;
}



struct NibblePath{
    using Nibble = uint8_t;
    const static uint8_t kEndOfNibbles = 0x10;
    static std::map<char,uint8_t> HextoNib ;
    static std::map<char,uint8_t> NibToHex ;
    const static uint8_t kNibbleSizeInBits = 4;
    const static uint8_t kUpperMask = 0xf0;
    const static uint8_t kLowerMask = 0x0f;

    static Nibble HexNibbleToNibble(char c){
        return HextoNib[c];
    }
    static Nibble getUpperNibble(uint8_t c){
        return (c & kUpperMask) >> kNibbleSizeInBits;
    }
    static Nibble getLowerNibble(uint8_t c){
        return (c & kLowerMask);
    }
    static std::string keyToNibbleHex(const std::string& key){
        std::string ret;
        for(auto c : key){
            ret.push_back(NibToHex[NibblePath::getUpperNibble(c)]);
            ret.push_back(NibToHex[NibblePath::getLowerNibble(c)]);
        }
        return ret;
    }
};

std::map<char,uint8_t> NibblePath::HextoNib = {{'0',0},{'1',1},{'2',2},{'3',3},{'4',4},{'5',5},{'6',6},{'7',7},{'8',8},{'9',9},{'A',10},{'B',11},{'C',12},{'D',13},{'E',14},{'F',15}};
std::map<char,uint8_t> NibblePath::NibToHex = {{0,'0'},{1,'1'},{2,'2'},{3,'3'},{4,'4'},{5,'5'},{6,'6'},{7,'7'},{8,'8'},{9,'9'},{10,'A'},{11,'B'},{12,'C'},{13,'D'},{14,'E'},{15,'F'}};

const std::string&  getRootKey(bool raw = false){
    const static std::string rootKey = ".";
    static std::string hexRootKey;
    if(hexRootKey.empty()){
        hexRootKey = NibblePath::keyToNibbleHex(rootKey);
    }
	return raw ? rootKey : hexRootKey;
}

struct Node{
	std::string valueHash;
	std::string extension;
    bool isValue = false;
	bool branches[16] = {false};
    std::string merkleHash;
};

std::ostream& operator<<(std::ostream& os, const Node& node) {
    os << "Node {" << "\n"
       << "  valueHash: " << NibblePath::keyToNibbleHex(node.valueHash) << "\n"
       << "  extension: " << node.extension << "\n"
       << "  isValue: " << (node.isValue ? "true" : "false") << "\n"
       << "  branches: [";
    for (int i = 0; i < 16; ++i) {
        os << (node.branches[i] ? "true" : "false");
        if (i != 15) os << ", ";
    }
    os << "]\n"
       << "  merkleHash: " << NibblePath::keyToNibbleHex(node.merkleHash) << "\n"
       << "}";
    return os;
}


//Currnetly we won't do a versioned tree
uint64_t rootVersion = 0;
// Represents the nodes that have changed and need to be written
std::map<std::string,Node*,std::less<>> nodesToUpdate;
//Represents the on disk data
std::map<std::string,Node*,std::less<>> db;
// Thinking of updateing it in a bfs manner i.e. having a queue of updated leaves at the end of the update,
// then poping nodes, each node updates its Merkle Value  and pushing to the queue its parent. 
std::queue<std::pair<std::string_view,Node*>> bfs;

Node* directGetNode(const std::string& key){
    if(nodesToUpdate.count(key) == 1){
        return nodesToUpdate[key];
    }
    if(db.count(key) == 0) return nullptr;
    return db[key];
}

std::pair<std::string,std::string> getKeyAndExtension(uint64_t divider,const std::string& hexNibKey){
    return {std::string(hexNibKey.c_str(),divider),std::string(hexNibKey.c_str() + divider, hexNibKey.size() - divider)};
}

std::pair<std::string_view,std::string_view> getKeyAndExtensionByView(uint64_t divider,const std::string& hexNibKey){
    return {std::string_view(hexNibKey.c_str(),divider),std::string_view(hexNibKey.c_str() + divider, hexNibKey.size() - divider)};
}

void AddNewNodeToUpdate(uint64_t pathPos,const std::string& hexNibKey,std::optional<std::reference_wrapper<const std::string>> val,std::string* retKey = nullptr){
    auto node = new Node();
    auto[key,extension] = getKeyAndExtension(pathPos,hexNibKey);
    node->extension = std::move(extension);
    if(val){
        node->isValue = true;
        node->valueHash = basicHash(*val);
    }
    std::cout << "Adding node " << *node << "\nwith key " << key << "\n";
    if(retKey!=nullptr){
        *retKey = key;
    }
    auto [it, success] = nodesToUpdate.insert({std::move(key), node});
    
    if (success) {
        std::cout << "Adding node " << *node << "\nwith key " << it->first << "\n";
        if(retKey != nullptr){
            *retKey = it->first; // Set the return key to the key from the map
        }
        if(val){
            std::cout << "Adding value node to bfs, bfs size " << bfs.size() << "\n"; 
            bfs.push({it->first, node}); // Now, the string_view points to the string within nodesToUpdate
        }
    }
}
//cases:
// if extension are not equal, shrink node to the common extension and add to it two nodes of the diverge
// an extension key node can be a value only if the key terminates at the extension
// if an extensions is a substring of the other extension then:
// - if its the updated key, it should update the value of the node
// - it its the node then we should skip the extension in the updatd key and continue
void splitCommonExtension(uint64_t pathPos,const std::string& hexNibKey,const std::string& val,Node* node){
    auto [currentKey,keyExtension] = getKeyAndExtensionByView(pathPos,hexNibKey);
    std::cout << "Splitting common extenstion, node extension " << node->extension << " key extension " << keyExtension << "\n";
    //equal i.e. update
    // E.L handled bdfore
    if(keyExtension == node->extension){
        node->isValue =true;
        node->valueHash = basicHash(val);
        std::cout << "Extension equals, update node " << *node << "\n";
        nodesToUpdate.emplace(std::move(currentKey),node);
        return;
    }
    uint64_t sizeOfCommonExtension = 0;
    for(;sizeOfCommonExtension < node->extension.size() && sizeOfCommonExtension < keyExtension.size() ; ++sizeOfCommonExtension ){
        if(node->extension[sizeOfCommonExtension] == keyExtension[sizeOfCommonExtension])continue;
        break;
    }
    std::cout << "Extensions common size " << sizeOfCommonExtension << "\n";
    if(sizeOfCommonExtension == keyExtension.size()){
        //assert(i<node->extension.size())
        // ++pathPos - to include the first nibble of the extension
        std::cout << "New key is a sub string of current on the path of\n";
        std::string newnodeKey;
        AddNewNodeToUpdate(pathPos,hexNibKey,val,&newnodeKey);
        auto newNode = nodesToUpdate[newnodeKey];
        // truncate the added key common extension from the node extension
        auto [_,remaining] =  getKeyAndExtension(sizeOfCommonExtension,node->extension);
        auto wholeString = hexNibKey + remaining;
        // New key equals the added key + the first char of the lefover of the old extension.
        auto [newKeyAfterSplit,newExtensionAfterSplit] = getKeyAndExtension(hexNibKey.size()+1,wholeString);
        node->extension = newExtensionAfterSplit;
        std::cout << "Prv extension new key " << newKeyAfterSplit << " and new extension " << node->extension << "\n";
        //the last hex char of the new key (converted to nubble) is where the branch of the new node happens.
        newNode->branches[NibblePath::HexNibbleToNibble(newKeyAfterSplit.back())] = true;
        std::cout << "New node key " << newnodeKey << " node " << *newNode;
        // the node was forwarded to 
        nodesToUpdate[newKeyAfterSplit] = node;
        std::cout << "Updated prv extension " << *node << "\n";
    } else {
        // common extenstion is smaller than both, create node for common extension (can be no extension at all) add to it a node for the new key,
        // and add also a the modified extension node with a new key
        //new extension node is the following path
        auto pathOfNewExtension = hexNibKey.substr(0,pathPos+sizeOfCommonExtension);
        std::string newExtNodeKey;
        AddNewNodeToUpdate(pathPos,pathOfNewExtension,std::nullopt,&newExtNodeKey); 
        auto newNode = nodesToUpdate[newExtNodeKey];
        // set true on new key and add it
        newNode->branches[NibblePath::HexNibbleToNibble(hexNibKey[pathPos + sizeOfCommonExtension])] = true;
        std::cout << "Common extension is smallaer then both, adding new extension node, with key " << newExtNodeKey << " node " << *newNode << "\n";
        AddNewNodeToUpdate(pathPos + sizeOfCommonExtension + 1,hexNibKey,val);
        //shorten node extension
        auto wholeKeyOfPrvExtension = std::string(currentKey) + node->extension;
        auto [updatedKey,updatedExtension] = getKeyAndExtension(pathPos + sizeOfCommonExtension + 1,wholeKeyOfPrvExtension);
        node->extension = updatedExtension;
        nodesToUpdate[updatedKey] = node;
        newNode->branches[NibblePath::HexNibbleToNibble(updatedKey.back())] = true;
    }
    
}

//key is a unique nibble path, if it's extension it terminates at the first mibble of the ext.
// if it has extension in the paths , the key contains their whole extension nibble
void insertKV(std::string key ,std::string val){
    std::string currentNodeKey = getRootKey();
    NibblePath nibblePath;
    Node* node = directGetNode(currentNodeKey);
    std::string hexNibbleKey = NibblePath::keyToNibbleHex(key);
    std::cout << "Inserting key " << key << " hex nibble key " << hexNibbleKey << "\n";
    //Starting from 2 since the first 2 nubbles are from the root key
	for(int i = 2 ; i < hexNibbleKey.size();++i){
        auto hexNibble = hexNibbleKey[i];
        std::cout << "Searching node " << *node << "\n";
        auto nibble = NibblePath::HexNibbleToNibble(hexNibble);
        std::cout << "current node key " << currentNodeKey << " Handling nibble value of " << (int)nibble << "\n";
        if(node->extension.empty()){
            if(node->branches[nibble] == false){
                std::cout << "branch is false, adding new node \n";
                AddNewNodeToUpdate(i+1,hexNibbleKey,val);
                node->branches[nibble]  = true;
                nodesToUpdate[currentNodeKey] = node;
                std::cout << "Updating current node to indicate new branch " << *node << "\n";
                return;
            }
            auto [crn,_] = getKeyAndExtension(i+1,hexNibbleKey);
            currentNodeKey = crn;
            node = directGetNode(currentNodeKey);
            std::cout << "Continue searhing, next node key " << currentNodeKey << "\n";
            assert(node!=nullptr);
            continue;
        }
        // Extension cases
        // check if node extension matches the key extension
        auto keyExtension = hexNibbleKey.substr(i,hexNibbleKey.size());
        std::cout << "Comparing if node extension " << node->extension << " is substring of key extension " << keyExtension ;
        auto isNodeExtensionIsSubstringOfKeyEx = ((keyExtension.size() > node->extension.size()) && (memcmp(node->extension.c_str(),keyExtension.c_str(),node->extension.size()) == 0));
        if(!isNodeExtensionIsSubstringOfKeyEx){
            std::cout << " No\n";
            splitCommonExtension(i,hexNibbleKey,val,node);
            return;
        }
         std::cout << " Yes\n";
        // The same extension
        if(node->extension == keyExtension){
            node->isValue = true;
            node->valueHash = basicHash(val);
            nodesToUpdate[currentNodeKey] = node;
            std::cout << "Key matches extension node, updating to " << *node << "\n";
            return;
        }
        auto endOfNewKey = i+node->extension.size() + 1;
        nibble = NibblePath::HexNibbleToNibble(hexNibbleKey[endOfNewKey+1]);
        if(node->branches[nibble] == false){
            std::cout << "Extension branch is false adding node\n";
            AddNewNodeToUpdate(endOfNewKey,hexNibbleKey,val);
            node->branches[nibble]  = true;
            nodesToUpdate[currentNodeKey] = node;
            std::cout << "Updating current node to indicate new branch " << *node << "\n";
            return;
        }
        i = endOfNewKey - 1; 
        auto[crn,_] = getKeyAndExtension(endOfNewKey,hexNibbleKey);
        currentNodeKey = crn;
        node = directGetNode(currentNodeKey);
        std::cout << "Continue with key " << currentNodeKey << " i is " << i << " node " << *node << "\n";
        assert(node!=nullptr);
	}
    // proabably key is path of another key,
    node->valueHash = basicHash(val);
    node->isValue = true;
    nodesToUpdate[hexNibbleKey] = node;
}

std::optional<Node*> findParentNode(std::string_view& key){
    while(!key.empty()){
        key.remove_suffix(1);
        std::cout << "Looking for parent of key  " << key << "\n"; 
        if(nodesToUpdate.count(key) == 1){
            std::cout << "Parent node is in nodesToUpdate with key " << key << "\n";
            return nodesToUpdate.find(key)->second;
        }
        if(db.count(key) == 1){
            std::cout << "Parent node is in db with key " << key << "\n";
            return db.find(key)->second;
        }
    }
    return std::nullopt;
}

void updateNodeMerkleValue(std::string key,Node* node){
    //compose whole key
    key += node->extension;
    // make space for a char to use in child keys
    key.push_back(0);
    //can reserve
    std::string toMerkleHash;
    std::cout << "calculating merkle value of node with key " << key << "\n";
    for(NibblePath::Nibble i = 0 ; i < NibblePath::kEndOfNibbles ; ++i){
        if(node->branches[i]){
            key[key.size()-1] = NibblePath::NibToHex[i];
            std::cout << "Node has child with key " << key << "\n";
            Node* cnode = nullptr;
            if(nodesToUpdate.count(key) == 1){
                cnode = nodesToUpdate[key];
            }else if(db.count(key) == 1){
                cnode = db[key];
            }
            assert(cnode != nullptr);
            std::cout << "Getting merkle of node with key " << key << " merkle " << NibblePath::keyToNibbleHex(cnode->merkleHash) << "\n";
            toMerkleHash +=  cnode->merkleHash;
        }
    }
    toMerkleHash += node->valueHash;
    node->merkleHash = basicHash(toMerkleHash);
}

void updateMerkleRoot(){
    while(!bfs.empty()){
        std::pair<std::string_view,Node*> nodePair = bfs.front();
        bfs.pop();
        updateNodeMerkleValue(std::string(nodePair.first),nodePair.second);
        // using find to enjoy the string_View lookup, furthermore item must exist
        if(nodesToUpdate.count(nodePair.first) == 1){
            nodesToUpdate.find(nodePair.first)->second = nodePair.second;
        }else{
            nodesToUpdate[std::string(nodePair.first)] = nodePair.second;
        }
        if(nodePair.first == getRootKey()){
            std::cout << "Arrived to root\n";
            continue;
        }
        // changes the key to the returned node's key
        std::cout << "Node Key is " << nodePair.first << "\n";
        auto optParentNode = findParentNode(nodePair.first);
        assert(optParentNode.has_value());
        bfs.push({nodePair.first,*optParentNode});
    }
    assert(bfs.empty());
    const auto& mh =  nodesToUpdate[getRootKey()]->merkleHash;
    std::cout << "Finished calculating tree, root merkle hash is " << NibblePath::keyToNibbleHex(nodesToUpdate[getRootKey()]->merkleHash) << " merkle hash size " << mh.size() << "\n";
}


void updateDb(){
    if(bfs.size() > 0){
        updateMerkleRoot();
    }
    for(auto[k,v] : nodesToUpdate){
        db[k] = v;
    }
    nodesToUpdate.clear();
}

void insertBlock(std::map<std::string,std::string> block){
	if(block.size() == 0 ) return ;
	for(const auto& [k,v] : block){
		insertKV(k,v);
	}
	++rootVersion;
    updateDb();
}

Node* getNode(const std::string& key){
    auto hexNibbleKey = NibblePath::keyToNibbleHex(key);
    std::cout << "Searching for " << hexNibbleKey << "\n";
    auto currentKey =  getRootKey();
    // bool isRoot = true;
    
    for(uint32_t loc = 0; loc < hexNibbleKey.size();){
        if(db.count(currentKey) == 0){
            std::cout << "Key not found, Didn't find node for key " << currentKey << "\n";
            break;
        }
        auto node = db[currentKey];
        // if(isRoot){
        //     currentKey = "";
        //     //E.L make transparent
        //     isRoot = false;
        // }
        // first check if node represents the key
        auto wholeKey = currentKey + node->extension;
        std::cout << "Current node represents the key " << wholeKey << "\n";
        if(wholeKey == hexNibbleKey){
            if(!node->isValue){
                std::cout << "Key not found. node's key matches but is not marked as value\n";
                break;
            }
            std::cout << "Found node, key " << currentKey << " extension " << node->extension << " as a whole " << wholeKey << "\n";
            return node;
        }
        if(wholeKey.size() > hexNibbleKey.size() || hexNibbleKey.substr(0,wholeKey.size())!=wholeKey){
            std::cout << "Key not found. Whole key is not substring of key\n";
            break;
        }
        std::cout << "Whole key is substring of key\n";
        loc = wholeKey.size();
        auto nextNibble = NibblePath::HexNibbleToNibble(hexNibbleKey[loc]);
        std::cout << "Checking next nibble btanch - " << (int)nextNibble << "\n";
        if(!node->branches[nextNibble]){
            std::cout << "Key not found, nibble branch is false " << *node << "\n";
            break;
        }
        currentKey = wholeKey + hexNibbleKey[loc];
        std::cout << "Current code " << currentKey << "\n";

    }
    return nullptr;
}


int main() {
    std::string choice;
    bool rawKey = true;
    db[getRootKey()] = new Node();

    while (true) {
        std::cout << "Please choose an action (insert or get): ";
        std::cin >> choice;

        if (choice == "insert") {
            std::map<std::string,std::string> block;
            std::string key, value;
            std::cout << "Enter key: ";
            std::cin >> key;
            key = getRootKey(rawKey) + key;
            std::cout << "Enter value: ";
            std::cin >> value;
            block[key] = value;
            insertBlock(block);
        } else if (choice == "get") {
            std::string key;
            std::cout << "Enter key: ";
            std::cin >> key;
            auto node = getNode(getRootKey(rawKey) + key);
            if(node == nullptr){
                std::cout << "Key wasn'ft found\n";
                continue;
            }
            std::cout << "node found " << *node << "\n";
           
        } else {
            std::cout << "Invalid choice. Please choose either 'insert' or 'get'.\n";
        }
    }

    return 0;
}
