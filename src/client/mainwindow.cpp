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
    : stub_(ocrservice::OCRService::NewStub(channel)), shutdown_(false), deadlineEnabled_(false)
{
}

OCRClientWorker::~OCRClientWorker() {
    shutdown_ = true;
}

void OCRClientWorker::processImage(int requestId, const QImage& image, const QString& filePath) {
    if (shutdown_) return;

    qDebug() << "[Client] Processing image. Request ID:" << requestId
        << "File:" << filePath
        << "Image size:" << image.size()
        << "Deadline enabled:" << deadlineEnabled_.load();

    try {
        // Convert QImage to byte array
        QByteArray imageBytes;
        QBuffer buffer(&imageBytes);
        buffer.open(QIODevice::WriteOnly);
        bool saved = image.save(&buffer, "PNG");

        if (!saved) {
            qDebug() << "[Client] Failed to convert image to PNG format";
            emit resultReady(requestId, "", false, "Failed to convert image to PNG");
            return;
        }

        qDebug() << "[Client] Image converted to PNG. Size:" << imageBytes.size() << "bytes";

        // Prepare gRPC request
        ocrservice::OCRRequest request;
        request.set_image_data(imageBytes.constData(), imageBytes.size());
        request.set_request_id(requestId);

        qDebug() << "[Client] Sending gRPC request. Request ID:" << request.request_id()
            << "Image data size:" << request.image_data().size() << "bytes";

        // Send request with optional deadline
        grpc::ClientContext context;

        // Set a deadline
        if (deadlineEnabled_) {
            auto deadline = std::chrono::system_clock::now() + std::chrono::milliseconds(500);
            context.set_deadline(deadline);
            qDebug() << "[Client] Deadline set to 500ms for request" << requestId;
        }

        ocrservice::OCRResponse response;

        auto start_time = std::chrono::high_resolution_clock::now();
        grpc::Status status = stub_->ProcessImage(&context, request, &response);
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

        qDebug() << "[Client] Received response in" << duration.count() << "ms";

        if (status.ok()) {
            qDebug() << "[Client] Response request_id:" << response.request_id();
            qDebug() << "[Client] Response success:" << response.success();
            qDebug() << "[Client] Response text length:" << response.text().length();
            qDebug() << "[Client] Request" << requestId << "successful. "
                << "Text length:" << response.text().length()
                << "Success:" << response.success();

            emit resultReady(requestId,
                QString::fromStdString(response.text()),
                response.success(),
                QString::fromStdString(response.error_message()));
        }
        else {
            // Check if it's a deadline exceeded error
            if (status.error_code() == grpc::StatusCode::DEADLINE_EXCEEDED) {
                qDebug() << "[Client] Deadline exceeded for request" << requestId;
                emit resultReady(requestId, "", false, "Deadline");
            }
            else {
                qDebug() << "[Client] gRPC error for request" << requestId
                    << ":" << QString::fromStdString(status.error_message());
                emit resultReady(requestId, "", false,
                    QString("gRPC error: %1").arg(status.error_message().c_str()));
            }
        }
    }
    catch (const std::exception& e) {
        qDebug() << "[Client] Exception for request" << requestId << ":" << e.what();
        emit resultReady(requestId, "", false, QString("Exception: %1").arg(e.what()));
    }
}

