//
// Created by Contarr on 27.04.2025.
//

#pragma once

//
// Created by Contarr on 26.04.2025.
//

#pragma once

#include <string>

namespace CrackStatuses {

const std::string JobStatusType[4] {
    "IN_PROGRESS",
    "READY",
    "ERROR",
    "PARTIAL_RESULT"
};

enum StatusCode
{
    kInProgress,
    kReady,
    kError,
    kPartialResult
};

const std::string WorkerStatusType[4] = {
    "DONE",
    "FAILED",
    "WAITING",
    "DID_NOT_DISTRIBUTE"
};

enum WorkersStatus {
    kDone,
    kFailed,
    kWaiting,
    kDidNotDistribute
};

}
