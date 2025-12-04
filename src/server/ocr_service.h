#pragma once

#include "ocr_service.grpc.pb.h"
#include "ocr_processor.h"
#include <grpcpp/grpcpp.h>
#include <memory>

#include <vector>
#include <thread>
#include <queue>
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

	grpc::Status ProcessImageStream(
		grpc::ServerContext* context,
		grpc::ServerReaderWriter<ocrservice::OCRResponse,
		ocrservice::OCRRequest>* stream) override;

private:
	struct Task {
		ocrservice::OCRRequest request;
		ocrservice::OCRResponse* response;
		std::shared_ptr<std::mutex> response_mutex;  // Changed to shared_ptr
		std::shared_ptr<std::condition_variable> cv;  // Changed to shared_ptr
		std::shared_ptr<bool> done;  // Changed to shared_ptr
	};

	void workerThread();
	void processTask(const Task& task);

	std::vector<std::thread> workers;
	std::queue<Task> task_queue;
	std::mutex queue_mutex;
	std::condition_variable queue_cv;
	bool shutdown;

	std::vector<std::unique_ptr<OCRProcessor>> processors;
};