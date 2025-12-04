#include "ocr_service.h"
#include <iostream>
#include <chrono>

OCRService::OCRService(int n_threads) : shutdown(false) {
    // make one processor per thread
    for (int i = 0; i < n_threads; ++i) {
        processors.emplace_back(std::make_unique<OCRProcessor>());
    }

    for (int i = 0; i < n_threads; ++i) {
        workers.emplace_back(&OCRService::workerThread, this);
    }

    std::cout << "OCRService started with " << n_threads << " threads." << std::endl;
}

OCRService::~OCRService() {
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        shutdown = true;
    }
    queue_cv.notify_all();

    for (auto& worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    std::cout << "OCRService shut down." << std::endl;
}

void OCRService::workerThread() {
    size_t thread_id = 0;
    { // assign unique thread ID
        static std::mutex id_mutex;
        static size_t next_id = 0;
        std::lock_guard<std::mutex> lock(id_mutex);
        thread_id = next_id++;
    }

    std::cout << "Worker thread " << thread_id << " started." << std::endl;

    while (true) {
        Task task;
        {
            // wait for task
            std::unique_lock<std::mutex> lock(queue_mutex);
            queue_cv.wait(lock, [this]() { return !task_queue.empty() || shutdown; });

            if (shutdown && task_queue.empty()) { break; }

            if (!task_queue.empty()) { // get task from queue
                task = std::move(task_queue.front());
                task_queue.pop();

                // DEBUG: Log received task
                std::cout << "[Server] Thread " << thread_id
                    << " received task. Request ID: " << task.request.request_id()
                    << ", Image size: " << task.request.image_data().size() << " bytes"
                    << std::endl;
            }
            else {
                continue;
            }
        }

        // DEBUG: Start timing
        auto start_time = std::chrono::high_resolution_clock::now();

        // process task
        std::vector<unsigned char> image_data(
            task.request.image_data().begin(),
            task.request.image_data().end());

        // DEBUG: Log before processing
        std::cout << "[Server] Thread " << thread_id
            << " processing image. Size: " << image_data.size() << " bytes"
            << std::endl;

        auto result = processors[thread_id]->processImage(image_data);

        // DEBUG: End timing and log results
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

        std::cout << "[Server] Thread " << thread_id
            << " completed in " << duration.count() << "ms. "
            << "Success: " << (result.success ? "Yes" : "No")
            << ", Text length: " << result.text.length()
            << ", Error: " << result.error_msg
            << std::endl;

        // send back response
        std::cout << "[Server] Thread " << thread_id << " preparing response..." << std::endl;
        std::cout << "[Server] Checking pointers: response=" << (task.response ? "valid" : "null")
            << ", mutex=" << (task.response_mutex ? "valid" : "null")
            << ", done=" << (task.done ? "valid" : "null")
            << ", cv=" << (task.cv ? "valid" : "null") << std::endl;

        if (task.response && task.response_mutex && task.done && task.cv) {
            std::cout << "[Server] Thread " << thread_id << " acquiring lock..." << std::endl;
            {
                std::lock_guard<std::mutex> lock(*task.response_mutex);
                std::cout << "[Server] Thread " << thread_id << " lock acquired, setting response fields..." << std::endl;

                task.response->set_text(result.text);
                task.response->set_request_id(task.request.request_id());
                task.response->set_success(result.success);
                task.response->set_error_message(result.error_msg);

                std::cout << "[Server] Thread " << thread_id << " response fields set. Text: \""
                    << result.text.substr(0, std::min<size_t>(50, result.text.length())) << "\"" << std::endl;

                *task.done = true;
                std::cout << "[Server] Thread " << thread_id << " marked as done" << std::endl;
            }

            std::cout << "[Server] Thread " << thread_id << " notifying condition variable..." << std::endl;
            task.cv->notify_one();
            std::cout << "[Server] Thread " << thread_id << " notification sent!" << std::endl;
        }
        else {
            std::cerr << "[Server] Thread " << thread_id << " ERROR: Invalid pointers, cannot send response!" << std::endl;
        }
    }

    std::cout << "Worker thread " << thread_id << " exited." << std::endl;
}

grpc::Status OCRService::ProcessImage(
    grpc::ServerContext* context,
    const ocrservice::OCRRequest* request,
    ocrservice::OCRResponse* response) {

    // DEBUG: Log incoming request
    std::cout << "[Server] ProcessImage called. Request ID: " << request->request_id()
        << ", Client: " << context->peer()
        << ", Image size: " << request->image_data().size() << " bytes"
        << std::endl;

    // Use shared_ptr for synchronization objects
    auto responseMutex = std::make_shared<std::mutex>();
    auto cv = std::make_shared<std::condition_variable>();
    auto completed = std::make_shared<bool>(false);

    Task task{ *request, response, responseMutex, cv, completed };

    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        task_queue.push(task);
        // DEBUG: Log queue status
        std::cout << "[Server] Task queued. Queue size: " << task_queue.size() << std::endl;
    }
    queue_cv.notify_one();

    // wait for completion
    std::unique_lock<std::mutex> lock(*responseMutex);
    cv->wait(lock, [completed] { return *completed; });

    // DEBUG: Log completion
    std::cout << "[Server] Response sent for Request ID: " << response->request_id()
        << ", Success: " << response->success()
        << std::endl;

    return grpc::Status::OK;
}

grpc::Status OCRService::ProcessImageStream(
    grpc::ServerContext* context,
    grpc::ServerReaderWriter<ocrservice::OCRResponse,
    ocrservice::OCRRequest>* stream) {

    ocrservice::OCRRequest request;
    std::mutex streamMutex;

    while (stream->Read(&request)) {
        // create response for this request
        auto response = std::make_shared<ocrservice::OCRResponse>();
        auto responseMutex = std::make_shared<std::mutex>();
        auto cv = std::make_shared<std::condition_variable>();
        auto completed = std::make_shared<bool>(false);

        Task task{ request, response.get(), responseMutex, cv, completed };

        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            task_queue.push(task);
        }
        queue_cv.notify_one();


        { // wait for completion 
            std::unique_lock<std::mutex> lock(*responseMutex);
            cv->wait(lock, [completed] { return *completed; });
        }

        { // fail to write response
            std::lock_guard<std::mutex> lock(streamMutex);
            if (!stream->Write(*response)) {
                return grpc::Status::CANCELLED;
            }
        }
    }

    return grpc::Status::OK;
}