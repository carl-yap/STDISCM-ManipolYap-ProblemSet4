#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QPushButton>
#include <QProgressBar>
#include <QTextEdit>
#include <QLabel>
#include <QVBoxLayout>
#include <QListWidget>
#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <atomic>
#include <memory>

#include "ocr_service.grpc.pb.h"
#include <grpcpp/grpcpp.h>

class OCRClientWorker;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

private slots:
    void onUploadClicked();
    void onClearClicked();
    void onOCRResultReady(int requestId, const QString& text, bool success, const QString& error);
    void onProgressUpdated();

private:
    void setupUI();
    void processNextImage();
    void startNewBatch();
    QWidget* createThumbnailWidget(const QImage& image, const QString& fileName, const QString& status);
    void updateThumbnailStatus(int index, const QString& status);

    struct ImageTask {
        int requestId;
        QString filePath;
        QImage image;
        bool completed;
        QString result;
    };

    QWidget* centralWidget;
    QVBoxLayout* mainLayout;
    QPushButton* uploadButton;
    QPushButton* clearButton;
    QProgressBar* progressBar;
    QTextEdit* resultsDisplay;
    QLabel* statusLabel;
    QListWidget* fileListWidget;

    std::unique_ptr<ocrservice::OCRService::Stub> stub_;
    std::shared_ptr<grpc::Channel> channel_;

    QList<ImageTask> currentBatch_;
    std::atomic<int> completedCount_;
    std::atomic<int> nextRequestId_;
    int totalInCurrentBatch_;

    QMutex batchMutex_;
    QThread* workerThread_;
    OCRClientWorker* worker_;
};

class OCRClientWorker : public QObject
{
    Q_OBJECT

public:
    OCRClientWorker(std::shared_ptr<grpc::Channel> channel);
    ~OCRClientWorker();

public slots:
    void processImage(int requestId, const QImage& image, const QString& filePath);

signals:
    void resultReady(int requestId, const QString& text, bool success, const QString& error);

private:
    std::unique_ptr<ocrservice::OCRService::Stub> stub_;
    std::atomic<bool> shutdown_;
};

#endif // MAINWINDOW_H