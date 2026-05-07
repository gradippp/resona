#include "crow.h"
#include "version.h"
#include "routes/routes.h"
#include <iostream>

int main() {
    crow::SimpleApp app;

    // Setup routes
    routes::setup(app);

    std::cout << "Cloud Download Service v" << CLOUD_DL_VERSION << " listening on port 8080..." << std::endl;
    app.port(8080).multithreaded().run();
}
