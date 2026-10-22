#include <memory>
#include <iostream>

#include "lrange.h"
#include "resp.h"


std::shared_ptr<RespElement> Lrange::run(State& state, int client_fd) {
	auto arr = dynamic_cast<RespArray*>(cmd_info_.get());
	std::string list_key = dynamic_cast<BulkString*>(arr->elts[1].get())->str;
	int start = std::stoi(dynamic_cast<BulkString*>(arr->elts[2].get())->str);
	int stop = std::stoi(dynamic_cast<BulkString*>(arr->elts[3].get())->str);
	auto lrange = std::make_shared<RespArray>();
	pthread_mutex_lock(&state.list_mtx);
	auto it = state.lists.find(list_key);
	if (it != state.lists.end()) {
		int n = it->second.size();
		if (start < 0) start += n;
		if (start < 0) start = 0;
		if (stop < 0) stop += n;
		if (stop < 0) stop = 0;
		for (int i = start; i <= std::min(stop, n - 1); ++i) {
			lrange->elts.push_back(it->second[i]);
		}
	}
	pthread_mutex_unlock(&state.list_mtx);
}

