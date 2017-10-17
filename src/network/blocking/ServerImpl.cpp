#include "ServerImpl.h"

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

#include <afina/execute/Command.h>
#include <afina/execute/Add.h>
#include <afina/execute/Append.h>
#include <afina/execute/Delete.h>
#include <afina/execute/Get.h>
#include <afina/execute/InsertCommand.h>
#include <afina/execute/Replace.h>
#include <afina/execute/Set.h>
#include <afina/Storage.h>
#include "../src/protocol/Parser.h"

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
    int server_socket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
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
            close(server_socket);
            throw std::runtime_error("Socket accept() failed");
        }

        // TODO: Start new thread and process data from/to connection
        {
            if(connections.size() < max_workers) {
				connections.push_back(0);
				Thread_args args{this, client_socket};
				if (pthread_create(&connections.back(), NULL, ServerImpl::RunConnectionThread, &args) < 0) {
					close(server_socket);
					close(client_socket);
					throw std::runtime_error("Could not create connection thread");
				}
			}
			else {
				std::cout << "Connections limit reached\n";
				close(client_socket);
			}
       }
    }
    // Cleanup on exit...
    close(server_socket);
    for(auto thread: connections) {
		pthread_join(thread, NULL);
	}
}

// See Server.h
void ServerImpl::RunConnection(const int client_socket) { 
	std::cout << "network debug: " << __PRETTY_FUNCTION__ << std::endl; 
	
	Afina::Protocol::Parser parser;
	char buf[BUFFSIZE];
	int read_state;
	bool is_connection = true;
	
	while(running.load() && is_connection) {
		std::memset(buf, 0, BUFFSIZE);
		if((read_state = read(client_socket, buf, BUFFSIZE)) > 0) {
			size_t parsed;
			if(parser.Parse(buf, BUFFSIZE, parsed)) {
				uint32_t body_size;
				std::unique_ptr<Execute::Command> command;
				if(command = parser.Build(body_size)) {
					if(buf + parsed + body_size >= buf + BUFFSIZE) {
						throw std::runtime_error("Buffer reading error");
					}
					const std::string args(buf + parsed, body_size);
					std::string out;
					try {
						command->Execute(*pStorage, args, out);
					}
					catch(std::exception &e) {
						out = "SERVER_ERROR: " + std::string(e.what()) + "\r\n";
					}
					catch(...) {
						out = "SERVER_ERROR: unknown error\r\n";
					}
					if(write(client_socket, out.c_str(), out.size()) == -1) {
						is_connection = false;
					}
				}
				parser.Reset();
			}
		}
		else if(read_state < 0) {
			throw std::runtime_error("Client socket reading error");
		}
		else { //read_state == 0
			//std::string test = "1";
			//if(write(client_socket, test.c_str(), test.size()) == -1) {
			is_connection = false;
			//}
		}
	}
	std::cout << "Connection closed\n";
	close(client_socket);
}

} // namespace Blocking
} // namespace Network
} // namespace Afina