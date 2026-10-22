#include <memory>

#include "echo.h"
#include "resp.h"

std::shared_ptr<RespElement> Echo::run(State& state, int client_fd) {
	return cmd_info_;
}

