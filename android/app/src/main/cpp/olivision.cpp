// OliVision native part: Real-ESRGAN on the phone GPU (ncnn + Vulkan)

#include <android/bitmap.h>
#include <android/log.h>
#include <jni.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "gpu.h"
#include "realesrgan/realesrgan.h"

#define LOG_TAG "OliVision"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace {

std::mutex g_mutex;
RealESRGAN *g_model = nullptr;
bool g_gpuReady = false;
std::string g_loadedModel;

inline uint8_t clamp8(int v)
{
    return static_cast<uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v));
}

// Resize packed RGB. Area averaging when shrinking a lot, bilinear otherwise.
void resizeRgb(const uint8_t *src, int sw, int sh, uint8_t *dst, int dw, int dh)
{
    if (sw == dw && sh == dh) {
        std::memcpy(dst, src, static_cast<size_t>(sw) * sh * 3);
        return;
    }
    if (dw * 2 <= sw || dh * 2 <= sh) {
        for (int y = 0; y < dh; y++) {
            int y0 = static_cast<int>(static_cast<int64_t>(y) * sh / dh);
            int y1 = (std::max)(y0 + 1, static_cast<int>(static_cast<int64_t>(y + 1) * sh / dh));
            for (int x = 0; x < dw; x++) {
                int x0 = static_cast<int>(static_cast<int64_t>(x) * sw / dw);
                int x1 = (std::max)(x0 + 1, static_cast<int>(static_cast<int64_t>(x + 1) * sw / dw));
                int sum[3] = {0, 0, 0};
                for (int yy = y0; yy < y1; yy++) {
                    const uint8_t *row = src + (static_cast<size_t>(yy) * sw + x0) * 3;
                    for (int xx = x0; xx < x1; xx++, row += 3) {
                        sum[0] += row[0];
                        sum[1] += row[1];
                        sum[2] += row[2];
                    }
                }
                int count = (y1 - y0) * (x1 - x0);
                uint8_t *out = dst + (static_cast<size_t>(y) * dw + x) * 3;
                out[0] = static_cast<uint8_t>(sum[0] / count);
                out[1] = static_cast<uint8_t>(sum[1] / count);
                out[2] = static_cast<uint8_t>(sum[2] / count);
            }
        }
        return;
    }
    const float sx = static_cast<float>(sw) / dw;
    const float sy = static_cast<float>(sh) / dh;
    for (int y = 0; y < dh; y++) {
        float fy = (y + 0.5f) * sy - 0.5f;
        int y0 = static_cast<int>(std::floor(fy));
        float wy = fy - y0;
        int ya = std::clamp(y0, 0, sh - 1);
        int yb = std::clamp(y0 + 1, 0, sh - 1);
        for (int x = 0; x < dw; x++) {
            float fx = (x + 0.5f) * sx - 0.5f;
            int x0 = static_cast<int>(std::floor(fx));
            float wx = fx - x0;
            int xa = std::clamp(x0, 0, sw - 1);
            int xb = std::clamp(x0 + 1, 0, sw - 1);
            const uint8_t *p00 = src + (static_cast<size_t>(ya) * sw + xa) * 3;
            const uint8_t *p01 = src + (static_cast<size_t>(ya) * sw + xb) * 3;
            const uint8_t *p10 = src + (static_cast<size_t>(yb) * sw + xa) * 3;
            const uint8_t *p11 = src + (static_cast<size_t>(yb) * sw + xb) * 3;
            uint8_t *out = dst + (static_cast<size_t>(y) * dw + x) * 3;
            for (int c = 0; c < 3; c++) {
                float top = p00[c] + (p01[c] - p00[c]) * wx;
                float bottom = p10[c] + (p11[c] - p10[c]) * wx;
                out[c] = clamp8(static_cast<int>(top + (bottom - top) * wy + 0.5f));
            }
        }
    }
}

