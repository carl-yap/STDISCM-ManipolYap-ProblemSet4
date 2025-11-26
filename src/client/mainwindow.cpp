// mainwindow.cpp
#include "mainwindow.h"
#include <QFileDialog>
#include <QMessageBox>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setupUI();
}

MainWindow::~MainWindow() {}

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

    // Setup progress bar
    progressBar->setRange(0, 100);
    progressBar->setValue(0);

    // Add to layout
    mainLayout->addWidget(statusLabel);
    mainLayout->addWidget(uploadButton);
    mainLayout->addWidget(clearButton);
    mainLayout->addWidget(progressBar);
    mainLayout->addWidget(resultsDisplay);

    // Connect signals
    connect(uploadButton, &QPushButton::clicked, this, &MainWindow::onUploadClicked);
    connect(clearButton, &QPushButton::clicked, this, &MainWindow::onClearClicked);
}

void MainWindow::onUploadClicked()
{
    // For now, just simulate file selection
    QStringList files = QFileDialog::getOpenFileNames(
        this,
        "Select Images",
        "",
        "Image Files (*.png *.jpg *.jpeg *.bmp)"
    );

    if (!files.isEmpty()) {
        statusLabel->setText(QString("Selected %1 images").arg(files.size()));
        // TODO: Add to processing queue and update progress
    }
}

void MainWindow::onClearClicked()
{
    resultsDisplay->clear();
    progressBar->setValue(0);
    statusLabel->setText("Results cleared");
}