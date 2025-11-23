#include "ocr_service.h"
#include <iostream>

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
			} else {
				continue;
			}
		}

		// process task
		std::vector<unsigned char> image_data(
			task.request.image_data().begin(),
			task.request.image_data().end());
		auto result = processors[thread_id]->processImage(image_data);

		// send back response
		if (task.response) {
			std::lock_guard<std::mutex> lock(*task.response_mutex);
			task.response->set_text(result.text);
			task.response->set_request_id(task.request.request_id());
			task.response->set_success(result.success);
			task.response->set_error_message(result.error_msg);
			*task.done = true;
		}

		if (task.cv) {
			task.cv->notify_one();
		}
	}

	std::cout << "Worker thread " << thread_id << " exited." << std::endl;
}

grpc::Status OCRService::ProcessImage(
	grpc::ServerContext* context,
	const ocrservice::OCRRequest* request,
	ocrservice::OCRResponse* response) {

	std::mutex responseMutex;
	std::condition_variable cv;
	bool completed = false;

	Task task{ *request, response, &responseMutex, &cv, &completed };

	{
		std::lock_guard<std::mutex> lock(queue_mutex);
		task_queue.push(task);
	}
	queue_cv.notify_one();

	// wait for completion
	std::unique_lock<std::mutex> lock(responseMutex);
	cv.wait(lock, [&completed] { return completed; });

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
		std::mutex responseMutex;
		std::condition_variable cv;
		bool completed = false;

		Task task{ request, response.get(), &responseMutex, &cv, &completed };

		{
			std::lock_guard<std::mutex> lock(queue_mutex);
			task_queue.push(task);
		}
		queue_cv.notify_one();

		
		{ // wait for completion 
			std::unique_lock<std::mutex> lock(responseMutex);
			cv.wait(lock, [&completed] { return completed; });
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