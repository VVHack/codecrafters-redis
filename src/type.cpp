#include <memory>
#include <iostream>

#include "type.h"
#include "resp.h"


std::shared_ptr<RespElement> Type::run(State& state, int client_fd) {
	auto arr = dynamic_cast<RespArray*>(cmd_info_.get());
	std::string key = dynamic_cast<BulkString*>(arr->elts[1].get())->str;
	pthread_mutex_lock(&state.key_mtx);
	auto it = state.key_val.find(key);
	if (it == state.key_val.end()) {
		pthread_mutex_unlock(&state.key_mtx);
		pthread_mutex_lock(&state.stream_mtx);
		auto jt = state.streams.find(key);
		if (jt == state.streams.end()) {
			pthread_mutex_unlock(&state.stream_mtx);
			return std::make_shared<SimpleString>("none");
		} else {
			pthread_mutex_unlock(&state.stream_mtx);
			return std::make_shared<SimpleString>("stream");
		}
	} else {
		pthread_mutex_unlock(&state.key_mtx);
		return std::make_shared<SimpleString>("string");
	}
}

