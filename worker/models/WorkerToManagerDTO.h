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
    string request_id;
    int part_number;
    vector<string> answer;

public:

    WorkerToManagerDTO(const Json::Value &json)
        : request_id(json["RequestId"].asString()),
          part_number(json["PartNumber"].asInt()) {
        for (auto word : json["Answer"]) {
            answer.push_back(word.asString());
        }
    }

    WorkerToManagerDTO() = default;

    WorkerToManagerDTO(const string &request_id,
                       const int &part_number,
                       const vector<string> &answer)
        : request_id(request_id), part_number(part_number), answer(answer) {}

    Json::Value toJson() {
        Json::Value json;
        json["RequestId"] = request_id;
        json["PartNumber"] = part_number;
        for (auto word : answer) {
            json["Answer"].append(word);
        }
        return json;
    }

    string getRequestId() const {return request_id; }

    int getPartNumber() const { return part_number; }

    vector<string> getAnswer() const { return answer; }
};

