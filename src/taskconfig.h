#ifndef TASKCONFIG_H
#define TASKCONFIG_H

#include <QString>

#include <libvideo2x/libvideo2x.h>

struct TaskConfig
{
    // VideoProcessor parameters
    video2x::processors::ProcessorConfig procCfg;
    video2x::encoder::EncoderConfig encCfg;
    uint32_t vkDeviceIndex;
    AVHWDeviceType hwDeviceType;
    bool benchmark = false;

    // Number of processor instances working on frames at the same time
    int gpuWorkers = 3;

    // VideoProcessor::process parameters
    std::filesystem::path inFname;
    std::filesystem::path outFname;

    // Additional data for restoring the TaskConfigDialog
    QString outputSuffix;
};

#endif // TASKCONFIG_H
