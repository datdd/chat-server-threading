#include <iostream>
#include <vector>
#include <thread>
#include <mutex>
#include <string>
#include <cstring> // for memset, strerror
#include <atomic>
#include <algorithm> // for std::remove

// Networking headers (POSIX)
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h> // for close
#include <errno.h>  // for errno

/**
 * @brief ChatServer class
 *
 * This class implements a simple chat server that listens for incoming connections
 * and allows multiple clients to communicate with each other.
 */
class ChatServer
{
public:
    /**
     * @brief Constructor
     *
     * Initializes the server socket and binds it to the specified port.
     *
     * @param port Port number for the server to listen on
     */
    ChatServer(int port);

    /**
     * @brief Destructor
     *
     * Closes the server socket and joins all client threads.
     * Ensures server resources are cleaned up properly.
     */
    ~ChatServer();

    /**
     * @brief Starts the server
     *
     * Sets up the socket and begins accepting incoming connections.
     */
    bool start();

    /**
     * @brief Stops the server
     *
     * Stops the server gracefully.
     * Closes all client connections and cleans up resources.
     */
    void stop();

private:
    // Constants
    static const int MAX_PENDING_CONNECTIONS = 5; // Maximum number of pending connections in queue
    static const int BUFFER_SIZE = 4096;          // Size of the buffer for receiving messages
    static const int MAX_CLIENTS = 100;           // Maximum number of clients

    // Member variables
    int port_;
    int server_fd_;                           // Listening socket file descriptor
    std::atomic<bool> running_;               // Flag to indicate if the server is running
    std::vector<int> client_sockets_;         // List of connected client sockets
    std::vector<std::thread> client_threads_; // Threads for handling clients
    std::mutex clients_mutex_;                // Mutex for synchronyzing access to client sockets
    std::thread accept_thread_;               // Thread for accepting new connections

    /**
     * @brief Accept incoming client connections
     *
     * This method runs in a loop, accepting new client connections and spawning
     * threads to handle each client.
     */
    void accept_clients();

    /**
     * @brief Handle communication with a single client. Runs in its own thread.
     *
     * This method receives messages from the client and broadcasts them to all other clients.
     * It also handles client disconnection and cleanup.
     *
     * @param client_fd The file descriptor of the client socket
     */
    void handle_client(int client_fd);

    /**
     * @brief Broadcast a message to all connected clients
     *
     * This method sends a message to all clients except the sender.
     *
     * @param message The message to broadcast
     * @param sender_fd The file descriptor of the sender client
     */
    void broadcast_message(const std::string &message, int sender_fd);

    /**
     * @brief Closes all client sockets and cleans up resources
     *
     */
    void cleanup_clients();

    /**
     * @brief Cleans up all threads
     *
     * This method joins all client threads and cleans up resources.
     */
    void cleanup_threads();

    /**
     * @brief Logs an error message
     *
     * This method logs an error message to the console.
     *
     * @param message The error message to log
     */
    void log_error(const char *message);
};