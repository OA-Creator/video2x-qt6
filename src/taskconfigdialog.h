#ifndef TASKCONFIGDIALOG_H
#define TASKCONFIGDIALOG_H

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <vector>
#include <QLabel>
#include <QStringList>
#include <QList>
#include <QMap>
#include <QSize>
#include <QSpinBox>
#include <QStandardItemModel>

#include "taskconfig.h"

namespace Ui {
class TaskConfigDialog;
}

class TaskConfigDialog : public QDialog
{
    Q_OBJECT

public:
    explicit TaskConfigDialog(QWidget *parent = nullptr);
    ~TaskConfigDialog();
    void updateScalingFactorAndNoiseLevelRange();
    void execErrorMessage(const QString &message);
    std::optional<TaskConfig> getTaskConfig();
    void setTaskConfig(const TaskConfig &taskConfig);
    void setOutputSuffix(QString suffix);
    void setOutputCodec(QString codec);
    void setInputFiles(const QStringList &fileNames);

    // Simple view: set the four choices on top of the saved advanced settings
    // speed: 0 = best quality, 1 = balanced, 2 = fast
    // targetSize: 0 = keep (only improve), 1 = Full HD, 2 = QHD, 3 = 4K
    void applySimpleChoices(int targetSize, int speed, bool denoise, bool cartoon);

    // What the current settings make of a file
    struct ResultInfo
    {
        QSize inSize;        // as displayed
        QSize outSize;       // as displayed
        QSize aiSize;        // size the AI produces per frame
        QSize storedSize;    // as stored in the file
        double durationSeconds = 0.0;
        double frameRate = 0.0;
        qint64 frameCount = 0;
        qint64 inBytes = 0;
        qint64 estimatedOutBytes = 0;
        QString modelKey;    // what the measured speed is stored under
    };
    bool resultInfo(const QString &fileName, ResultInfo &info) const;

    // Name of the selected graphics card
    QString selectedGpuName() const;

private slots:
    void on_applyPushButton_clicked();
    void on_addCustomOptionPushButton_clicked();
    void on_removeSelectedOptionPushButton_clicked();
    void on_libplaceboSelectCustomGlslShaderPushButton_clicked();

private:
    void populateVulkanDevices();
    void populateRealesrganModels();
    void setupEncoderPresets();
    void setupGpuWorkers();
    void setupMaxResolution();
    void setupHardwareDetection();
    void updateHardwareInfo();
    QString detectedEncoder() const;
    int recommendedGpuWorkers() const;
    void applyEncoderSettings(const QString &codec);
    static QString encoderDescription(const QString &codec);
    void saveSettings();
    QString realesrganModelName() const;
    static QString realesrganModelDescription(const QString &modelName);
    QLabel *m_modelDescriptionLabel = nullptr;
    bool isAnimeHidden(QComboBox *comboBox, int index) const;
    void setupModelLabelsAndAnimeFilter();
    void applyAnimeFilter();
    QCheckBox *m_showAnimeCheckBox = nullptr;
    void restoreSettings();

    struct GpuInfo
    {
        QString name;
        uint32_t vendorId = 0;
        bool integrated = false;
        quint64 vramBytes = 0;
    };
    std::vector<GpuInfo> m_gpus;
    QLabel *m_hardwareInfoLabel = nullptr;
    void updateOutputPreview();
    QSize finalSizeFor(int inWidth, int inHeight, QSize *aiSize = nullptr) const;
    bool m_keepOriginalSize = false;
    static bool probeVideoSize(const QString &fileName, int &width, int &height, bool &rotated);
    void applyEncoderPreset(int index);
    void addCustomEncoderOption(const QString &option, const QString &value);

    // Scaling factors available for each Real-ESRGAN model found in the models directory
    QMap<QString, QList<int>> m_realesrganModelScales;
    QComboBox *m_encoderPresetComboBox = nullptr;
    QSpinBox *m_gpuWorkersSpinBox = nullptr;
    QComboBox *m_maxResolutionComboBox = nullptr;
    QComboBox *m_speedComboBox = nullptr;
    QLabel *m_outputPreviewLabel = nullptr;
    QStringList m_inputFiles;
    int m_inputWidth = 0;
    int m_inputHeight = 0;
    bool m_inputRotated = false;

    Ui::TaskConfigDialog *ui;
    QStandardItemModel *m_customEncoderOptionsTableModel;
};

#endif // TASKCONFIGDIALOG_H
