#include <memory>
#include <iostream>

#include "lpop.h"
#include "resp.h"


std::shared_ptr<RespElement> Lpop::run(State& state, int client_fd) {
	auto arr = dynamic_cast<RespArray*>(cmd_info_.get());
	std::string list_key = dynamic_cast<BulkString*>(arr->elts[1].get())->str;
	int to_remove = 1;
	if (arr->elts.size() == 3) {
		to_remove = std::stoi(dynamic_cast<BulkString*>(arr->elts[2].get())->str);
	}
	pthread_mutex_lock(&state.list_mtx);
	auto it = state.lists.find(list_key);
	if (it != state.lists.end() && !it->second.empty()) {
		if (to_remove == 1) {
			auto elt = it->second.front();
			it->second.pop_front();
			pthread_mutex_unlock(&state.list_mtx);
			return elt;
		} else {
			auto elts = std::make_shared<RespArray>();
			int tot = 0; 
			while (!it->second.empty() && tot < to_remove) {
				auto elt = it->second.front();
				elts->elts.push_back(elt);
				it->second.pop_front();
				tot++;
			}
			pthread_mutex_unlock(&state.list_mtx);
			return elts;
		}
	} else {
		pthread_mutex_unlock(&state.list_mtx);
		return std::make_shared<NullBulkString>();
	}
}

