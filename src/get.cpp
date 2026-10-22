#include <memory>
#include <iostream>

#include "get.h"
#include "resp.h"

std::shared_ptr<RespElement> Get::run(State& state, int client_fd) {
	auto arr = dynamic_cast<RespArray*>(cmd_info_.get());
	std::string key = dynamic_cast<BulkString*>(arr->elts[1].get())->str;
	pthread_mutex_lock(&state.key_mtx);
	auto it = state.key_val.find(key);
	if (it == state.key_val.end()) {
		pthread_mutex_unlock(&state.key_mtx);
		return std::make_shared<NullBulkString>();
	} else {
		auto response = std::make_shared<BulkString>(it->second);
		pthread_mutex_unlock(&state.key_mtx);
		return response;
	}
}

