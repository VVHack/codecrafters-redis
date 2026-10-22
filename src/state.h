#ifndef state_h_
#define state_h_

#include <memory>
#include <string>
#include <cstring>
#include <map>
#include <netdb.h>
#include <time.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <deque>
#include <pthread.h>

#include "resp.h"

struct State {
	struct ListPopDetails {
		ListPopDetails() : cond(PTHREAD_COND_INITIALIZER) {}
		ListPopDetails(std::string list_key) : list_key(list_key), cond(PTHREAD_COND_INITIALIZER) {}
		std::string list_key;
		pthread_cond_t cond;
		std::deque<int> waiter_client_fds;
		std::unordered_set<int> abandoned_waiters;
	};

	std::unordered_map<std::string, std::string> key_val;
	std::unordered_map<std::string, int> key_to_id;


	std::unordered_map<std::string, std::deque<std::shared_ptr<RespElement>>> lists;
	std::unordered_map<std::string, std::unique_ptr<ListPopDetails>> list_pop_details;

	std::unordered_map<std::string,
		std::map<long long,
		std::map<int, std::shared_ptr<RespArray>>>> streams;
	std::unordered_map<std::string, std::pair<long long, int>> stream_last_entry;

	int latest_key_id = 0;
	pthread_mutex_t key_mtx = PTHREAD_MUTEX_INITIALIZER, list_mtx = PTHREAD_MUTEX_INITIALIZER;
	pthread_mutex_t stream_mtx = PTHREAD_MUTEX_INITIALIZER;

};

#endif
