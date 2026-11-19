#include "taskconfigdialog.h"
#include "ui_taskconfigdialog.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/display.h>
}

#include <algorithm>
#include <cmath>

#include <QFileInfo>
#include <QColor>
#include <QPalette>
#include <QSettings>
#include <QSignalBlocker>
#include <QSize>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QRegularExpression>
#include <QPushButton>
#include <QTimer>

extern "C" {
#include <libavutil/hwcontext.h>
}

#include <libvideo2x/logger_manager.h>
#include <vulkan/vulkan.h>

#include "utils.h"

// Processing mode selection
#define FILTER_MODE 0
#define INTERP_MODE 1

// Filter selection
#define PLACEBO_MODE 0
#define ESRGAN_MODE 1
#define CUGAN_MODE 2

// Interpolation selection
#define RIFE_MODE 0

// Real-ESRGAN models
#define REALESRGAN_MODEL_REALESRGAN_PLUS 0
#define REALESRGAN_MODEL_REALESRGAN_PLUS_ANIME 1
#define REALESRGAN_MODEL_REALESR_ANIMEVIDEOV3 2
#define REALESRGAN_MODEL_REALESR_GENERALV3 3
// Indices above this belong to additional models found in the models directory
#define REALESRGAN_MODEL_LAST_BUILTIN REALESRGAN_MODEL_REALESR_GENERALV3

// Encoder presets
#define ENCODER_PRESET_AUTO 0
#define ENCODER_PRESET_CUSTOM 1
#define ENCODER_PRESET_LIBX264 2
#define ENCODER_PRESET_AMD_AV1 3
#define ENCODER_PRESET_AMD_HEVC 4
#define ENCODER_PRESET_AMD_H264 5

// Real-CUGAN models
#define REALCUGAN_MODEL_NOSE 0
#define REALCUGAN_MODEL_PRO 1
#define REALCUGAN_MODEL_SE 2

TaskConfigDialog::TaskConfigDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::TaskConfigDialog)
{
    ui->setupUi(this);

    // Setup the model and headers for the custom options table
    m_customEncoderOptionsTableModel = new QStandardItemModel(this);
    m_customEncoderOptionsTableModel->setHorizontalHeaderLabels(QStringList()
                                                                << tr("Option") << tr("Value"));
    ui->customEncoderOptionsTableView->setModel(m_customEncoderOptionsTableModel);

    // Add preset custom options crf=20 and preset=slow
    addCustomEncoderOption("crf", "20");
    addCustomEncoderOption("preset", "slow");

    // Add additional Real-ESRGAN models found in the models directory
    populateRealesrganModels();

    // Add the encoder preset selector
    setupEncoderPresets();

    // Add the selector for the number of parallel GPU jobs
    setupGpuWorkers();

    // Add the maximum resolution selector and the result preview
    setupMaxResolution();

    // Set components default visibility
    ui->interpolatorSelectionGroupBox->setVisible(false);
    ui->frameInterpolationOptionsGroupBox->setVisible(false);
    ui->libplaceboOptionsGroupBox->setVisible(false);
    ui->realcuganOptionsGroupBox->setVisible(false);
    ui->rifeOptionsGroupBox->setVisible(false);

    ui->outputWidthLabel->setVisible(false);
    ui->outputHeightLabel->setVisible(false);
    ui->outputWidthSpinBox->setVisible(false);
    ui->outputHeightSpinBox->setVisible(false);

    ui->noiseLevelLabel->setVisible(false);
    ui->noiseLevelSpinBox->setVisible(false);

    // Make processing mode updates also trigger filter/interpolator selection updates
    connect(ui->processingModeSelectionComboBox,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,
            [this](int) {
                emit ui->filterSelectionComboBox->currentIndexChanged(
                    ui->filterSelectionComboBox->currentIndex());
            });
    connect(ui->processingModeSelectionComboBox,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,
            [this](int) {
                emit ui->interpolationSelectionComboBox->currentIndexChanged(
                    ui->interpolationSelectionComboBox->currentIndex());
            });

    // Filter options
    connect(ui->processingModeSelectionComboBox,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,
            [this](int index) { ui->filterSelectionGroupBox->setVisible(index == FILTER_MODE); });
    connect(ui->processingModeSelectionComboBox,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,
            [this](int index) { ui->filterOptionsGroupBox->setVisible(index == FILTER_MODE); });

    connect(ui->filterSelectionComboBox,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,
            [this](int index) {
                // Output width and height
                ui->outputWidthLabel->setVisible(index == PLACEBO_MODE);
                ui->outputWidthSpinBox->setVisible(index == PLACEBO_MODE);
                ui->outputHeightLabel->setVisible(index == PLACEBO_MODE);
                ui->outputHeightSpinBox->setVisible(index == PLACEBO_MODE);

                // Scaling factor
                ui->scalingFactorLabel->setVisible(index == ESRGAN_MODE || index == CUGAN_MODE);
                ui->scalingFactorSpinBox->setVisible(index == ESRGAN_MODE || index == CUGAN_MODE);

                // Noise level
                ui->noiseLevelLabel->setVisible(index == CUGAN_MODE
                                                || (index == ESRGAN_MODE
                                                    && ui->realesrganModelComboBox->currentIndex()
                                                           == REALESRGAN_MODEL_REALESR_GENERALV3));
                ui->noiseLevelSpinBox->setVisible(index == CUGAN_MODE
                                                  || (index == ESRGAN_MODE
                                                      && ui->realesrganModelComboBox->currentIndex()
                                                             == REALESRGAN_MODEL_REALESR_GENERALV3));
            });

    connect(ui->filterSelectionComboBox,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,
            &TaskConfigDialog::updateScalingFactorAndNoiseLevelRange);

    // Frame interpolation options
    connect(ui->processingModeSelectionComboBox,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,
            [this](int index) {
                ui->frameInterpolationOptionsGroupBox->setVisible(index == INTERP_MODE);
            });
    connect(ui->processingModeSelectionComboBox,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,
            [this](int index) {
                ui->interpolatorSelectionGroupBox->setVisible(index == INTERP_MODE);
            });

    // Frame rate multiplier
    connect(ui->interpolationSelectionComboBox,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,
            [this](int index) { ui->frameRateMultiplierLabel->setVisible(index == RIFE_MODE); });
    connect(ui->interpolationSelectionComboBox,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,
            [this](int index) { ui->frameRateMultiplierSpinBox->setVisible(index == RIFE_MODE); });

    // Filter selection
    connect(ui->filterSelectionComboBox,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,
            [this](int index) {
                // libplacebo options
                ui->libplaceboOptionsGroupBox->setVisible(
                    ui->processingModeSelectionComboBox->currentIndex() == FILTER_MODE
                    && index == PLACEBO_MODE);

                // Real-ESRGAN options
                ui->realesrganOptionsGroupBox->setVisible(
                    ui->processingModeSelectionComboBox->currentIndex() == FILTER_MODE
                    && index == ESRGAN_MODE);

                // Real-CUGAN options
                ui->realcuganOptionsGroupBox->setVisible(
                    ui->processingModeSelectionComboBox->currentIndex() == FILTER_MODE
                    && index == CUGAN_MODE);
            });

    // Real-ESRGAN options
    connect(ui->realesrganModelComboBox,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,
            &TaskConfigDialog::updateScalingFactorAndNoiseLevelRange);
    connect(ui->realesrganModelComboBox,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,
            [this](int index) {
                ui->noiseLevelLabel->setVisible(
                    ui->filterSelectionComboBox->currentIndex() == CUGAN_MODE
                    || (ui->filterSelectionComboBox->currentIndex() == ESRGAN_MODE
                        && index == REALESRGAN_MODEL_REALESR_GENERALV3));
                ui->noiseLevelSpinBox->setVisible(
                    ui->filterSelectionComboBox->currentIndex() == CUGAN_MODE
                    || (ui->filterSelectionComboBox->currentIndex() == ESRGAN_MODE
                        && index == REALESRGAN_MODEL_REALESR_GENERALV3));
            });

    // Real-CUGAN options
    connect(ui->realcuganModelComboBox,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,
            &TaskConfigDialog::updateScalingFactorAndNoiseLevelRange);

    // Interpolation selection
    connect(ui->interpolationSelectionComboBox,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,
            [this](int index) {
                ui->rifeOptionsGroupBox->setVisible(
                    ui->processingModeSelectionComboBox->currentIndex() == INTERP_MODE
                    && index == RIFE_MODE);
            });

    // Cancel button
    connect(ui->cancelPushButton, &QPushButton::clicked, this, &QDialog::reject);

    // Enumerate and populate Vulkan devices
    populateVulkanDevices();

    // Real recordings are the most common case
    ui->realesrganModelComboBox->setCurrentIndex(REALESRGAN_MODEL_REALESR_GENERALV3);

    // Show the detected hardware and choose matching settings
    setupHardwareDetection();

    // Use the settings of the last task
    restoreSettings();

    // Set default optimal height
    resize(385, 560);
}

