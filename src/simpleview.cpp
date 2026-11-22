#include "simpleview.h"

#include <algorithm>
#include <cmath>

#include <QDesktopServices>
#include <QDir>
#include <QDragEnterEvent>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QProcess>
#include <QWindow>
#include <QDropEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMimeDatabase>
#include <QMimeData>
#include <QScrollArea>
#include <QSettings>
#include <QSize>
#include <QUrl>

// Brand colors of OliVision
static const char *kGreen = "#39d353";
static const char *kNavy = "#0d1b2a";
static const char *kPanel = "#16293d";
static const char *kLine = "#2a4a6a";
static const char *kMuted = "#a0b8cc";

static QSettings choiceSettings()
{
    return QSettings(QSettings::IniFormat, QSettings::UserScope, "video2x", "olivision-simple");
}

SimpleView::SimpleView(QWidget *parent)
    : QWidget(parent)
{
    setAcceptDrops(true);
    setMouseTracking(true);
    setObjectName("simpleView");
    setStyleSheet(QString(R"(
        #simpleView { background: %2; }
        QFrame#panel { background: #1a3550; border: 1px solid %4; border-radius: 12px; }
        QFrame#card { background: %3; border: 1px solid %4; border-radius: 8px; }
        QLabel { color: #e8eef4; background: transparent; }
        QLabel#muted { color: %5; }
        QLabel#question { color: %5; font-size: 10pt; }
        QPushButton#choice { background: %3; color: %5; border: 1px solid %4; border-radius: 8px;
                             padding: 6px 12px; font-size: 10pt; }
        QPushButton#choice:checked { background: %1; color: %2; border-color: %1; font-weight: bold; }
        QPushButton#choice:hover:!checked { border-color: %1; }
        QPushButton#choice:disabled { color: #5c7690; border-color: #1f3a55; }
        QPushButton#choice:checked:disabled { background: #2b8a3e; color: #0d1b2a; }
        QPushButton#start { background: %1; color: %2; border: none; border-radius: 8px;
                            font-size: 12pt; font-weight: bold; min-height: 40px; }
        QPushButton#start:hover { background: #4fe068; }
        QPushButton#secondary { background: %3; color: #e8eef4; border: 1px solid %4;
                                border-radius: 8px; min-height: 40px; font-size: 11pt; }
        QPushButton#link { background: transparent; color: %5; border: none; font-size: 9pt;
                           text-align: left; padding: 2px; }
        QPushButton#link:hover { color: %1; }
        QPushButton#drop { background: transparent; color: %5; border: 2px dashed %1;
                           border-radius: 12px; min-height: 70px; font-size: 10pt; }
        QPushButton#drop:hover { background: %3; }
        QPushButton#remove { background: transparent; color: %5; border: none; font-size: 11pt; }
        QPushButton#remove:hover { color: #ff6b6b; }
        QProgressBar { background: %4; border: none; border-radius: 2px; max-height: 4px; }
        QProgressBar::chunk { background: %1; border-radius: 2px; }
        QScrollArea { background: transparent; border: none; }
        QPushButton#windowButton, QPushButton#windowClose { background: transparent; color: %5;
                     border: none; border-radius: 6px; font-size: 11pt; }
        QPushButton#windowButton:hover { background: #1a3550; color: #e8eef4; }
        QPushButton#windowClose:hover { background: #c42b1c; color: #ffffff; }
        QPushButton#openFolder { background: transparent; color: %1; border: 1px solid %1;
                     border-radius: 6px; padding: 3px 10px; font-size: 9pt; }
        QPushButton#openFolder:hover { background: %1; color: %2; }
        QLabel#hint { color: #8fb0cf; font-size: 9pt; }
        QScrollArea > QWidget > QWidget { background: transparent; }
    )")
                      .arg(kGreen, kNavy, kPanel, kLine, kMuted));

    QVBoxLayout *outer = new QVBoxLayout(this);
    outer->setContentsMargins(14, 14, 14, 14);

    QFrame *panel = new QFrame(this);
    panel->setObjectName("panel");
    outer->addWidget(panel);

    QVBoxLayout *panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(0, 0, 0, 0);
    panelLayout->setSpacing(0);

    // Header
    QWidget *header = new QWidget(panel);
    header->setStyleSheet(QString("background: #0f1f2e; border-top-left-radius: 12px;"
                                  "border-top-right-radius: 12px; border-bottom: 1px solid %1;")
                              .arg(kLine));
    QHBoxLayout *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(14, 10, 14, 10);
    QLabel *title = new QLabel(QString("<span style=\"color:%1\">●</span>&nbsp; <b>OliVision</b>")
                                   .arg(kGreen),
                               header);
    title->setStyleSheet("font-size: 12pt; border: none;");
    m_gpuLabel = new QLabel(header);
    m_gpuLabel->setObjectName("muted");
    m_gpuLabel->setStyleSheet("border: none;");
    headerLayout->addWidget(title);
    headerLayout->addStretch();
    headerLayout->addWidget(m_gpuLabel);
    headerLayout->addSpacing(12);

    // Own window buttons, the window has no Windows frame
    const QStringList windowButtonTexts = {"–", "□", "✕"};
    for (int i = 0; i < windowButtonTexts.size(); i++) {
        QPushButton *button = new QPushButton(windowButtonTexts[i], header);
        button->setObjectName(i == 2 ? "windowClose" : "windowButton");
        button->setFixedSize(34, 26);
        button->setFocusPolicy(Qt::NoFocus);
        headerLayout->addWidget(button);
        if (i == 0) {
            connect(button, &QPushButton::clicked, this, &SimpleView::minimizeClicked);
        } else if (i == 1) {
            connect(button, &QPushButton::clicked, this, &SimpleView::maximizeClicked);
        } else {
            connect(button, &QPushButton::clicked, this, &SimpleView::closeClicked);
        }
    }
    m_header = header;
    header->installEventFilter(this);
    title->installEventFilter(this);
    m_gpuLabel->installEventFilter(this);
    panelLayout->addWidget(header);

    QHBoxLayout *body = new QHBoxLayout();
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(0);
    panelLayout->addLayout(body, 1);

    // Left: drop zone and video list
    QWidget *left = new QWidget(panel);
    QVBoxLayout *leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(14, 14, 14, 14);
    leftLayout->setSpacing(10);

    QPushButton *dropButton = new QPushButton(tr("Drag videos or photos here or click to choose"), left);
    dropButton->setObjectName("drop");
    dropButton->setCursor(Qt::PointingHandCursor);
    connect(dropButton, &QPushButton::clicked, this, &SimpleView::addFilesClicked);
    leftLayout->addWidget(dropButton);

    QScrollArea *scroll = new QScrollArea(left);
    scroll->setWidgetResizable(true);
    QWidget *cardsWidget = new QWidget(scroll);
    m_cardsLayout = new QVBoxLayout(cardsWidget);
    m_cardsLayout->setContentsMargins(0, 0, 0, 0);
    m_cardsLayout->setSpacing(8);
    m_emptyLabel = new QLabel(tr("No videos or photos yet."), cardsWidget);
    m_emptyLabel->setObjectName("muted");
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_cardsLayout->addWidget(m_emptyLabel);
    m_cardsLayout->addStretch();
    scroll->setWidget(cardsWidget);
    leftLayout->addWidget(scroll, 1);

    body->addWidget(left, 3);

    QFrame *divider = new QFrame(panel);
    divider->setFixedWidth(1);
    divider->setStyleSheet(QString("background: %1; border: none;").arg(kLine));
    body->addWidget(divider);

    // Right: the four questions and the buttons
    QWidget *right = new QWidget(panel);
    right->setMinimumWidth(300);
    QVBoxLayout *rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(14, 14, 14, 14);
    rightLayout->setSpacing(14);

    m_targetGroup = addChoice(rightLayout,
                              tr("How large should it become?"),
                              {tr("Same (only improve)"), tr("Full HD"), tr("QHD"), tr("4K")},
                              3,
                              "targetSize");
    m_speedGroup = addChoice(rightLayout,
                             tr("What matters more to you?"),
                             {tr("Fast"), tr("Balanced"), tr("Best quality")},
                             1,
                             "speed");
    m_denoiseGroup = addChoice(rightLayout,
                               tr("Noise reduction?"),
                               {tr("Off"), tr("On")},
                               0,
                               "denoise",
                               tr("On: for old or dark recordings with picture noise. The picture "
                                  "gets calmer, very fine details a little softer."));
    m_cartoonGroup = addChoice(rightLayout,
                               tr("Cartoon?"),
                               {tr("No"), tr("Yes")},
                               0,
                               "cartoon");

    rightLayout->addStretch();

    m_startButton = new QPushButton(tr("▶  Start"), right);
    m_startButton->setObjectName("start");
    m_startButton->setCursor(Qt::PointingHandCursor);
    connect(m_startButton, &QPushButton::clicked, this, &SimpleView::startClicked);
    rightLayout->addWidget(m_startButton);

    QHBoxLayout *runLayout = new QHBoxLayout();
    m_pauseButton = new QPushButton(tr("Pause"), right);
    m_pauseButton->setObjectName("secondary");
    m_abortButton = new QPushButton(tr("Abort"), right);
    m_abortButton->setObjectName("secondary");
    runLayout->addWidget(m_pauseButton);
    runLayout->addWidget(m_abortButton);
    rightLayout->addLayout(runLayout);
    connect(m_pauseButton, &QPushButton::clicked, this, [this]() {
        if (m_paused) {
            emit resumeClicked();
        } else {
            emit pauseClicked();
        }
    });
    connect(m_abortButton, &QPushButton::clicked, this, &SimpleView::abortClicked);

    QHBoxLayout *linksLayout = new QHBoxLayout();
    QPushButton *advancedButton = new QPushButton(tr("⚙ Advanced"), right);
    QPushButton *logsButton = new QPushButton(tr("☰ Log"), right);
    QPushButton *aboutButton = new QPushButton(tr("ⓘ About"), right);
    for (QPushButton *button : {advancedButton, logsButton, aboutButton}) {
        button->setObjectName("link");
        button->setCursor(Qt::PointingHandCursor);
        linksLayout->addWidget(button);
    }
    linksLayout->addStretch();
    rightLayout->addLayout(linksLayout);
    connect(advancedButton, &QPushButton::clicked, this, &SimpleView::advancedClicked);
    connect(logsButton, &QPushButton::clicked, this, &SimpleView::logsClicked);
    connect(aboutButton, &QPushButton::clicked, this, &SimpleView::aboutClicked);

    body->addWidget(right, 2);

    setRunning(false, false);
}

