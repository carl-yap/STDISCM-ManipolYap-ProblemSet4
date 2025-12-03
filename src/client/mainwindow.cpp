#include "mainwindow.h"
#include <QFileDialog>
#include <QMessageBox>
#include <QImageReader>
#include <QBuffer>
#include <QLabel>
#include <QHBoxLayout>
#include <QListWidgetItem>
#include <QTimer>
#include <QFileInfo>

// OCRClientWorker implementation
OCRClientWorker::OCRClientWorker(std::shared_ptr<grpc::Channel> channel)
    : stub_(ocrservice::OCRService::NewStub(channel)), shutdown_(false)
{
}

OCRClientWorker::~OCRClientWorker() {
    shutdown_ = true;
}

void OCRClientWorker::processImage(int requestId, const QImage& image, const QString& filePath) {
    if (shutdown_) return;

    try {
        // Convert QImage to byte array
        QByteArray imageBytes;
        QBuffer buffer(&imageBytes);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "PNG");

        // Prepare gRPC request
        ocrservice::OCRRequest request;
        request.set_request_id(requestId);
        request.set_image_data(imageBytes.constData(), imageBytes.size());

        // Send request
        grpc::ClientContext context;
        ocrservice::OCRResponse response;

        grpc::Status status = stub_->ProcessImage(&context, request, &response);

        if (status.ok()) {
            emit resultReady(requestId,
                QString::fromStdString(response.text()),
                response.success(),
                QString::fromStdString(response.error_message()));
        }
        else {
            emit resultReady(requestId, "", false,
                QString("gRPC error: %1").arg(status.error_message().c_str()));
        }
    }
    catch (const std::exception& e) {
        emit resultReady(requestId, "", false, QString("Exception: %1").arg(e.what()));
    }
}

// MainWindow implementation
MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent), completedCount_(0), nextRequestId_(1), totalInCurrentBatch_(0)
{
    // Setup gRPC channel
    channel_ = grpc::CreateChannel("localhost:50051", grpc::InsecureChannelCredentials());
    stub_ = ocrservice::OCRService::NewStub(channel_);

    // Setup worker thread
    workerThread_ = new QThread(this);
    worker_ = new OCRClientWorker(channel_);
    worker_->moveToThread(workerThread_);

    connect(worker_, &OCRClientWorker::resultReady,
        this, &MainWindow::onOCRResultReady);
    connect(workerThread_, &QThread::finished, worker_, &QObject::deleteLater);

    workerThread_->start();

    setupUI();
}

MainWindow::~MainWindow() {
    workerThread_->quit();
    workerThread_->wait();
}

void MainWindow::setupUI()
{
    centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    mainLayout = new QVBoxLayout(centralWidget);

    // Create UI elements
    uploadButton = new QPushButton("Upload Images", this);
    clearButton = new QPushButton("Clear Results", this);
    progressBar = new QProgressBar(this);
    resultsDisplay = new QTextEdit(this);
    statusLabel = new QLabel("Ready to upload images", this);
    fileListWidget = new QListWidget(this);

    // Setup progress bar
    progressBar->setRange(0, 100);
    progressBar->setValue(0);
    progressBar->setTextVisible(true);

    // Setup list widget for better thumbnail display
    fileListWidget->setViewMode(QListWidget::IconMode);
    fileListWidget->setIconSize(QSize(140, 140));
    fileListWidget->setResizeMode(QListWidget::Adjust);
    fileListWidget->setSpacing(10);
    fileListWidget->setMovement(QListWidget::Static);
    fileListWidget->setUniformItemSizes(false);

    // Add to layout
    mainLayout->addWidget(statusLabel);
    mainLayout->addWidget(uploadButton);
    mainLayout->addWidget(clearButton);
    mainLayout->addWidget(progressBar);
    mainLayout->addWidget(new QLabel("Processing Queue:", this));
    mainLayout->addWidget(fileListWidget);
    mainLayout->addWidget(new QLabel("OCR Results:", this));
    mainLayout->addWidget(resultsDisplay);

    // Connect signals
    connect(uploadButton, &QPushButton::clicked, this, &MainWindow::onUploadClicked);
    connect(clearButton, &QPushButton::clicked, this, &MainWindow::onClearClicked);

    setWindowTitle("Distributed OCR Client");
    resize(800, 600);
}

QWidget* MainWindow::createThumbnailWidget(const QString& fileName, const QString& status, const QString& ocrText)
{
    QWidget* widget = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(5, 5, 5, 5);

    // Create thumbnail - either OCR text preview or placeholder
    QLabel* thumbnailLabel = new QLabel();
    thumbnailLabel->setFixedSize(120, 120);
    thumbnailLabel->setAlignment(Qt::AlignCenter);
    thumbnailLabel->setWordWrap(true);
    thumbnailLabel->setStyleSheet(
        "QLabel { "
        "background-color: white; "
        "border: 2px solid #cccccc; "
        "border-radius: 5px; "
        "padding: 5px; "
        "font-size: 9px; "
        "}"
    );

    if (ocrText.isEmpty()) {
        // Show processing placeholder
        thumbnailLabel->setText("Processing...");
        thumbnailLabel->setStyleSheet(
            "QLabel { "
            "background-color: #f0f0f0; "
            "border: 2px solid #cccccc; "
            "border-radius: 5px; "
            "color: #666666; "
            "font-size: 10px; "
            "}"
        );
    }
    else {
        // Show OCR text preview (first 150 characters)
        QString preview = ocrText.left(150);
        if (ocrText.length() > 150) {
            preview += "...";
        }
        thumbnailLabel->setText(preview);
    }

    // Create info label with filename and status
    QLabel* infoLabel = new QLabel(QString("<b>%1</b><br>%2").arg(fileName).arg(status));
    infoLabel->setAlignment(Qt::AlignCenter);
    infoLabel->setWordWrap(true);
    infoLabel->setMaximumWidth(140);
    infoLabel->setStyleSheet("font-size: 9px;");

    layout->addWidget(thumbnailLabel);
    layout->addWidget(infoLabel);

    return widget;
}

