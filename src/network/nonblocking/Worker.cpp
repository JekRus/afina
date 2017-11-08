#include "Worker.h"

#include <iostream>

#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "Utils.h"


namespace Afina {
namespace Network {
namespace NonBlocking {

// See Worker.h
Worker::Worker(std::shared_ptr<Afina::Storage> ps) {
	// TODO: implementation here
}

// See Worker.h
Worker::~Worker() {
	// TODO: implementation here
}

void *Worker::Run_Thread(void *args) {
	int serv_sock = static_cast<thread_args *>(args)->server_socket;
	try {
		static_cast<thread_args *>(args)->worker->OnRun(&serv_sock);
	} catch (std::runtime_error &ex) {
		std::cerr << "Error in Worker::OnRun(): " << ex.what() << std::endl;
	}
	return 0;
}

// See Worker.h
void Worker::Start(int server_socket) {
	std::cout << "network debug: " << __PRETTY_FUNCTION__ << std::endl;
	// TODO: implementation here
	thread_args args{this, server_socket};
	pthread_create(&thread, NULL, Worker::Run_Thread, &args);
	
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
	
	int server_socket = *(static_cast<int *>(args));
	const int maxevents = 20;
	int ep_fd;
	struct epoll_event ev;
	struct epoll_event* events;
	events = static_cast<struct epoll_event *>(calloc(maxevents, sizeof(epoll_event)));
	
	if((ep_fd = epoll_create(maxevents)) == -1) {
		throw std::runtime_error("Can not create epoll");
	}
	
	ev.events = EPOLLIN | EPOLLET;
	ev.data.fd = server_socket;
	if(epoll_ctl(ep_fd, EPOLL_CTL_ADD, server_socket, &ev) == -1) {
		throw std::runtime_error("epoll_ctl() error");
	}
	
	while (1) {
		const int epoll_timeout = -1;
		int nfds = epoll_wait(ep_fd, events, maxevents, epoll_timeout);
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
				//new connection
				int client_socket;
				struct sockaddr_in client_addr;
				socklen_t sinSize = sizeof(struct sockaddr_in);
				if ((client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &sinSize)) == -1) {
					throw std::runtime_error("Socket accept() failed");
				}
				make_socket_non_blocking(client_socket);
				struct epoll_event client_event;
				client_event.events = EPOLLIN | EPOLLOUT | EPOLLET;
				client_event.data.fd = client_socket;
				if(epoll_ctl(ep_fd, EPOLL_CTL_ADD, client_socket, &client_event) == -1) {
					throw std::runtime_error("epoll_ctl() error");
				}
			}
			else {
				//client event
				handle_connection(&events[i]);
			}
		}
	}
}


void Worker::handle_connection(void* args) {
	struct epoll_event event = *(static_cast<struct epoll_event *>(args));
}



} // namespace NonBlocking
} // namespace Network
} // namespace Afina