QButtonGroup *SimpleView::addChoice(QVBoxLayout *layout,
                                    const QString &question,
                                    const QStringList &options,
                                    int defaultIndex,
                                    const QString &settingsKey,
                                    const QString &hint)
{
    QLabel *label = new QLabel(question, this);
    label->setObjectName("question");

    QHBoxLayout *row = new QHBoxLayout();
    row->setSpacing(6);
    QButtonGroup *group = new QButtonGroup(this);
    group->setExclusive(true);

    int saved = choiceSettings().value(settingsKey, defaultIndex).toInt();
    if (saved < 0 || saved >= options.size()) {
        saved = defaultIndex;
    }

    for (int i = 0; i < options.size(); i++) {
        QPushButton *button = new QPushButton(options[i], this);
        button->setObjectName("choice");
        button->setCheckable(true);
        button->setCursor(Qt::PointingHandCursor);
        button->setChecked(i == saved);

        // Wide enough for the bold text of the selected button
        QFont boldFont = button->font();
        boldFont.setBold(true);
        boldFont.setPointSizeF(10);
        button->setMinimumWidth(QFontMetrics(boldFont).horizontalAdvance(options[i]) + 30);
        group->addButton(button, i);
        row->addWidget(button);
    }
    row->addStretch();

    QVBoxLayout *block = new QVBoxLayout();
    block->setSpacing(6);
    block->addWidget(label);
    block->addLayout(row);
    if (!hint.isEmpty()) {
        QLabel *hintLabel = new QLabel(hint, this);
        hintLabel->setObjectName("hint");
        hintLabel->setWordWrap(true);
        block->addWidget(hintLabel);
    }
    layout->addLayout(block);

    connect(group, &QButtonGroup::idClicked, this, [this]() {
        saveChoices();
        emit choicesChanged();
    });
    return group;
}