TaskConfigDialog::~TaskConfigDialog()
{
    delete ui;
}

void TaskConfigDialog::execErrorMessage(const QString &message)
{
    QMessageBox msgBox;
    msgBox.setIcon(QMessageBox::Critical);
    msgBox.setWindowTitle(tr("Error"));
    msgBox.setText(message);
    msgBox.setStandardButtons(QMessageBox::Ok);
    msgBox.exec();
}

void TaskConfigDialog::updateScalingFactorAndNoiseLevelRange()
{
    if (ui->filterSelectionComboBox->currentIndex() == ESRGAN_MODE) {
        const QList<int> modelScales = m_realesrganModelScales.value(
            ui->realesrganModelComboBox->currentText());

        if (ui->realesrganModelComboBox->currentIndex() > REALESRGAN_MODEL_LAST_BUILTIN
            && !modelScales.isEmpty()) {
            // Additional models: allow the scaling factors that exist as model files
            ui->scalingFactorSpinBox->setRange(*std::min_element(modelScales.begin(),
                                                                 modelScales.end()),
                                               *std::max_element(modelScales.begin(),
                                                                 modelScales.end()));
        } else {
            // For Real-ESRGAN, the maximum scaling factor is 4
            ui->scalingFactorSpinBox->setMaximum(4);

            // Set minimum scaling factor for Real-ESRGAN
            if (ui->realesrganModelComboBox->currentIndex()
                == REALESRGAN_MODEL_REALESR_ANIMEVIDEOV3) {
                ui->scalingFactorSpinBox->setMinimum(2);
            } else {
                ui->scalingFactorSpinBox->setMinimum(4);
            }
        }

        if (ui->realesrganModelComboBox->currentIndex() == REALESRGAN_MODEL_REALESR_ANIMEVIDEOV3) {
            ui->noiseLevelSpinBox->setMinimum(0);
            ui->noiseLevelSpinBox->setMaximum(1);
        }
    } else if (ui->filterSelectionComboBox->currentIndex() == CUGAN_MODE) {
        // For Real-CUGAN, the minimum scaling factor is 2
        ui->scalingFactorSpinBox->setMinimum(2);

        // Set maximum scaling factor and noise level for Real-CUGAN
        switch (ui->realcuganModelComboBox->currentIndex()) {
        case REALCUGAN_MODEL_NOSE:
            ui->scalingFactorSpinBox->setMaximum(2);
            ui->noiseLevelSpinBox->setMinimum(0);
            ui->noiseLevelSpinBox->setMaximum(0);
            break;
        case REALCUGAN_MODEL_PRO:
            ui->scalingFactorSpinBox->setMaximum(3);
            ui->noiseLevelSpinBox->setMinimum(-1);
            ui->noiseLevelSpinBox->setMaximum(3);
            break;
        case REALCUGAN_MODEL_SE:
        default:
            ui->scalingFactorSpinBox->setMaximum(4);
            ui->noiseLevelSpinBox->setMinimum(-1);
            ui->noiseLevelSpinBox->setMaximum(3);
            break;
        }
    } else {
        ui->scalingFactorSpinBox->setMinimum(1);
        ui->scalingFactorSpinBox->setMaximum(99);
    }

    // Clamp scaling factor within the allowed range
    if (ui->scalingFactorSpinBox->value() > ui->scalingFactorSpinBox->maximum()) {
        ui->scalingFactorSpinBox->setValue(ui->scalingFactorSpinBox->maximum());
    } else if (ui->scalingFactorSpinBox->value() < ui->scalingFactorSpinBox->minimum()) {
        ui->scalingFactorSpinBox->setValue(ui->scalingFactorSpinBox->minimum());
    }

    // Clamp noise level within the allowed range
    if (ui->noiseLevelSpinBox->value() > ui->noiseLevelSpinBox->maximum()
        || ui->noiseLevelSpinBox->value() < ui->noiseLevelSpinBox->minimum()) {
        ui->noiseLevelSpinBox->setValue(0);
    }
}

void TaskConfigDialog::addCustomEncoderOption(const QString &option, const QString &value)
{
    m_customEncoderOptionsTableModel->appendRow(QList<QStandardItem *>()
                                                << new QStandardItem(option)
                                                << new QStandardItem(value));
}

void TaskConfigDialog::populateRealesrganModels()
{
    // Model files are named <model>-x<scale>.param / <model>-x<scale>.bin
    QDir modelsDir(QCoreApplication::applicationDirPath() + "/models/realesrgan");
    if (!modelsDir.exists()) {
        return;
    }

    static const QRegularExpression modelFileRegex("^(.+)-x(\\d+)\\.param$");
    const QStringList paramFiles = modelsDir.entryList(QStringList() << "*.param",
                                                       QDir::Files,
                                                       QDir::Name);
    for (const QString &paramFile : paramFiles) {
        QRegularExpressionMatch match = modelFileRegex.match(paramFile);
        if (!match.hasMatch()) {
            continue;
        }

        QString modelName = match.captured(1);
        int scale = match.captured(2).toInt();

        // The -wdn variant is selected automatically through the noise level
        if (modelName.endsWith("-wdn")) {
            continue;
        }

        // Both the .param and the .bin file are required
        QString binFile = modelName + "-x" + QString::number(scale) + ".bin";
        if (!QFile::exists(modelsDir.filePath(binFile))) {
            continue;
        }

        if (!m_realesrganModelScales[modelName].contains(scale)) {
            m_realesrganModelScales[modelName].append(scale);
        }
    }

    // Append the models that are not already part of the built-in list
    for (auto it = m_realesrganModelScales.constBegin(); it != m_realesrganModelScales.constEnd();
         ++it) {
        if (ui->realesrganModelComboBox->findText(it.key()) < 0) {
            ui->realesrganModelComboBox->addItem(it.key());
        }
    }
}

void TaskConfigDialog::setupEncoderPresets()
{
    m_encoderPresetComboBox = new QComboBox(this);
    m_encoderPresetComboBox->addItem(tr("Automatic (recommended)"));
    m_encoderPresetComboBox->addItem(tr("Custom"));
    m_encoderPresetComboBox->addItem(tr("Default (libx264, CPU)"));
    m_encoderPresetComboBox->addItem(tr("AMD AV1 (GPU)"));
    m_encoderPresetComboBox->addItem(tr("AMD HEVC (GPU)"));
    m_encoderPresetComboBox->addItem(tr("AMD H.264 (GPU)"));
    m_encoderPresetComboBox->setToolTip(
        tr("Fills in the codec, bit rate and custom options for a common encoder setup. "
           "Automatic picks the best encoder the graphics card supports. "
           "The AMD presets encode on the graphics card (AMF)."));

    // A bit rate of 0 lets hardware encoders choose one that fits the output size
    ui->bitRateSpinBox->setSpecialValueText(tr("Automatic"));

    QHBoxLayout *presetLayout = new QHBoxLayout();
    presetLayout->addWidget(new QLabel(tr("Preset"), this));
    presetLayout->addWidget(m_encoderPresetComboBox);
    ui->verticalLayout_14->insertLayout(0, presetLayout);

    connect(m_encoderPresetComboBox,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,
            &TaskConfigDialog::applyEncoderPreset);
}

