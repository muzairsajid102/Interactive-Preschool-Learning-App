#include "MainWindow.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QAudioOutput>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequence>
#include <QLabel>
#include <QLineF>
#include <QMediaPlayer>
#include <QMouseEvent>
#include <QPainter>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QUrl>
#include <QVBoxLayout>
#include <QVideoWidget>
#include <QtGlobal>

TracingCanvas::TracingCanvas(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(620, 430);
    setMouseTracking(true);
    setStyleSheet("background:white; border-radius:28px; border:4px solid #74b9ff;");
}

void TracingCanvas::setTemplateImage(const QPixmap &pixmap)
{
    templatePixmap = pixmap;
    clearDrawing();
}

void TracingCanvas::clearDrawing()
{
    path = QPainterPath();
    userPoints.clear();
    update();
}

QRect TracingCanvas::imageRectInWidget() const
{
    if (templatePixmap.isNull()) {
        return QRect();
    }

    QSize target = templatePixmap.size();
    target.scale(size() - QSize(50, 50), Qt::KeepAspectRatio);

    QRect rect(QPoint(0, 0), target);
    rect.moveCenter(this->rect().center());

    return rect;
}

QVector<QPointF> TracingCanvas::targetPoints() const
{
    QVector<QPointF> points;

    if (templatePixmap.isNull()) {
        return points;
    }

    QRect displayed = imageRectInWidget();
    QImage img = templatePixmap.toImage();

    for (int y = 0; y < img.height(); y += 5) {
        for (int x = 0; x < img.width(); x += 5) {
            QColor color = img.pixelColor(x, y);

            if (color.alpha() > 90 && color.lightness() < 185) {
                double wx = displayed.left() + (double(x) / img.width()) * displayed.width();
                double wy = displayed.top() + (double(y) / img.height()) * displayed.height();

                points.append(QPointF(wx, wy));
            }
        }
    }

    return points;
}

double TracingCanvas::accuracyPercent() const
{
    QVector<QPointF> targets = targetPoints();

    if (targets.isEmpty() || userPoints.isEmpty()) {
        return 0.0;
    }

    const double radius = 22.0;

    int coveredTargets = 0;

    for (const QPointF &target : targets) {
        bool covered = false;

        for (const QPointF &user : userPoints) {
            if (QLineF(target, user).length() <= radius) {
                covered = true;
                break;
            }
        }

        if (covered) {
            coveredTargets++;
        }
    }

    int onPathUserPoints = 0;

    for (const QPointF &user : userPoints) {
        bool onPath = false;

        for (const QPointF &target : targets) {
            if (QLineF(target, user).length() <= radius) {
                onPath = true;
                break;
            }
        }

        if (onPath) {
            onPathUserPoints++;
        }
    }

    double coverage = (double(coveredTargets) / targets.size()) * 100.0;
    double neatness = (double(onPathUserPoints) / userPoints.size()) * 100.0;

    return qBound(0.0, (coverage * 0.70) + (neatness * 0.30), 100.0);
}

void TracingCanvas::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    painter.fillRect(rect(), Qt::white);

    if (!templatePixmap.isNull()) {
        painter.setOpacity(0.62);
        painter.drawPixmap(imageRectInWidget(), templatePixmap);
        painter.setOpacity(1.0);
    }

    painter.setPen(QPen(QColor("#0984e3"), 18, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawPath(path);
}

void TracingCanvas::mousePressEvent(QMouseEvent *event)
{
    drawing = true;
    lastPoint = event->position();

    path.moveTo(lastPoint);
    userPoints.append(lastPoint);
}

void TracingCanvas::mouseMoveEvent(QMouseEvent *event)
{
    if (!drawing) {
        return;
    }

    QPointF point = event->position();

    path.lineTo(point);
    lastPoint = point;
    userPoints.append(point);

    update();
}

void TracingCanvas::mouseReleaseEvent(QMouseEvent *)
{
    drawing = false;
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle("Interactive Alphabet Learning System");

    fillData();
    loadLastIndex();

    nounPlayer = new QMediaPlayer(this);
    nounAudio = new QAudioOutput(this);
    nounPlayer->setAudioOutput(nounAudio);
    nounAudio->setVolume(0.9);

    songPlayer = new QMediaPlayer(this);
    songAudio = new QAudioOutput(this);
    songPlayer->setAudioOutput(songAudio);
    songAudio->setVolume(0.9);

    voiceProcess = new QProcess(this);

    connect(
        voiceProcess,
        QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
        this,
        [this](int, QProcess::ExitStatus) {
            voicePlaying = false;
        }
        );

    connect(
        voiceProcess,
        &QProcess::errorOccurred,
        this,
        [this](QProcess::ProcessError) {
            voicePlaying = false;
        }
        );

    stack = new QStackedWidget(this);
    setCentralWidget(stack);

    welcomePage = makeWelcomePage();
    learnPage = makeLearnPage();
    tracePage = makeTracePage();
    videoPage = makeVideoPage();
    songPage = makeSongPage();

    stack->addWidget(welcomePage);
    stack->addWidget(learnPage);
    stack->addWidget(tracePage);
    stack->addWidget(videoPage);
    stack->addWidget(songPage);

    QShortcut *rightShortcut = new QShortcut(QKeySequence(Qt::Key_Right), this);
    QShortcut *leftShortcut = new QShortcut(QKeySequence(Qt::Key_Left), this);
    QShortcut *escapeShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);

    connect(rightShortcut, &QShortcut::activated, this, [this]() {
        nextAlphabet();
    });

    connect(leftShortcut, &QShortcut::activated, this, [this]() {
        previousAlphabet();
    });

    connect(escapeShortcut, &QShortcut::activated, this, [this]() {
        showWelcome();
    });

    updateLearnPage();
    resize(1280, 780);
}

