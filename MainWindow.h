#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QPainterPath>
#include <QPointF>
#include <QPixmap>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

class QLabel;
class QPushButton;
class QStackedWidget;
class QMediaPlayer;
class QAudioOutput;
class QVideoWidget;
class QProcess;

struct AlphabetItem
{
    QString upper;
    QString lower;
    QString word1;
    QString word2;
    QString visual1;
    QString visual2;
    QString phrase1;
    QString phrase2;
    QString videoFile;
    QString color1;
    QString color2;
};

class TracingCanvas : public QWidget
{
public:
    explicit TracingCanvas(QWidget *parent = nullptr);

    void setTemplateImage(const QPixmap &pixmap);
    void clearDrawing();
    double accuracyPercent() const;

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    QPixmap templatePixmap;
    QPainterPath path;
    QVector<QPointF> userPoints;
    QPointF lastPoint;
    bool drawing = false;

    QRect imageRectInWidget() const;
    QVector<QPointF> targetPoints() const;
};

class MainWindow : public QMainWindow
{
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private:
    QVector<AlphabetItem> items;
    int currentIndex = 0;
    bool tracingUppercase = true;

    QStackedWidget *stack = nullptr;

    QWidget *welcomePage = nullptr;
    QWidget *learnPage = nullptr;
    QWidget *tracePage = nullptr;
    QWidget *videoPage = nullptr;
    QWidget *songPage = nullptr;

    QLabel *learnLetter = nullptr;
    QLabel *learnProgress = nullptr;
    QLabel *nounVisual1 = nullptr;
    QLabel *nounVisual2 = nullptr;
    QLabel *nounText1 = nullptr;
    QLabel *nounText2 = nullptr;

    QLabel *traceTitle = nullptr;
    QLabel *traceInfo = nullptr;
    QLabel *traceResult = nullptr;
    TracingCanvas *traceCanvas = nullptr;
    QPushButton *traceCaseButton = nullptr;

    QLabel *videoTitle = nullptr;
    QLabel *videoMessage = nullptr;
    QMediaPlayer *nounPlayer = nullptr;
    QAudioOutput *nounAudio = nullptr;
    QVideoWidget *nounVideoWidget = nullptr;

    QLabel *songMessage = nullptr;
    QMediaPlayer *songPlayer = nullptr;
    QAudioOutput *songAudio = nullptr;
    QVideoWidget *songVideoWidget = nullptr;

    QProcess *voiceProcess = nullptr;
    bool voicePlaying = false;

    QWidget *makeWelcomePage();
    QWidget *makeLearnPage();
    QWidget *makeTracePage();
    QWidget *makeVideoPage();
    QWidget *makeSongPage();

    QLabel *label(const QString &text, int size, const QString &color, bool bold = true);
    QPushButton *button(const QString &text, const QString &color, const QString &hover);
    QWidget *card(QWidget *content);
    void applyBackground(QWidget *page, const QString &c1, const QString &c2);
    void fillData();

    void showWelcome();
    void showLearning();
    void showSong();
    void showTraceForCurrent();
    void showVideoForCurrent();

    void nextAlphabet();
    void previousAlphabet();

    void updateLearnPage();
    void updateTracePage(bool resetCanvas = true);
    void updateVideoPage();

    void speakCurrentWords();

    void startNounVideo();
    void pauseNounVideo();
    void resumeNounVideo();

    void startSongVideo();
    void pauseSongVideo();
    void resumeSongVideo();

    void resetTracing();
    void checkTracing();
    void showTracingProgressDialog();
    void retraceSpecificAlphabet();

    QString projectRootPath() const;
    QString tracingRootPath() const;
    QString currentTracingImagePath() const;

    QString currentNounVideoPath() const;
    QString songVideoPath() const;
    QString copyResourceVideoToTempFile(const QString &resourcePath) const;

    QString tracingKey() const;
    QString speechText() const;

    void saveLastIndex();
    void loadLastIndex();

    double savedBestAccuracy(const QString &key) const;
    int savedAttemptCount(const QString &key) const;
    double savedLastAccuracy(const QString &key) const;
    QStringList savedHistory(const QString &key) const;
    void saveTracingAttempt(const QString &key, double accuracy);
    void clearAllTracingProgress();

    void setPlayerSourceIfExists(QMediaPlayer *player,
                                 QLabel *message,
                                 const QString &path,
                                 const QString &missingText);
};

#endif