void TaskConfigDialog::setupGpuWorkers()
{
    m_gpuWorkersSpinBox = new QSpinBox(this);
    m_gpuWorkersSpinBox->setRange(1, 4);
    m_gpuWorkersSpinBox->setValue(3);
    m_gpuWorkersSpinBox->setToolTip(
        tr("Number of frames the graphics card works on at the same time. "
           "More parallel jobs keep the GPU busier but use more video memory. "
           "1 processes frames one after another."));

    QHBoxLayout *workersLayout = new QHBoxLayout();
    workersLayout->addWidget(new QLabel(tr("Parallel GPU jobs"), this));
    workersLayout->addWidget(m_gpuWorkersSpinBox);
    ui->verticalLayout_4->addLayout(workersLayout);
}

bool TaskConfigDialog::probeVideoSize(const QString &fileName, int &width, int &height, bool &rotated)
{
    width = 0;
    height = 0;
    rotated = false;

    AVFormatContext *fmtCtx = nullptr;
    if (avformat_open_input(&fmtCtx, fileName.toUtf8().constData(), nullptr, nullptr) < 0) {
        return false;
    }

    int streamIndex = av_find_best_stream(fmtCtx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (streamIndex >= 0 && fmtCtx->streams[streamIndex]->codecpar->width <= 0) {
        // Some containers only know the size after reading a few packets
        avformat_find_stream_info(fmtCtx, nullptr);
        streamIndex = av_find_best_stream(fmtCtx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    }

    if (streamIndex >= 0) {
        const AVCodecParameters *par = fmtCtx->streams[streamIndex]->codecpar;
        width = par->width;
        height = par->height;

        const AVPacketSideData *sideData = av_packet_side_data_get(par->coded_side_data,
                                                                   par->nb_coded_side_data,
                                                                   AV_PKT_DATA_DISPLAYMATRIX);
        if (sideData != nullptr && sideData->size >= 9 * static_cast<int>(sizeof(int32_t))) {
            double rotation = av_display_rotation_get(
                reinterpret_cast<const int32_t *>(sideData->data));
            rotated = std::abs(std::abs(std::fmod(rotation, 180.0)) - 90.0) < 1.0;
        }
    }

    avformat_close_input(&fmtCtx);
    return width > 0 && height > 0;
}

void TaskConfigDialog::setInputFiles(const QStringList &fileNames)
{
    m_inputFiles = fileNames;
    m_inputWidth = 0;
    m_inputHeight = 0;
    m_inputRotated = false;
    if (!fileNames.isEmpty()) {
        probeVideoSize(fileNames.first(), m_inputWidth, m_inputHeight, m_inputRotated);
    }
    updateOutputPreview();
}

void TaskConfigDialog::setupMaxResolution()
{
    m_maxResolutionComboBox = new QComboBox(this);
    m_maxResolutionComboBox->addItem(tr("No limit"), QSize(0, 0));
    m_maxResolutionComboBox->addItem(tr("Full HD (1920 × 1080)"), QSize(1920, 1080));
    m_maxResolutionComboBox->addItem(tr("QHD (2560 × 1440)"), QSize(2560, 1440));
    m_maxResolutionComboBox->addItem(tr("4K UHD (3840 × 2160)"), QSize(3840, 2160));
    m_maxResolutionComboBox->setCurrentIndex(3);
    m_maxResolutionComboBox->setToolTip(
        tr("Results larger than this are scaled down after upscaling, keeping the aspect "
           "ratio. Portrait videos are handled automatically."));

    QLabel *maxResolutionLabel = new QLabel(tr("Maximum resolution"), this);
    QHBoxLayout *maxResolutionLayout = new QHBoxLayout();
    maxResolutionLayout->addWidget(maxResolutionLabel);
    maxResolutionLayout->addWidget(m_maxResolutionComboBox);
    ui->verticalLayout_9->addLayout(maxResolutionLayout);

    m_outputPreviewLabel = new QLabel(this);
    m_outputPreviewLabel->setTextFormat(Qt::RichText);
    m_outputPreviewLabel->setWordWrap(true);
    m_outputPreviewLabel->setFrameShape(QFrame::StyledPanel);
    m_outputPreviewLabel->setMargin(6);
    ui->verticalLayout_9->addWidget(m_outputPreviewLabel);

    auto update = [this]() { updateOutputPreview(); };
    connect(m_maxResolutionComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, update);
    connect(ui->scalingFactorSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, update);
    connect(ui->outputWidthSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, update);
    connect(ui->outputHeightSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, update);
    connect(ui->filterSelectionComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, update);
    connect(ui->realesrganModelComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, update);
    connect(ui->processingModeSelectionComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, update);

    updateOutputPreview();
}

void TaskConfigDialog::updateOutputPreview()
{
    if (m_outputPreviewLabel == nullptr) {
        return;
    }

    auto sizeText = [](int w, int h) { return QString("%1 × %2").arg(w).arg(h); };
    // Halfway between text and background, readable in light and dark mode
    const QColor textColor = palette().color(QPalette::WindowText);
    const QColor windowColor = palette().color(QPalette::Window);
    const QString mutedColor = QColor((textColor.red() + windowColor.red()) / 2,
                                      (textColor.green() + windowColor.green()) / 2,
                                      (textColor.blue() + windowColor.blue()) / 2)
                                   .name();
    const QSize maxSize = m_maxResolutionComboBox->currentData().toSize();

    // Size produced by the selected filter for the given input size
    auto processedSize = [this](int w, int h) {
        if (ui->filterSelectionComboBox->currentIndex() == PLACEBO_MODE) {
            return QSize(ui->outputWidthSpinBox->value(), ui->outputHeightSpinBox->value());
        }
        int factor = ui->scalingFactorSpinBox->value();
        return QSize(w * factor, h * factor);
    };

    if (m_inputWidth <= 0 || m_inputHeight <= 0) {
        QString text;
        if (maxSize.width() > 0) {
            text = tr("Result: at most <b>%1</b>").arg(sizeText(maxSize.width(), maxSize.height()));
        } else if (ui->filterSelectionComboBox->currentIndex() == PLACEBO_MODE) {
            text = tr("Result: <b>%1</b>")
                       .arg(sizeText(ui->outputWidthSpinBox->value(),
                                     ui->outputHeightSpinBox->value()));
        } else {
            text = tr("Result: <b>%1 × the original size</b>").arg(ui->scalingFactorSpinBox->value());
        }
        m_outputPreviewLabel->setText(text);
        return;
    }

    // Sizes as stored in the file, the encoder keeps the rotation
    QSize processed = processedSize(m_inputWidth, m_inputHeight);
    int finalWidth = processed.width();
    int finalHeight = processed.height();
    if (maxSize.width() > 0 && maxSize.height() > 0) {
        int maxWidth = maxSize.width();
        int maxHeight = maxSize.height();
        if ((finalHeight > finalWidth) != (maxHeight > maxWidth)) {
            std::swap(maxWidth, maxHeight);
        }
        if (finalWidth > maxWidth || finalHeight > maxHeight) {
            double factor = (std::min)(static_cast<double>(maxWidth) / finalWidth,
                                     static_cast<double>(maxHeight) / finalHeight);
            finalWidth = (std::max)(2, static_cast<int>(std::floor(finalWidth * factor / 2.0)) * 2);
            finalHeight = (std::max)(2, static_cast<int>(std::floor(finalHeight * factor / 2.0)) * 2);
        }
    }

    // Show the sizes the way the video is displayed
    int inW = m_inputWidth, inH = m_inputHeight;
    int procW = processed.width(), procH = processed.height();
    int outW = finalWidth, outH = finalHeight;
    if (m_inputRotated) {
        std::swap(inW, inH);
        std::swap(procW, procH);
        std::swap(outW, outH);
    }

    QString fileName = QFileInfo(m_inputFiles.first()).fileName().toHtmlEscaped();
    if (m_inputFiles.size() > 1) {
        fileName += " " + tr("(+%1 more)").arg(m_inputFiles.size() - 1);
    }

    QString text = QString("<b>%1</b><br>%2 &nbsp;→&nbsp; <b>%3</b>")
                       .arg(fileName, sizeText(inW, inH), sizeText(outW, outH));

    bool noBenefit = outW <= inW && outH <= inH;
    if (!noBenefit && (procW != outW || procH != outH)) {
        text += QString("<br><span style=\"color:%1\">%2</span>")
                    .arg(mutedColor,
                         tr("Upscaled to %1, then scaled down to the maximum resolution.")
                             .arg(sizeText(procW, procH)));
    }

    if (noBenefit) {
        text += QString("<br><span style=\"color:#d97706\">%1</span>")
                    .arg(tr("The video already has this resolution or more. Upscaling brings no "
                            "benefit here."));
    }

    m_outputPreviewLabel->setText(text);
}

// Try to open an encoder with a typical setup to see if the hardware supports it
static bool canOpenEncoder(const char *name)
{
    const AVCodec *codec = avcodec_find_encoder_by_name(name);
    if (codec == nullptr) {
        return false;
    }

    AVCodecContext *ctx = avcodec_alloc_context3(codec);
    if (ctx == nullptr) {
        return false;
    }

    // Prefer NV12 or YUV420P, otherwise the first software format of the encoder
    AVPixelFormat pixFmt = AV_PIX_FMT_NONE;
    const AVPixelFormat *formats = nullptr;
    int numConfigs = 0;
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 13, 100)
    const void *configs = nullptr;
    if (avcodec_get_supported_config(ctx, codec, AV_CODEC_CONFIG_PIX_FORMAT, 0, &configs, &numConfigs) >= 0) {
        formats = static_cast<const AVPixelFormat *>(configs);
    }
#else
    formats = codec->pix_fmts;
    while (formats != nullptr && formats[numConfigs] != AV_PIX_FMT_NONE) {
        numConfigs++;
    }
#endif
    if (formats != nullptr) {
        for (int i = 0; i < numConfigs; i++) {
            if (formats[i] == AV_PIX_FMT_NV12 || formats[i] == AV_PIX_FMT_YUV420P) {
                pixFmt = formats[i];
                break;
            }
        }
        if (pixFmt == AV_PIX_FMT_NONE && numConfigs > 0) {
            pixFmt = formats[0];
        }
    }
    if (pixFmt == AV_PIX_FMT_NONE) {
        pixFmt = AV_PIX_FMT_NV12;
    }

    ctx->width = 1920;
    ctx->height = 1080;
    ctx->pix_fmt = pixFmt;
    ctx->time_base = AVRational{1, 30};
    ctx->framerate = AVRational{30, 1};
    ctx->bit_rate = 8000000;

    bool ok = avcodec_open2(ctx, codec, nullptr) == 0;
    avcodec_free_context(&ctx);
    return ok;
}

QString TaskConfigDialog::detectedEncoder() const
{
    int index = ui->vulkanDeviceSelectionComboBox->currentIndex();
    uint32_t vendorId = (index >= 0 && index < static_cast<int>(m_gpus.size()))
                            ? m_gpus[static_cast<size_t>(index)].vendorId
                            : 0;

    // The result does not change while the program runs
    static QMap<uint32_t, QString> cache;
    if (cache.contains(vendorId)) {
        return cache.value(vendorId);
    }

    QStringList candidates;
    switch (vendorId) {
    case 0x1002: // AMD
        candidates << "av1_amf" << "hevc_amf" << "h264_amf";
        break;
    case 0x10DE: // NVIDIA
        candidates << "av1_nvenc" << "hevc_nvenc" << "h264_nvenc";
        break;
    case 0x8086: // Intel
        candidates << "av1_qsv" << "hevc_qsv" << "h264_qsv";
        break;
    default:
        break;
    }

    QString result = "libx264";
    for (const QString &candidate : candidates) {
        if (canOpenEncoder(candidate.toUtf8().constData())) {
            result = candidate;
            break;
        }
    }

    cache.insert(vendorId, result);
    return result;
}

int TaskConfigDialog::recommendedGpuWorkers() const
{
    int index = ui->vulkanDeviceSelectionComboBox->currentIndex();
    if (index < 0 || index >= static_cast<int>(m_gpus.size())) {
        return 1;
    }
    const GpuInfo &gpu = m_gpus[static_cast<size_t>(index)];
    const quint64 gib = 1024ull * 1024ull * 1024ull;
    if (gpu.integrated || gpu.vramBytes < 6 * gib) {
        return 1;
    }
    if (gpu.vramBytes < 10 * gib) {
        return 2;
    }
    return 3;
}

void TaskConfigDialog::applyEncoderSettings(const QString &codec)
{
    m_customEncoderOptionsTableModel->removeRows(0, m_customEncoderOptionsTableModel->rowCount());
    ui->codecLineEdit->setText(codec);

    if (codec == "libx264") {
        ui->bitRateSpinBox->setValue(0);
        addCustomEncoderOption("crf", "20");
        addCustomEncoderOption("preset", "slow");
        return;
    }

    // Hardware encoders: the bit rate is chosen automatically from the output size
    ui->bitRateSpinBox->setValue(0);
    if (codec.endsWith("_amf")) {
        addCustomEncoderOption("quality", "quality");
    } else if (codec.endsWith("_nvenc")) {
        addCustomEncoderOption("preset", "p5");
    } else if (codec.endsWith("_qsv")) {
        addCustomEncoderOption("preset", "slow");
    }
}

QString TaskConfigDialog::encoderDescription(const QString &codec)
{
    QString format = codec.section('_', 0, 0).toUpper();
    if (format == "LIBX264") {
        return tr("H.264 on the processor");
    }
    if (format == "H264") {
        format = "H.264";
    }
    return tr("%1 on the graphics card").arg(format);
}

void TaskConfigDialog::setupHardwareDetection()
{
    m_hardwareInfoLabel = new QLabel(this);
    m_hardwareInfoLabel->setTextFormat(Qt::RichText);
    m_hardwareInfoLabel->setWordWrap(true);
    ui->verticalLayout_4->addWidget(m_hardwareInfoLabel);

    connect(ui->vulkanDeviceSelectionComboBox,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,
            [this]() {
                m_gpuWorkersSpinBox->setValue(recommendedGpuWorkers());
                if (m_encoderPresetComboBox->currentIndex() == ENCODER_PRESET_AUTO) {
                    applyEncoderPreset(ENCODER_PRESET_AUTO);
                }
                updateHardwareInfo();
            });

    m_gpuWorkersSpinBox->setValue(recommendedGpuWorkers());
    {
        QSignalBlocker blocker(m_encoderPresetComboBox);
        m_encoderPresetComboBox->setCurrentIndex(ENCODER_PRESET_AUTO);
    }
    applyEncoderPreset(ENCODER_PRESET_AUTO);
    updateHardwareInfo();
}

void TaskConfigDialog::updateHardwareInfo()
{
    int index = ui->vulkanDeviceSelectionComboBox->currentIndex();
    if (index < 0 || index >= static_cast<int>(m_gpus.size())) {
        m_hardwareInfoLabel->clear();
        return;
    }

    const GpuInfo &gpu = m_gpus[static_cast<size_t>(index)];
    QString memory = gpu.integrated
                         ? tr("integrated")
                         : QString("%1 GB").arg(
                               qRound(static_cast<double>(gpu.vramBytes) / (1024.0 * 1024.0 * 1024.0)));
    m_hardwareInfoLabel->setText(tr("<b>Detected:</b> %1 · %2 → %3, %4 parallel jobs")
                                     .arg(gpu.name.toHtmlEscaped(),
                                          memory,
                                          encoderDescription(detectedEncoder()))
                                     .arg(recommendedGpuWorkers()));
}

void TaskConfigDialog::saveSettings()
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, "video2x", "video2x-qt6-task");
    settings.setValue("vulkanDevice", ui->vulkanDeviceSelectionComboBox->currentIndex());
    settings.setValue("gpuWorkers", m_gpuWorkersSpinBox->value());
    settings.setValue("processingMode", ui->processingModeSelectionComboBox->currentIndex());
    settings.setValue("filter", ui->filterSelectionComboBox->currentIndex());
    settings.setValue("realesrganModel", ui->realesrganModelComboBox->currentText());
    settings.setValue("realcuganModel", ui->realcuganModelComboBox->currentText());
    settings.setValue("rifeModel", ui->rifeModelComboBox->currentText());
    settings.setValue("scalingFactor", ui->scalingFactorSpinBox->value());
    settings.setValue("noiseLevel", ui->noiseLevelSpinBox->value());
    settings.setValue("frameRateMultiplier", ui->frameRateMultiplierSpinBox->value());
    settings.setValue("maxResolution", m_maxResolutionComboBox->currentIndex());
    settings.setValue("encoderPreset", m_encoderPresetComboBox->currentIndex());
    settings.setValue("codec", ui->codecLineEdit->text());
    settings.setValue("bitRate", ui->bitRateSpinBox->value());

    QStringList options;
    for (int row = 0; row < m_customEncoderOptionsTableModel->rowCount(); row++) {
        QString key = m_customEncoderOptionsTableModel->index(row, 0).data().toString();
        QString value = m_customEncoderOptionsTableModel->index(row, 1).data().toString();
        if (!key.isEmpty()) {
            options << key + "=" + value;
        }
    }
    settings.setValue("customOptions", options);
}

