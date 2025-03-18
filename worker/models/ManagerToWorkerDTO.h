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
    string request_id;
    int part_number;
    int part_count;
    string hash;
    int max_length;
    vector<string> alphabet;
public:

    ManagerToWorkerDTO() = default;

    ManagerToWorkerDTO(const string &request_id,
                       const int &part_number,
                       const int &part_count,
                       const string &hash,
                       const int &max_length,
                       const vector<string> &alphabet)
        : request_id(request_id),
          part_number(part_number),
          part_count(part_count),
          hash(hash),
          max_length(max_length),
          alphabet(alphabet) {}

    ManagerToWorkerDTO(const Json::Value &json)
        : request_id(json["RequestId"].asString()),
          part_number(json["PartNumber"].asInt()),
          part_count(json["PartCount"].asInt()),
          hash(json["Hash"].asString()),
          max_length(json["MaxLength"].asInt()) {
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

    string getRequestId() const {return request_id;}
    int getPartNumber() const {return part_number;}
    int getPartCount() const {return part_count;}
    string getHash() const {return hash;}
    int getMaxLength() const {return max_length;}
    vector<string> getAlphabet() const {return alphabet;}
};

