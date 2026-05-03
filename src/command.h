#ifndef command_h_
#define command_h_

#include <algorithm>
#include <memory>

#include "resp.h"

enum class Command {
	PING,
	ECHO,
	SET,
	GET,
	RPUSH,
	LPUSH,
	LPOP,
	BLPOP,
	LRANGE,
	LLEN,
	TYPE,
	XADD,
	XRANGE,
	UNKNOWN,
};

std::pair<Command, std::unique_ptr<RespElement>> ParseCommand(const char* req, int len);

#endif
