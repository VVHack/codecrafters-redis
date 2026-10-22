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
#include "state.h"

State state;

struct ClientDetails {
	int32_t client_fd = -1;
	int32_t server_fd = -1;
	std::vector<int> client_list;
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
				auto cmd = ParseCommand(buffer, 1024);
				if (cmd == nullptr) {
					send(client.client_list[i], "Internal error", strlen("Internal error"), 0);
				}
				auto result = cmd->run(state, client.client_list[i]);
				if (result != nullptr) {
					char response[1024] = {0};
					int num_bytes = result->encode(response, 1024);
					if (num_bytes == -1) {
						send(client.client_list[i], "Internal error", strlen("Internal error"), 0);
					} else {
						std::cout << "Sending back " << response << std::endl;
						send(client.client_list[i], response, num_bytes, 0);
					}
				}
			}
		}
	}

	close(client.server_fd);

	return 0;
}
