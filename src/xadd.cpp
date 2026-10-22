#include <format>
#include <memory>
#include <iostream>
#include <time.h>

#include "xadd.h"
#include "resp.h"


std::shared_ptr<RespElement> Xadd::run(State& state, int client_fd) {
	auto arr = dynamic_cast<RespArray*>(cmd_info_.get());
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

	pthread_mutex_lock(&state.stream_mtx);
	auto it = state.streams.find(stream_key);
	if (it == state.streams.end()) {
		if (seq == -1) {
			if (millis == 0) {
				seq = 1;
			} else {
				seq = 0;
			}
		}
		if (millis == 0 && seq == 0) {
			pthread_mutex_unlock(&state.stream_mtx);
			return std::make_shared<SimpleError>("ERR The ID specified in XADD must be greater than 0-0");
		} else {
			// Initialize the array first, otherwise the map contains garbage.
			state.streams[stream_key][millis][seq] = std::make_shared<RespArray>();
			auto& elts = state.streams[stream_key][millis][seq]->elts;
			for (int i = 3; i < arr->elts.size(); i += 2) {
				auto pr = std::make_shared<RespArray>(std::vector<std::shared_ptr<RespElement>>{
						arr->elts[i], arr->elts[i + 1]});
				elts.push_back(pr);
			}
			state.stream_last_entry[stream_key] = {millis, seq};
			pthread_mutex_unlock(&state.stream_mtx);
			return std::make_shared<BulkString>(std::format("{}-{}", std::to_string(millis), std::to_string(seq)));
		}
	} else {
		auto [prev_millis, prev_seq] = state.stream_last_entry[stream_key];
		if (seq == -1) {
			if (millis == prev_millis) {
				seq = prev_seq + 1;
			} else {
				seq = 0;
			}
		}
		if ((millis < prev_millis) || ((millis == prev_millis) && (seq <= prev_seq))) {
			pthread_mutex_unlock(&state.stream_mtx);
			return std::make_shared<SimpleError>((millis == 0 && seq == 0) ?
						"ERR The ID specified in XADD must be greater than 0-0" :
						"ERR The ID specified in XADD is equal or smaller than the target stream top item");
		} else {
			state.stream_last_entry[stream_key] = {millis, seq};
			state.streams[stream_key][millis][seq] = std::make_shared<RespArray>();
			auto& elts = state.streams[stream_key][millis][seq]->elts;
			for (int i = 3; i < arr->elts.size(); i += 2) {
				auto pr = std::make_shared<RespArray>(std::vector<std::shared_ptr<RespElement>>{
						arr->elts[i], arr->elts[i + 1]});
				elts.push_back(pr);
			}
			pthread_mutex_unlock(&state.stream_mtx);
			return std::make_shared<BulkString>(std::format("{}-{}", std::to_string(millis), std::to_string(seq)));
		}
	}
}

