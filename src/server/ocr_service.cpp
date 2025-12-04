#include "ocr_service.h"
#include <iostream>
#include <chrono>
#include <thread>

OCRService::OCRService(int n_threads) : shutdown_(false) {
    // Create one processor per thread
    for (int i = 0; i < n_threads; ++i) {
        processors_.emplace_back(std::make_unique<OCRProcessor>());
    }

    // Start worker threads
    for (int i = 0; i < n_threads; ++i) {
        workers_.emplace_back(&OCRService::workerThread, this, i);
    }

    std::cout << "OCRService started with " << n_threads << " threads." << std::endl;
}

OCRService::~OCRService() {
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        shutdown_ = true;
    }
    queue_cv_.notify_all();

    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    std::cout << "OCRService shut down." << std::endl;
}

void OCRService::workerThread(int thread_id) {
    std::cout << "[Server] Worker thread " << thread_id << " started." << std::endl;

    while (true) {
        ImageTask task;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this]() { return !task_queue_.empty() || shutdown_; });

            if (shutdown_ && task_queue_.empty()) {
                break;
            }

            if (!task_queue_.empty()) {
                task = std::move(task_queue_.front());
                task_queue_.pop();

                std::cout << "[Server] Thread " << thread_id
                    << " received task. Request ID: " << task.request_id
                    << ", Image size: " << task.image_data.size() << " bytes" << std::endl;
            }
            else {
                continue;
            }
        }

        // Process the image
        auto start_time = std::chrono::high_resolution_clock::now();

        std::cout << "[Server] Thread " << thread_id << " processing image..." << std::endl;
        auto result = processors_[thread_id]->processImage(task.image_data);

        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

        std::cout << "[Server] Thread " << thread_id
            << " completed in " << duration.count() << "ms. "
            << "Success: " << (result.success ? "Yes" : "No")
            << ", Text length: " << result.text.length() << std::endl;

        // Store the result
        {
            std::lock_guard<std::mutex> lock(results_mutex_);
            TaskResult task_result;
            task_result.request_id = task.request_id;
            task_result.text = result.text;
            task_result.success = result.success;
            task_result.error_message = result.error_msg;
            task_result.completed = true;

            results_[task.request_id] = task_result;

            std::cout << "[Server] Thread " << thread_id
                << " stored result for request ID: " << task.request_id << std::endl;
        }

        // Notify that result is ready
        results_cv_.notify_all();
    }

    std::cout << "[Server] Worker thread " << thread_id << " exited." << std::endl;
}

grpc::Status OCRService::ProcessImage(
    grpc::ServerContext* context,
    const ocrservice::OCRRequest* request,
    ocrservice::OCRResponse* response) {

    int request_id = request->request_id();

    std::cout << "[Server] ProcessImage called. Request ID: " << request_id
        << ", Client: " << context->peer()
        << ", Image size: " << request->image_data().size() << " bytes" << std::endl;

    // Queue the task
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);

        ImageTask task;
        task.request_id = request_id;
        task.image_data.assign(request->image_data().begin(), request->image_data().end());

        task_queue_.push(std::move(task));

        std::cout << "[Server] Task queued. Queue size: " << task_queue_.size() << std::endl;
    }
    queue_cv_.notify_one();

    // Wait for the result
    std::cout << "[Server] Waiting for result of request ID: " << request_id << std::endl;

    {
        std::unique_lock<std::mutex> lock(results_mutex_);
        results_cv_.wait(lock, [this, request_id]() {
            return results_.find(request_id) != results_.end() && results_[request_id].completed;
            });

        // Get the result
        const TaskResult& result = results_[request_id];

        response->set_request_id(result.request_id);
        response->set_text(result.text);
        response->set_success(result.success);
        response->set_error_message(result.error_message);

        std::cout << "[Server] Response prepared for Request ID: " << request_id
            << ", Success: " << result.success
            << ", Text length: " << result.text.length() << std::endl;

        // Clean up the result
        results_.erase(request_id);
    }

    std::cout << "[Server] Response sent for Request ID: " << request_id << std::endl;

    return grpc::Status::OK;
}

grpc::Status OCRService::ProcessImageStream(
    grpc::ServerContext* context,
    grpc::ServerReaderWriter<ocrservice::OCRResponse, ocrservice::OCRRequest>* stream) {

    ocrservice::OCRRequest request;
    std::mutex streamMutex;

    while (stream->Read(&request)) {
        int request_id = request.request_id();

        // Queue the task
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);

            ImageTask task;
            task.request_id = request_id;
            task.image_data.assign(request.image_data().begin(), request.image_data().end());

            task_queue_.push(std::move(task));
        }
        queue_cv_.notify_one();

        // Wait for the result
        ocrservice::OCRResponse response;
        {
            std::unique_lock<std::mutex> lock(results_mutex_);
            results_cv_.wait(lock, [this, request_id]() {
                return results_.find(request_id) != results_.end() && results_[request_id].completed;
                });

            const TaskResult& result = results_[request_id];

            response.set_request_id(result.request_id);
            response.set_text(result.text);
            response.set_success(result.success);
            response.set_error_message(result.error_message);

            results_.erase(request_id);
        }

        // Send response
        {
            std::lock_guard<std::mutex> lock(streamMutex);
            if (!stream->Write(response)) {
                return grpc::Status::CANCELLED;
            }
        }
    }

    return grpc::Status::OK;
}