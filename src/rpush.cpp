#include <memory>
#include <iostream>

#include "rpush.h"
#include "resp.h"


std::shared_ptr<RespElement> Rpush::run(State& state, int client_fd) {
	auto arr = dynamic_cast<RespArray*>(cmd_info_.get());
	std::string list_key = dynamic_cast<BulkString*>(arr->elts[1].get())->str;
	pthread_mutex_lock(&state.list_mtx);
	for (int i = 2; i < arr->elts.size(); ++i) {
		state.lists[list_key].push_back(arr->elts[i]);
	}
	auto it = state.list_pop_details.find(list_key);
	if (it == state.list_pop_details.end()) {
		state.list_pop_details[list_key] = std::make_unique<State::ListPopDetails>(list_key);
	}
	pthread_cond_signal(&state.list_pop_details[list_key]->cond);
	auto response = std::make_shared<Number>(state.lists[list_key].size());
	pthread_mutex_unlock(&state.list_mtx);
	return response;
}

