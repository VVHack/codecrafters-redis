#include <memory>
#include <iostream>
#include <time.h>

#include "set.h"
#include "resp.h"

struct KeyDetails {
	KeyDetails(const std::string& key, int key_id, long long expiry_millis, State* state) :
		key_id(key_id), key(key), expiry_millis(expiry_millis), state(state) {}
  // This id is included in order to avoid deleting a later key with the same name.
	int key_id;
	std::string key;
	long long expiry_millis;
	State* state;
};

void* key_deleter(void* key_details) {
	KeyDetails* kd = (KeyDetails*)key_details;
	int seconds = kd->expiry_millis / 1000;
	long long millis = kd->expiry_millis % 1000;
	struct timespec remaining, request = {seconds, millis * 1000000LL};
	if (nanosleep(&request, &remaining) == 0) {
		pthread_mutex_lock(&kd->state->key_mtx);
		int cur_key_id = kd->state->key_to_id[kd->key];
		if (cur_key_id == kd->key_id) {
			std::cout << "Deleting key " << kd->key << std::endl;
			kd->state->key_val.erase(kd->key);
			kd->state->key_to_id.erase(kd->key);
		}
		pthread_mutex_unlock(&kd->state->key_mtx);
	}
	delete kd;
	return nullptr;
}

std::shared_ptr<RespElement> Set::run(State& state, int client_fd) {
	auto arr = dynamic_cast<RespArray*>(cmd_info_.get());
	std::string key = dynamic_cast<BulkString*>(arr->elts[1].get())->str;
	std::string val = dynamic_cast<BulkString*>(arr->elts[2].get())->str;
	pthread_mutex_lock(&state.key_mtx);
	state.key_val[key] = val;
	state.key_to_id[key] = ++state.latest_key_id;
	if (arr->elts.size() == 5) {
		std::string time_unit = dynamic_cast<BulkString*>(arr->elts[3].get())->str;
		long long expiry = std::stoll(dynamic_cast<BulkString*>(arr->elts[4].get())->str);
		if (time_unit == "EX") {
			expiry *= 1000;
		}
		pthread_t thread;
		pthread_create(&thread, nullptr, key_deleter,
				new KeyDetails(key, state.latest_key_id, expiry, &state));
		pthread_detach(thread);
	}
	pthread_mutex_unlock(&state.key_mtx);
	return std::make_shared<SimpleString>("OK");
}