// Shrink (optional), upscale with the AI, then fit to the final size
bool runModel(std::vector<uint8_t> &rgb, int &w, int &h, int pw, int ph, int fw, int fh)
{
    if (g_model == nullptr) {
        return false;
    }
    if (pw > 0 && ph > 0 && (pw != w || ph != h)) {
        std::vector<uint8_t> shrunk(static_cast<size_t>(pw) * ph * 3);
        resizeRgb(rgb.data(), w, h, shrunk.data(), pw, ph);
        rgb.swap(shrunk);
        w = pw;
        h = ph;
    }

    // ncnn reads a little past the end of the last tile, so the picture gets spare room behind it
    const int scale = g_model->scale;
    const size_t used = static_cast<size_t>(w) * h * 3;
    const size_t spare = static_cast<size_t>(w) * 3 * (g_model->tilesize + 2 * g_model->prepadding + 8) + (1 << 20);
    rgb.resize(used + spare);
    ncnn::Mat in(w, h, static_cast<void *>(rgb.data()), static_cast<size_t>(3), 3);
    ncnn::Mat out(w * scale, h * scale, static_cast<size_t>(3), 3);
    if (g_model->process(in, out) != 0) {
        return false;
    }

    int ow = w * scale, oh = h * scale;
    std::vector<uint8_t> result(static_cast<size_t>(fw) * fh * 3);
    resizeRgb(static_cast<const uint8_t *>(out.data), ow, oh, result.data(), fw, fh);
    rgb.swap(result);
    w = fw;
    h = fh;
    return true;
}

// YUV (BT.709 for HD and larger, BT.601 below, limited range) <-> RGB
struct YuvCoeffs
{
    int rv, gu, gv, bu;   // YUV -> RGB, scaled by 1024
    int yr, yg, yb, ur, ug, ub, vr, vg, vb;   // RGB -> YUV, scaled by 1024
};

YuvCoeffs coeffsFor(int height)
{
    if (height >= 720) {
        return {1836, 218, 546, 2163, 186, 627, 63, -103, -346, 450, 450, -409, -41};
    }
    return {1634, 401, 832, 2066, 263, 516, 100, -152, -298, 450, 450, -377, -73};
}

void yuvToRgb(const uint8_t *yp, int yStride, int yPix, const uint8_t *up, const uint8_t *vp,
              int uvStride, int uvPix, bool highBit, int w, int h, uint8_t *rgb)
{
    // 10 bit (P010): little endian 16 bit samples, the high byte is enough
    const int hb = highBit ? 1 : 0;
    YuvCoeffs k = coeffsFor(h);
    for (int y = 0; y < h; y++) {
        const uint8_t *yrow = yp + static_cast<size_t>(y) * yStride + hb;
        const uint8_t *urow = up + static_cast<size_t>(y / 2) * uvStride + hb;
        const uint8_t *vrow = vp + static_cast<size_t>(y / 2) * uvStride + hb;
        uint8_t *out = rgb + static_cast<size_t>(y) * w * 3;
        for (int x = 0; x < w; x++) {
            int Y = (yrow[x * yPix] - 16) * 1192;
            int U = urow[(x / 2) * uvPix] - 128;
            int V = vrow[(x / 2) * uvPix] - 128;
            out[0] = clamp8((Y + k.rv * V + 512) >> 10);
            out[1] = clamp8((Y - k.gu * U - k.gv * V + 512) >> 10);
            out[2] = clamp8((Y + k.bu * U + 512) >> 10);
            out += 3;
        }
    }
}

