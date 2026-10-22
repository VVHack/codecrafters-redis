#include <format>
#include <memory>
#include <iostream>
#include <cstdlib>
#include <unistd.h>

#include "xrange.h"
#include "resp.h"


std::shared_ptr<RespElement> Xrange::run(State& state, int client_fd) {
	auto arr = dynamic_cast<RespArray*>(cmd_info_.get());
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
	auto result = std::make_shared<RespArray>();

	pthread_mutex_lock(&state.stream_mtx);
	auto it = state.streams.find(stream_key);
	if (it == state.streams.end()) {
		pthread_mutex_unlock(&state.stream_mtx);
		return std::make_shared<SimpleError>("ERR Invalid stream key");
	} else {
		auto jt = it->second.find(millis_1);
		if (jt == it->second.end()) {
			pthread_mutex_unlock(&state.stream_mtx);
			return std::make_shared<SimpleError>("ERR Invalid starting entry id");
		} else {
			while (jt != it->second.end()) {
				auto kt = jt->second.begin();
				if (jt->first == millis_1) {
					kt = jt->second.find(seq_1);
				}
				if (kt == jt->second.end()) {
					pthread_mutex_unlock(&state.stream_mtx);
					return std::make_shared<SimpleError>("ERR Invalid starting seq number or no entries found");
				} else {
					while (kt != jt->second.end()) {
						auto k_v_pairs = std::make_shared<RespArray>();
						auto cur = std::make_shared<RespArray>(std::vector<std::shared_ptr<RespElement>>{
								std::make_shared<BulkString>(std::format("{}-{}", std::to_string(jt->first),
											std::to_string(kt->first))), k_v_pairs});
						for (auto key_val : kt->second->elts) {
							auto key = dynamic_cast<RespArray*>(key_val.get())->elts[0];
							auto val = dynamic_cast<RespArray*>(key_val.get())->elts[1];
							k_v_pairs->elts.push_back(key);
							k_v_pairs->elts.push_back(val);
						}
						result->elts.push_back(cur);
						if (jt->first == millis_2 && kt->first == seq_2) {
							break;
						}
						++kt;
					}
				}
				if (jt->first == millis_2) {
					break;
				}
				++jt;
			}
		}
	}
	return result;
}

