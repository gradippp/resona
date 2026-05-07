#include "system.h"
#include "../utils/response.h"
#include "version.h"

namespace routes {
namespace system {

void setup(crow::SimpleApp& app) {
    // GET /v1/version - Get service version
    CROW_ROUTE(app, "/v1/version")([]() {
        nlohmann::json response;
        response["version"] = CLOUD_DL_VERSION;
        response["description"] = "Cloud Download Service";
        return utils::json_response(response);
    });
}

} // namespace system
} // namespace routes
