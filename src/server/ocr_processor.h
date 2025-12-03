#pragma once

#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <tesseract/baseapi.h>

class OCRProcessor {
public:
	OCRProcessor();
	~OCRProcessor();

	struct Result {
		std::string text;
		bool success;
		std::string error_msg;
	};

	Result processImage(const std::vector<unsigned char>& image_data);

private:
	std::unique_ptr<tesseract::TessBaseAPI> tess_api;
	std::mutex mutex;

	bool initializeTesseract(const std::string& lang = "eng");
};