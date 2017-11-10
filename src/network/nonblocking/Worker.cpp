#include "Worker.h"

#include <iostream>
#include <cstring>
#include <iostream>
#include <vector>

#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>

#include "Utils.h"
#include "../src/protocol/Parser.h"
#include <afina/Storage.h>
#include <afina/execute/Add.h>
#include <afina/execute/Append.h>
#include <afina/execute/Command.h>
#include <afina/execute/Delete.h>
#include <afina/execute/Get.h>
#include <afina/execute/InsertCommand.h>
#include <afina/execute/Replace.h>
#include <afina/execute/Set.h>

#define BUFFSIZE 4096


namespace Afina {
namespace Network {
namespace NonBlocking {

// See Worker.h
Worker::Worker(std::shared_ptr<Afina::Storage> ps) : storage(ps) {
	// TODO: implementation here
}

// See Worker.h
Worker::~Worker() {
	// TODO: implementation here
}

void *Worker::Run_Thread(void *args) {
	try {
		static_cast<Worker *>(args)->OnRun(nullptr);
	} catch (std::runtime_error &ex) {
		std::cerr << "Error in Worker::OnRun(): " << ex.what() << std::endl;
	}
	return 0;
}

// See Worker.h
void Worker::Start(int server_socket) {
	std::cout << "network debug: " << __PRETTY_FUNCTION__ << std::endl;
	// TODO: implementation here
	this->server_socket = server_socket;
	if(pthread_create(&thread, NULL, Worker::Run_Thread, this) != 0) {
		throw std::runtime_error("phread_create() error");
	}
}

// See Worker.h
void Worker::Stop() {
	std::cout << "network debug: " << __PRETTY_FUNCTION__ << std::endl;
	// TODO: implementation here
	
	
	
}

// See Worker.h
void Worker::Join() {
	std::cout << "network debug: " << __PRETTY_FUNCTION__ << std::endl;
	// TODO: implementation here
	if(pthread_join(thread, nullptr) != 0) {
		throw std::runtime_error("error at Worker::Join()");
	}
}

// See Worker.h
void Worker::OnRun(void *args) {
	std::cout << "network debug: " << __PRETTY_FUNCTION__ << std::endl;

	// TODO: implementation here
	// 1. Create epoll_context here
	// 2. Add server_socket to context
	// 3. Accept new connections, don't forget to call make_socket_nonblocking on
	//    the client socket descriptor
	// 4. Add connections to the local context
	// 5. Process connection events
	//
	// Do not forget to use EPOLLEXCLUSIVE flag when register socket
	// for events to avoid thundering herd type behavior.
	
	const int maxevents = 20;
	int ep_fd;
	struct epoll_event ev;
	//struct epoll_event* events;
	std::vector<struct epoll_event> events(maxevents);
	
	if((ep_fd = epoll_create(maxevents)) == -1) {
		throw std::runtime_error("Can not create epoll");
	}
	
	ev.events = EPOLLIN | EPOLLET;
	ev.data.fd = server_socket;
	if(epoll_ctl(ep_fd, EPOLL_CTL_ADD, server_socket, &ev) == -1) {
		throw std::runtime_error("epoll_ctl() error");
	}
	
	while (true) {
		const int epoll_timeout = -1;
		int nfds = epoll_wait(ep_fd, events.data(), maxevents, epoll_timeout);
		if(nfds == - 1) {
			throw std::runtime_error("epoll_wait() error");
		}
		for(int i = 0; i < nfds; i++) {
			if ((events[i].events & EPOLLERR) || (events[i].events & EPOLLHUP)) {
				throw std::runtime_error("epoll events error");
				close(events[i].data.fd);
                continue;
			}
			else if(events[i].data.fd == server_socket) {
				std::cout << "SERV_SOCK: " << server_socket << std::endl;
				//new connections
				while(true) {
					int client_socket;
					struct sockaddr_in client_addr;
					socklen_t sinSize = sizeof(struct sockaddr_in);
					if ((client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &sinSize)) == -1) {
						if(errno == EAGAIN || errno == EWOULDBLOCK) {
							break;
						}
						else {
							throw std::runtime_error("accept() error");
						}
					}
					std::cout << "CLIENT_SOCK: " << client_socket << std::endl;
					make_socket_non_blocking(client_socket);
					struct epoll_event client_event;
					client_event.events = EPOLLIN | EPOLLOUT | EPOLLET;
					std::cout << "EVENTS1: " << client_event.events << std::endl;
					client_event.data.fd = client_socket;
					if(epoll_ctl(ep_fd, EPOLL_CTL_ADD, client_socket, &client_event) == -1) {
						throw std::runtime_error("epoll_ctl() error");
					}
				}
			}
			else {
				//client event
				std::cout << "EVENTS2: " << events[i].events << std::endl;
				handle_connection(ep_fd, events[i]);
			}
		}
	}
}

void Worker::handle_connection(int epoll_fd, struct epoll_event event) {
	int client_socket = event.data.fd;
	std::cout << "CLIENT_SOCK2: " << client_socket << std::endl;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_socket, &event) == -1) {
        throw std::runtime_error("epoll_ctl() error");
    }
	std::cout << "HALLO, EPTA\n";
    bool is_connection = true;
    std::string msg_to;
    std::string msg_from;
	std::cout << "EVENTS: " << event.events << std::endl;
    if (event.events & EPOLLIN) {
        std::cout << "HERE1\n";
        Afina::Protocol::Parser parser;
		char buffer[BUFFSIZE];
		std::memset(buffer, 0, BUFFSIZE);
		
		int read_count = 0;
		while ((read_count = read(client_socket, buffer, BUFFSIZE)) > 0) {
            std::cout << "HERE3\n";
            msg_from += std::string(buffer, read_count);
        }
        size_t parsed = 0;
        try {
            while (msg_from.size() > 0) {
                std::cout << "HEREHRER\n";
                bool is_parsed;
                is_parsed = parser.Parse(msg_from, parsed);
                if (is_parsed) {
					uint32_t body_size;
					std::unique_ptr<Execute::Command> command;
					if (command = parser.Build(body_size)) {
                        if (is_parsed + body_size <= msg_from.size()) {
							std::string args = msg_from.substr(parsed, body_size);
                            msg_from.erase(0, parsed + body_size);
                            std::string out;
                            command->Execute(*storage, args, out);
                            msg_to += out;
                            msg_to += "\r\n";
                        }
                    } else {
                        throw std::runtime_error("Parser error");
                    }
                    parser.Reset();
                    parsed = 0;
                    if (!msg_from.empty() && msg_from[0] == '\r') {
                        msg_from.erase(0, 2);
                    }
                } else {
                    break;
                }
            }
        } catch (std::runtime_error &ex) {
            msg_to += "SERVER_ERROR " + std::string(ex.what()) + "\r\n";
        } catch (...) {
            msg_to += "SERVER_ERROR: unknown error";
            msg_to += "\r\n";
        }
        if (read_count < 0) {
            std::cout << "HERE2\n";
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                throw std::runtime_error("Connection read error");
            } else {
                std::cout << "HERE3\n";
            }
        } else {
            // read_count is 0
            is_connection = false;
        }
    }
	
	if ((event.events & EPOLLOUT) && !msg_to.empty()) {
        std::cout << "HERE\n";
		if (write(client_socket, msg_to.c_str(), msg_to.size()) == -1) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                throw std::runtime_error("Connection read error");
            } else {
                std::cerr << "something wrong here\n";
            }
        }
    } else if(msg_to.empty()) {
		std::cout << "MSG_EMPTY\n";
		//is_connection = false;
	}
    if (is_connection) {
		event.events = EPOLLIN | EPOLLOUT | EPOLLET;
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_socket, &event) == -1) {
            throw std::runtime_error("epoll_ctl() error");
        }
    } else {
        close(client_socket);
    }
}

