void SimpleView::saveChoices()
{
    QSettings settings = choiceSettings();
    settings.setValue("targetSize", m_targetGroup->checkedId());
    settings.setValue("speed", m_speedGroup->checkedId());
    settings.setValue("denoise", m_denoiseGroup->checkedId());
    settings.setValue("cartoon", m_cartoonGroup->checkedId());
}

int SimpleView::targetSize() const
{
    return m_targetGroup->checkedId();
}

int SimpleView::speed() const
{
    // Buttons: fast, balanced, best quality; the processor counts the other way round
    switch (m_speedGroup->checkedId()) {
    case 0:
        return 2;
    case 2:
        return 0;
    default:
        return 1;
    }
}

bool SimpleView::denoise() const
{
    return m_denoiseGroup->checkedId() == 1;
}

bool SimpleView::cartoon() const
{
    return m_cartoonGroup->checkedId() == 1;
}

void SimpleView::clearCards()
{
    for (Card &card : m_cards) {
        card.frame->deleteLater();
    }
    m_cards.clear();
    m_emptyLabel->setVisible(true);
}

void SimpleView::addCard(const QString &fileName, const QString &sizesText)
{
    Card card;
    card.frame = new QFrame(this);
    card.frame->setObjectName("card");

    QVBoxLayout *layout = new QVBoxLayout(card.frame);
    layout->setContentsMargins(12, 10, 12, 10);
    layout->setSpacing(4);

    QHBoxLayout *top = new QHBoxLayout();
    card.nameLabel = new QLabel(QFileInfo(fileName).fileName(), card.frame);
    card.nameLabel->setStyleSheet("font-weight: bold; border: none;");
    card.nameLabel->setToolTip(fileName);
    card.statusLabel = new QLabel(card.frame);
    card.statusLabel->setObjectName("muted");
    card.statusLabel->setStyleSheet("border: none;");
    card.removeButton = new QPushButton("✕", card.frame);
    card.removeButton->setObjectName("remove");
    card.removeButton->setToolTip(tr("Remove from the list"));
    card.removeButton->setCursor(Qt::PointingHandCursor);
    top->addWidget(card.nameLabel, 1);
    top->addWidget(card.statusLabel);
    top->addWidget(card.removeButton);
    layout->addLayout(top);

    card.sizesLabel = new QLabel(sizesText, card.frame);
    card.sizesLabel->setObjectName("muted");
    card.sizesLabel->setStyleSheet("border: none;");
    card.sizesLabel->setWordWrap(true);
    layout->addWidget(card.sizesLabel);

    // Name of the result, can be changed before the start
    QFileInfo inInfo(fileName);
    bool photo = QMimeDatabase().mimeTypeForFile(fileName, QMimeDatabase::MatchContent).name().startsWith("image/");
    QString ending = ".mkv";
    if (photo) {
        QString suffix = inInfo.suffix().toLower();
        ending = (suffix == "jpg" || suffix == "jpeg") ? ".jpg" : ".png";
    }
    card.nameRow = new QWidget(card.frame);
    QHBoxLayout *nameLayout = new QHBoxLayout(card.nameRow);
    nameLayout->setContentsMargins(0, 2, 0, 2);
    nameLayout->setSpacing(6);
    QLabel *saveAs = new QLabel(tr("Save as:"), card.nameRow);
    saveAs->setStyleSheet("border: none;");
    card.nameEdit = new QLineEdit(inInfo.completeBaseName() + "_OliVision", card.nameRow);
    card.nameEdit->setStyleSheet(QString("QLineEdit { background: %1; color: #e8eef4; border: 1px solid %2;"
                                         " border-radius: 6px; padding: 3px 6px; }"
                                         "QLineEdit:focus { border-color: %3; }"
                                         "QLineEdit:disabled { color: #8fb0cf; }")
                                     .arg(kNavy, kLine, kGreen));
    QLabel *endingLabel = new QLabel(ending, card.nameRow);
    endingLabel->setObjectName("muted");
    endingLabel->setStyleSheet("border: none;");
    QLabel *folderLabel = new QLabel(photo ? tr("in Pictures\\OliVision") : tr("in Videos\\OliVision"),
                                     card.nameRow);
    folderLabel->setObjectName("muted");
    folderLabel->setStyleSheet("border: none;");
    nameLayout->addWidget(saveAs);
    nameLayout->addWidget(card.nameEdit, 1);
    nameLayout->addWidget(endingLabel);
    nameLayout->addWidget(folderLabel);
    layout->addWidget(card.nameRow);

    QHBoxLayout *outputRow = new QHBoxLayout();
    card.outputLabel = new QLabel(card.frame);
    card.outputLabel->setObjectName("muted");
    card.outputLabel->setStyleSheet("border: none;");
    card.outputLabel->setVisible(false);
    card.openFolderButton = new QPushButton(tr("Open folder"), card.frame);
    card.openFolderButton->setObjectName("openFolder");
    card.openFolderButton->setCursor(Qt::PointingHandCursor);
    card.openFolderButton->setVisible(false);
    outputRow->addWidget(card.outputLabel, 1);
    outputRow->addWidget(card.openFolderButton);
    layout->addLayout(outputRow);

    card.progressBar = new QProgressBar(card.frame);
    card.progressBar->setTextVisible(false);
    card.progressBar->setRange(0, 1);
    card.progressBar->setValue(0);
    card.progressBar->setFixedHeight(4);
    layout->addWidget(card.progressBar);

    QFrame *frame = card.frame;
    connect(card.openFolderButton, &QPushButton::clicked, this, [this, frame]() {
        for (const Card &existing : m_cards) {
            if (existing.frame == frame && !existing.outputPath.isEmpty()) {
#ifdef _WIN32
                QProcess::startDetached("explorer.exe",
                                        {"/select,", QDir::toNativeSeparators(existing.outputPath)});
#else
                QDesktopServices::openUrl(
                    QUrl::fromLocalFile(QFileInfo(existing.outputPath).absolutePath()));
#endif
                return;
            }
        }
    });
    connect(card.removeButton, &QPushButton::clicked, this, [this, frame]() {
        for (int i = 0; i < m_cards.size(); i++) {
            if (m_cards[i].frame == frame) {
                emit removeCardClicked(i);
                return;
            }
        }
    });

    // Insert before the stretch at the end
    m_cardsLayout->insertWidget(m_cardsLayout->count() - 1, card.frame);
    m_cards.append(card);
    m_emptyLabel->setVisible(false);
    setCardState(static_cast<int>(m_cards.size()) - 1, CardState::Waiting);
}