void rgbToYuv(const uint8_t *rgb, int w, int h, uint8_t *yp, int yStride, uint8_t *up, uint8_t *vp,
              int uvStride, int uvPixelStride)
{
    YuvCoeffs k = coeffsFor(h);
    for (int y = 0; y < h; y++) {
        const uint8_t *in = rgb + static_cast<size_t>(y) * w * 3;
        uint8_t *yrow = yp + static_cast<size_t>(y) * yStride;
        for (int x = 0; x < w; x++, in += 3) {
            yrow[x] = clamp8(((k.yr * in[0] + k.yg * in[1] + k.yb * in[2] + 512) >> 10) + 16);
        }
    }
    for (int y = 0; y < h / 2; y++) {
        uint8_t *urow = up + static_cast<size_t>(y) * uvStride;
        uint8_t *vrow = vp + static_cast<size_t>(y) * uvStride;
        for (int x = 0; x < w / 2; x++) {
            // Average of the 2x2 block
            int r = 0, g = 0, b = 0;
            for (int dy = 0; dy < 2; dy++) {
                const uint8_t *p = rgb + (static_cast<size_t>(y * 2 + dy) * w + x * 2) * 3;
                r += p[0] + p[3];
                g += p[1] + p[4];
                b += p[2] + p[5];
            }
            r /= 4;
            g /= 4;
            b /= 4;
            urow[x * uvPixelStride] = clamp8(((k.ur * r + k.ug * g + k.ub * b + 512) >> 10) + 128);
            vrow[x * uvPixelStride] = clamp8(((k.vr * r + k.vg * g + k.vb * b + 512) >> 10) + 128);
        }
    }
}

}  // namespace

extern "C" {

JNIEXPORT jboolean JNICALL Java_de_oli_olivision_Native_init(JNIEnv *env,
                                                              jclass,
                                                              jstring paramPath,
                                                              jstring binPath)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_gpuReady) {
        ncnn::create_gpu_instance();
        if (ncnn::get_gpu_count() <= 0) {
            LOGE("No Vulkan GPU");
            return JNI_FALSE;
        }
        g_gpuReady = true;
    }

    const char *param = env->GetStringUTFChars(paramPath, nullptr);
    const char *bin = env->GetStringUTFChars(binPath, nullptr);
    std::string key = std::string(param) + "|" + bin;
    bool ok = true;
    if (g_model == nullptr || g_loadedModel != key) {
        delete g_model;
        g_model = new RealESRGAN(0, false);
        g_model->scale = 4;
        g_model->prepadding = 10;
        uint32_t heap = ncnn::get_gpu_device(0)->get_heap_budget();
        g_model->tilesize = heap > 1900 ? 200 : (heap > 550 ? 100 : 64);
        ok = g_model->load(param, bin) == 0;
        g_loadedModel = ok ? key : std::string();
        LOGI("Model %s loaded=%d tile=%d", param, ok, g_model->tilesize);
    }
    env->ReleaseStringUTFChars(paramPath, param);
    env->ReleaseStringUTFChars(binPath, bin);
    return ok ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jstring JNICALL Java_de_oli_olivision_Native_gpuName(JNIEnv *env, jclass)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_gpuReady) {
        ncnn::create_gpu_instance();
        g_gpuReady = ncnn::get_gpu_count() > 0;
    }
    if (!g_gpuReady) {
        return env->NewStringUTF("");
    }
    return env->NewStringUTF(ncnn::get_gpu_info(0).device_name());
}