void TaskConfigDialog::restoreSettings()
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, "video2x", "video2x-qt6-task");
    if (!settings.contains("encoderPreset")) {
        // First start: keep the automatic settings
        return;
    }

    int device = settings.value("vulkanDevice", 0).toInt();
    if (device >= 0 && device < ui->vulkanDeviceSelectionComboBox->count()) {
        ui->vulkanDeviceSelectionComboBox->setCurrentIndex(device);
    }
    m_gpuWorkersSpinBox->setValue(settings.value("gpuWorkers", recommendedGpuWorkers()).toInt());

    int mode = settings.value("processingMode", 0).toInt();
    if (mode >= 0 && mode < ui->processingModeSelectionComboBox->count()) {
        ui->processingModeSelectionComboBox->setCurrentIndex(mode);
    }
    int filter = settings.value("filter", ESRGAN_MODE).toInt();
    if (filter >= 0 && filter < ui->filterSelectionComboBox->count()) {
        ui->filterSelectionComboBox->setCurrentIndex(filter);
    }

    auto restoreText = [&settings](QComboBox *comboBox, const char *key) {
        int index = comboBox->findText(settings.value(key).toString());
        if (index >= 0) {
            comboBox->setCurrentIndex(index);
        }
    };
    restoreText(ui->realesrganModelComboBox, "realesrganModel");
    restoreText(ui->realcuganModelComboBox, "realcuganModel");
    restoreText(ui->rifeModelComboBox, "rifeModel");

    ui->scalingFactorSpinBox->setValue(
        settings.value("scalingFactor", ui->scalingFactorSpinBox->value()).toInt());
    ui->noiseLevelSpinBox->setValue(
        settings.value("noiseLevel", ui->noiseLevelSpinBox->value()).toInt());
    ui->frameRateMultiplierSpinBox->setValue(
        settings.value("frameRateMultiplier", ui->frameRateMultiplierSpinBox->value()).toInt());

    int maxResolution = settings.value("maxResolution", 3).toInt();
    if (maxResolution >= 0 && maxResolution < m_maxResolutionComboBox->count()) {
        m_maxResolutionComboBox->setCurrentIndex(maxResolution);
    }

    int preset = settings.value("encoderPreset", ENCODER_PRESET_AUTO).toInt();
    if (preset < 0 || preset >= m_encoderPresetComboBox->count()) {
        preset = ENCODER_PRESET_AUTO;
    }
    {
        QSignalBlocker blocker(m_encoderPresetComboBox);
        m_encoderPresetComboBox->setCurrentIndex(preset);
    }
    if (preset == ENCODER_PRESET_CUSTOM) {
        // Restore the codec and options exactly as they were
        m_customEncoderOptionsTableModel->removeRows(0, m_customEncoderOptionsTableModel->rowCount());
        ui->codecLineEdit->setText(settings.value("codec", "libx264").toString());
        ui->bitRateSpinBox->setValue(settings.value("bitRate", 0).toInt());
        const QStringList options = settings.value("customOptions").toStringList();
        for (const QString &option : options) {
            addCustomEncoderOption(option.section('=', 0, 0), option.section('=', 1));
        }
    } else {
        // Presets are applied fresh so they match the current hardware
        applyEncoderPreset(preset);
    }

    updateHardwareInfo();
    updateOutputPreview();
}

