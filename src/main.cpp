#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdio.h>
#include <unistd.h>
#include <uv.h>
#include <signal.h>
#include <sys/signalfd.h>
#include <sys/epoll.h>


#include <cxxopts.hpp>

#include <afina/Storage.h>
#include <afina/Version.h>
#include <afina/network/Server.h>

#include "network/blocking/ServerImpl.h"
#include "network/nonblocking/ServerImpl.h"
#include "network/uv/ServerImpl.h"
#include "storage/MapBasedGlobalLockImpl.h"

#define BUFFSIZE 4096

typedef struct {
    std::shared_ptr<Afina::Storage> storage;
    std::shared_ptr<Afina::Network::Server> server;
} Application;

// Handle all signals catched
void signal_handler(uv_signal_t *handle, int signum) {
    Application *pApp = static_cast<Application *>(handle->data);

    std::cout << "Receive stop signal" << std::endl;
    uv_stop(handle->loop);
}

// Called when it is time to collect passive metrics from services
void timer_handler(uv_timer_t *handle) {
    Application *pApp = static_cast<Application *>(handle->data);
    std::cout << "Start passive metrics collection" << std::endl;
}

// my signalfd handler
void signalfd_handler(int &epoll_fd, const int maxevents) {
    sigset_t sig_mask;
    sigemptyset(&sig_mask);
    sigaddset(&sig_mask, SIGPIPE);
    sigaddset(&sig_mask, SIGINT);
    sigaddset(&sig_mask, SIGTERM);
    if (pthread_sigmask(SIG_BLOCK, &sig_mask, NULL) != 0) {
        throw std::runtime_error("pthread_sigmask() error");
    }
    sigdelset(&sig_mask, SIGPIPE);
    int sig_fd;
    sig_fd = signalfd(-1, &sig_mask, SFD_NONBLOCK);
    struct epoll_event ev;
    if ((epoll_fd = epoll_create(maxevents)) == -1) {
        throw std::runtime_error("Can not create epoll");
    }
    ev.events = EPOLLIN;
    ev.data.fd = sig_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sig_fd, &ev) == -1) {
        throw std::runtime_error("epoll_ctl() error");
    }
}

int main(int argc, char **argv) {
    // Build version
    // TODO: move into Version.h as a function
    std::stringstream app_string;
    app_string << "Afina " << Afina::Version_Major << "." << Afina::Version_Minor << "." << Afina::Version_Patch;
    if (Afina::Version_SHA.size() > 0) {
        app_string << "-" << Afina::Version_SHA;
    }

    // Command line arguments parsing
    cxxopts::Options options("afina", "Simple memory caching server");
    try {
        // TODO: use custom cxxopts::value to print options possible values in help message
        // and simplify validation below
        options.add_options()("s,storage", "Type of storage service to use", cxxopts::value<std::string>());
        options.add_options()("n,network", "Type of network service to use", cxxopts::value<std::string>());
        options.add_options()("d,daemon", "Run as daemon");
        options.add_options()("p,pid", "Write pid in file", cxxopts::value<std::string>());
        options.add_options()("h,help", "Print usage info");
        options.parse(argc, argv);

        if (options.count("help") > 0) {
            std::cerr << options.help() << std::endl;
            return 0;
        }
    } catch (cxxopts::OptionParseException &ex) {
        std::cerr << "Error: " << ex.what() << std::endl;
        return 1;
    }

    // daemon and pid options
    if (options.count("daemon") > 0) {
        pid_t k;
        if ((k = fork()) < 0) {
            std::cerr << "Error. fork() failed\n";
            return 1;
        } else if (k > 0) {
            return 0;
        }
        // child
        if (setsid() == -1) {
            std::cerr << "Error. Cannot create new session\n";
            return 1;
        }
        if (close(STDIN_FILENO) || close(STDOUT_FILENO) || close(STDERR_FILENO)) {
            std::cerr << "Error. Cannot close STDIN/STDOUT\n";
            return 1;
        }
    }
    try {
        if (options.count("pid") > 0) {
            std::fstream fs;
            fs.open(options["pid"].as<std::string>(), std::fstream::out | std::fstream::trunc);
            fs << getpid();
            fs.close();
        }
    } catch (std::exception &e) {
        std::cerr << "Error" << e.what() << std::endl;
    }

    // Start boot sequence
    Application app;
    std::cout << "Starting " << app_string.str() << std::endl;

    // Build new storage instance
    std::string storage_type = "map_global";
    if (options.count("storage") > 0) {
        storage_type = options["storage"].as<std::string>();
    }

    if (storage_type == "map_global") {
        app.storage = std::make_shared<Afina::Backend::MapBasedGlobalLockImpl>();
    } else {
        throw std::runtime_error("Unknown storage type");
    }

    // Build  & start network layer
    std::string network_type = "uv";
    if (options.count("network") > 0) {
        network_type = options["network"].as<std::string>();
    }

    if (network_type == "uv") {
        app.server = std::make_shared<Afina::Network::UV::ServerImpl>(app.storage);
    } else if (network_type == "blocking") {
        app.server = std::make_shared<Afina::Network::Blocking::ServerImpl>(app.storage);
    } else if (network_type == "nonblocking") {
        app.server = std::make_shared<Afina::Network::NonBlocking::ServerImpl>(app.storage);
    } else {
        throw std::runtime_error("Unknown network type");
    }

    // Start services
    try {
        int ep_fd;
        const int maxevents = 20;
        std::vector<struct epoll_event> events(maxevents);
        signalfd_handler(ep_fd, maxevents);
        
        app.storage->Start();
        app.server->Start(8080);

        std::cout << "Application started" << std::endl;
        
        bool running = true;
        struct signalfd_siginfo sig_info;
        const int epoll_timeout = 100;
        while (running) {
            int nfds = epoll_wait(ep_fd, events.data(), maxevents, epoll_timeout);
            if (nfds == -1) {
                throw std::runtime_error("epoll_wait() error");
            }
            for(int i = 0; i < nfds; ++i) {
                int fd = events[i].data.fd;
                int read_count = read(fd, &sig_info, sizeof(sig_info));
                if(read_count == sizeof(sig_info) && (sig_info.ssi_signo == SIGINT || sig_info.ssi_signo == SIGTERM)) {
                    //correct signal => shutdown server
                    close(fd);
                    running = false;
                }
                else {
                    throw std::runtime_error("signal handle error");
                }
            }
        }

        // Stop services
        close(ep_fd);
        app.server->Stop();
        app.server->Join();
        app.storage->Stop();

        std::cout << "Application stopped" << std::endl;
    } catch (std::exception &e) {
        std::cerr << "Fatal error" << e.what() << std::endl;
    }

    return 0;
}