void SimpleView::removeCard(int row)
{
    if (row < 0 || row >= m_cards.size()) {
        return;
    }
    m_cards[row].frame->deleteLater();
    m_cards.removeAt(row);
    m_emptyLabel->setVisible(m_cards.isEmpty());
}

void SimpleView::setCardOutput(int row, const QString &outputPath, const QString &sizeText)
{
    if (row < 0 || row >= m_cards.size()) {
        return;
    }
    Card &card = m_cards[row];
    card.outputPath = outputPath;
    Q_UNUSED(sizeText);
    QFileInfo out(outputPath);
    QString suffix = out.suffix().toLower();
    bool photo = suffix == "jpg" || suffix == "jpeg" || suffix == "png";
    card.outputLabel->setText(tr("Saved: %1\\%2")
                                  .arg(photo ? tr("Pictures\\OliVision") : tr("Videos\\OliVision"),
                                       out.fileName()));
    card.outputLabel->setVisible(true);
    card.nameRow->setVisible(false);
    card.openFolderButton->setVisible(true);
}

void SimpleView::setCardSizes(int row, const QString &sizesText)
{
    if (row >= 0 && row < m_cards.size()) {
        m_cards[row].sizesLabel->setText(sizesText);
    }
}

void SimpleView::setCardState(int row, CardState state, const QString &detail)
{
    if (row < 0 || row >= m_cards.size()) {
        return;
    }
    Card &card = m_cards[row];

    QString text;
    QString color = kMuted;
    QString chunk = kGreen;
    switch (state) {
    case CardState::Waiting:
        text = tr("waiting");
        break;
    case CardState::Running:
        text = detail.isEmpty() ? tr("running") : detail;
        break;
    case CardState::Paused:
        text = tr("paused");
        color = "#ffc107";
        chunk = "#ffc107";
        break;
    case CardState::Done:
        text = tr("✓ Done");
        color = kGreen;
        card.progressBar->setRange(0, 1);
        card.progressBar->setValue(1);
        break;
    case CardState::Failed:
        text = tr("Failed");
        color = "#ff6b6b";
        chunk = "#ff6b6b";
        break;
    case CardState::Aborted:
        text = tr("Aborted");
        color = "#ff6b6b";
        chunk = "#ff6b6b";
        break;
    }
    card.statusLabel->setText(text);
    card.statusLabel->setStyleSheet(QString("color: %1; border: none;").arg(color));
    card.progressBar->setStyleSheet(
        QString("QProgressBar::chunk { background: %1; border-radius: 2px; }").arg(chunk));
    card.removeButton->setVisible(state != CardState::Running && state != CardState::Paused);
    card.nameEdit->setEnabled(state == CardState::Waiting || state == CardState::Failed
                              || state == CardState::Aborted);
}

