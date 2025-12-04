#pragma once

#include "ocr_service.grpc.pb.h"
#include "ocr_processor.h"
#include <grpcpp/grpcpp.h>
#include <memory>
#include <vector>
#include <thread>
#include <queue>
#include <map>
#include <condition_variable>
#include <mutex>

class OCRService final : public ocrservice::OCRService::Service {
public:
    OCRService(int n_threads = 4);
    ~OCRService();

    grpc::Status ProcessImage(
        grpc::ServerContext* context,
        const ocrservice::OCRRequest* request,
        ocrservice::OCRResponse* response) override;

private:
    struct ImageTask {
        int request_id;
        std::vector<unsigned char> image_data;
    };

    struct TaskResult {
        int request_id;
        std::string text;
        bool success;
        std::string error_message;
        bool completed = false;
    };

    void workerThread(int thread_id);

    std::vector<std::thread> workers_;
    std::queue<ImageTask> task_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;

    std::map<int, TaskResult> results_;
    std::mutex results_mutex_;
    std::condition_variable results_cv_;

    bool shutdown_;
    std::vector<std::unique_ptr<OCRProcessor>> processors_;
};