void TaskConfigDialog::applyEncoderPreset(int index)
{
    if (index == ENCODER_PRESET_CUSTOM) {
        return;
    }

    m_customEncoderOptionsTableModel->removeRows(0, m_customEncoderOptionsTableModel->rowCount());

    switch (index) {
    case ENCODER_PRESET_LIBX264:
        applyEncoderSettings("libx264");
        break;
    case ENCODER_PRESET_AUTO:
        applyEncoderSettings(detectedEncoder());
        break;
    case ENCODER_PRESET_AMD_AV1:
        applyEncoderSettings("av1_amf");
        break;
    case ENCODER_PRESET_AMD_HEVC:
        applyEncoderSettings("hevc_amf");
        break;
    case ENCODER_PRESET_AMD_H264:
        applyEncoderSettings("h264_amf");
        break;
    default:
        break;
    }
}

void TaskConfigDialog::populateVulkanDevices()
{
    // Set default placeholder text
    ui->vulkanDeviceSelectionComboBox->addItem(tr("No GPUs with Vulkan support found!"));

    // Create a Vulkan instance
    VkInstance instance;
    VkInstanceCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    if (vkCreateInstance(&create_info, nullptr, &instance) != VK_SUCCESS) {
        video2x::logger()->error("Failed to create Vulkan instance.");
        return;
    }

    // Enumerate physical devices
    uint32_t device_count = 0;
    VkResult result = vkEnumeratePhysicalDevices(instance, &device_count, nullptr);
    if (result != VK_SUCCESS) {
        video2x::logger()->error("Failed to enumerate Vulkan physical devices.");
        vkDestroyInstance(instance, nullptr);
        return;
    }

    // Check if any devices are found
    if (device_count == 0) {
        video2x::logger()->warn("No Vulkan-compatible physical devices found.");
        vkDestroyInstance(instance, nullptr);
        return;
    }

    // Get physical device properties
    std::vector<VkPhysicalDevice> physical_devices(device_count);
    result = vkEnumeratePhysicalDevices(instance, &device_count, physical_devices.data());
    if (result != VK_SUCCESS) {
        video2x::logger()->error("Failed to enumerate Vulkan physical devices.");
        vkDestroyInstance(instance, nullptr);
        return;
    }

    // Populate GPU information
    ui->vulkanDeviceSelectionComboBox->clear();
    m_gpus.clear();
    for (uint32_t i = 0; i < device_count; i++) {
        VkPhysicalDevice device = physical_devices[i];
        VkPhysicalDeviceProperties device_properties;
        vkGetPhysicalDeviceProperties(device, &device_properties);
        ui->vulkanDeviceSelectionComboBox->addItem(QString::number(i) + ". "
                                                   + device_properties.deviceName);

        // Remember vendor, type and video memory for the automatic settings
        VkPhysicalDeviceMemoryProperties memory_properties;
        vkGetPhysicalDeviceMemoryProperties(device, &memory_properties);
        VkDeviceSize vram = 0;
        for (uint32_t h = 0; h < memory_properties.memoryHeapCount; h++) {
            if (memory_properties.memoryHeaps[h].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
                vram = (std::max)(vram, memory_properties.memoryHeaps[h].size);
            }
        }
        GpuInfo info;
        info.name = QString::fromUtf8(device_properties.deviceName);
        info.vendorId = device_properties.vendorID;
        info.integrated = device_properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
        info.vramBytes = static_cast<quint64>(vram);
        m_gpus.push_back(info);
        video2x::logger()->debug("Found Vulkan device: {}.", device_properties.deviceName);
    }

    // Clean up Vulkan instance
    vkDestroyInstance(instance, nullptr);
}