MainWindow::~MainWindow() = default;

void MainWindow::fillData()
{
    items = {
        {"A", "a", "Apple", "Ant", "🍎", "🐜", "A for Apple", "A for Ant", "A_apple_ant.mp4", "#ffecd2", "#fcb69f"},
        {"B", "b", "Bat", "Ball", "🏏", "⚽", "B for Bat", "B for Ball", "B_bat_ball.mp4", "#a1c4fd", "#c2e9fb"},
        {"C", "c", "Cat", "Cup", "🐱", "☕", "C for Cat", "C for Cup", "C_cat_cup.mp4", "#d4fc79", "#96e6a1"},
        {"D", "d", "Dog", "Dot", "🐶", "•", "D for Dog", "D for Dot", "D_dog_dot.mp4", "#fbc2eb", "#a6c1ee"},
        {"E", "e", "Egg", "Elephant", "🥚", "🐘", "E for Egg", "E for Elephant", "E_egg_elephant.mp4", "#84fab0", "#8fd3f4"},
        {"F", "f", "Frog", "Fish", "🐸", "🐟", "F for Frog", "F for Fish", "F_frog_fish.mp4", "#f6d365", "#fda085"},
        {"G", "g", "Goat", "Grass", "🐐", "🌱", "G for Goat", "G for Grass", "G_goat_grass.mp4", "#cfd9df", "#e2ebf0"},
        {"H", "h", "Hand", "Hat", "✋", "🎩", "H for Hand", "H for Hat", "H_hand_hat.mp4", "#a18cd1", "#fbc2eb"},
        {"I", "i", "Ink", "Insect", "🖋️", "🐞", "I for Ink", "I for Insect", "I_ink_insect.mp4", "#fad0c4", "#ffd1ff"},
        {"J", "j", "Juice", "Jar", "🧃", "🫙", "J for Juice", "J for Jar", "J_juice_jar.mp4", "#ff9a9e", "#fecfef"},
        {"K", "k", "King", "Kite", "🤴", "🪁", "K for King", "K for Kite", "K_king_kite.mp4", "#a8edea", "#fed6e3"},
        {"L", "l", "Lab", "Leaf", "🧪", "🍃", "L for Lab", "L for Leaf", "L_lab_leaf.mp4", "#fddb92", "#d1fdff"},
        {"M", "m", "Man", "Monkey", "🧍", "🐵", "M for Man", "M for Monkey", "M_man_monkey.mp4", "#9890e3", "#b1f4cf"},
        {"N", "n", "Neck", "Nose", "🧣", "👃", "N for Neck", "N for Nose", "N_neck_nose.mp4", "#ebc0fd", "#d9ded8"},
        {"O", "o", "Ocean", "Oar", "🌊", "🚣", "O for Ocean", "O for Oar", "O_ocean_oar.mp4", "#f093fb", "#f5576c"},
        {"P", "p", "Plum", "Parrot", "🟣", "🦜", "P for Plum", "P for Parrot", "P_plum_parrot.mp4", "#4facfe", "#00f2fe"},
        {"Q", "q", "Queen", "Quail", "👑", "🐦", "Q for Queen", "Q for Quail", "Q_queen_quail.mp4", "#fa709a", "#fee140"},
        {"R", "r", "Rope", "Rat", "🪢", "🐀", "R for Rope", "R for Rat", "R_rope_rat.mp4", "#30cfd0", "#330867"},
        {"S", "s", "Sun", "Sunflower", "☀️", "🌻", "S for Sun", "S for Sunflower", "S_sun_sunflower.mp4", "#f6d365", "#fda085"},
        {"T", "t", "Tap", "Tub", "🚰", "🛁", "T for Tap", "T for Tub", "T_tap_tub.mp4", "#96fbc4", "#f9f586"},
        {"U", "u", "Uncle", "Umbrella", "👨", "☂️", "U for Uncle", "U for Umbrella", "U_uncle_umbrella.mp4", "#84fab0", "#8fd3f4"},
        {"V", "v", "Violin", "Violet", "🎻", "💜", "V for Violin", "V for Violet", "V_violin_violet.mp4", "#c2e9fb", "#a1c4fd"},
        {"W", "w", "Well", "Water", "🕳️", "💧", "W for Well", "W for Water", "W_well_water.mp4", "#89f7fe", "#66a6ff"},
        {"X", "x", "Box", "Fox", "📦", "🦊", "X for Box", "X for Fox", "X_box_fox.mp4", "#fddb92", "#d1fdff"},
        {"Y", "y", "Yacht", "Yellow", "⛵", "🟡", "Y for Yacht", "Y for Yellow", "Y_yacht_yellow.mp4", "#ffecd2", "#fcb69f"},
        {"Z", "z", "Zoo", "Zebra", "🦁", "🦓", "Z for Zoo", "Z for Zebra", "Z_zoo_zebra.mp4", "#e0c3fc", "#8ec5fc"}
    };
}

