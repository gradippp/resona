#include "routes.h"
#include "system.h"
#include "batch.h"

namespace routes {

void setup(crow::SimpleApp& app) {
    batch::setup(app);
    system::setup(app);
}

} // namespace routes
