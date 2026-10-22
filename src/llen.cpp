#include <memory>
#include <iostream>

#include "llen.h"
#include "resp.h"


std::shared_ptr<RespElement> Llen::run(State& state, int client_fd) {
	auto arr = dynamic_cast<RespArray*>(cmd_info_.get());
	std::string list_key = dynamic_cast<BulkString*>(arr->elts[1].get())->str;
	pthread_mutex_lock(&state.list_mtx);
	auto it = state.lists.find(list_key);
	int n = 0;
	if (it != state.lists.end()) {
		n = it->second.size();
	}
	pthread_mutex_unlock(&state.list_mtx);
	return std::make_shared<Number>(n);
}