QLabel *MainWindow::label(const QString &text, int size, const QString &color, bool bold)
{
    QLabel *l = new QLabel(text);
    l->setAlignment(Qt::AlignCenter);
    l->setWordWrap(true);
    l->setFont(QFont("Segoe UI", size, bold ? QFont::Bold : QFont::Normal));
    l->setStyleSheet("color:" + color + "; background:transparent;");
    return l;
}

QPushButton *MainWindow::button(const QString &text, const QString &color, const QString &hover)
{
    QPushButton *b = new QPushButton(text);
    b->setCursor(Qt::PointingHandCursor);
    b->setMinimumHeight(56);
    b->setFont(QFont("Segoe UI", 13, QFont::Bold));

    b->setStyleSheet(
        "QPushButton{background:" + color + ";color:white;border-radius:23px;padding:8px 18px;border:3px solid white;}"
                                            "QPushButton:hover{background:" + hover + ";}"
                  "QPushButton:disabled{background:#b2bec3;color:#ecf0f1;}"
        );

    return b;
}

QWidget *MainWindow::card(QWidget *content)
{
    QFrame *f = new QFrame;
    f->setStyleSheet("QFrame{background:rgba(255,255,255,235);border-radius:32px;}");

    QVBoxLayout *layout = new QVBoxLayout(f);
    layout->setContentsMargins(28, 24, 28, 24);
    layout->addWidget(content);

    auto *shadow = new QGraphicsDropShadowEffect(f);
    shadow->setBlurRadius(28);
    shadow->setOffset(0, 8);
    shadow->setColor(QColor(0, 0, 0, 75));
    f->setGraphicsEffect(shadow);

    return f;
}

void MainWindow::applyBackground(QWidget *page, const QString &c1, const QString &c2)
{
    page->setStyleSheet(
        "QWidget{background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 "
        + c1 + ",stop:1 " + c2 + ");}"
        );
}

QWidget *MainWindow::makeWelcomePage()
{
    QWidget *page = new QWidget;
    applyBackground(page, "#74ebd5", "#ACB6E5");

    QVBoxLayout *main = new QVBoxLayout(page);
    main->setAlignment(Qt::AlignCenter);

    QWidget *inside = new QWidget;
    QVBoxLayout *box = new QVBoxLayout(inside);
    box->setSpacing(22);

    box->addWidget(label("Interactive Alphabet Learning System", 38, "#2d3436"));

    QPushButton *learn = button("Click for Alphabet Learning", "#00b894", "#019875");
    QPushButton *song = button("Listen Alphabet Song", "#d63031", "#b71c1c");

    learn->setMinimumWidth(520);
    song->setMinimumWidth(520);

    box->addWidget(learn, 0, Qt::AlignCenter);
    box->addWidget(song, 0, Qt::AlignCenter);

    connect(learn, &QPushButton::clicked, this, [this]() {
        showLearning();
    });

    connect(song, &QPushButton::clicked, this, [this]() {
        showSong();
    });

    main->addWidget(card(inside), 0, Qt::AlignCenter);
    return page;
}

