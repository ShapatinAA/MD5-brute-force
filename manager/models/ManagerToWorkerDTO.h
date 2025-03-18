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
    Json::Value toJson() const {
        Json::Value result;
        result["RequestId"] = requestId;
        result["PartNumber"] = partNumber;
        result["PartCount"] = partCount;
        result["Hash"] = hash;
        result["MaxLength"] = maxLength;
        result["Alphabet"] = Json::Value(Json::arrayValue);
        for (string letter : alphabet) {
            result["Alphabet"].append(letter);
        }
        return result;
    }

    ManagerToWorkerDTO() = default;

    ManagerToWorkerDTO(const string &requestId,
                          const int &partNumber,
                          const int &partCount,
                          const string &hash,
                          const int &maxLength,
                          vector<string> alphabet) :
    requestId(requestId), partNumber(partNumber), partCount(partCount), hash(hash), maxLength(maxLength), alphabet(alphabet) {}
};
