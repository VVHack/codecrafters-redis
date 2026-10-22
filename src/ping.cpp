#include <memory>

#include "ping.h"
#include "resp.h"

std::shared_ptr<RespElement> Ping::run(State& state, int client_fd) {
	return std::make_shared<SimpleString>("PONG");
}

