#include "Worker.h"

#include <cstring>
#include <iostream>
#include <vector>

#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "../src/protocol/Parser.h"
#include "Utils.h"
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

Worker::Worker(const Worker &other) {
    thread = other.thread;
    server_socket = other.server_socket;
    storage = other.storage;
    connections = other.connections;
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
    running.store(true);
    if (pthread_create(&thread, NULL, Worker::Run_Thread, this) != 0) {
        throw std::runtime_error("phread_create() error");
    }
}

// See Worker.h
void Worker::Stop() {
    std::cout << "network debug: " << __PRETTY_FUNCTION__ << std::endl;
    // TODO: implementation here
    running.store(false);
}

// See Worker.h
void Worker::Join() {
    std::cout << "network debug: " << __PRETTY_FUNCTION__ << std::endl;
    // TODO: implementation here
    pthread_cancel(thread);
    if (pthread_join(thread, nullptr) != 0) {
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
    thread = pthread_self();
    const int maxevents = 20;
    int ep_fd;
    struct epoll_event ev;
    std::vector<struct epoll_event> events(maxevents);

    if ((ep_fd = epoll_create(maxevents)) == -1) {
        throw std::runtime_error("Can not create epoll");
    }
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = server_socket;
    if (epoll_ctl(ep_fd, EPOLL_CTL_ADD, server_socket, &ev) == -1) {
        throw std::runtime_error("epoll_ctl() error");
    }
    const int epoll_timeout = 100;
    while (true) {
        int nfds = epoll_wait(ep_fd, events.data(), maxevents, epoll_timeout);
        if (nfds == -1) {
            throw std::runtime_error("epoll_wait() error");
        } else if (nfds == 0) {
            if (running.load() == false) {
                return;
            } else {
                continue;
            }
        }
        for (int i = 0; i < nfds; i++) {
            if ((events[i].events & EPOLLERR) || (events[i].events & EPOLLHUP)) {
                std::cerr << "epoll events error\n";
                close(events[i].data.fd);
                continue;
            } else if (events[i].data.fd == server_socket) {
                // new connections
                while (true) {
                    int client_socket;
                    struct sockaddr_in client_addr;
                    socklen_t sinSize = sizeof(struct sockaddr_in);
                    if ((client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &sinSize)) == -1) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            break;
                        } else {
                            throw std::runtime_error("accept() error");
                        }
                    }
                    make_socket_non_blocking(client_socket);
                    struct epoll_event client_event;
                    client_event.events = EPOLLIN | EPOLLOUT | EPOLLET;
                    client_event.data.fd = client_socket;
                    if (epoll_ctl(ep_fd, EPOLL_CTL_ADD, client_socket, &client_event) == -1) {
                        throw std::runtime_error("epoll_ctl() error");
                    }
                    connections[client_socket] = Connection_handler(client_socket, storage);
                }
            } else {
                // client event
                int client_socket = events[i].data.fd;
                connections[client_socket].handle_connection(ep_fd, events[i]);
            }
        }
    }
}

void Connection_handler::handle_connection(int epoll_fd, struct epoll_event event) {
    int client_socket = event.data.fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_socket, &event) == -1) {
        throw std::runtime_error("epoll_ctl() error");
    }
    bool is_connection = true;
    if (event.events & EPOLLIN) {
        Afina::Protocol::Parser parser;
        char buffer[BUFFSIZE];
        std::memset(buffer, 0, BUFFSIZE);

        int read_count = 0;
        while ((read_count = read(client_socket, buffer, BUFFSIZE)) > 0) {
            msg_from += std::string(buffer, read_count);
		}
        size_t parsed = 0;
        try {
            while (msg_from.size() > 0) {
                bool is_parsed;
                is_parsed = parser.Parse(msg_from, parsed);
                if (is_parsed) {
                    uint32_t body_size;
                    std::unique_ptr<Execute::Command> command;
                    if (command = parser.Build(body_size)) {
                        if (is_parsed + body_size <= msg_from.size()) {
                            std::string args = msg_from.substr(parsed, body_size);
                            msg_from.erase(0, parsed + body_size);
                            if (msg_from.size() >= 2 && msg_from[0] == '\r' && msg_from[1] == '\n') {
                                msg_from.erase(0, 2);
                            }
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
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                throw std::runtime_error("Connection read error");
            }
        } else {
            // read_count is 0
            is_connection = false;
        }
    }

    if ((event.events & EPOLLOUT) && !msg_to.empty()) {
        int write_count;
        if ((write_count = write(client_socket, msg_to.c_str(), msg_to.size())) == -1) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                throw std::runtime_error("Connection read error");
            }
        } else {
            msg_to.erase(0, write_count);
        }
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