// MainWindow implementation
MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent), completedCount_(0), nextRequestId_(1), totalInCurrentBatch_(0), deadlineEnabled_(false)
{
    // Setup gRPC channel
    channel_ = grpc::CreateChannel("10.98.53.240:50051", grpc::InsecureChannelCredentials());
    stub_ = ocrservice::OCRService::NewStub(channel_);

    // Setup worker thread
    workerThread_ = new QThread(this);
    worker_ = new OCRClientWorker(channel_);
    worker_->moveToThread(workerThread_);

    connect(worker_, &OCRClientWorker::resultReady,
        this, &MainWindow::onOCRResultReady, Qt::QueuedConnection);

    qDebug() << "[Client] Signal-slot connection established";
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

    // Create button row
    QHBoxLayout* buttonLayout = new QHBoxLayout();

    // Create UI elements
    uploadButton = new QPushButton("Upload Images", this);
    clearButton = new QPushButton("Clear Results", this);
    deadlineButton = new QPushButton("Deadline: OFF", this);
    deadlineButton->setCheckable(true);
    deadlineButton->setStyleSheet(
        "QPushButton { background-color: #4CAF50; color: white; padding: 5px; }"
        "QPushButton:checked { background-color: #f44336; }"
    );

    buttonLayout->addWidget(uploadButton);
    buttonLayout->addWidget(clearButton);
    buttonLayout->addWidget(deadlineButton);

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
    fileListWidget->setIconSize(QSize(120, 120));
    fileListWidget->setResizeMode(QListWidget::Adjust);
    fileListWidget->setSpacing(10);
    fileListWidget->setMovement(QListWidget::Static);

    // Add to layout
    mainLayout->addWidget(statusLabel);
    mainLayout->addLayout(buttonLayout);
    mainLayout->addWidget(progressBar);
    mainLayout->addWidget(new QLabel("Processing Queue:", this));
    mainLayout->addWidget(fileListWidget);
    mainLayout->addWidget(new QLabel("OCR Results:", this));
    mainLayout->addWidget(resultsDisplay);

    // Connect signals
    connect(uploadButton, &QPushButton::clicked, this, &MainWindow::onUploadClicked);
    connect(clearButton, &QPushButton::clicked, this, &MainWindow::onClearClicked);
    connect(deadlineButton, &QPushButton::clicked, this, &MainWindow::onDeadlineToggled);

    setWindowTitle("Distributed OCR Client");
    resize(800, 600);
}

void MainWindow::onDeadlineToggled()
{
    deadlineEnabled_ = deadlineButton->isChecked();

    if (deadlineEnabled_) {
        deadlineButton->setText("Deadline: ON");
        statusLabel->setText("Deadline mode enabled (100ms timeout)");
    }
    else {
        deadlineButton->setText("Deadline: OFF");
        statusLabel->setText("Ready to upload images");
    }

    worker_->setDeadlineEnabled(deadlineEnabled_);
    qDebug() << "[Client] Deadline mode:" << (deadlineEnabled_ ? "ENABLED" : "DISABLED");
}

