#include <iostream>
#include <cstdlib>
#include <chrono>
#include <memory>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <map>
#include <netdb.h>
#include <time.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <deque>
#include <pthread.h>

#include "command.h"
#include "resp.h"

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

struct ClientDetails {
	int32_t client_fd = -1;
	int32_t server_fd = -1;
	std::vector<int> client_list;
};

struct KeyDetails {
	KeyDetails(const std::string& key, int key_id, long long expiry_millis) :
		key_id(key_id), key(key), expiry_millis(expiry_millis) {}
  // This id is included in order to avoid deleting a later key with the same name.
	int key_id;
	std::string key;
	long long expiry_millis;
};

void* key_deleter(void* key_details) {
	KeyDetails* kd = (KeyDetails*)key_details;
	int seconds = kd->expiry_millis / 1000;
	long long millis = kd->expiry_millis % 1000;
	struct timespec remaining, request = {seconds, millis * 1000000LL};
	if (nanosleep(&request, &remaining) == 0) {
		pthread_mutex_lock(&key_mtx);
		int cur_key_id = key_to_id[kd->key];
		if (cur_key_id == kd->key_id) {
			std::cout << "Deleting key " << kd->key << std::endl;
			key_val.erase(kd->key);
			key_to_id.erase(kd->key);
		}
		pthread_mutex_unlock(&key_mtx);
	}
	delete kd;
	return nullptr;
}

void* list_popper(void* list_details) {
	pthread_mutex_lock(&list_mtx);
	ListPopDetails* ld = (ListPopDetails*)list_details;
	while (lists[ld->list_key].empty()) {
		if (list_pop_details[ld->list_key]->abandoned_waiters.count(ld->waiter_client_fds.front())) {
			send(ld->waiter_client_fds.front(), "*-1\r\n", 5, 0);
			ld->waiter_client_fds.pop_front();
		}
		pthread_cond_wait(&ld->cond, &list_mtx);
	}
	if (!ld->waiter_client_fds.empty()) {
		int fd = ld->waiter_client_fds.front();
		ld->waiter_client_fds.pop_front();
		pthread_mutex_unlock(&list_mtx);
		char response[1024] = {0};
		RespArray result;
		auto list_name = std::make_shared<BulkString>(ld->list_key);
		auto elt = lists[ld->list_key].front();
		lists[ld->list_key].pop_front();
		result.elts.push_back(list_name);
		result.elts.push_back(elt);
		int num_bytes = result.encode(response, 1024);
		send(fd, response, num_bytes, 0);
	}
	return nullptr;
}

struct ListPopTimeoutDetails {
	ListPopTimeoutDetails(int waiter_id, const std::string& list_key, long long expiry_millis) :
		waiter_id(waiter_id),
		list_key(list_key), expiry_millis(expiry_millis) {}
	int waiter_id;
	std::string list_key;
	long long expiry_millis;
};

void* list_pop_timeout(void* timeout_details) {
	ListPopTimeoutDetails* td = (ListPopTimeoutDetails*)timeout_details;
	int seconds = td->expiry_millis / 1000;
	long long millis = td->expiry_millis % 1000;
	struct timespec remaining, request = {seconds, millis * 1000000LL};
	if (nanosleep(&request, &remaining) == 0) {
		pthread_mutex_lock(&list_mtx);
		list_pop_details[td->list_key]->abandoned_waiters.insert(td->waiter_id);
		std::cout << "Signaling due to waiter " << td->waiter_id << " for list " << td->list_key << std::endl;
		pthread_cond_signal(&list_pop_details[td->list_key]->cond);
		pthread_mutex_unlock(&list_mtx);
	}
	delete td;
	return nullptr;
};

