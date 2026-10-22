#include <memory>

#include "command.h"

class Type : public Command {
public:
	explicit Type(std::shared_ptr<RespElement> cmd_info): Command(cmd_info) {}

	virtual std::shared_ptr<RespElement> run(State& state, int client_fd) override;
};
