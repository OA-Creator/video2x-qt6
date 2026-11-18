#ifndef TASKCONFIGDIALOG_H
#define TASKCONFIGDIALOG_H

#include <QComboBox>
#include <QDialog>
#include <QList>
#include <QMap>
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

private slots:
    void on_applyPushButton_clicked();
    void on_addCustomOptionPushButton_clicked();
    void on_removeSelectedOptionPushButton_clicked();
    void on_libplaceboSelectCustomGlslShaderPushButton_clicked();

private:
    void populateVulkanDevices();
    void populateRealesrganModels();
    void setupEncoderPresets();
    void applyEncoderPreset(int index);
    void addCustomEncoderOption(const QString &option, const QString &value);

    // Scaling factors available for each Real-ESRGAN model found in the models directory
    QMap<QString, QList<int>> m_realesrganModelScales;
    QComboBox *m_encoderPresetComboBox = nullptr;

    Ui::TaskConfigDialog *ui;
    QStandardItemModel *m_customEncoderOptionsTableModel;
};

#endif // TASKCONFIGDIALOG_H
