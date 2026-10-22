#include <memory>

#include "echo.h"
#include "resp.h"

std::shared_ptr<RespElement> Echo::run(State& state, int client_fd) {
	auto arr = dynamic_cast<RespArray*>(cmd_info_.get());
	return arr->elts[1];
}