int main(int argc, char **argv) {
	// Flush after every std::cout / std::cerr
	std::cout << std::unitbuf;
	std::cerr << std::unitbuf;

	ClientDetails client;

	client.server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (client.server_fd < 0) {
		std::cerr << "Failed to create server socket\n";
		return 1;
	}

	// Since the tester restarts your program quite often, setting SO_REUSEADDR
	// ensures that we don't run into 'Address already in use' errors
	int reuse = 1;
	if (setsockopt(client.server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
		std::cerr << "setsockopt failed\n";
		return 1;
	}

	struct sockaddr_in server_addr;
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = INADDR_ANY;
	server_addr.sin_port = htons(6379);

	if (bind(client.server_fd, (struct sockaddr *) &server_addr, sizeof(server_addr)) != 0) {
		std::cerr << "Failed to bind to port 6379\n";
		return 1;
	}

	int connection_backlog = 5;
	if (listen(client.server_fd, connection_backlog) != 0) {
		std::cerr << "listen failed\n";
		return 1;
	}

	fd_set readfds;
	struct sockaddr_in client_addr;
	int max_fd;
	int activity;
	int client_addr_len = sizeof(client_addr);
	std::cout << "Waiting for a client to connect...\n";

	while (true) {
		FD_ZERO(&readfds);
		FD_SET(client.server_fd, &readfds);
		max_fd = client.server_fd;
		for (int sd : client.client_list) {
			FD_SET(sd, &readfds);
			if (sd > max_fd) {
				max_fd = sd;
			}
		}
		activity = select(max_fd + 1, &readfds, nullptr, nullptr, nullptr);
		if (activity < 0) {
			std::cerr << "select error" << std::endl;
			continue;
		}
		if (FD_ISSET(client.server_fd, &readfds)) {
			int client_fd = accept(client.server_fd, (struct sockaddr *) &client_addr, (socklen_t *) &client_addr_len);
			std::cout << "New client connected with fd " << client_fd << std::endl;
			client.client_list.push_back(client_fd);
		}
		char buffer[1024] = {0};
		ssize_t valread;
		for (int i = 0; i < client.client_list.size(); ++i) {
			if (FD_ISSET(client.client_list[i], &readfds)) {
				valread = recv(client.client_list[i], buffer, 1024 - 1, 0);
				if (valread == -1 || valread == 0) break;
				auto [cmd, resp] = ParseCommand(buffer, 1024);
				char response[1024] = {0};
				switch (cmd) {
					case Command::PING: {
						send(client.client_list[i], "+PONG\r\n", 7, 0);
						break;
					}
					case Command::ECHO: {
						auto arr = dynamic_cast<RespArray*>(resp.get());
						int num_bytes =
							dynamic_cast<BulkString*>(arr->elts[1].get())->encode(response, 1024);
						if (num_bytes == -1) {
							send(client.client_list[i], "Internal error", strlen("Internal error"), 0);
						} else {
							std::cout << "Sending back " << response << std::endl;
							send(client.client_list[i], response, num_bytes, 0);
						}
						break;
					}
					case Command::SET: {
						auto arr = dynamic_cast<RespArray*>(resp.get());
						std::string key = dynamic_cast<BulkString*>(arr->elts[1].get())->str;
						std::string val = dynamic_cast<BulkString*>(arr->elts[2].get())->str;
						pthread_mutex_lock(&key_mtx);
						key_val[key] = val;
						key_to_id[key] = ++latest_key_id;
						if (arr->elts.size() == 5) {
							std::string time_unit = dynamic_cast<BulkString*>(arr->elts[3].get())->str;
							long long expiry = std::stoll(dynamic_cast<BulkString*>(arr->elts[4].get())->str);
							if (time_unit == "EX") {
								expiry *= 1000;
							}
							pthread_t thread;
							pthread_create(&thread, nullptr, key_deleter, new KeyDetails(key, latest_key_id, expiry));
							pthread_detach(thread);
						}
						pthread_mutex_unlock(&key_mtx);
						send(client.client_list[i], "+OK\r\n", 5, 0);
			  	  break;
					} 
					case Command::GET: {
						auto arr = dynamic_cast<RespArray*>(resp.get());
						std::string key = dynamic_cast<BulkString*>(arr->elts[1].get())->str;
						pthread_mutex_lock(&key_mtx);
						auto it = key_val.find(key);
						if (it == key_val.end()) {
							pthread_mutex_unlock(&key_mtx);
							send(client.client_list[i], "$-1\r\n", 5, 0);
						} else {
							int num_bytes = BulkString(it->second).encode(response, 1024);
							pthread_mutex_unlock(&key_mtx);
							if (num_bytes == -1) {
								send(client.client_list[i], "Internal error", strlen("Internal error"), 0);
							} else {
								send(client.client_list[i], response, num_bytes, 0);
							}
						}
			  	  break;
					} 
					case Command::RPUSH: {
						auto arr = dynamic_cast<RespArray*>(resp.get());
						std::string list_key = dynamic_cast<BulkString*>(arr->elts[1].get())->str;
						pthread_mutex_lock(&list_mtx);
						for (int i = 2; i < arr->elts.size(); ++i) {
							lists[list_key].push_back(arr->elts[i]);
						}
						auto it = list_pop_details.find(list_key);
						if (it == list_pop_details.end()) {
							list_pop_details[list_key] = std::make_unique<ListPopDetails>(list_key);
						}
						pthread_cond_signal(&list_pop_details[list_key]->cond);
						int num_bytes = Number(lists[list_key].size()).encode(response, 1024);
						pthread_mutex_unlock(&list_mtx);
						if (num_bytes == -1) {
							send(client.client_list[i], "Internal error", strlen("Internal error"), 0);
						} else {
							send(client.client_list[i], response, num_bytes, 0);
						}
						break;
					}
					case Command::LPUSH: {
						auto arr = dynamic_cast<RespArray*>(resp.get());
						std::string list_key = dynamic_cast<BulkString*>(arr->elts[1].get())->str;
						pthread_mutex_lock(&list_mtx);
						for (int i = 2; i < arr->elts.size(); ++i) {
							lists[list_key].push_front(arr->elts[i]);
						}
						auto it = list_pop_details.find(list_key);
						if (it == list_pop_details.end()) {
							list_pop_details[list_key] = std::make_unique<ListPopDetails>(list_key);
						}
						pthread_cond_signal(&list_pop_details[list_key]->cond);
						int num_bytes = Number(lists[list_key].size()).encode(response, 1024);
						pthread_mutex_unlock(&list_mtx);
						if (num_bytes == -1) {
							send(client.client_list[i], "Internal error", strlen("Internal error"), 0);
						} else {
							send(client.client_list[i], response, num_bytes, 0);
						}
						break;
					}
					case Command::LPOP: {
						auto arr = dynamic_cast<RespArray*>(resp.get());
						std::string list_key = dynamic_cast<BulkString*>(arr->elts[1].get())->str;
						int to_remove = 1;
						if (arr->elts.size() == 3) {
							to_remove = std::stoi(dynamic_cast<BulkString*>(arr->elts[2].get())->str);
						}
						pthread_mutex_lock(&list_mtx);
						auto it = lists.find(list_key);
						if (it != lists.end() && !it->second.empty()) {
							if (to_remove == 1) {
								pthread_mutex_unlock(&list_mtx);
								auto elt = it->second.front();
								it->second.pop_front();
								int num_bytes = elt->encode(response, 1024);
								send(client.client_list[i], response, num_bytes, 0);
							} else {
								RespArray elts;
								int tot = 0; 
								while (!it->second.empty() && tot < to_remove) {
									auto elt = it->second.front();
									elts.elts.push_back(elt);
									it->second.pop_front();
									tot++;
								}
								pthread_mutex_unlock(&list_mtx);
								int num_bytes = elts.encode(response, 1024);
								send(client.client_list[i], response, num_bytes, 0);
							}
						} else {
							pthread_mutex_unlock(&list_mtx);
							send(client.client_list[i], "$-1\r\n", 5, 0);
						}
						break;
					}
					case Command::BLPOP: {
						auto arr = dynamic_cast<RespArray*>(resp.get());
						std::string list_key = dynamic_cast<BulkString*>(arr->elts[1].get())->str;
						double expiry = std::stod(dynamic_cast<BulkString*>(arr->elts[2].get())->str);
						pthread_mutex_lock(&list_mtx);
						auto it = list_pop_details.find(list_key);
						if (it == list_pop_details.end()) {
							list_pop_details[list_key] = std::make_unique<ListPopDetails>(list_key);
						}
						list_pop_details[list_key]->waiter_client_fds.push_back(client.client_list[i]);
						pthread_t thread;
						pthread_create(&thread, nullptr, list_popper, list_pop_details[list_key].get());
						pthread_detach(thread);
						pthread_mutex_unlock(&list_mtx);
						if (expiry > 0) {
							pthread_t timeout_thread;
							pthread_create(&timeout_thread, nullptr, list_pop_timeout,
									new ListPopTimeoutDetails(client.client_list[i], list_key, (long long)(1000 * expiry)));
							pthread_detach(timeout_thread);
						}
						break;
					}
					case Command::LRANGE: {
						auto arr = dynamic_cast<RespArray*>(resp.get());
						std::string list_key = dynamic_cast<BulkString*>(arr->elts[1].get())->str;
						int start = std::stoi(dynamic_cast<BulkString*>(arr->elts[2].get())->str);
						int stop = std::stoi(dynamic_cast<BulkString*>(arr->elts[3].get())->str);
						RespArray lrange;
						pthread_mutex_lock(&list_mtx);
						auto it = lists.find(list_key);
						if (it != lists.end()) {
							int n = it->second.size();
							if (start < 0) start += n;
							if (start < 0) start = 0;
							if (stop < 0) stop += n;
							if (stop < 0) stop = 0;
							for (int i = start; i <= std::min(stop, n - 1); ++i) {
								lrange.elts.push_back(it->second[i]);
							}
						}
						int num_bytes = lrange.encode(response, 1024);
						pthread_mutex_unlock(&list_mtx);
						if (num_bytes == -1) {
							send(client.client_list[i], "Internal error", strlen("Internal error"), 0);
						} else {
							send(client.client_list[i], response, num_bytes, 0);
						}
						break;
					}
					case Command::LLEN: {
						auto arr = dynamic_cast<RespArray*>(resp.get());
						std::string list_key = dynamic_cast<BulkString*>(arr->elts[1].get())->str;
						pthread_mutex_lock(&list_mtx);
						auto it = lists.find(list_key);
						int n = 0;
						if (it != lists.end()) {
							n = it->second.size();
						}
						int num_bytes = Number(n).encode(response, 1024);
						pthread_mutex_unlock(&list_mtx);
						if (num_bytes == -1) {
							send(client.client_list[i], "Internal error", strlen("Internal error"), 0);
						} else {
							send(client.client_list[i], response, num_bytes, 0);
						}
						break;
					}
					case Command::TYPE: {
						auto arr = dynamic_cast<RespArray*>(resp.get());
						std::string key = dynamic_cast<BulkString*>(arr->elts[1].get())->str;
						pthread_mutex_lock(&key_mtx);
						auto it = key_val.find(key);
						if (it == key_val.end()) {
							pthread_mutex_unlock(&key_mtx);
							pthread_mutex_lock(&stream_mtx);
							auto jt = streams.find(key);
							if (jt == streams.end()) {
								pthread_mutex_unlock(&stream_mtx);
								int num_bytes = SimpleString("none").encode(response, 1024);
								if (num_bytes == -1) {
									send(client.client_list[i], "Internal error", strlen("Internal error"), 0);
								} else {
									send(client.client_list[i], response, num_bytes, 0);
								}
							} else {
								pthread_mutex_unlock(&stream_mtx);
								int num_bytes = SimpleString("stream").encode(response, 1024);
								if (num_bytes == -1) {
									send(client.client_list[i], "Internal error", strlen("Internal error"), 0);
								} else {
									send(client.client_list[i], response, num_bytes, 0);
								}
							}
						} else {
							pthread_mutex_unlock(&key_mtx);
							int num_bytes = SimpleString("string").encode(response, 1024);
							if (num_bytes == -1) {
								send(client.client_list[i], "Internal error", strlen("Internal error"), 0);
							} else {
								send(client.client_list[i], response, num_bytes, 0);
							}
						}
						break;
					}
					case Command::XADD: {
						auto arr = dynamic_cast<RespArray*>(resp.get());
						std::string stream_key = dynamic_cast<BulkString*>(arr->elts[1].get())->str;
						std::string entry_id = dynamic_cast<BulkString*>(arr->elts[2].get())->str;

						long long millis; int seq;
						int dash_idx = 0;
						if (entry_id == "*") {
							const auto p1 = std::chrono::system_clock::now();
							millis = std::chrono::duration_cast<std::chrono::milliseconds>(
                   p1.time_since_epoch()).count();
							seq = 0;
						} else {
							while (dash_idx < entry_id.size() && entry_id[dash_idx] != '-') dash_idx++;
							millis = std::stoll(entry_id.substr(0, dash_idx));
							std::string seq_str  = entry_id.substr(dash_idx + 1, entry_id.size() - dash_idx - 1);
							seq = (seq_str == "*") ? -1 : std::stoi(seq_str);
						}

						pthread_mutex_lock(&stream_mtx);
						auto it = streams.find(stream_key);
						if (it == streams.end()) {
							if (seq == -1) {
								if (millis == 0) {
									seq = 1;
								} else {
									seq = 0;
								}
							}
							if (millis == 0 && seq == 0) {
								pthread_mutex_unlock(&stream_mtx);
								int num_bytes = SimpleError("ERR The ID specified in XADD must be greater than 0-0")
									.encode(response, 1024);
								if (num_bytes == -1) {
									send(client.client_list[i], "Internal error", strlen("Internal error"), 0);
								} else {
									send(client.client_list[i], response, num_bytes, 0);
								}
							} else {
								// Initialize the array first, otherwise the map contains garbage.
								streams[stream_key][millis][seq] = std::make_shared<RespArray>();
								auto& elts = streams[stream_key][millis][seq]->elts;
								for (int i = 3; i < arr->elts.size(); i += 2) {
									auto pr = std::make_shared<RespArray>(std::vector<std::shared_ptr<RespElement>>{
											arr->elts[i], arr->elts[i + 1]});
									elts.push_back(pr);
								}
								stream_last_entry[stream_key] = {millis, seq};
								pthread_mutex_unlock(&stream_mtx);
								int num_bytes =
								BulkString(std::format("{}-{}", std::to_string(millis), std::to_string(seq))).encode(response, 1024);
								if (num_bytes == -1) {
									send(client.client_list[i], "Internal error", strlen("Internal error"), 0);
								} else {
									send(client.client_list[i], response, num_bytes, 0);
								}
							}
						} else {
							auto [prev_millis, prev_seq] = stream_last_entry[stream_key];
							if (seq == -1) {
								if (millis == prev_millis) {
									seq = prev_seq + 1;
								} else {
									seq = 0;
								}
							}
							if ((millis < prev_millis) || ((millis == prev_millis) && (seq <= prev_seq))) {
								pthread_mutex_unlock(&stream_mtx);
								int num_bytes =
									SimpleError((millis == 0 && seq == 0) ?
											"ERR The ID specified in XADD must be greater than 0-0" :
											"ERR The ID specified in XADD is equal or smaller than the target stream top item")
									.encode(response, 1024);
								if (num_bytes == -1) {
									send(client.client_list[i], "Internal error", strlen("Internal error"), 0);
								} else {
									send(client.client_list[i], response, num_bytes, 0);
								}
							} else {
								stream_last_entry[stream_key] = {millis, seq};
								streams[stream_key][millis][seq] = std::make_shared<RespArray>();
								pthread_mutex_unlock(&stream_mtx);
								int num_bytes =
								BulkString(std::format("{}-{}", std::to_string(millis), std::to_string(seq))).encode(response, 1024);
								if (num_bytes == -1) {
									send(client.client_list[i], "Internal error", strlen("Internal error"), 0);
								} else {
									send(client.client_list[i], response, num_bytes, 0);
								}
							}
						}
						break;
					}
					case Command::XRANGE: {
						auto arr = dynamic_cast<RespArray*>(resp.get());
						std::string stream_key = dynamic_cast<BulkString*>(arr->elts[1].get())->str;
						std::string entry_id_1 = dynamic_cast<BulkString*>(arr->elts[2].get())->str;
						std::string entry_id_2 = dynamic_cast<BulkString*>(arr->elts[3].get())->str;

						auto parse_entry_id = [] (const std::string& entry_id) {
							int dash_idx = 0;
							while (dash_idx < entry_id.size() && entry_id[dash_idx] != '-') dash_idx++;
							if (dash_idx == entry_id.size()) return std::pair<long long, int>(std::stoll(entry_id), -1);
							long long millis = std::stoll(entry_id.substr(0, dash_idx));
							std::string seq_str  = entry_id.substr(dash_idx + 1, entry_id.size() - dash_idx - 1);
							int seq = std::stoi(seq_str);
							return std::pair<long long, int>(millis, seq);
						};
						auto [millis_1, seq_1] = parse_entry_id(entry_id_1);
						if (seq_1 == -1) seq_1 = 0;
						auto [millis_2, seq_2] = parse_entry_id(entry_id_2);
						RespArray result;
						std::cout << "millis_1 = " << millis_1 << ", seq_1 = " << seq_1 << std::endl;
						std::cout << "millis_2 = " << millis_2 << ", seq_2 = " << seq_2 << std::endl;

						pthread_mutex_lock(&stream_mtx);
						auto it = streams.find(stream_key);
						if (it == streams.end()) {
							int num_bytes = SimpleError("ERR Invalid stream key")
								.encode(response, 1024);
							if (num_bytes == -1) {
								send(client.client_list[i], "Internal error", strlen("Internal error"), 0);
							} else {
								send(client.client_list[i], response, num_bytes, 0);
							}
						} else {
							std::cout << "GETS HERE 1" << std::endl;
							auto jt = it->second.find(millis_1);
							if (jt == it->second.end()) {
								int num_bytes = SimpleError("ERR Invalid starting entry id")
									.encode(response, 1024);
								if (num_bytes == -1) {
									send(client.client_list[i], "Internal error", strlen("Internal error"), 0);
								} else {
									send(client.client_list[i], response, num_bytes, 0);
								}
							} else {
								std::cout << "GETS HERE 2" << std::endl;
								auto kt = jt->second.find(seq_1);
								if (kt == jt->second.end()) {
									int num_bytes = SimpleError("ERR Invalid starting seq number")
										.encode(response, 1024);
									if (num_bytes == -1) {
										send(client.client_list[i], "Internal error", strlen("Internal error"), 0);
									} else {
										send(client.client_list[i], response, num_bytes, 0);
									}
								} else {
									std::cout << "GETS HERE 3" << std::endl;
									while (kt != jt->second.end()) {
										result.elts.push_back(kt->second);
										++kt;
									}
								}
							}
						}
						int num_bytes = result.encode(response, 1024);
						if (num_bytes == -1) {
							send(client.client_list[i], "Internal error", strlen("Internal error"), 0);
						} else {
							send(client.client_list[i], response, num_bytes, 0);
						}
						break;
					}
					case Command::UNKNOWN:
					default: {
					  std::string err_msg = "Invalid command or command not supported yet";
					  int num_bytes = BulkString(err_msg).encode(response, 1024);
					  send(client.client_list[i], response, num_bytes, 0);
					}
				}
			}
		}
	}

	close(client.server_fd);

	return 0;
}