void MainWindow::updateThumbnailWithResult(int index, const QString& status, const QString& ocrText)
{
    if (index >= 0 && index < fileListWidget->count()) {
        QListWidgetItem* item = fileListWidget->item(index);
        if (item) {
            QMutexLocker locker(&batchMutex_);
            if (index < currentBatch_.size()) {
                QString fileName = QFileInfo(currentBatch_[index].filePath).fileName();
                QWidget* widget = createThumbnailWidget(fileName, status, ocrText);
                item->setSizeHint(QSize(160, 180));
                fileListWidget->setItemWidget(item, widget);
            }
        }
    }
}

void MainWindow::onUploadClicked()
{
    QStringList files = QFileDialog::getOpenFileNames(
        this,
        "Select Images",
        "",
        "Image Files (*.png *.jpg *.jpeg *.bmp)"
    );

    if (!files.isEmpty()) {
        // Check if we need to start a new batch
        if (progressBar->value() == 100 || totalInCurrentBatch_ == 0) {
            startNewBatch();
        }

        // Add files to current batch
        for (const QString& filePath : files) {
            QImage image(filePath);
            if (!image.isNull()) {
                ImageTask task;
                task.requestId = nextRequestId_++;
                task.filePath = filePath;
                task.image = image;
                task.completed = false;
                task.result = "Processing...";

                QMutexLocker locker(&batchMutex_);
                currentBatch_.append(task);
                totalInCurrentBatch_++;

                // Add thumbnail to list widget
                QListWidgetItem* item = new QListWidgetItem();
                item->setSizeHint(QSize(160, 180));
                fileListWidget->addItem(item);

                // Create and set thumbnail widget (initially with no OCR text)
                QString fileName = QFileInfo(filePath).fileName();
                QWidget* widget = createThumbnailWidget(fileName, "Processing...", "");
                fileListWidget->setItemWidget(item, widget);

                // Start processing this image
                QMetaObject::invokeMethod(worker_, "processImage",
                    Qt::QueuedConnection,
                    Q_ARG(int, task.requestId),
                    Q_ARG(QImage, image),
                    Q_ARG(QString, filePath));
            }
        }

        statusLabel->setText(QString("Processing %1 images in current batch").arg(totalInCurrentBatch_));
        progressBar->setValue(0);
    }
}

void MainWindow::onClearClicked()
{
    resultsDisplay->clear();
    fileListWidget->clear();
    progressBar->setValue(0);
    statusLabel->setText("Results cleared");

    QMutexLocker locker(&batchMutex_);
    currentBatch_.clear();
    completedCount_ = 0;
    totalInCurrentBatch_ = 0;
}

void MainWindow::onOCRResultReady(int requestId, const QString& text, bool success, const QString& error)
{
    QMutexLocker locker(&batchMutex_);

    // Find the task and update it
    for (int i = 0; i < currentBatch_.size(); ++i) {
        if (currentBatch_[i].requestId == requestId && !currentBatch_[i].completed) {
            currentBatch_[i].completed = true;
            currentBatch_[i].result = success ? text : ("Error: " + error);
            completedCount_++;

            // Update thumbnail with OCR result
            QString status = success ? "✓ Completed" : "✗ Failed";
            QString displayText = success ? text : "";
            updateThumbnailWithResult(i, status, displayText);

            // Update results display immediately
            QString resultEntry = QString("\n=== Image: %1 ===\n")
                .arg(QFileInfo(currentBatch_[i].filePath).fileName());
            if (success) {
                resultEntry += text + "\n";
            }
            else {
                resultEntry += "ERROR: " + error + "\n";
            }

            resultsDisplay->append(resultEntry);

            break;
        }
    }

    // Update progress
    onProgressUpdated();
}

void MainWindow::onProgressUpdated()
{
    if (totalInCurrentBatch_ > 0) {
        int progress = (completedCount_ * 100) / totalInCurrentBatch_;
        progressBar->setValue(progress);

        statusLabel->setText(
            QString("Processed %1/%2 images (%3%)")
            .arg(completedCount_.load())
            .arg(totalInCurrentBatch_)
            .arg(progress));

        // Scroll to bottom of results
        QTextCursor cursor = resultsDisplay->textCursor();
        cursor.movePosition(QTextCursor::End);
        resultsDisplay->setTextCursor(cursor);
    }
}

void MainWindow::startNewBatch()
{
    QMutexLocker locker(&batchMutex_);

    if (progressBar->value() == 100) {
        // Clear previous results if we're starting a new batch after completion
        resultsDisplay->clear();
        fileListWidget->clear();
    }

    currentBatch_.clear();
    completedCount_ = 0;
    totalInCurrentBatch_ = 0;
    progressBar->setValue(0);
}