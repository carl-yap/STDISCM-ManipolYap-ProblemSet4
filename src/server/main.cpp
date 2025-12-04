#include "ocr_service.h"
#include <grpcpp/grpcpp.h>
#include <iostream>
#include <csignal>

std::unique_ptr<grpc::Server> server;

void signalHandler(int signum) {
    std::cout << "\nShutting down server..." << std::endl;
    if (server) {
        server->Shutdown();
    }
}

int main(int argc, char* argv[]) {
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    std::string server_address("10.98.53.240:50051");
    OCRService service(4); // 4 worker threads

    grpc::ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);

    server = builder.BuildAndStart();
    std::cout << "OCR Server listening on " << server_address << std::endl;

    server->Wait();

    return 0;
}