QWidget *MainWindow::makeLearnPage()
{
    QWidget *page = new QWidget;
    QVBoxLayout *main = new QVBoxLayout(page);
    main->setContentsMargins(32, 20, 32, 20);

    QHBoxLayout *top = new QHBoxLayout;

    learnLetter = label("", 82, "#d63031");
    learnLetter->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    learnProgress = label("", 17, "#2d3436", false);
    learnProgress->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    top->addWidget(learnLetter, 1);
    top->addWidget(learnProgress, 1);

    QWidget *middle = new QWidget;
    QHBoxLayout *midLayout = new QHBoxLayout(middle);
    midLayout->setSpacing(24);

    QWidget *noun1 = new QWidget;
    QVBoxLayout *n1 = new QVBoxLayout(noun1);

    nounVisual1 = label("", 105, "#000000");
    nounText1 = label("", 34, "#0984e3");

    n1->addWidget(nounVisual1);
    n1->addWidget(nounText1);

    QWidget *noun2 = new QWidget;
    QVBoxLayout *n2 = new QVBoxLayout(noun2);

    nounVisual2 = label("", 105, "#000000");
    nounText2 = label("", 34, "#0984e3");

    n2->addWidget(nounVisual2);
    n2->addWidget(nounText2);

    midLayout->addWidget(card(noun1));
    midLayout->addWidget(card(noun2));

    QHBoxLayout *bottom = new QHBoxLayout;

    QPushButton *prev = button("Previous", "#6c5ce7", "#5544c9");
    QPushButton *home = button("Home", "#e17055", "#d35400");
    QPushButton *next = button("Next", "#00b894", "#019875");
    QPushButton *voice = button("Listen", "#0984e3", "#0769b3");
    QPushButton *draw = button("Practice Writing", "#fd79a8", "#e84393");
    QPushButton *video = button("Watch Video", "#2d3436", "#000000");

    bottom->addWidget(prev);
    bottom->addWidget(home);
    bottom->addWidget(next);
    bottom->addWidget(voice);
    bottom->addWidget(draw);
    bottom->addWidget(video);

    connect(prev, &QPushButton::clicked, this, [this]() {
        previousAlphabet();
    });

    connect(home, &QPushButton::clicked, this, [this]() {
        showWelcome();
    });

    connect(next, &QPushButton::clicked, this, [this]() {
        nextAlphabet();
    });

    connect(voice, &QPushButton::clicked, this, [this]() {
        speakCurrentWords();
    });

    connect(draw, &QPushButton::clicked, this, [this]() {
        showTraceForCurrent();
    });

    connect(video, &QPushButton::clicked, this, [this]() {
        showVideoForCurrent();
    });

    main->addLayout(top);
    main->addWidget(middle, 1);
    main->addLayout(bottom);

    return page;
}

QWidget *MainWindow::makeTracePage()
{
    QWidget *page = new QWidget;
    applyBackground(page, "#d4fc79", "#96e6a1");

    QVBoxLayout *main = new QVBoxLayout(page);
    main->setContentsMargins(28, 18, 28, 18);

    traceTitle = label("", 25, "#2d3436");
    traceInfo = label("", 15, "#636e72", false);
    traceResult = label("", 18, "#0984e3", false);

    traceCanvas = new TracingCanvas;

    QHBoxLayout *buttons = new QHBoxLayout;

    QPushButton *reset = button("Start Over", "#fdcb6e", "#e1a800");
    QPushButton *check = button("Check", "#00b894", "#019875");
    QPushButton *progress = button("My Progress", "#636e72", "#2d3436");
    QPushButton *retrace = button("Practice Another", "#fd79a8", "#e84393");

    traceCaseButton = button("Switch Case", "#0984e3", "#0769b3");

    QPushButton *back = button("Back to Learning", "#e17055", "#d35400");

    buttons->addWidget(reset);
    buttons->addWidget(check);
    buttons->addWidget(progress);
    buttons->addWidget(retrace);
    buttons->addWidget(traceCaseButton);
    buttons->addWidget(back);

    connect(reset, &QPushButton::clicked, this, [this]() {
        resetTracing();
    });

    connect(check, &QPushButton::clicked, this, [this]() {
        checkTracing();
    });

    connect(progress, &QPushButton::clicked, this, [this]() {
        showTracingProgressDialog();
    });

    connect(retrace, &QPushButton::clicked, this, [this]() {
        retraceSpecificAlphabet();
    });

    connect(traceCaseButton, &QPushButton::clicked, this, [this]() {
        tracingUppercase = !tracingUppercase;
        updateTracePage(true);
    });

    connect(back, &QPushButton::clicked, this, [this]() {
        showLearning();
    });

    main->addWidget(traceTitle);
    main->addWidget(traceInfo);
    main->addWidget(traceCanvas, 1, Qt::AlignCenter);
    main->addWidget(traceResult);
    main->addLayout(buttons);

    return page;
}

