#include "chat_server.hpp"

int main(int argc, char* argv[]) {
    // Default port or get from command line argument
    int port = 8080;
    if (argc > 1) {
        try {
            port = std::stoi(argv[1]);
        } catch (const std::invalid_argument& e) {
            std::cerr << "Invalid port number: " << argv[1] << std::endl;
            return 1;
        } catch (const std::out_of_range& e) {
            std::cerr << "Port number out of range: " << argv[1] << std::endl;
            return 1;
        }
    }

    // Create and start the server
    ChatServer server(port);
    if (!server.start()) {
        std::cerr << "Failed to start the server." << std::endl;
        return 1;
    }

    // Keep the main thread alive.
    // You could wait for user input to stop, or handle signals (SIGINT, SIGTERM).
    std::cout << "Server started. Press Enter to stop." << std::endl;
    std::cin.get(); // Wait for user input

    // Stop the server
    server.stop();

    return 0;
}