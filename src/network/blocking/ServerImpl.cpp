#include "ServerImpl.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>

#include <pthread.h>
#include <signal.h>

#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>

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
namespace Blocking {

void *ServerImpl::RunAcceptorProxy(void *p) {
    ServerImpl *srv = reinterpret_cast<ServerImpl *>(p);
    try {
        srv->RunAcceptor();
    } catch (std::runtime_error &ex) {
        std::cerr << "Server fails: " << ex.what() << std::endl;
    }
    return 0;
}

void *ServerImpl::RunConnectionThread(void *p) {
    Thread_args args = *(reinterpret_cast<Thread_args *>(p));
    try {
        args.srv->RunConnection(args.client_descriptor);
    } catch (std::runtime_error &ex) {
        std::cerr << "Connection fails: " << ex.what() << std::endl;
        close(args.client_descriptor);
    }
    return 0;
}

// See Server.h
ServerImpl::ServerImpl(std::shared_ptr<Afina::Storage> ps) : Server(ps) {}

// See Server.h
ServerImpl::~ServerImpl() {}

// See Server.h
void ServerImpl::Start(uint32_t port, uint16_t n_workers) {
    std::cout << "network debug: " << __PRETTY_FUNCTION__ << std::endl;

    // If a client closes a connection, this will generally produce a SIGPIPE
    // signal that will kill the process. We want to ignore this signal, so send()
    // just returns -1 when this happens.
    sigset_t sig_mask;
    sigemptyset(&sig_mask);
    sigaddset(&sig_mask, SIGPIPE);
    if (pthread_sigmask(SIG_BLOCK, &sig_mask, NULL) != 0) {
        throw std::runtime_error("Unable to mask SIGPIPE");
    }

    // Setup server parameters BEFORE thread created, that will guarantee
    // variable value visibility
    max_workers = n_workers;
    listen_port = port;

    // The pthread_create function creates a new thread.
    //
    // The first parameter is a pointer to a pthread_t variable, which we can use
    // in the remainder of the program to manage this thread.
    //
    // The second parameter is used to specify the attributes of this new thread
    // (e.g., its stack size). We can leave it NULL here.
    //
    // The third parameter is the function this thread will run. This function *must*
    // have the following prototype:
    //    void *f(void *args);
    //
    // Note how the function expects a single parameter of type void*. We are using it to
    // pass this pointer in order to proxy call to the class member function. The fourth
    // parameter to pthread_create is used to specify this parameter value.
    //
    // The thread we are creating here is the "server thread", which will be
    // responsible for listening on port 23300 for incoming connections. This thread,
    // in turn, will spawn threads to service each incoming connection, allowing
    // multiple clients to connect simultaneously.
    // Note that, in this particular example, creating a "server thread" is redundant,
    // since there will only be one server thread, and the program's main thread (the
    // one running main()) could fulfill this purpose.
    running.store(true);
    if (pthread_create(&accept_thread, NULL, ServerImpl::RunAcceptorProxy, this) < 0) {
        throw std::runtime_error("Could not create server thread");
    }
}

// See Server.h
void ServerImpl::Stop() {
    std::cout << "network debug: " << __PRETTY_FUNCTION__ << std::endl;
    running.store(false);
    shutdown(server_socket, SHUT_RDWR);
}

// See Server.h
void ServerImpl::Join() {
    std::cout << "network debug: " << __PRETTY_FUNCTION__ << std::endl;
    pthread_join(accept_thread, 0);
    std::cout << "Joined\n";
}

// See Server.h
void ServerImpl::RunAcceptor() {
    std::cout << "network debug: " << __PRETTY_FUNCTION__ << std::endl;

    // For IPv4 we use struct sockaddr_in:
    // struct sockaddr_in {
    //     short int          sin_family;  // Address family, AF_INET
    //     unsigned short int sin_port;    // Port number
    //     struct in_addr     sin_addr;    // Internet address
    //     unsigned char      sin_zero[8]; // Same size as struct sockaddr
    // };
    //
    // Note we need to convert the port to network order

    struct sockaddr_in server_addr;
    std::memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;          // IPv4
    server_addr.sin_port = htons(listen_port); // TCP port number
    server_addr.sin_addr.s_addr = INADDR_ANY;  // Bind to any address

    // Arguments are:
    // - Family: IPv4
    // - Type: Full-duplex stream (reliable)
    // - Protocol: TCP
    server_socket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_socket == -1) {
        throw std::runtime_error("Failed to open socket");
    }