QWidget *MainWindow::makeVideoPage()
{
    QWidget *page = new QWidget;

    QVBoxLayout *main = new QVBoxLayout(page);
    main->setContentsMargins(35, 25, 35, 25);

    videoTitle = label("", 24, "#2d3436");
    videoMessage = label("", 15, "#d63031", false);

    nounVideoWidget = new QVideoWidget;
    nounVideoWidget->setMinimumHeight(470);
    nounVideoWidget->setStyleSheet("background:black; border-radius:20px;");

    nounPlayer->setVideoOutput(nounVideoWidget);

    QHBoxLayout *buttons = new QHBoxLayout;

    QPushButton *start = button("Start", "#00b894", "#019875");
    QPushButton *pause = button("Pause", "#0984e3", "#0769b3");
    QPushButton *resume = button("Resume", "#d63031", "#b71c1c");
    QPushButton *back = button("Back to Learning", "#e17055", "#d35400");

    buttons->addWidget(start);
    buttons->addWidget(pause);
    buttons->addWidget(resume);
    buttons->addWidget(back);

    connect(start, &QPushButton::clicked, this, [this]() {
        startNounVideo();
    });

    connect(pause, &QPushButton::clicked, this, [this]() {
        pauseNounVideo();
    });

    connect(resume, &QPushButton::clicked, this, [this]() {
        resumeNounVideo();
    });

    connect(back, &QPushButton::clicked, this, [this]() {
        showLearning();
    });

    main->addWidget(videoMessage);
    main->addWidget(nounVideoWidget, 1);
    main->addLayout(buttons);

    return page;
}

QWidget *MainWindow::makeSongPage()
{
    QWidget *page = new QWidget;
    applyBackground(page, "#fbc2eb", "#a6c1ee");

    QVBoxLayout *main = new QVBoxLayout(page);
    main->setContentsMargins(35, 25, 35, 25);

    main->addWidget(label("Alphabet Song", 30, "#2d3436"));

    songMessage = label("", 15, "#d63031", false);

    songVideoWidget = new QVideoWidget;
    songVideoWidget->setMinimumHeight(490);
    songVideoWidget->setStyleSheet("background:black; border-radius:20px;");

    songPlayer->setVideoOutput(songVideoWidget);

    QHBoxLayout *buttons = new QHBoxLayout;

    QPushButton *start = button("Start", "#00b894", "#019875");
    QPushButton *pause = button("Pause", "#0984e3", "#0769b3");
    QPushButton *resume = button("Resume", "#d63031", "#b71c1c");
    QPushButton *home = button("Home", "#e17055", "#d35400");

    buttons->addWidget(start);
    buttons->addWidget(pause);
    buttons->addWidget(resume);
    buttons->addWidget(home);

    connect(start, &QPushButton::clicked, this, [this]() {
        startSongVideo();
    });

    connect(pause, &QPushButton::clicked, this, [this]() {
        pauseSongVideo();
    });

    connect(resume, &QPushButton::clicked, this, [this]() {
        resumeSongVideo();
    });

    connect(home, &QPushButton::clicked, this, [this]() {
        showWelcome();
    });

    main->addWidget(songMessage);
    main->addWidget(songVideoWidget, 1);
    main->addLayout(buttons);

    return page;
}

QString MainWindow::projectRootPath() const
{
    return QString::fromUtf8(PROJECT_ROOT_DIR);
}

QString MainWindow::tracingRootPath() const
{
    return QDir(projectRootPath()).filePath("tracing");
}

QString MainWindow::currentTracingImagePath() const
{
    return QDir(tracingRootPath()).filePath(
        tracingUppercase
            ? ("_" + items[currentIndex].upper + ".png")
            : (items[currentIndex].lower + ".png")
        );
}

QString MainWindow::currentNounVideoPath() const
{
    QString fileName = items[currentIndex].videoFile;
    QString diskFileName;

    // K has a different filename on disk
    if (fileName == "K_king_kite.mp4") {
        diskFileName = "K_king_kite.mp.mp4";
    } else {
        diskFileName = fileName + ".mp4";
    }

    // Load directly from disk using PROJECT_ROOT_DIR defined in CMakeLists.txt
    QString path = QDir(projectRootPath()).filePath("videos/nouns/" + diskFileName);

    if (QFileInfo::exists(path)) {
        return path;
    }

    return "";
}

QString MainWindow::songVideoPath() const
{
    // Load directly from disk using PROJECT_ROOT_DIR defined in CMakeLists.txt
    QString path = QDir(projectRootPath()).filePath("videos/song/alphabet_song.mp4.mp4");

    if (QFileInfo::exists(path)) {
        return path;
    }

    return "";
}

// copyResourceVideoToTempFile removed — videos now loaded directly from disk

QString MainWindow::tracingKey() const
{
    return QString("%1_%2").arg(
        tracingUppercase ? "upper" : "lower",
        tracingUppercase ? items[currentIndex].upper : items[currentIndex].lower
        );
}

