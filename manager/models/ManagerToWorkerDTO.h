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
    string request_id;
    int part_number;
    int part_count;
    string hash;
    int max_length;
    vector<string> alphabet;
public:
    Json::Value toJson() const {
        Json::Value result;
        result["RequestId"] = request_id;
        result["PartNumber"] = part_number;
        result["PartCount"] = part_count;
        result["Hash"] = hash;
        result["MaxLength"] = max_length;
        result["Alphabet"] = Json::Value(Json::arrayValue);
        for (string letter : alphabet) {
            result["Alphabet"].append(letter);
        }
        return result;
    }

    ManagerToWorkerDTO() = default;

    ManagerToWorkerDTO(const string &request_id, const int &part_number,
                       const int &part_count, const string &hash,
                       const int &max_length, vector<string> alphabet)
        : request_id(request_id),
          part_number(part_number),
          part_count(part_count),
          hash(hash),
          max_length(max_length),
          alphabet(alphabet) {}
};