QWidget* MainWindow::createThumbnailWidget(const QImage& image, const QString& fileName, const QString& status)
{
    QWidget* widget = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(5, 5, 5, 5);

    // Create thumbnail
    QLabel* imageLabel = new QLabel();
    QPixmap pixmap = QPixmap::fromImage(image.scaled(100, 100, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    imageLabel->setPixmap(pixmap);
    imageLabel->setAlignment(Qt::AlignCenter);

    // Create text label
    QLabel* textLabel = new QLabel(QString("%1\n%2").arg(fileName).arg(status));
    textLabel->setAlignment(Qt::AlignCenter);
    textLabel->setWordWrap(true);
    textLabel->setMaximumWidth(120);

    layout->addWidget(imageLabel);
    layout->addWidget(textLabel);

    return widget;
}

void MainWindow::updateThumbnailStatus(int index, const QString& status)
{
    if (index >= 0 && index < fileListWidget->count()) {
        QListWidgetItem* item = fileListWidget->item(index);
        if (item) {
            QMutexLocker locker(&batchMutex_);
            if (index < currentBatch_.size()) {
                QString fileName = QFileInfo(currentBatch_[index].filePath).fileName();
                QWidget* widget = createThumbnailWidget(
                    currentBatch_[index].image,
                    fileName,
                    status
                );
                item->setSizeHint(QSize(140, 160));
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

    qDebug() << "[Client] Selected" << files.size() << "files";

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
                task.requestId = nextRequestId_;
                task.filePath = filePath;
                task.image = image;
                task.completed = false;
                task.result = "Processing...";

                qDebug() << "[Client] Creating task for file:" << filePath
                    << "Request ID:" << task.requestId;

                QMutexLocker locker(&batchMutex_);
                currentBatch_.append(task);
                totalInCurrentBatch_++;

                // Add thumbnail to list widget
                QListWidgetItem* item = new QListWidgetItem();
                item->setSizeHint(QSize(140, 160));
                fileListWidget->addItem(item);

                // Create and set thumbnail widget
                QString fileName = QFileInfo(filePath).fileName();
                QWidget* widget = createThumbnailWidget(image, fileName, "Processing...");
                fileListWidget->setItemWidget(item, widget);

                // Start processing this image
                qDebug() << "[Client] Invoking worker for request ID:" << task.requestId;
                QMetaObject::invokeMethod(worker_, "processImage",
                    Qt::QueuedConnection,
                    Q_ARG(int, task.requestId),
                    Q_ARG(QImage, image),
                    Q_ARG(QString, filePath));

                nextRequestId_++;
            }
            else {
                qDebug() << "[Client] Failed to load image:" << filePath;
            }
        }

        QString deadlineStatus = deadlineEnabled_ ? " (Deadline mode ON)" : "";
        statusLabel->setText(QString("Processing %1 images in current batch%2").arg(totalInCurrentBatch_).arg(deadlineStatus));
        progressBar->setValue(0);

        qDebug() << "[Client] Current batch size:" << totalInCurrentBatch_;
    }
}

void MainWindow::onClearClicked()
{
    resultsDisplay->clear();
    fileListWidget->clear();
    progressBar->setValue(0);

    QString deadlineStatus = deadlineEnabled_ ? " (Deadline mode ON)" : "";
    statusLabel->setText(QString("Results cleared%1").arg(deadlineStatus));

    QMutexLocker locker(&batchMutex_);
    currentBatch_.clear();
    completedCount_ = 0;
    totalInCurrentBatch_ = 0;
}

void MainWindow::onOCRResultReady(int requestId, const QString& text, bool success, const QString& error)
{
    qDebug() << "[Client] Received result for Request ID:" << requestId
        << "Success:" << success
        << "Error:" << error;

    QString resultEntry;
    QString status;
    int taskIndex = -1;

    // Minimize the locked section - only access shared data
    {
        QMutexLocker locker(&batchMutex_);

        // Find the task and update it
        for (int i = 0; i < currentBatch_.size(); ++i) {
            if (currentBatch_[i].requestId == requestId && !currentBatch_[i].completed) {
                currentBatch_[i].completed = true;
                currentBatch_[i].result = success ? text : ("Error: " + error);
                completedCount_++;
                taskIndex = i;

                // Prepare data while we have the lock
                resultEntry = QString("\n=== Image: %1 ===\n")
                    .arg(QFileInfo(currentBatch_[i].filePath).fileName());

                if (success) {
                    resultEntry += text + "\n";
                    qDebug() << "[Client] OCR Text extracted:" << text.left(50) << "...";
                    status = "✓ Completed";
                }
                else {
                    // Check if it's a deadline error
                    if (error == "Deadline") {
                        resultEntry += "[Error: Deadline]\n";
                        qDebug() << "[Client] Deadline exceeded";
                        status = "⏱ Deadline";
                    }
                    else {
                        resultEntry += "ERROR: " + error + "\n";
                        qDebug() << "[Client] OCR Error:" << error;
                        status = "✗ Failed";
                    }
                }
                break;
            }
        }
    } // Lock is released here

    // Now do UI updates outside the lock
    if (taskIndex != -1) {
        updateThumbnailStatus(taskIndex, status);
        resultsDisplay->append(resultEntry);

        // Scroll to bottom of results
        QTextCursor cursor = resultsDisplay->textCursor();
        cursor.movePosition(QTextCursor::End);
        resultsDisplay->setTextCursor(cursor);
    }

    // Update progress
    onProgressUpdated();
}

void MainWindow::onProgressUpdated()
{
    if (totalInCurrentBatch_ > 0) {
        int progress = (completedCount_ * 100) / totalInCurrentBatch_;
        progressBar->setValue(progress);

        QString deadlineStatus = deadlineEnabled_ ? " (Deadline mode ON)" : "";
        statusLabel->setText(
            QString("Processed %1/%2 images (%3%%)%4")
            .arg(completedCount_.load())
            .arg(totalInCurrentBatch_)
            .arg(progress)
            .arg(deadlineStatus));

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