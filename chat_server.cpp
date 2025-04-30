#include "chat_server.hpp"

ChatServer::ChatServer(int port) : port_(port), server_fd_(-1), running_(false)
{
    std::cout << "Initializing Chat Server on port " << port_ << std::endl;
}

ChatServer::~ChatServer()
{
    stop(); // Attempt graceful shutdown if not already stopped
    // Ensure threads are joined if stop was not called explicitly or fully completed
    cleanup_threads();
    if (server_fd_ != -1)
    {
        close(server_fd_);
    }
}

bool ChatServer::start()
{
    // 1. Create socket
    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ == -1)
    {
        log_error("Failed to create socket");
        return false;
    }

    int opt = 1;
    if (setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        log_error("setsockopt(SO_REUSEADDR) failed");
    }

    // 2. Bind socket to address and port
    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY; // Listen on all interfaces
    server_addr.sin_port = htons(port_);

    if (bind(server_fd_, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        log_error("Failed to bind socket");
        close(server_fd_);
        server_fd_ = -1;
        return false;
    }

    // 3. Listen for incoming connections
    if (listen(server_fd_, MAX_PENDING_CONNECTIONS) < 0)
    {
        log_error("Failed to listen on socket");
        close(server_fd_);
        server_fd_ = -1;
        return false;
    }

    std::cout << "Chat Server started on port " << port_ << std::endl;
    running_ = true;

    // 4. Start the main accept loop in a separate thread
    //    This allows 'start()' to return, letting the main program continue
    //    if needed while the server runs in the background, or just wait cleanly.
    accept_thread_ = std::thread(&ChatServer::accept_clients, this);

    return true;
}

void ChatServer::stop()
{
    if (!running_.exchange(false))
    {
        // Already stopped or stopping
        return;
    }
    std::cout << "Stopping Chat Server..." << std::endl;

    // Close the listenning socket to interrupt the accept call
    if (server_fd_ != -1)
    {
        shutdown(server_fd_, SHUT_RDWR);
        close(server_fd_);
        server_fd_ = -1;
    }

    // Wait for accepting thread to finish
    if (accept_thread_.joinable())
    {
        accept_thread_.join();
    }

    // Signal all client threads to stop (by closing their sockets)
    // and cleanup clients resources
    cleanup_clients();

    std::cout << "Chat Server stopped." << std::endl;
}

void ChatServer::accept_clients()
{
    while (running_)
    {
        sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        // Accept a new client connection
        // This will block until a new connection is made
        // or an error occurs
        int client_fd = accept(server_fd_, (struct sockaddr *)&client_addr, &client_len);

        // Check running status again after accept potentially blocking
        if (!running_)
        {
            // Server is stopping, break out of the loop
            break;
        }

        if (client_fd < 0)
        {
            // Only log error if the server is supposed to be running
            if (errno != EBADF && errno != EINVAL)
            { // Errors often seen during shutdown
                log_error("Failed to accept client connection");
            }
            // Potentially add a small sleep here to prevent tight loop on continuous errors
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue; // Continue accepting
        }

        // Get client IP address for logging
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        std::cout << "Accepted connection from " << client_ip << ":" << ntohs(client_addr.sin_port) << std::endl;

        // Add client socket to the list and spawn a handling thread
        { // Scope for the mutex lock
            std::lock_guard<std::mutex> lock(clients_mutex_);
            client_sockets_.push_back(client_fd);
            // Spawn a thread to handle the client
            // Note: Use emplace_back to construct the thread in place
            client_threads_.emplace_back(&ChatServer::handle_client, this, client_fd);
        }
    }
    std::cout << "Accept loop finished." << std::endl;
}

// Handles communication with a single client. Runs in its own thread.
void ChatServer::handle_client(int client_fd)
{
    char buffer[BUFFER_SIZE];
    std::string client_id = "Client<" + std::to_string(client_fd) + ">"; // Simple identifier

    while (running_)
    {
        memset(buffer, 0, BUFFER_SIZE); // Clear buffer before receiving

        // Receive data from the client
        ssize_t bytes_received = recv(client_fd, buffer, BUFFER_SIZE - 1, 0);

        if (bytes_received <= 0)
        {
            // Error or connection closed by client
            if (bytes_received == 0)
            {
                std::cout << client_id << " disconnected." << std::endl;
            }
            else if (running_)
            { // Only log error if server is running
                log_error(("Receive error from " + client_id).c_str());
            }
            break; // Exit loop to clean up this client
        }

        // Null-terminate the received data to treat it as a C-string
        buffer[bytes_received] = '\0';
        std::string message(buffer);
        std::cout << "Received from " << client_id << ": " << message << std::endl;

        // Broadcast the message to other clients
        broadcast_message(client_id + ": " + message, client_fd);
    }

    // --- Cleanup for this client ---
    close(client_fd); // Close the socket

    // Remove the client socket from the shared list (thread-safe)
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        // std::remove shifts elements, doesn't resize. erase removes them.
        client_sockets_.erase(
            std::remove(client_sockets_.begin(), client_sockets_.end(), client_fd),
            client_sockets_.end());
    }
    std::cout << "Cleaned up connection for " << client_id << std::endl;
    // Note: The std::thread object for this client remains in client_threads_
    // until server shutdown (stop() method). A more complex design might
    // manage thread removal here or have a dedicated cleanup thread.
}

// Broadcasts a message to all connected clients except the sender.
void ChatServer::broadcast_message(const std::string &message, int sender_fd)
{
    std::lock_guard<std::mutex> lock(clients_mutex_); // Lock for iterating clients
    for (int client_fd : client_sockets_)
    {
        if (client_fd != sender_fd)
        {
            ssize_t bytes_sent = send(client_fd, message.c_str(), message.length(), 0);
            if (bytes_sent < 0 && running_)
            {
                log_error(("Failed to send message to client " + std::to_string(client_fd)).c_str());
                // Consider marking this client for removal later if send fails repeatedly
            }
        }
    }
}

// Closes all client sockets (e.g., during shutdown)
void ChatServer::cleanup_clients()
{
    std::lock_guard<std::mutex> lock(clients_mutex_);
    std::cout << "Closing " << client_sockets_.size() << " client sockets..." << std::endl;
    for (int client_fd : client_sockets_)
    {
        shutdown(client_fd, SHUT_RDWR); // Politely signal closure
        close(client_fd);
    }
    client_sockets_.clear(); // Empty the list
}

// Joins all client handler threads (e.g., during shutdown)
void ChatServer::cleanup_threads()
{
    std::cout << "Joining " << client_threads_.size() << " client threads..." << std::endl;
    // Need to detach or join threads. Joining is generally preferred.
    // Note: Iterating and joining directly modifies the container while iterating, which is unsafe.
    //       It's safer to move threads out or iterate carefully.
    //       Simplest here: assume stop() already cleaned sockets, threads should exit.
    for (std::thread &t : client_threads_)
    {
        if (t.joinable())
        {
            t.join();
        }
    }
    client_threads_.clear(); // Clear the list after joining
}

// Helper to log socket errors.
void ChatServer::log_error(const char *message)
{
    // Check errno only if message is not nullptr to avoid hiding other errors
    if (message)
    {
        std::cerr << "Error: " << message << " - " << strerror(errno) << std::endl;
    }
    else
    {
        std::cerr << "Unknown error occurred." << std::endl;
    }
}