QString MainWindow::speechText() const
{
    const auto &a = items[currentIndex];
    return a.upper + " " + a.lower + ", " + a.phrase1 + ", " + a.phrase2;
}

void MainWindow::saveLastIndex()
{
    QSettings settings("OOPProject", "InteractiveAlphabetLearningSystem");
    settings.setValue("lastAlphabetIndex", currentIndex);
}

void MainWindow::loadLastIndex()
{
    QSettings settings("OOPProject", "InteractiveAlphabetLearningSystem");

    currentIndex = settings.value("lastAlphabetIndex", 0).toInt();

    if (currentIndex < 0 || currentIndex >= 26) {
        currentIndex = 0;
    }
}

double MainWindow::savedBestAccuracy(const QString &key) const
{
    QSettings settings("OOPProject", "InteractiveAlphabetLearningSystem");
    return settings.value("tracing/" + key + "/best", -1.0).toDouble();
}

int MainWindow::savedAttemptCount(const QString &key) const
{
    QSettings settings("OOPProject", "InteractiveAlphabetLearningSystem");
    return settings.value("tracing/" + key + "/attempts", 0).toInt();
}

double MainWindow::savedLastAccuracy(const QString &key) const
{
    QSettings settings("OOPProject", "InteractiveAlphabetLearningSystem");
    return settings.value("tracing/" + key + "/last", -1.0).toDouble();
}

QStringList MainWindow::savedHistory(const QString &key) const
{
    QSettings settings("OOPProject", "InteractiveAlphabetLearningSystem");
    return settings.value("tracing/" + key + "/history").toStringList();
}

void MainWindow::saveTracingAttempt(const QString &key, double accuracy)
{
    QSettings settings("OOPProject", "InteractiveAlphabetLearningSystem");

    int attempts = settings.value("tracing/" + key + "/attempts", 0).toInt() + 1;
    double best = settings.value("tracing/" + key + "/best", -1.0).toDouble();

    QStringList history = settings.value("tracing/" + key + "/history").toStringList();
    history << QString::number(accuracy, 'f', 1);

    settings.setValue("tracing/" + key + "/attempts", attempts);
    settings.setValue("tracing/" + key + "/last", accuracy);
    settings.setValue("tracing/" + key + "/best", qMax(best, accuracy));
    settings.setValue("tracing/" + key + "/history", history);
}

void MainWindow::setPlayerSourceIfExists(QMediaPlayer *player,
                                         QLabel *message,
                                         const QString &path,
                                         const QString &missingText)
{
    if (!path.isEmpty() && QFileInfo::exists(path)) {
        player->stop();
        player->setSource(QUrl::fromLocalFile(path));

        message->clear();
    } else {
        player->stop();
        player->setSource(QUrl());
        message->setText(missingText + " Video file/resource was not found.");
    }
}

void MainWindow::showWelcome()
{
    nounPlayer->stop();
    songPlayer->stop();

    if (voiceProcess && voiceProcess->state() != QProcess::NotRunning) {
        voiceProcess->kill();
        voiceProcess->waitForFinished(300);
        voicePlaying = false;
    }

    stack->setCurrentWidget(welcomePage);
}

void MainWindow::showLearning()
{
    nounPlayer->stop();
    songPlayer->stop();

    updateLearnPage();
    stack->setCurrentWidget(learnPage);
}

void MainWindow::showSong()
{
    nounPlayer->stop();

    QString songPath = songVideoPath();

    setPlayerSourceIfExists(
        songPlayer,
        songMessage,
        songPath,
        "Alphabet song video is missing."
        );

    stack->setCurrentWidget(songPage);
}

void MainWindow::showTraceForCurrent()
{
    nounPlayer->stop();
    songPlayer->stop();

    tracingUppercase = true;
    updateTracePage(true);
    stack->setCurrentWidget(tracePage);
}

void MainWindow::showVideoForCurrent()
{
    songPlayer->stop();

    updateVideoPage();
    stack->setCurrentWidget(videoPage);
}

void MainWindow::nextAlphabet()
{
    if (stack->currentWidget() != learnPage &&
        stack->currentWidget() != videoPage &&
        stack->currentWidget() != tracePage) {
        return;
    }

    if (currentIndex < items.size() - 1) {
        currentIndex++;
        saveLastIndex();

        updateLearnPage();
        updateTracePage(true);
        updateVideoPage();
    }
}

void MainWindow::previousAlphabet()
{
    if (stack->currentWidget() != learnPage &&
        stack->currentWidget() != videoPage &&
        stack->currentWidget() != tracePage) {
        return;
    }

    if (currentIndex > 0) {
        currentIndex--;
        saveLastIndex();

        updateLearnPage();
        updateTracePage(true);
        updateVideoPage();
    }
}

