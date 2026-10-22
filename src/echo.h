#include <memory>

#include "command.h"

class Echo : public Command {
public:
	explicit Echo(std::shared_ptr<RespElement> cmd): Command(std::move(cmd)) {}

	virtual std::shared_ptr<RespElement> run(State& state, int client_fd) override;
};

