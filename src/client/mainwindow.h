#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QPushButton>
#include <QProgressBar>
#include <QTextEdit>
#include <QLabel>
#include <QVBoxLayout>

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

private slots:
    void onUploadClicked();
    void onClearClicked();

private:
    void setupUI();

    QWidget* centralWidget;
    QVBoxLayout* mainLayout;
    QPushButton* uploadButton;
    QPushButton* clearButton;
    QProgressBar* progressBar;
    QTextEdit* resultsDisplay;
    QLabel* statusLabel;
};

#endif // MAINWINDOW_H