std::optional<TaskConfig> TaskConfigDialog::getTaskConfig()
{
    // Create TaskConfig struct on stack
    TaskConfig taskConfig;

    taskConfig.vkDeviceIndex = ui->vulkanDeviceSelectionComboBox->currentIndex();
    taskConfig.gpuWorkers = m_gpuWorkersSpinBox->value();

    // Maximum output resolution (only used for upscaling)
    if (ui->processingModeSelectionComboBox->currentIndex() == 0) {
        QSize maxSize = m_maxResolutionComboBox->currentData().toSize();
        taskConfig.procCfg.max_width = maxSize.width();
        taskConfig.procCfg.max_height = maxSize.height();
    }

    // Processing mode selection
    video2x::processors::ProcessingMode procMode;
    switch (ui->processingModeSelectionComboBox->currentIndex()) {
    case 0:
        procMode = video2x::processors::ProcessingMode::Filter;
        break;
    case 1:
        procMode = video2x::processors::ProcessingMode::Interpolate;
        break;
    default:
        video2x::logger()->error("Unknown processing mode selected: {}.",
            ui->processingModeSelectionComboBox->currentIndex());
        return std::nullopt;
    }

    // Processor type
    if (procMode == video2x::processors::ProcessingMode::Filter) {
        switch (ui->filterSelectionComboBox->currentIndex()) {
        case PLACEBO_MODE: { // libplacebo
            taskConfig.procCfg.processor_type = video2x::processors::ProcessorType::Libplacebo;
            taskConfig.procCfg.width = ui->outputWidthSpinBox->value();
            taskConfig.procCfg.height = ui->outputHeightSpinBox->value();
            video2x::processors::LibplaceboConfig libplaceboConfig;
            if (ui->libplaceboCustomGlslShaderPathLineEdit->text().isEmpty()) {
                std::optional<QString> anime4kShaderNameOpt = findAnime4kFileNameByDisplayName(
                    ui->libplaceboGlslShaderComboBox->currentText());

                // This parse should always succeed and return a value
                // If it does not, the text in the UI or the parse is wrong
                if (!anime4kShaderNameOpt.has_value()) {
                    video2x::logger()->error("Failed to parse Anime4K shader name.");
                    return std::nullopt;
                }

                QString anime4kShaderName = anime4kShaderNameOpt.value();
#ifdef _WIN32
                libplaceboConfig.shader_path = anime4kShaderName.toStdWString();
#else
                libplaceboConfig.shader_path = anime4kShaderName.toStdString();
#endif
            } else {
#ifdef _WIN32
                libplaceboConfig.shader_path = ui->libplaceboCustomGlslShaderPathLineEdit->text()
                                                   .toStdWString();
#else
                libplaceboConfig.shader_path = ui->libplaceboCustomGlslShaderPathLineEdit->text()
                                                   .toStdString();
#endif
            }
            taskConfig.procCfg.config = libplaceboConfig;
            break;
        }
        case ESRGAN_MODE: { // Real-ESRGAN
            taskConfig.procCfg.processor_type = video2x::processors::ProcessorType::RealESRGAN;
            taskConfig.procCfg.scaling_factor = ui->scalingFactorSpinBox->value();
            taskConfig.procCfg.noise_level = !ui->noiseLevelSpinBox->isHidden()
                                                 ? ui->noiseLevelSpinBox->value()
                                                 : -1;
            video2x::processors::RealESRGANConfig realesrgan_config;
#ifdef _WIN32
            realesrgan_config.model_name = ui->realesrganModelComboBox->currentText().toStdWString();
#else
            realesrgan_config.model_name = ui->realesrganModelComboBox->currentText().toStdString();
#endif
            taskConfig.procCfg.config = realesrgan_config;
            break;
        }
        case CUGAN_MODE: { // Real-CUGAN
            taskConfig.procCfg.processor_type = video2x::processors::ProcessorType::RealCUGAN;
            taskConfig.procCfg.scaling_factor = ui->scalingFactorSpinBox->value();
            taskConfig.procCfg.noise_level = ui->noiseLevelSpinBox->value();
            video2x::processors::RealCUGANConfig realcugan_config;
#ifdef _WIN32
            realcugan_config.model_name = ui->realcuganModelComboBox->currentText().toStdWString();
#else
            realcugan_config.model_name = ui->realcuganModelComboBox->currentText().toStdString();
#endif
            realcugan_config.num_threads = ui->realcuganThreadsSpinBox->value();
            realcugan_config.syncgap = ui->realcuganSyncGapSpinBox->value();
            taskConfig.procCfg.config = realcugan_config;
            break;
        }
        default:
            video2x::logger()->error("Unknown filter type: {}.", ui->filterSelectionComboBox->currentIndex());
            return std::nullopt;
        }
    } else if (procMode == video2x::processors::ProcessingMode::Interpolate) {
        switch (ui->interpolationSelectionComboBox->currentIndex()) {
        case 0: {
            taskConfig.procCfg.processor_type = video2x::processors::ProcessorType::RIFE;
            taskConfig.procCfg.frm_rate_mul = ui->frameRateMultiplierSpinBox->value();
            taskConfig.procCfg.scn_det_thresh = ui->sceneDetectionThresholdDoubleSpinBox->value();
            video2x::processors::RIFEConfig rifeConfig;

            std::optional<QString> rifeModelNameOpt = findRifeModelNameByDisplayName(
                ui->rifeModelComboBox->currentText());

            // This parse should always succeed and return a value
            // If it does not, the text in the UI or the parse is wrong
            if (!rifeModelNameOpt.has_value()) {
                video2x::logger()->error("Failed to parse RIFE model name.");
                return std::nullopt;
            }

            QString rifeModelName = rifeModelNameOpt.value();
#ifdef _WIN32
            rifeConfig.model_name = rifeModelName.toStdWString();
#else
            rifeConfig.model_name = rifeModelName.toStdString();
#endif
            taskConfig.procCfg.config = rifeConfig;
            break;
        }
        default:
            video2x::logger()->error("Unknown interpolator type: {}", 
                ui->interpolationSelectionComboBox->currentIndex());
            return std::nullopt;
        }
    }

    // EncoderConfig
    taskConfig.outputSuffix = ui->suffixLineEdit->text();
    taskConfig.encCfg.copy_audio_streams = ui->copyStreamsCheckBox->isChecked();
    taskConfig.encCfg.copy_subtitle_streams = ui->copyStreamsCheckBox->isChecked();

    // Rate control and compression
    taskConfig.encCfg.bit_rate = ui->bitRateSpinBox->value();
    taskConfig.encCfg.rc_buffer_size = ui->rcBufferSizeSpinBox->value();
    taskConfig.encCfg.rc_min_rate = ui->rcMinRateSpinBox->value();
    taskConfig.encCfg.rc_max_rate = ui->rcMaxRateSpinBox->value();
    taskConfig.encCfg.qmin = ui->qMinSpinBox->value();
    taskConfig.encCfg.qmax = ui->qMaxSpinBox->value();

    // GOP and frame structure
    taskConfig.encCfg.gop_size = ui->gopSizeSpinBox->value();
    taskConfig.encCfg.max_b_frames = ui->maxBFramesSpinBox->value();
    taskConfig.encCfg.keyint_min = ui->keyIntMinSpinBox->value();
    taskConfig.encCfg.refs = ui->refsSpinBox->value();

    // Performance and threading
    taskConfig.encCfg.thread_count = ui->threadCountSpinBox->value();

    // Latency and buffering
    taskConfig.encCfg.delay = ui->delaySpinBox->value();

    // Parse codec
    taskConfig.encCfg.codec = "libx264";
    if (avcodec_find_encoder_by_name(ui->codecLineEdit->text().toUtf8().constData()) == nullptr) {
        video2x::logger()->error("Invalid codec specified: '{}'.", ui->codecLineEdit->text().toStdString());
        return std::nullopt;
    }
    taskConfig.encCfg.codec = ui->codecLineEdit->text().toStdString();

    // Parse pix_fmt
    taskConfig.encCfg.pix_fmt = AV_PIX_FMT_NONE;
    if (ui->pixFmtLineEdit->text() != "auto") {
        taskConfig.encCfg.pix_fmt = av_get_pix_fmt(ui->pixFmtLineEdit->text().toUtf8().constData());
        if (taskConfig.encCfg.pix_fmt == AV_PIX_FMT_NONE) {
            video2x::logger()->error("Invalid pix_fmt value: '{}'", ui->pixFmtLineEdit->text().toStdString());
            return std::nullopt;
        }
    }

    // Parse hwaccel
    taskConfig.hwDeviceType = AV_HWDEVICE_TYPE_NONE;
    if (ui->hwaccelLineEdit->text() != "none") {
        taskConfig.hwDeviceType = av_hwdevice_find_type_by_name(
            ui->hwaccelLineEdit->text().toUtf8().constData());
        if (taskConfig.hwDeviceType == AV_HWDEVICE_TYPE_NONE) {
            video2x::logger()->error("Invalid hwaccel value: '{}'", ui->hwaccelLineEdit->text().toStdString());
            return std::nullopt;
        }
    }

    // Populate custom options
    for (int row = 0; row < m_customEncoderOptionsTableModel->rowCount(); ++row) {
        QStandardItem *optionItem = m_customEncoderOptionsTableModel->item(row, 0);
        QStandardItem *valueItem = m_customEncoderOptionsTableModel->item(row, 1);

        // Ensure we have valid items
        if (!optionItem || !valueItem) {
            continue;
        }

        std::string key = optionItem->text().toStdString();
        std::string value = valueItem->text().toStdString();

        if (!key.empty() && !value.empty()) {
            taskConfig.encCfg.extra_opts.emplace_back(key, value);
        }
    }

    return taskConfig;
}