QString SimpleView::cardOutputName(int row) const
{
    if (row < 0 || row >= m_cards.size()) {
        return QString();
    }
    return m_cards[row].nameEdit->text().trimmed();
}

void SimpleView::setCardProgress(int row, int processed, int total, const QString &remainingText)
{
    if (row < 0 || row >= m_cards.size()) {
        return;
    }
    Card &card = m_cards[row];
    card.progressBar->setRange(0, total > 0 ? total : 1);
    card.progressBar->setValue(processed);
    card.statusLabel->setText(remainingText);
}

int SimpleView::cardCount() const
{
    return static_cast<int>(m_cards.size());
}

void SimpleView::setGpuName(const QString &name)
{
    m_gpuLabel->setText(name);
}

void SimpleView::setRunning(bool running, bool paused)
{
    m_paused = paused;
    m_startButton->setVisible(!running);
    m_pauseButton->setVisible(running);
    m_abortButton->setVisible(running);
    m_pauseButton->setText(paused ? tr("Resume") : tr("Pause"));

    // The choices only apply to videos that have not started yet
    for (QButtonGroup *group : {m_targetGroup, m_speedGroup, m_denoiseGroup, m_cartoonGroup}) {
        for (QAbstractButton *button : group->buttons()) {
            button->setEnabled(!running);
        }
    }
}