void MainWindow::updateLearnPage()
{
    const auto &a = items[currentIndex];

    applyBackground(learnPage, a.color1, a.color2);

    learnLetter->setText(a.upper + a.lower);
    learnProgress->setText("Alphabet " + QString::number(currentIndex + 1) + " of 26");

    nounVisual1->setText(a.visual1);
    nounVisual2->setText(a.visual2);

    nounText1->setText(a.phrase1);
    nounText2->setText(a.phrase2);

    saveLastIndex();
}

void MainWindow::updateTracePage(bool resetCanvas)
{
    const auto &a = items[currentIndex];

    if (resetCanvas) {
        QPixmap pix(currentTracingImagePath());
        traceCanvas->setTemplateImage(pix);
    }

    QString shownLetter = tracingUppercase ? a.upper : a.lower;

    traceTitle->setText("Practice Writing: " + shownLetter);

    QString key = tracingKey();

    double best = savedBestAccuracy(key);
    double last = savedLastAccuracy(key);
    int attempts = savedAttemptCount(key);

    QString saved = attempts > 0
                        ? QString("Saved progress: attempts %1, last %2%, best %3%.")
                              .arg(attempts)
                              .arg(last, 0, 'f', 1)
                              .arg(best, 0, 'f', 1)
                        : "No saved progress for this alphabet yet.";

    traceInfo->setText("Trace carefully on the dotted track. " + saved);

    if (resetCanvas) {
        traceResult->setText("Press Check after tracing. Your drawing will stay visible after checking.");
    }

    traceCaseButton->setText(tracingUppercase ? "Practice lowercase" : "Practice uppercase");
}

void MainWindow::updateVideoPage()
{
    const auto &a = items[currentIndex];

    applyBackground(videoPage, a.color1, a.color2);

    videoTitle->setText("Video: " + a.phrase1 + " and " + a.phrase2);

    setPlayerSourceIfExists(
        nounPlayer,
        videoMessage,
        currentNounVideoPath(),
        "Noun video is missing."
        );
}

void MainWindow::speakCurrentWords()
{
    if (voicePlaying) {
        return;
    }

#ifdef Q_OS_WIN
    if (!voiceProcess) {
        voiceProcess = new QProcess(this);

        connect(
            voiceProcess,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this,
            [this](int, QProcess::ExitStatus) {
                voicePlaying = false;
            }
            );

        connect(
            voiceProcess,
            &QProcess::errorOccurred,
            this,
            [this](QProcess::ProcessError) {
                voicePlaying = false;
            }
            );
    }

    QString text = speechText();

    text.replace("'", "''");

    QString command =
        "Add-Type -AssemblyName System.Speech; "
        "$speak = New-Object System.Speech.Synthesis.SpeechSynthesizer; "
        "$speak.Rate = -2; "
        "$speak.Volume = 100; "
        "$speak.Speak('" + text + "');";

    voicePlaying = true;

    voiceProcess->start(
        "powershell",
        {
            "-NoProfile",
            "-ExecutionPolicy",
            "Bypass",
            "-Command",
            command
        }
        );

    if (!voiceProcess->waitForStarted(1000)) {
        voicePlaying = false;
    }

#else
    voicePlaying = true;
    QApplication::beep();
    voicePlaying = false;
#endif
}

void MainWindow::startNounVideo()
{
    updateVideoPage();

    if (!nounPlayer->source().isEmpty()) {
        nounPlayer->setPosition(0);
        nounPlayer->play();
    }
}

void MainWindow::pauseNounVideo()
{
    nounPlayer->pause();
}

void MainWindow::resumeNounVideo()
{
    if (nounPlayer->source().isEmpty()) {
        updateVideoPage();
    }

    if (!nounPlayer->source().isEmpty()) {
        nounPlayer->play();
    }
}

void MainWindow::startSongVideo()
{
    setPlayerSourceIfExists(
        songPlayer,
        songMessage,
        songVideoPath(),
        "Alphabet song video is missing."
        );

    if (!songPlayer->source().isEmpty()) {
        songPlayer->setPosition(0);
        songPlayer->play();
    }
}

void MainWindow::pauseSongVideo()
{
    songPlayer->pause();
}

void MainWindow::resumeSongVideo()
{
    if (songPlayer->source().isEmpty()) {
        setPlayerSourceIfExists(
            songPlayer,
            songMessage,
            songVideoPath(),
            "Alphabet song video is missing."
            );
    }

    if (!songPlayer->source().isEmpty()) {
        songPlayer->play();
    }
}