    // when the server closes the socket,the connection must stay in the TIME_WAIT state to
    // make sure the client received the acknowledgement that the connection has been terminated.
    // During this time, this port is unavailable to other processes, unless we specify this option
    //
    // This option let kernel knows that we are OK that multiple threads/processes are listen on the
    // same port. In a such case kernel will balance input traffic between all listeners (except those who
    // are closed already)
    int opts = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opts, sizeof(opts)) == -1) {
        close(server_socket);
        throw std::runtime_error("Socket setsockopt() failed");
    }

    // Bind the socket to the address. In other words let kernel know data for what address we'd
    // like to see in the socket
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        close(server_socket);
        throw std::runtime_error("Socket bind() failed");
    }

    // Start listening. The second parameter is the "backlog", or the maximum number of
    // connections that we'll allow to queue up. Note that listen() doesn't block until
    // incoming connections arrive. It just makesthe OS aware that this process is willing
    // to accept connections on this socket (which is bound to a specific IP and port)
    if (listen(server_socket, 5) == -1) {
        close(server_socket);
        throw std::runtime_error("Socket listen() failed");
    }

    int client_socket;
    struct sockaddr_in client_addr;
    socklen_t sinSize = sizeof(struct sockaddr_in);

    while (running.load()) {
        std::cout << "network debug: waiting for connection..." << std::endl;

        // When an incoming connection arrives, accept it. The call to accept() blocks until
        // the incoming connection arrives
        if ((client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &sinSize)) == -1) {
            if (running.load()) {
                throw std::runtime_error("Socket accept() failed");
            } else {
                break;
            }
        }

        // TODO: Start new thread and process data from/to connection
        {
            auto it = connections.begin();
            while (it != connections.end()) {
                if (pthread_kill(*it, 0) != 0) {
                    pthread_join(*it, 0);
                    it = connections.erase(it);
                } else {
                    ++it;
                }
            }
            if (connections.size() < max_workers) {
                pthread_t thread;
                Thread_args args{this, client_socket};
                if (pthread_create(&thread, NULL, ServerImpl::RunConnectionThread, &args) != 0) {
                    close(server_socket);
                    close(client_socket);
                    throw std::runtime_error("Could not create connection thread");
                }
            } else {
                std::cout << "Connections limit reached\n";
                close(client_socket);
            }
        }
    }
    // Cleanup on exit...
    close(server_socket);

    // Wait until for all connections to be complete
    std::unique_lock<std::mutex> __lock(connections_mutex);
    while (!connections.empty()) {
        connections_cv.wait(__lock);
    }
}

// See Server.h
void ServerImpl::RunConnection(const int client_socket) {
    std::cout << "network debug: " << __PRETTY_FUNCTION__ << std::endl;
    pthread_t self = pthread_self();

    // Thread just spawn, register itself as a connection
    {
        std::unique_lock<std::mutex> __lock(connections_mutex);
        connections.insert(self);
    }

    // TODO: All connection work is here

    Afina::Protocol::Parser parser;
    char buffer[BUFFSIZE];
    bool is_connection = true;
    int read_count = 0;
    std::string args;
    std::unique_ptr<Execute::Command> command;
    uint32_t body_size;
    bool extra_args = false;

    while (running.load() && is_connection) {
        std::memset(buffer, 0, BUFFSIZE);
        if ((read_count = read(client_socket, buffer, BUFFSIZE)) > 0) {
            std::cout << "r_c=" << read_count << std::endl;
            char *buf_p = buffer;
            size_t parsed = 0;
            try {
                if (extra_args) {
                    size_t extra_size = body_size - args.size();
                    if (buf_p + extra_size <= buffer + BUFFSIZE) {
                        args.append(buf_p, extra_size);
                        buf_p += extra_size;
                        extra_args = false;
                        read_count -= extra_size;
                    } else {
                        args.append(buf_p, BUFFSIZE);
                        continue;
                    }
                    if (args.size() == body_size) {
                        std::string out;
                        command->Execute(*pStorage, args, out);
                        out += "\r\n";
                        if (write(client_socket, out.c_str(), out.size()) == -1) {
                            std::cerr << "Connection closed ahead of time\n";
                            is_connection = false;
                        }
                    } else {
                        throw std::runtime_error("Arguments reading error");
                    }
                }

                while (read_count - (buf_p - buffer) > 0) {
                    bool is_parsed;
                    is_parsed = parser.Parse(buf_p, read_count - (buf_p - buffer), parsed);
                    buf_p += parsed;
                    if (is_parsed) {
                        if (command = parser.Build(body_size)) {
                            if (buf_p + body_size <= buffer + BUFFSIZE) {
                                args = std::string(buf_p, body_size);
                                buf_p += body_size;
                                std::string out;
                                command->Execute(*pStorage, args, out);
                                out += "\r\n";
                                if (write(client_socket, out.c_str(), out.size()) == -1) {
                                    std::cerr << "Connection closed ahead of time\n";
                                    is_connection = false;
                                }
                            }
                            // not enough chars in buffer to get args for command
                            else {
                                args = std::string(buf_p, buffer + BUFFSIZE - buf_p);
                                extra_args = true;
                                break;
                            }
                        } else {
                            throw std::runtime_error("Parser error");
                        }
                        parser.Reset();
                        parsed = 0;
                    }
                    if (*buf_p == '\r') {
                        buf_p++;
                    }
                    if (*buf_p == '\n') {
                        buf_p++;
                    }
                }
            } catch (std::runtime_error &ex) {
                std::string out = "SERVER_ERROR " + std::string(ex.what()) + "\r\n";
                if (write(client_socket, out.c_str(), out.size()) == -1) {
                    std::cerr << "Connection closed ahead of time\n";
                    is_connection = false;
                }
            } catch (...) {
                std::string out = "SERVER_ERROR: unknown error2";
                out += "\r\n";
                if (write(client_socket, out.c_str(), out.size()) == -1) {
                    std::cerr << "Connection closed ahead of time\n";
                    is_connection = false;
                }
            }
        } else if (read_count < 0) {
            throw std::runtime_error("Connection read error");
        } else {
            std::cout << "Connection closed\n";
            is_connection = false;
            close(client_socket);
        }
    }

    // Thread is about to stop, remove self from list of connections
    // and if it was the very last one, notify main thread
    {
        std::unique_lock<std::mutex> __lock(connections_mutex);
        auto pos = connections.find(self);

        assert(pos != connections.end());
        connections.erase(pos);

        if (connections.empty()) {
            // Better to unlock before notify in order to let notified thread
            // hold the mutex. Otherwise notification might be skipped
            __lock.unlock();

            // We are pretty sure that only ONE thread is waiting for connections
            // queue to be empty - main thread
            connections_cv.notify_one();
        }
    }
}

} // namespace Blocking
} // namespace Network
} // namespace Afina
