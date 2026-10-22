#include <memory>

#include "command.h"

class Xadd : public Command {
public:
	explicit Xadd(std::shared_ptr<RespElement> cmd_info): Command(cmd_info) {}

	virtual std::shared_ptr<RespElement> run(State& state, int client_fd) override;
};
