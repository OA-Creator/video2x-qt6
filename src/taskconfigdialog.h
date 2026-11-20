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
    void applySimpleChoices(bool target4k, int speed, bool denoise, bool cartoon);

    // Size of the file and of the result with the current settings (as displayed)
    bool resultSizes(const QString &fileName, QSize &inSize, QSize &outSize) const;

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
    QSize finalSizeFor(int inWidth, int inHeight) const;
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
