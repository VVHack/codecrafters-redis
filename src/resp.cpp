#include <format>
#include <iostream>
#include <memory>

#include "resp.h"

bool is_digit(char c) {
	return '0' <= c && c <= '9';
}

std::unique_ptr<RespElement> Parse(const char* buff, int len, int& pos) {
	if (pos >= len) return nullptr;
	std::unique_ptr<RespElement> elt;
	if (buff[pos] == ':') {
		elt = std::make_unique<Number>();
	} else if (buff[pos] == '$') {
		if (pos + 1 < len && buff[pos + 1] == '-') {
			elt = std::make_unique<NullBulkString>();
		} else {
			elt = std::make_unique<BulkString>();
		}
	} else if (buff[pos] == '+') {
		elt = std::make_unique<SimpleString>();
	} else if (buff[pos] == '-') {
		elt = std::make_unique<SimpleError>();
	} else if (buff[pos] == '*') {
		elt = std::make_unique<RespArray>();
	} else {
		return nullptr;
	}
	pos++;
	elt->parse(buff, len, pos);
	return elt;
}

void Number::parse(const char* buff, int len, int& pos) {
	int sign = 1;
	if (buff[pos] == '+') {
		pos++;
	} else if (buff[pos] == '-') {
		sign *= -1;
		pos++;
	}
	while (pos < len && is_digit(buff[pos])) {
		num = 10 * num + (buff[pos] - '0');
		++pos;
	}
	num *= sign;
	if ((pos + 2) <= len && buff[pos] == '\r' && buff[pos + 1] == '\n') {
		pos += 2;
	}
}

int Number::encode(char* buff, int len) const {
	std::string val = std::to_string(num);
	if (len < (3 + val.size())) return -1;
	int pos = 0;
	std::string formatted = std::format(":{}\r\n", val);
	for (char c : formatted) {
		buff[pos++] = c;
	}
	return formatted.size();
}

void BulkString::parse(const char* buff, int len, int& pos) {
	int str_len = 0;
	while (pos < len && is_digit(buff[pos])) {
		str_len = 10 * str_len + (buff[pos] - '0');
		++pos;
	}
	if ((pos + 2) <= len && buff[pos] == '\r' && buff[pos + 1] == '\n') {
		pos += 2;
	}
	str = std::string(buff + pos, str_len);
	pos += str_len;
	if ((pos + 2) <= len && buff[pos] == '\r' && buff[pos + 1] == '\n') {
		pos += 2;
	}
}

int BulkString::encode(char* buff, int len) const {
	if (len < (4 + std::to_string(str.size()).size() + str.size())) return -1;
	int pos = 0;
	std::string formatted = std::format("${}\r\n{}\r\n", (int)str.size(), str);
	for (char c : formatted) {
		buff[pos++] = c;
	}
	return formatted.size();
}

void NullBulkString::parse(const char* buff, int len, int& pos) {
	if (pos + 5 <= len &&
			buff[0] == '$' && buff[1] == '-' && buff[2] == '1' && buff[3] == '\r' && buff[4] == '\n') {
		pos += 5;
	}
}

int NullBulkString::encode(char* buff, int len) const {
	if (len < 5) return -1;
	int pos = 0;
	std::string formatted = "$-1\r\n";
	for (char c : formatted) {
		buff[pos++] = c;
	}
	return formatted.size();
}

void SimpleString::parse(const char* buff, int len, int& pos) {
	int end = pos;
	while (end < len && buff[end] != '\r') {
		++end;
	}
	str = std::string(buff + pos, end - pos);
	if ((pos + 2) <= len && buff[pos] == '\r' && buff[pos + 1] == '\n') {
		pos += 2;
	}
}

int SimpleString::encode(char* buff, int len) const {
	if (len < (3 + str.size())) return -1;
	int pos = 0;
	std::string formatted = std::format("+{}\r\n", str);
	for (char c : formatted) {
		buff[pos++] = c;
	}
	return formatted.size();
}

void SimpleError::parse(const char* buff, int len, int& pos) {
	int end = pos;
	while (end < len && buff[end] != '\r') {
		++end;
	}
	err_msg = std::string(buff + pos, end - pos);
	if ((pos + 2) <= len && buff[pos] == '\r' && buff[pos + 1] == '\n') {
		pos += 2;
	}
}

int SimpleError::encode(char* buff, int len) const {
	if (len < (3 + err_msg.size())) return -1;
	int pos = 0;
	std::string formatted = std::format("-{}\r\n", err_msg);
	for (char c : formatted) {
		buff[pos++] = c;
	}
	return formatted.size();
}

void RespArray::parse(const char* buff, int len, int& pos) {
	int num_elts = 0;
	while (pos < len && is_digit(buff[pos])) {
		num_elts = 10 * num_elts + (buff[pos] - '0');
		++pos;
	}
	if ((pos + 2) <= len && buff[pos] == '\r' && buff[pos + 1] == '\n') {
		pos += 2;
	}
	for (int i = 0; i < num_elts; ++i) {
		if (pos >= len) break;
		auto elt = Parse(buff, len, pos);
		if (elt == nullptr) {
			break;
		}
		elts.push_back(std::move(elt));
	}
}

int RespArray::encode(char* buff, int len) const {
	int pos = 0;
	std::string formatted = std::format("*{}\r\n", (int)elts.size());
	if (formatted.size() > len) return -1;
	for (char c : formatted) {
		buff[pos++] = c;
	}
	for (const auto& elt : elts) {
		int sz = elt->encode(buff + pos, len);
		if (sz == -1) return -1;
		pos += sz;
	}
	return pos;
}