void Connection_handler::handle_connection(int epoll_fd, struct epoll_event event) {
	int client_socket = event.data.fd;
	std::cout << "CLIENT_SOCK2: " << client_socket << std::endl;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_socket, &event) == -1) {
        throw std::runtime_error("epoll_ctl() error");
    }
	std::cout << "HALLO, EPTA\n";
    bool is_connection = true;
    std::string msg_to;
    std::string msg_from;
	std::cout << "EVENTS: " << event.events << std::endl;
    if (event.events & EPOLLIN) {
        std::cout << "HERE1\n";
        Afina::Protocol::Parser parser;
		char buffer[BUFFSIZE];
		std::memset(buffer, 0, BUFFSIZE);
		
		int read_count = 0;
		while ((read_count = read(client_socket, buffer, BUFFSIZE)) > 0) {
            std::cout << "HERE3\n";
            msg_from += std::string(buffer, read_count);
        }
        size_t parsed = 0;
        try {
            while (msg_from.size() > 0) {
                std::cout << "HEREHRER\n";
                bool is_parsed;
                is_parsed = parser.Parse(msg_from, parsed);
                if (is_parsed) {
					uint32_t body_size;
					std::unique_ptr<Execute::Command> command;
					if (command = parser.Build(body_size)) {
                        if (is_parsed + body_size <= msg_from.size()) {
							std::string args = msg_from.substr(parsed, body_size);
                            msg_from.erase(0, parsed + body_size);
                            std::string out;
                            command->Execute(*storage, args, out);
                            msg_to += out;
                            msg_to += "\r\n";
                        }
                    } else {
                        throw std::runtime_error("Parser error");
                    }
                    parser.Reset();
                    parsed = 0;
                    if (!msg_from.empty() && msg_from[0] == '\r') {
                        msg_from.erase(0, 2);
                    }
                } else {
                    break;
                }
            }
        } catch (std::runtime_error &ex) {
            msg_to += "SERVER_ERROR " + std::string(ex.what()) + "\r\n";
        } catch (...) {
            msg_to += "SERVER_ERROR: unknown error";
            msg_to += "\r\n";
        }
        if (read_count < 0) {
            std::cout << "HERE2\n";
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                throw std::runtime_error("Connection read error");
            } else {
                std::cout << "HERE3\n";
            }
        } else {
            // read_count is 0
            is_connection = false;
        }
    }
	
	if ((event.events & EPOLLOUT) && !msg_to.empty()) {
        std::cout << "HERE\n";
		if (write(client_socket, msg_to.c_str(), msg_to.size()) == -1) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                throw std::runtime_error("Connection read error");
            } else {
                std::cerr << "something wrong here\n";
            }
        }
    } else if(msg_to.empty()) {
		std::cout << "MSG_EMPTY\n";
		//is_connection = false;
	}
    if (is_connection) {
		event.events = EPOLLIN | EPOLLOUT | EPOLLET;
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_socket, &event) == -1) {
            throw std::runtime_error("epoll_ctl() error");
        }
    } else {
        close(client_socket);
    }
}

} // namespace NonBlocking
} // namespace Network
} // namespace Afina