QString SimpleView::describeSize(const QSize &size, bool photo)
{
    if (photo) {
        double megapixels = static_cast<double>(size.width()) * size.height() / 1e6;
        QString mp = megapixels < 10 ? QString::number(megapixels, 'f', 1).replace('.', ',')
                                     : QString::number(qRound(megapixels));
        return QString("%1 MP (%2 × %3)").arg(mp).arg(size.width()).arg(size.height());
    }

    // Name by the shorter side so that portrait videos get the same names
    int shortSide = (std::min)(size.width(), size.height());
    int longSide = (std::max)(size.width(), size.height());
    QString name;
    if (shortSide >= 2000) {
        name = "4K";
    } else if (shortSide >= 1300) {
        name = "QHD";
    } else if (shortSide >= 1000) {
        name = "Full HD";
    } else if (shortSide >= 700) {
        name = "HD";
    } else {
        name = "SD";
    }

    // Narrower than widescreen: say so, otherwise 1440 × 1080 looks wrong next to "Full HD"
    if (shortSide > 0) {
        double ratio = static_cast<double>(longSide) / shortSide;
        if (std::abs(ratio - 4.0 / 3.0) < 0.04) {
            name += " 4:3";
        } else if (std::abs(ratio - 3.0 / 2.0) < 0.03) {
            name += " 3:2";
        } else if (std::abs(ratio - 1.0) < 0.03) {
            name += " 1:1";
        } else if (ratio < 16.0 / 9.0 - 0.05) {
            name += " " + tr("narrow");
        }
    }
    return QString("%1 (%2 × %3)").arg(name).arg(size.width()).arg(size.height());
}

