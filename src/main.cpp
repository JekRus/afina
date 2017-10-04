#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <memory>
#include <stdio.h>
#include <unistd.h>
#include <uv.h>

#include <cxxopts.hpp>

#include <afina/Storage.h>
#include <afina/Version.h>
#include <afina/network/Server.h>

#include "network/blocking/ServerImpl.h"
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

int main(int argc, char **argv) {
/*
<<<<<<< HEAD
    if (argc > 4) {
        std::cout << "Usage ./afina [-d] [-p <filepath>]\n";
        return 1;
    }
    bool is_p = false;
    bool is_d = false;
    int i = 1;
    std::string filepath;
    while (i < argc) {
        if ((std::string(argv[i]) == "-d") && !is_d) {
            is_d = true;
        } else if ((std::string(argv[i]) == "-p") && !is_p && (++i < argc)) {
            is_p = true;
            filepath = std::string(argv[i]);
        } else {
            std::cout << "Usage ./afina [-d] [-p <filepath>]\n";
            return 1;
        }
        ++i;
    }
    if (is_d) {
        // parent
        if (fork()) {
            return 0;
        }
        // child
        if (setsid() == -1) {
            std::cerr << "Error. Cannot create new session\n";
        }
        if (close(STDIN_FILENO) || close(STDOUT_FILENO)) {
            std::cerr << "Error. Cannot close STDIN/STDOUT\n";
            return 1;
        }
    }
    if (is_p) {
        char buffer[BUFFSIZE] = {0};
        int fd;
        fd = open(filepath.c_str(), O_WRONLY | O_CREAT, 0644);
        if (fd == -1) {
            std::cerr << "Error. Cannot open file\n";
            return 1;
        }
        pid_t pid = getpid();
        sprintf(buffer, "%d", pid);
        if (write(fd, buffer, strlen(buffer)) == -1) {
            std::cerr << "Error. Write to file failed\n";
            return 1;
        }
        if (close(fd)) {
            std::cerr << "Error. Cannot close file\n";
            return 1;
        }
    }

    std::cout << "Starting Afina " << Afina::Version_Major << "." << Afina::Version_Minor << "."
              << Afina::Version_Patch;
=======
*/
    // Build version
    // TODO: move into Version.h as a function
    std::stringstream app_string;
    app_string << "Afina " << Afina::Version_Major << "." << Afina::Version_Minor << "." << Afina::Version_Patch;
//>>>>>>> d1889550123c497a6de29856d5b3958b58228cd6

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
    } else {
        throw std::runtime_error("Unknown network type");
    }

    // Init local loop. It will react to signals and performs some metrics collections. Each
    // subsystem is able to push metrics actively, but some metrics could be collected only
    // by polling, so loop here will does that work
    uv_loop_t loop;
    uv_loop_init(&loop);

    uv_signal_t sig;
    uv_signal_init(&loop, &sig);
    uv_signal_start(&sig, signal_handler, SIGTERM | SIGKILL);
    sig.data = &app;

    uv_timer_t timer;
    uv_timer_init(&loop, &timer);
    timer.data = &app;
    uv_timer_start(&timer, timer_handler, 0, 5000);

    // Start services
    try {
        app.storage->Start();
        app.server->Start(8080);

        // Freeze current thread and process events
        std::cout << "Application started" << std::endl;
        uv_run(&loop, UV_RUN_DEFAULT);

        // Stop services
        app.server->Stop();
        app.server->Join();
        app.storage->Stop();

        std::cout << "Application stopped" << std::endl;
    } catch (std::exception &e) {
        std::cerr << "Fatal error" << e.what() << std::endl;
    }

    return 0;
}
