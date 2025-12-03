#include "ocr_processor.h"
#include <leptonica/allheaders.h>
#include <iostream>

OCRProcessor::OCRProcessor() {
	tess_api = std::make_unique<tesseract::TessBaseAPI>();
	// Initialize Tesseract with English language
	if (!initializeTesseract("eng_fast")) {
		std::cerr << "Failed to initialize Tesseract!" << std::endl;
	}
}

OCRProcessor::~OCRProcessor() {
	if (tess_api) {
		tess_api->End();
	}
}

bool OCRProcessor::initializeTesseract(const std::string& lang) {
	if (tess_api->Init(nullptr, lang.c_str())) {
		std::cerr << "Could not initialize tesseract with language: " << lang << std::endl;
		return false;
	}
	return true;
}

OCRProcessor::Result OCRProcessor::processImage(const std::vector<unsigned char>& image_data) {
	const std::lock_guard<std::mutex> lock(mutex);

	Result result;
	result.success = false;

	Pix* image = pixReadMem(image_data.data(), static_cast<size_t>(image_data.size()));
	if (!image) {
		result.error_msg = "Failed to read image from memory.";
		return result;
	}

	// Preprocess and inference
	image = pixConvertTo8(image, false);
	image = pixOpenGray(image, 3, 3);
	image = pixCloseGray(image, 3, 3);
	tess_api->SetImage(image);
	char* text = tess_api->GetUTF8Text();
	if (text) {
		result.text = std::string(text);
		result.success = true;
		delete[] text;
	}
	else {
		result.error_msg = "Tesseract failed to extract text.";
	}

	pixDestroy(&image); // cleanup
	return result;
}