//
// Created by Contarr on 12.03.2025.
//

#pragma once
#include <string>
#include <vector>
#include <json/value.h>

using namespace std;

struct WorkerToManagerDTO {

private:
    string requestId;
    int partNumber;
    vector<string> answer;

public:

    WorkerToManagerDTO(const Json::Value &json) :
        requestId(json["RequestId"].asString()), partNumber(json["PartNumber"].asInt())
    {
        for (auto word : json["Answer"]) {
            answer.push_back(word.asString());
        }
    }

    WorkerToManagerDTO() = default;

    WorkerToManagerDTO(const string &requestId,
                          const int &partNumber,
                          const vector<string> &answer) :
    requestId(requestId), partNumber(partNumber), answer(answer) {}

    Json::Value toJson() {
        Json::Value json;
        json["RequestId"] = requestId;
        json["PartNumber"] = partNumber;
        for (auto word : answer) {
            json["Answer"].append(word);
        }
        return json;
    }

    string getRequestId() {return requestId; }

    int getPartNumber() { return partNumber; }

    vector<string> getAnswer() { return answer; }
};

