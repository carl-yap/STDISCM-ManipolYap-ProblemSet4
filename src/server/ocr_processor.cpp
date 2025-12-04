#include "ocr_processor.h"
#include <leptonica/allheaders.h>
#include <iostream>

OCRProcessor::OCRProcessor() {
	tess_api = std::make_unique<tesseract::TessBaseAPI>();
	// Initialize Tesseract with English language
	if (!initializeTesseract("eng")) {
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
	std::cout << "[OCRProcessor] Tesseract initialized successfully with language: " << lang << std::endl;
	return true;
}

OCRProcessor::Result OCRProcessor::processImage(const std::vector<unsigned char>& image_data) {
	const std::lock_guard<std::mutex> lock(mutex);

	Result result;
	result.success = false;

	std::cout << "[OCRProcessor] Starting image processing. Data size: " << image_data.size() << " bytes" << std::endl;

	Pix* image = pixReadMem(image_data.data(), static_cast<size_t>(image_data.size()));
	if (!image) {
		result.error_msg = "Failed to read image from memory.";
		std::cerr << "[OCRProcessor] ERROR: " << result.error_msg << std::endl;
		return result;
	}

	std::cout << "[OCRProcessor] Image loaded successfully. Width: " << pixGetWidth(image)
		<< ", Height: " << pixGetHeight(image)
		<< ", Depth: " << pixGetDepth(image) << std::endl;

	// Preprocess and inference
	image = pixConvertTo8(image, false);
	std::cout << "[OCRProcessor] Converted to 8-bit grayscale" << std::endl;

	image = pixOpenGray(image, 3, 3);
	std::cout << "[OCRProcessor] Applied morphological opening" << std::endl;

	image = pixCloseGray(image, 3, 3);
	std::cout << "[OCRProcessor] Applied morphological closing" << std::endl;

	tess_api->SetImage(image);
	std::cout << "[OCRProcessor] Image set in Tesseract, starting OCR..." << std::endl;

	char* text = tess_api->GetUTF8Text();
	if (text) {
		result.text = std::string(text);
		result.success = true;
		std::cout << "[OCRProcessor] OCR SUCCESS! Extracted " << result.text.length() << " characters" << std::endl;
		std::cout << "[OCRProcessor] Text preview: \"" << result.text.substr(0, std::min<size_t>(100, result.text.length())) << "\"" << std::endl;
		//delete[] text;
	}
	else {
		result.error_msg = "Tesseract failed to extract text.";
		std::cerr << "[OCRProcessor] ERROR: " << result.error_msg << std::endl;
	}

	pixDestroy(&image); // cleanup
	std::cout << "[OCRProcessor] Processing complete. Success: " << (result.success ? "YES" : "NO") << std::endl;
	return result;
}