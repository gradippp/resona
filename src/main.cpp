#include "crow.h"
#include "version.h"
#include "routes/routes.h"
#include "utils/logger.h"
#include <iostream>

int main() {
    // Set custom logger
    static utils::CustomLogger logger;
    crow::logger::setHandler(&logger);

    crow::SimpleApp app;

    // Setup routes
    routes::setup(app);

    std::cout << "Cloud Download Service v" << CLOUD_DL_VERSION << " listening on port 8080..." << std::endl;
    app.port(8080).multithreaded().run();
}
