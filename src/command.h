#ifndef command_h_
#define command_h_

#include <algorithm>
#include <memory>

#include "resp.h"
#include "state.h"


class Command {
public:
	explicit Command(std::shared_ptr<RespElement> cmd_info): cmd_info_(std::move(cmd_info)) {}

	virtual std::shared_ptr<RespElement> run(State& state, int client_fd) = 0;

	virtual ~Command() = default;
	
protected:
	std::shared_ptr<RespElement> cmd_info_;
};

std::unique_ptr<Command> ParseCommand(const char* req, int len);

#endif