void TaskConfigDialog::setTaskConfig(const TaskConfig &taskConfig)
{
    // vkDeviceIndex
    ui->vulkanDeviceSelectionComboBox->setCurrentIndex(taskConfig.vkDeviceIndex);
    m_gpuWorkersSpinBox->setValue(taskConfig.gpuWorkers);

    // Maximum output resolution
    if (taskConfig.procCfg.max_width > 0 && taskConfig.procCfg.max_height > 0) {
        int index = m_maxResolutionComboBox->findData(
            QSize(taskConfig.procCfg.max_width, taskConfig.procCfg.max_height));
        if (index >= 0) {
            m_maxResolutionComboBox->setCurrentIndex(index);
        }
    } else {
        m_maxResolutionComboBox->setCurrentIndex(0);
    }

    // Show the result for the file of this task
    if (!taskConfig.inFname.empty()) {
#ifdef _WIN32
        setInputFiles(QStringList() << QString::fromStdWString(taskConfig.inFname.wstring()));
#else
        setInputFiles(QStringList() << QString::fromStdString(taskConfig.inFname.string()));
#endif
    }

    // Determine ProcessingMode by processor_type
    video2x::processors::ProcessingMode procMode;
    switch (taskConfig.procCfg.processor_type) {
    case video2x::processors::ProcessorType::Libplacebo:
    case video2x::processors::ProcessorType::RealESRGAN:
    case video2x::processors::ProcessorType::RealCUGAN:
        procMode = video2x::processors::ProcessingMode::Filter;
        ui->processingModeSelectionComboBox->setCurrentIndex(0); // Filter mode
        break;

    case video2x::processors::ProcessorType::RIFE:
        procMode = video2x::processors::ProcessingMode::Interpolate;
        ui->processingModeSelectionComboBox->setCurrentIndex(1); // Interpolate mode
        break;

    default:
        // Unknown processor type, handle gracefully
        video2x::logger()->error("Unknown processor type in taskConfig");
        return;
    }

    // Set UI based on ProcessingMode and processor_type
    if (procMode == video2x::processors::ProcessingMode::Filter) {
        // Filter Mode
        switch (taskConfig.procCfg.processor_type) {
        case video2x::processors::ProcessorType::Libplacebo: {
            ui->filterSelectionComboBox->setCurrentIndex(0);
            ui->outputWidthSpinBox->setValue(taskConfig.procCfg.width);
            ui->outputHeightSpinBox->setValue(taskConfig.procCfg.height);

            video2x::processors::LibplaceboConfig libplaceboConfig
                = std::get<video2x::processors::LibplaceboConfig>(taskConfig.procCfg.config);
#ifdef _WIN32
            QString shaderName = QString::fromWCharArray(libplaceboConfig.shader_path.c_str());
#else
            QString shaderName = QString::fromStdString(libplaceboConfig.shader_path);
#endif

            if (!shaderName.isEmpty()) {
                std::optional<QString> displayNameOpt = findAnime4kDisplayNameByFileName(shaderName);
                if (displayNameOpt.has_value()) {
                    ui->libplaceboCustomGlslShaderPathLineEdit->clear();
                    ui->libplaceboGlslShaderComboBox->setCurrentText(displayNameOpt.value());
                } else {
                    // Custom shader path
                    ui->libplaceboCustomGlslShaderPathLineEdit->setText(shaderName);
                }
            } else {
                // If empty, maybe set to a default state
                ui->libplaceboCustomGlslShaderPathLineEdit->clear();
                ui->libplaceboGlslShaderComboBox->setCurrentIndex(0);
            }

            break;
        }
        case video2x::processors::ProcessorType::RealESRGAN: {
            ui->filterSelectionComboBox->setCurrentIndex(1);
            ui->scalingFactorSpinBox->setValue(taskConfig.procCfg.scaling_factor);

            video2x::processors::RealESRGANConfig realesrganConfig
                = std::get<video2x::processors::RealESRGANConfig>(taskConfig.procCfg.config);
#ifdef _WIN32
            QString modelName = QString::fromWCharArray(realesrganConfig.model_name.c_str());
#else
            QString modelName = QString::fromStdString(realesrganConfig.model_name);
#endif
            ui->realesrganModelComboBox->setCurrentText(modelName);
            if (taskConfig.procCfg.noise_level >= 0) {
                ui->noiseLevelSpinBox->setValue(taskConfig.procCfg.noise_level);
            }
            break;
        }
        case video2x::processors::ProcessorType::RealCUGAN: {
            ui->filterSelectionComboBox->setCurrentIndex(2);
            ui->scalingFactorSpinBox->setValue(taskConfig.procCfg.scaling_factor);
            ui->noiseLevelSpinBox->setValue(taskConfig.procCfg.noise_level);

            video2x::processors::RealCUGANConfig realcuganConfig
                = std::get<video2x::processors::RealCUGANConfig>(taskConfig.procCfg.config);
#ifdef _WIN32
            QString modelName = QString::fromWCharArray(realcuganConfig.model_name.c_str());
#else
            QString modelName = QString::fromStdString(realcuganConfig.model_name);
#endif
            ui->realcuganModelComboBox->setCurrentText(modelName);
            ui->realcuganThreadsSpinBox->setValue(realcuganConfig.num_threads);
            ui->realcuganSyncGapSpinBox->setValue(realcuganConfig.syncgap);
            break;
        }
        default:
            // Unknown filter type, handle gracefully
            video2x::logger()->error("Unknown filter processor type.");
            return;
        }
    } else {
        // Interpolate Mode
        switch (taskConfig.procCfg.processor_type) {
        case video2x::processors::ProcessorType::RIFE: {
            ui->interpolationSelectionComboBox->setCurrentIndex(0);
            ui->frameRateMultiplierSpinBox->setValue(taskConfig.procCfg.frm_rate_mul);
            ui->sceneDetectionThresholdDoubleSpinBox->setValue(taskConfig.procCfg.scn_det_thresh);

            video2x::processors::RIFEConfig rifeConfig = std::get<video2x::processors::RIFEConfig>(
                taskConfig.procCfg.config);
#ifdef _WIN32
            QString rifeModelName = QString::fromWCharArray(rifeConfig.model_name.c_str());
#else
            QString rifeModelName = QString::fromStdString(rifeConfig.model_name);
#endif

            // Map back to display name if possible
            // findRifeDisplayNameByModelName is a hypothetical reverse lookup
            std::optional<QString> rifeDisplayNameOpt = findRifeDisplayNameByModelName(
                rifeModelName);
            if (rifeDisplayNameOpt.has_value()) {
                ui->rifeModelComboBox->setCurrentText(rifeDisplayNameOpt.value());
            } else {
                ui->rifeModelComboBox->setCurrentText(rifeModelName);
            }

            break;
        }
        default:
            video2x::logger()->error("Unknown interpolation processor type.");
            return;
        }
    }

    // Suffix
    ui->suffixLineEdit->setText(taskConfig.outputSuffix);

    // copy_streams
    ui->copyStreamsCheckBox->setChecked(taskConfig.encCfg.copy_audio_streams
                                        || taskConfig.encCfg.copy_subtitle_streams);

    // frameRateMultiplier (only relevant if Interpolate)
    if (procMode == video2x::processors::ProcessingMode::Interpolate) {
        ui->frameRateMultiplierSpinBox->setValue(taskConfig.procCfg.frm_rate_mul);
    } else {
        ui->frameRateMultiplierSpinBox->setValue(0);
    }

    // Rate control and compression
    ui->bitRateSpinBox->setValue(taskConfig.encCfg.bit_rate);
    ui->rcBufferSizeSpinBox->setValue(taskConfig.encCfg.rc_buffer_size);
    ui->rcMinRateSpinBox->setValue(taskConfig.encCfg.rc_min_rate);
    ui->rcMaxRateSpinBox->setValue(taskConfig.encCfg.rc_max_rate);
    ui->qMinSpinBox->setValue(taskConfig.encCfg.qmin);
    ui->qMaxSpinBox->setValue(taskConfig.encCfg.qmax);

    // GOP and frame structure
    ui->gopSizeSpinBox->setValue(taskConfig.encCfg.gop_size);
    ui->maxBFramesSpinBox->setValue(taskConfig.encCfg.max_b_frames);
    ui->keyIntMinSpinBox->setValue(taskConfig.encCfg.keyint_min);
    ui->refsSpinBox->setValue(taskConfig.encCfg.refs);

    // Performance and threading
    ui->threadCountSpinBox->setValue(taskConfig.encCfg.thread_count);

    // Latency and buffering
    ui->delaySpinBox->setValue(taskConfig.encCfg.delay);

    // Codec
    ui->codecLineEdit->setText(QString::fromStdString(taskConfig.encCfg.codec));

    // Pix_fmt
    if (taskConfig.encCfg.pix_fmt == AV_PIX_FMT_NONE) {
        ui->pixFmtLineEdit->setText("auto");
    } else {
        const char *pixFmtName = av_get_pix_fmt_name(taskConfig.encCfg.pix_fmt);
        if (pixFmtName) {
            ui->pixFmtLineEdit->setText(QString::fromUtf8(pixFmtName));
        } else {
            ui->pixFmtLineEdit->setText("auto");
        }
    }

    // hwaccel
    if (taskConfig.hwDeviceType == AV_HWDEVICE_TYPE_NONE) {
        ui->hwaccelLineEdit->setText("none");
    } else {
        const char *hwTypeName = av_hwdevice_get_type_name(taskConfig.hwDeviceType);
        if (hwTypeName) {
            ui->hwaccelLineEdit->setText(QString::fromUtf8(hwTypeName));
        } else {
            ui->hwaccelLineEdit->setText("none");
        }
    }

    // Clear the table first if needed
    m_customEncoderOptionsTableModel->clear();

    // Populate from extra_opts
    for (const auto &kv : taskConfig.encCfg.extra_opts) {
        QString qKey = QString::fromStdString(kv.first);
        QString qValue = QString::fromStdString(kv.second);
        QStandardItem *keyItem = new QStandardItem(qKey);
        QStandardItem *valueItem = new QStandardItem(qValue);
        m_customEncoderOptionsTableModel->appendRow({keyItem, valueItem});
    }
}

