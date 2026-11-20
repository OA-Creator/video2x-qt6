#ifndef SIMPLEVIEW_H
#define SIMPLEVIEW_H

#include <QButtonGroup>
#include <QFrame>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QStringList>
#include <QVBoxLayout>
#include <QWidget>

// The simple OliVision view: a list of videos on the left and four questions on the right.
// It only shows things and reports clicks; the main window does the processing.
class SimpleView : public QWidget
{
    Q_OBJECT

public:
    enum class CardState { Waiting, Running, Paused, Done, Failed, Aborted };

    explicit SimpleView(QWidget *parent = nullptr);

    // The four choices
    bool target4k() const;
    int speed() const; // 0 = best quality, 1 = balanced, 2 = fast
    bool denoise() const;
    bool cartoon() const;

    // Video cards
    void clearCards();
    void addCard(const QString &fileName, const QString &sizesText);
    void removeCard(int row);
    void setCardSizes(int row, const QString &sizesText);
    void setCardState(int row, CardState state, const QString &detail = QString());
    void setCardProgress(int row, int processed, int total, const QString &remainingText);
    int cardCount() const;

    void setGpuName(const QString &name);

    // Idle, running or paused
    void setRunning(bool running, bool paused);

    // Name of a resolution, e.g. "Full HD (1920 × 1080)"
    static QString describeSize(const QSize &size);

signals:
    void filesDropped(const QStringList &fileNames);
    void addFilesClicked();
    void removeCardClicked(int row);
    void choicesChanged();
    void startClicked();
    void pauseClicked();
    void resumeClicked();
    void abortClicked();
    void advancedClicked();
    void logsClicked();
    void aboutClicked();

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    struct Card
    {
        QFrame *frame = nullptr;
        QLabel *nameLabel = nullptr;
        QLabel *statusLabel = nullptr;
        QLabel *sizesLabel = nullptr;
        QProgressBar *progressBar = nullptr;
        QPushButton *removeButton = nullptr;
    };

    QButtonGroup *addChoice(QVBoxLayout *layout,
                            const QString &question,
                            const QStringList &options,
                            int defaultIndex,
                            const QString &settingsKey);
    void saveChoices();

    QVBoxLayout *m_cardsLayout = nullptr;
    QLabel *m_emptyLabel = nullptr;
    QLabel *m_gpuLabel = nullptr;
    QList<Card> m_cards;

    QButtonGroup *m_targetGroup = nullptr;
    QButtonGroup *m_speedGroup = nullptr;
    QButtonGroup *m_denoiseGroup = nullptr;
    QButtonGroup *m_cartoonGroup = nullptr;

    QPushButton *m_startButton = nullptr;
    QPushButton *m_pauseButton = nullptr;
    QPushButton *m_abortButton = nullptr;
    bool m_paused = false;
};

#endif // SIMPLEVIEW_H