void MainWindow::resetTracing()
{
    traceCanvas->clearDrawing();
    traceResult->setText("Drawing cleared. Trace again and press Check.");
}

void MainWindow::checkTracing()
{
    double accuracy = traceCanvas->accuracyPercent();

    QString key = tracingKey();

    saveTracingAttempt(key, accuracy);

    QString result;

    if (accuracy >= 80.0) {
        result = "Excellent! Your track is clear and close to the guide.";
    } else if (accuracy >= 55.0) {
        result = "Good effort. Try once more to improve the track.";
    } else {
        result = "Keep practicing. Start over and follow more of the dotted line.";
    }

    traceResult->setText(
        QString("Score: %1% - %2 Attempts: %3 | Best: %4%")
            .arg(accuracy, 0, 'f', 1)
            .arg(result)
            .arg(savedAttemptCount(key))
            .arg(savedBestAccuracy(key), 0, 'f', 1)
        );

    updateTracePage(false);
}

void MainWindow::showTracingProgressDialog()
{
    QDialog dialog(this);
    dialog.setWindowTitle("My Writing Progress");
    dialog.resize(760, 540);

    QVBoxLayout *layout = new QVBoxLayout(&dialog);

    QTableWidget *table = new QTableWidget(52, 5, &dialog);
    table->setHorizontalHeaderLabels({"Letter", "Case", "Attempts", "Best Score", "All Scores"});

    table->horizontalHeader()->setStretchLastSection(true);
    table->verticalHeader()->setVisible(false);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);

    int row = 0;

    for (const auto &item : items) {
        QString upperKey = "upper_" + item.upper;

        table->setItem(row, 0, new QTableWidgetItem(item.upper));
        table->setItem(row, 1, new QTableWidgetItem("Uppercase"));
        table->setItem(row, 2, new QTableWidgetItem(QString::number(savedAttemptCount(upperKey))));

        double upperBest = savedBestAccuracy(upperKey);

        table->setItem(row, 3, new QTableWidgetItem(upperBest >= 0 ? QString::number(upperBest, 'f', 1) + "%" : "Not traced"));
        table->setItem(row, 4, new QTableWidgetItem(savedHistory(upperKey).join(", ")));

        row++;

        QString lowerKey = "lower_" + item.lower;

        table->setItem(row, 0, new QTableWidgetItem(item.lower));
        table->setItem(row, 1, new QTableWidgetItem("Lowercase"));
        table->setItem(row, 2, new QTableWidgetItem(QString::number(savedAttemptCount(lowerKey))));

        double lowerBest = savedBestAccuracy(lowerKey);

        table->setItem(row, 3, new QTableWidgetItem(lowerBest >= 0 ? QString::number(lowerBest, 'f', 1) + "%" : "Not traced"));
        table->setItem(row, 4, new QTableWidgetItem(savedHistory(lowerKey).join(", ")));

        row++;
    }

    layout->addWidget(table);

    QHBoxLayout *buttons = new QHBoxLayout;

    QPushButton *clear = new QPushButton("Clear All Progress");
    QPushButton *close = new QPushButton("Close");

    buttons->addWidget(clear);
    buttons->addWidget(close);

    layout->addLayout(buttons);

    connect(clear, &QPushButton::clicked, this, [this, &dialog]() {
        clearAllTracingProgress();
        dialog.accept();
    });

    connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);

    dialog.exec();
}

void MainWindow::clearAllTracingProgress()
{
    QSettings settings("OOPProject", "InteractiveAlphabetLearningSystem");

    settings.beginGroup("tracing");
    settings.remove("");
    settings.endGroup();

    traceResult->setText("All writing progress has been cleared.");

    updateTracePage(false);
}

void MainWindow::retraceSpecificAlphabet()
{
    QDialog dialog(this);
    dialog.setWindowTitle("Practice Another Alphabet");

    QVBoxLayout *layout = new QVBoxLayout(&dialog);

    layout->addWidget(new QLabel("Choose alphabet and case to practice:"));

    QComboBox *letterBox = new QComboBox;

    for (const auto &item : items) {
        letterBox->addItem(item.upper + item.lower);
    }

    letterBox->setCurrentIndex(currentIndex);

    QComboBox *caseBox = new QComboBox;
    caseBox->addItems({"Uppercase", "Lowercase"});
    caseBox->setCurrentIndex(tracingUppercase ? 0 : 1);

    QDialogButtonBox *buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);

    layout->addWidget(letterBox);
    layout->addWidget(caseBox);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() == QDialog::Accepted) {
        currentIndex = letterBox->currentIndex();
        tracingUppercase = caseBox->currentIndex() == 0;

        saveLastIndex();

        updateLearnPage();
        updateTracePage(true);

        stack->setCurrentWidget(tracePage);
    }
}