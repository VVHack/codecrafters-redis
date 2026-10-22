#include <memory>
#include <iostream>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <time.h>

#include "blpop.h"
#include "resp.h"

struct ListPopperDetails {
	ListPopperDetails(State* state, State::ListPopDetails* list_pop_details) :
		state(state), list_pop_details(list_pop_details) {}
	State* state;
	State::ListPopDetails* list_pop_details;
};

void* list_popper(void* list_popper_details) {
	ListPopperDetails* lpd = (ListPopperDetails*)list_popper_details;
	State* state = lpd->state;
	pthread_mutex_lock(&state->list_mtx);
	State::ListPopDetails* ld = lpd->list_pop_details;
	while (state->lists[ld->list_key].empty()) {
		if (state->list_pop_details[ld->list_key]->abandoned_waiters.count(ld->waiter_client_fds.front())) {
			send(ld->waiter_client_fds.front(), "*-1\r\n", 5, 0);
			ld->waiter_client_fds.pop_front();
		}
		pthread_cond_wait(&ld->cond, &state->list_mtx);
	}
	if (!ld->waiter_client_fds.empty()) {
		int fd = ld->waiter_client_fds.front();
		ld->waiter_client_fds.pop_front();
		pthread_mutex_unlock(&state->list_mtx);
		char response[1024] = {0};
		RespArray result;
		auto list_name = std::make_shared<BulkString>(ld->list_key);
		auto elt = state->lists[ld->list_key].front();
		state->lists[ld->list_key].pop_front();
		result.elts.push_back(list_name);
		result.elts.push_back(elt);
		int num_bytes = result.encode(response, 1024);
		send(fd, response, num_bytes, 0);
	}
	delete lpd;
	return nullptr;
}

struct ListPopTimeoutDetails {
	ListPopTimeoutDetails(int waiter_id, const std::string& list_key, long long expiry_millis, State* state) :
		waiter_id(waiter_id),
		list_key(list_key), expiry_millis(expiry_millis), state(state) {}
	int waiter_id;
	std::string list_key;
	long long expiry_millis;
	State* state;
};

void* list_pop_timeout(void* timeout_details) {
	ListPopTimeoutDetails* td = (ListPopTimeoutDetails*)timeout_details;
	int seconds = td->expiry_millis / 1000;
	long long millis = td->expiry_millis % 1000;
	struct timespec remaining, request = {seconds, millis * 1000000LL};
	if (nanosleep(&request, &remaining) == 0) {
		pthread_mutex_lock(&td->state->list_mtx);
		td->state->list_pop_details[td->list_key]->abandoned_waiters.insert(td->waiter_id);
		std::cout << "Signaling due to waiter " << td->waiter_id << " for list " << td->list_key << std::endl;
		pthread_cond_signal(&td->state->list_pop_details[td->list_key]->cond);
		pthread_mutex_unlock(&td->state->list_mtx);
	}
	delete td;
	return nullptr;
};

std::shared_ptr<RespElement> Blpop::run(State& state, int client_fd) {
	auto arr = dynamic_cast<RespArray*>(cmd_info_.get());
	std::string list_key = dynamic_cast<BulkString*>(arr->elts[1].get())->str;
	double expiry = std::stod(dynamic_cast<BulkString*>(arr->elts[2].get())->str);
	pthread_mutex_lock(&state.list_mtx);
	auto it = state.list_pop_details.find(list_key);
	if (it == state.list_pop_details.end()) {
		state.list_pop_details[list_key] = std::make_unique<State::ListPopDetails>(list_key);
	}
	state.list_pop_details[list_key]->waiter_client_fds.push_back(client_fd);
	pthread_t thread;
	pthread_create(&thread, nullptr, list_popper,
			new ListPopperDetails(&state, state.list_pop_details[list_key].get()));
	pthread_detach(thread);
	pthread_mutex_unlock(&state.list_mtx);
	if (expiry > 0) {
		pthread_t timeout_thread;
		pthread_create(&timeout_thread, nullptr, list_pop_timeout,
				new ListPopTimeoutDetails(client_fd, list_key, (long long)(1000 * expiry), &state));
		pthread_detach(timeout_thread);
	}
}

