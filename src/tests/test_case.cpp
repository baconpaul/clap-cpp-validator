#include "test_case.h"

namespace clap_validator {

std::string statusCodeToString(TestStatusCode status) {
    switch (status) {
        case TestStatusCode::Success:
            return "success";
        case TestStatusCode::Crashed:
            return "crashed";
        case TestStatusCode::Failed:
            return "failed";
        case TestStatusCode::Skipped:
            return "skipped";
        case TestStatusCode::Warning:
            return "warning";
        default:
            return "unknown";
    }
}

} // namespace clap_validator