void SimpleView::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void SimpleView::dropEvent(QDropEvent *event)
{
    QStringList files;
    for (const QUrl &url : event->mimeData()->urls()) {
        if (url.isLocalFile()) {
            files << url.toLocalFile();
        }
    }
    if (!files.isEmpty()) {
        emit filesDropped(files);
    }
}

bool SimpleView::eventFilter(QObject *watched, QEvent *event)
{
    // The header works as title bar: drag to move, double click to maximize
    if (event->type() == QEvent::MouseButtonPress) {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::LeftButton && window()->windowHandle() != nullptr) {
            window()->windowHandle()->startSystemMove();
            return true;
        }
    } else if (event->type() == QEvent::MouseButtonDblClick) {
        emit maximizeClicked();
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

Qt::Edges SimpleView::edgesAt(const QPoint &pos) const
{
    const int border = 8;
    Qt::Edges edges;
    if (window()->isMaximized()) {
        return edges;
    }
    if (pos.x() < border) {
        edges |= Qt::LeftEdge;
    }
    if (pos.x() > width() - border) {
        edges |= Qt::RightEdge;
    }
    if (pos.y() < border) {
        edges |= Qt::TopEdge;
    }
    if (pos.y() > height() - border) {
        edges |= Qt::BottomEdge;
    }
    return edges;
}

void SimpleView::mouseMoveEvent(QMouseEvent *event)
{
    // Resize cursor at the window edges
    Qt::Edges edges = edgesAt(event->position().toPoint());
    if ((edges & Qt::LeftEdge && edges & Qt::TopEdge)
        || (edges & Qt::RightEdge && edges & Qt::BottomEdge)) {
        setCursor(Qt::SizeFDiagCursor);
    } else if ((edges & Qt::RightEdge && edges & Qt::TopEdge)
               || (edges & Qt::LeftEdge && edges & Qt::BottomEdge)) {
        setCursor(Qt::SizeBDiagCursor);
    } else if (edges & (Qt::LeftEdge | Qt::RightEdge)) {
        setCursor(Qt::SizeHorCursor);
    } else if (edges & (Qt::TopEdge | Qt::BottomEdge)) {
        setCursor(Qt::SizeVerCursor);
    } else {
        unsetCursor();
    }
    QWidget::mouseMoveEvent(event);
}

void SimpleView::mousePressEvent(QMouseEvent *event)
{
    Qt::Edges edges = edgesAt(event->position().toPoint());
    if (event->button() == Qt::LeftButton && edges && window()->windowHandle() != nullptr) {
        window()->windowHandle()->startSystemResize(edges);
        return;
    }
    QWidget::mousePressEvent(event);
}
