#include "routes.h"
#include "system.h"
#include "batch.h"

namespace routes {

void setup(crow::SimpleApp& app) {
    system::setup(app);
    batch::setup(app);
}

} // namespace routes
