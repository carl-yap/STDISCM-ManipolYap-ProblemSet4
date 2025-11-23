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


};