void TaskConfigDialog::setOutputSuffix(QString suffix)
{
    ui->suffixLineEdit->setText(suffix);
}

void TaskConfigDialog::setOutputCodec(QString codec)
{
    QSignalBlocker blocker(m_encoderPresetComboBox);
    m_encoderPresetComboBox->setCurrentIndex(ENCODER_PRESET_CUSTOM);
    ui->codecLineEdit->setText(codec);
}

void TaskConfigDialog::on_applyPushButton_clicked()
{
    // Validate Real-CUGAN noise level for models-pro
    if (ui->processingModeSelectionComboBox->currentIndex() == FILTER_MODE
        && ui->filterSelectionComboBox->currentIndex() == CUGAN_MODE
        && ui->realcuganModelComboBox->currentIndex() == REALCUGAN_MODEL_PRO
        && ui->noiseLevelSpinBox->value() > 0 && ui->noiseLevelSpinBox->value() < 3) {
        execErrorMessage(
            tr("Invalid noise level for Real-CUGAN model 'models-pro'.\nAllowed values "
               "are -1, 0, and 3"));
        return;
    }

    // Validate codec
    if (avcodec_find_encoder_by_name(ui->codecLineEdit->text().toUtf8().constData()) == nullptr) {
        execErrorMessage(tr("Invalid codec value: '") + ui->codecLineEdit->text() + "'");
        return;
    }

    // Validate pix_fmt
    if (ui->pixFmtLineEdit->text() != "auto"
        && av_get_pix_fmt(ui->pixFmtLineEdit->text().toUtf8().constData()) == AV_PIX_FMT_NONE) {
        execErrorMessage(tr("Invalid pix_fmt value: '") + ui->pixFmtLineEdit->text() + "'");
        return;
    }

    // Validate hwaccel
    if (ui->hwaccelLineEdit->text() != "none"
        && av_hwdevice_find_type_by_name(ui->hwaccelLineEdit->text().toUtf8().constData())
               == AV_HWDEVICE_TYPE_NONE) {
        execErrorMessage(tr("Invalid hwaccel value: '") + ui->hwaccelLineEdit->text() + "'");
        return;
    }

    // Validate libplacebo GLSL shader path
    if (!ui->libplaceboCustomGlslShaderPathLineEdit->text().isEmpty()) {
        std::filesystem::path glslPath(
            ui->libplaceboCustomGlslShaderPathLineEdit->text().toUtf8().constData());
        if (!std::filesystem::exists(glslPath)) {
            execErrorMessage(tr("libplacebo GLSL shader file not found: '")
                             + ui->libplaceboCustomGlslShaderPathLineEdit->text() + "'");
            return;
        }
    }

    // Remember the settings for the next task
    saveSettings();

    accept();
}

void TaskConfigDialog::on_addCustomOptionPushButton_clicked()
{
    QStandardItem *optionItem = new QStandardItem("");
    QStandardItem *valueItem = new QStandardItem("");
    m_customEncoderOptionsTableModel->appendRow(QList<QStandardItem *>() << optionItem << valueItem);
}

void TaskConfigDialog::on_removeSelectedOptionPushButton_clicked()
{
    if (!ui->customEncoderOptionsTableView->selectionModel()) {
        return;
    }

    QModelIndexList selectedRows = ui->customEncoderOptionsTableView->selectionModel()
                                       ->selectedRows();
    if (selectedRows.isEmpty()) {
        return;
    }

    // Sort the selected rows in reverse order
    std::sort(selectedRows.rbegin(), selectedRows.rend());

    // Remove each selected row from the model
    for (const QModelIndex &index : selectedRows) {
        m_customEncoderOptionsTableModel->removeRow(index.row());
    }
}

void TaskConfigDialog::on_libplaceboSelectCustomGlslShaderPushButton_clicked()
{
    // Open a file dialog to select a .glsl file
    QString fileName = QFileDialog::getOpenFileName(this,
                                                    tr("Select GLSL Shader"),
                                                    QString(),
                                                    tr("GLSL Files (*.glsl);;All Files (*.*)"));

    // No file selected
    if (fileName.isEmpty()) {
        return;
    }

    ui->libplaceboCustomGlslShaderPathLineEdit->setText(fileName);
}