// Picture: ARGB_8888 bitmap in, ARGB_8888 bitmap out (final size)
JNIEXPORT jboolean JNICALL Java_de_oli_olivision_Native_processBitmap(JNIEnv *env,
                                                                       jclass,
                                                                       jobject inBitmap,
                                                                       jobject outBitmap,
                                                                       jint pw,
                                                                       jint ph)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    AndroidBitmapInfo inInfo, outInfo;
    if (AndroidBitmap_getInfo(env, inBitmap, &inInfo) < 0
        || AndroidBitmap_getInfo(env, outBitmap, &outInfo) < 0) {
        return JNI_FALSE;
    }
    int w = static_cast<int>(inInfo.width), h = static_cast<int>(inInfo.height);
    std::vector<uint8_t> rgb(static_cast<size_t>(w) * h * 3);

    void *inPixels = nullptr;
    if (AndroidBitmap_lockPixels(env, inBitmap, &inPixels) < 0) {
        return JNI_FALSE;
    }
    for (int y = 0; y < h; y++) {
        const uint8_t *row = static_cast<const uint8_t *>(inPixels) + static_cast<size_t>(y) * inInfo.stride;
        uint8_t *out = rgb.data() + static_cast<size_t>(y) * w * 3;
        for (int x = 0; x < w; x++) {
            out[x * 3] = row[x * 4];
            out[x * 3 + 1] = row[x * 4 + 1];
            out[x * 3 + 2] = row[x * 4 + 2];
        }
    }
    AndroidBitmap_unlockPixels(env, inBitmap);

    int fw = static_cast<int>(outInfo.width), fh = static_cast<int>(outInfo.height);
    if (!runModel(rgb, w, h, pw, ph, fw, fh)) {
        return JNI_FALSE;
    }

    void *outPixels = nullptr;
    if (AndroidBitmap_lockPixels(env, outBitmap, &outPixels) < 0) {
        return JNI_FALSE;
    }
    for (int y = 0; y < fh; y++) {
        uint8_t *row = static_cast<uint8_t *>(outPixels) + static_cast<size_t>(y) * outInfo.stride;
        const uint8_t *in = rgb.data() + static_cast<size_t>(y) * fw * 3;
        for (int x = 0; x < fw; x++) {
            row[x * 4] = in[x * 3];
            row[x * 4 + 1] = in[x * 3 + 1];
            row[x * 4 + 2] = in[x * 3 + 2];
            row[x * 4 + 3] = 255;
        }
    }
    AndroidBitmap_unlockPixels(env, outBitmap);
    return JNI_TRUE;
}

// Video frame: YUV planes of the decoder image in, YUV planes of the encoder image out
JNIEXPORT jboolean JNICALL Java_de_oli_olivision_Native_processYuv(JNIEnv *env,
                                                                    jclass,
                                                                    jobject inY,
                                                                    jint inYStride,
                                                                    jint inYPixelStride,
                                                                    jobject inU,
                                                                    jobject inV,
                                                                    jint inUvStride,
                                                                    jint inUvPixelStride,
                                                                    jboolean inHighBit,
                                                                    jint w,
                                                                    jint h,
                                                                    jint pw,
                                                                    jint ph,
                                                                    jobject outY,
                                                                    jint outYStride,
                                                                    jobject outU,
                                                                    jobject outV,
                                                                    jint outUvStride,
                                                                    jint outUvPixelStride,
                                                                    jint fw,
                                                                    jint fh)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    auto *iy = static_cast<uint8_t *>(env->GetDirectBufferAddress(inY));
    auto *iu = static_cast<uint8_t *>(env->GetDirectBufferAddress(inU));
    auto *iv = static_cast<uint8_t *>(env->GetDirectBufferAddress(inV));
    auto *oy = static_cast<uint8_t *>(env->GetDirectBufferAddress(outY));
    auto *ou = static_cast<uint8_t *>(env->GetDirectBufferAddress(outU));
    auto *ov = static_cast<uint8_t *>(env->GetDirectBufferAddress(outV));
    if (!iy || !iu || !iv || !oy || !ou || !ov) {
        LOGE("No direct buffer");
        return JNI_FALSE;
    }

    std::vector<uint8_t> rgb(static_cast<size_t>(w) * h * 3);
    yuvToRgb(iy, inYStride, inYPixelStride, iu, iv, inUvStride, inUvPixelStride, inHighBit == JNI_TRUE, w,
             h, rgb.data());

    int cw = w, ch = h;
    if (!runModel(rgb, cw, ch, pw, ph, fw, fh)) {
        return JNI_FALSE;
    }
    rgbToYuv(rgb.data(), fw, fh, oy, outYStride, ou, ov, outUvStride, outUvPixelStride);
    return JNI_TRUE;
}

}  // extern "C"
