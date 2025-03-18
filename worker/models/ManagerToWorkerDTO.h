//
// Created by Contarr on 12.03.2025.
//

//
// Created by Contarr on 11.03.2025.
//

#pragma once

#include <string>
#include <vector>
#include <json/value.h>

using namespace std;

struct ManagerToWorkerDTO {
private:
    string requestId;
    int partNumber;
    int partCount;
    string hash;
    int maxLength;
    vector<string> alphabet;
public:

    ManagerToWorkerDTO() = default;

    ManagerToWorkerDTO(const string &requestId,
                          const int &partNumber,
                          const int &partCount,
                          const string &hash,
                          const int &maxLength,
                          const vector<string> &alphabet) :
    requestId(requestId), partNumber(partNumber), partCount(partCount), hash(hash), maxLength(maxLength), alphabet(alphabet) {}

    ManagerToWorkerDTO(const Json::Value &json) :
    requestId(json["RequestId"].asString()), partNumber(json["PartNumber"].asInt()), partCount(json["PartCount"].asInt()),
    hash(json["Hash"].asString()), maxLength(json["MaxLength"].asInt()) {
        for (auto letter : json["Alphabet"]) {
            alphabet.push_back(letter.asString());
        }
    }

    static bool validateJson(const shared_ptr<Json::Value> &json) {
        if (json == nullptr ||
            !(*json)["RequestId"].isString() ||
            !(*json)["PartNumber"].isInt() ||
            !(*json)["PartCount"].isInt() ||
            !(*json)["Hash"].isString() ||
            !(*json)["MaxLength"].isInt() ||
            !(*json)["Alphabet"].isArray()) {
            return false;
        }
        return true;
    }

    string getRequestId() {return requestId;}
    int getPartNumber() {return partNumber;}
    int getPartCount() {return partCount;}
    string getHash() {return hash;}
    int getMaxLength() {return maxLength;}
    vector<string> getAlphabet() const {return alphabet;}
};

