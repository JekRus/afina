#ifndef AFINA_NETWORK_NONBLOCKING_WORKER_H
#define AFINA_NETWORK_NONBLOCKING_WORKER_H

#include <atomic>
#include <map>
#include <memory>
#include <pthread.h>
#include <sys/epoll.h>

namespace Afina {

// Forward declaration, see afina/Storage.h
class Storage;

namespace Network {
namespace NonBlocking {

class Connection_handler {
public:
    Connection_handler() = default;
    Connection_handler(int sock, std::shared_ptr<Afina::Storage> ps) : client_socket(sock), storage(ps) {}
    ~Connection_handler() {}
    void handle_connection(int epoll_fd, struct epoll_event event);

private:
    int client_socket;
    std::string msg_to;
    std::string msg_from;
    std::shared_ptr<Afina::Storage> storage;
};

/**
 * # Thread running epoll
 * On Start spaws background thread that is doing epoll on the given server
 * socket and process incoming connections and its data
 */
class Worker {
public:
    Worker(std::shared_ptr<Afina::Storage> ps);
    Worker(const Worker &);
    ~Worker();

    /**
     * Spaws new background thread that is doing epoll on the given server
     * socket. Once connection accepted it must be registered and being processed
     * on this thread
     */
    void Start(int server_socket);

    /**
     * Signal background thread to stop. After that signal thread must stop to
     * accept new connections and must stop read new commands from existing. Once
     * all readed commands are executed and results are send back to client, thread
     * must stop
     */
    void Stop();

    /**
     * Blocks calling thread until background one for this worker is actually
     * been destoryed
     */
    void Join();

protected:
    /**
     * Method executing by background thread
     */
    void OnRun(void *args);
    static void *Run_Thread(void *args);

private:
    pthread_t thread;
    int server_socket;
    std::atomic<bool> running;
    std::shared_ptr<Afina::Storage> storage;
    std::map<int, Connection_handler> connections;
};

} // namespace NonBlocking
} // namespace Network
} // namespace Afina
#endif // AFINA_NETWORK_NONBLOCKING_WORKER_H
