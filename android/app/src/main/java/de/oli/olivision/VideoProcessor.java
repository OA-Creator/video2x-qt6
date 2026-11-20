package de.oli.olivision;

import android.content.Context;
import android.graphics.ImageFormat;
import android.media.Image;
import android.media.MediaCodec;
import android.media.MediaCodecInfo;
import android.media.MediaCodecList;
import android.media.MediaExtractor;
import android.media.MediaFormat;
import android.media.MediaMuxer;
import android.net.Uri;
import android.os.Build;
import android.os.ParcelFileDescriptor;

import java.nio.ByteBuffer;

/** Videos: decode with the phone chip, AI per picture, encode with the phone chip (HEVC), keep the sound. */
final class VideoProcessor {
    private static final long TIMEOUT_US = 10_000;

    private final Context context;
    private final Job job;
    private final Job.Plan plan;

    private MediaExtractor videoIn;
    private MediaExtractor audioIn;
    private MediaCodec decoder;
    private MediaCodec encoder;
    private MediaMuxer muxer;
    private ParcelFileDescriptor outFd;
    private int muxVideoTrack = -1;
    private int muxAudioTrack = -1;
    private boolean muxerStarted;
    private MediaFormat audioFormat;
    private boolean audioDone = true;
    private final MediaCodec.BufferInfo audioInfo = new MediaCodec.BufferInfo();
    private ByteBuffer audioBuffer;

    private long framesDone;
    private long startNs;

    VideoProcessor(Context context, Job job, Job.Plan plan) {
        this.context = context;
        this.job = job;
        this.plan = plan;
    }

    void run() throws Exception {
        Uri target = null;
        try {
            openInput();
            String name = Output.resultName(job.name, "mp4");
            target = Output.create(context, true, name, "video/mp4");
            if (target == null) {
                throw new IllegalStateException("Speichern nicht möglich.");
            }
            outFd = context.getContentResolver().openFileDescriptor(target, "rw");
            muxer = new MediaMuxer(outFd.getFileDescriptor(), MediaMuxer.OutputFormat.MUXER_OUTPUT_MPEG_4);
            muxer.setOrientationHint(job.rotation);
            CrashReport.step("Video geöffnet");
            createEncoder();
            CrashReport.step("Encoder bereit");
            loop();
            CrashReport.step("Bilder fertig: " + framesDone);
            finishAudio();
            muxer.stop();
            muxer.release();
            muxer = null;
            outFd.close();
            outFd = null;
            Output.publish(context, target);

            double seconds = (System.nanoTime() - startNs) / 1e9;
            if (seconds > 1 && framesDone > 0) {
                Settings.learnSpeed(context, plan.modelKey,
                        (double) plan.aiWidth * plan.aiHeight * framesDone / seconds);
            }
            job.outputUri = target;
            job.outputName = "Filme/OliVision/" + name;
            job.outputBytes = Output.size(context, target);
            job.progress = 1;
        } catch (Exception e) {
            Output.delete(context, target);
            throw e;
        } finally {
            release();
        }
    }

    private void openInput() throws Exception {
        videoIn = new MediaExtractor();
        videoIn.setDataSource(context, job.uri, null);
        int videoTrack = -1;
        int audioTrack = -1;
        for (int i = 0; i < videoIn.getTrackCount(); i++) {
            String mime = videoIn.getTrackFormat(i).getString(MediaFormat.KEY_MIME);
            if (mime == null) {
                continue;
            }
            if (videoTrack < 0 && mime.startsWith("video/")) {
                videoTrack = i;
            } else if (audioTrack < 0 && mime.startsWith("audio/")) {
                audioTrack = i;
            }
        }
        if (videoTrack < 0) {
            throw new IllegalStateException("Kein Bild im Video gefunden.");
        }
        videoIn.selectTrack(videoTrack);
        MediaFormat format = videoIn.getTrackFormat(videoTrack);
        String mime = format.getString(MediaFormat.KEY_MIME);
        format.setInteger(MediaFormat.KEY_COLOR_FORMAT,
                MediaCodecInfo.CodecCapabilities.COLOR_FormatYUV420Flexible);
        if (Build.VERSION.SDK_INT >= 31) {
            // HDR videos: ask the chip for normal colours if it can
            format.setInteger(MediaFormat.KEY_COLOR_TRANSFER_REQUEST, MediaFormat.COLOR_TRANSFER_SDR_VIDEO);
        }
        decoder = MediaCodec.createDecoderByType(mime);
        decoder.configure(format, null, null, 0);
        decoder.start();

        if (audioTrack >= 0) {
            audioIn = new MediaExtractor();
            audioIn.setDataSource(context, job.uri, null);
            audioIn.selectTrack(audioTrack);
            audioFormat = audioIn.getTrackFormat(audioTrack);
            int max = audioFormat.containsKey(MediaFormat.KEY_MAX_INPUT_SIZE)
                    ? audioFormat.getInteger(MediaFormat.KEY_MAX_INPUT_SIZE) : 0;
            audioBuffer = ByteBuffer.allocateDirect(Math.max(max, 1 << 20));
        }
    }

    private void createEncoder() throws Exception {
        String mime = MediaFormat.MIMETYPE_VIDEO_HEVC;
        double factor = 1.3;
        if (!supports(mime, plan.finalWidth, plan.finalHeight)) {
            mime = MediaFormat.MIMETYPE_VIDEO_AVC;
            factor = 2.0;
            if (!supports(mime, plan.finalWidth, plan.finalHeight)) {
                throw new IllegalStateException("Das Handy kann diese Größe nicht als Video speichern. "
                        + "Bitte eine kleinere Größe wählen.");
            }
        }
        double fps = job.frameRate > 0 ? job.frameRate : 30;
        double bitrate = Job.bitrate(plan.finalWidth, plan.finalHeight, fps, factor);

        MediaFormat f = MediaFormat.createVideoFormat(mime, plan.finalWidth, plan.finalHeight);
        f.setInteger(MediaFormat.KEY_COLOR_FORMAT, MediaCodecInfo.CodecCapabilities.COLOR_FormatYUV420Flexible);
        f.setInteger(MediaFormat.KEY_BIT_RATE, (int) bitrate);
        f.setInteger(MediaFormat.KEY_BITRATE_MODE, MediaCodecInfo.EncoderCapabilities.BITRATE_MODE_VBR);
        f.setFloat(MediaFormat.KEY_FRAME_RATE, (float) fps);
        f.setInteger(MediaFormat.KEY_I_FRAME_INTERVAL, 1);
        boolean hd = plan.finalHeight >= 720 && plan.finalWidth >= 720;
        f.setInteger(MediaFormat.KEY_COLOR_STANDARD,
                hd ? MediaFormat.COLOR_STANDARD_BT709 : MediaFormat.COLOR_STANDARD_BT601_PAL);
        f.setInteger(MediaFormat.KEY_COLOR_RANGE, MediaFormat.COLOR_RANGE_LIMITED);
        f.setInteger(MediaFormat.KEY_COLOR_TRANSFER, MediaFormat.COLOR_TRANSFER_SDR_VIDEO);
        encoder = MediaCodec.createEncoderByType(mime);
        encoder.configure(f, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE);
        encoder.start();
    }

    private static boolean supports(String mime, int w, int h) {
        for (MediaCodecInfo info : new MediaCodecList(MediaCodecList.REGULAR_CODECS).getCodecInfos()) {
            if (!info.isEncoder()) {
                continue;
            }
            for (String type : info.getSupportedTypes()) {
                if (!type.equalsIgnoreCase(mime)) {
                    continue;
                }
                MediaCodecInfo.VideoCapabilities v = info.getCapabilitiesForType(type).getVideoCapabilities();
                if (v != null && (v.isSizeSupported(w, h) || v.isSizeSupported(h, w))) {
                    return true;
                }
            }
        }
        return false;
    }

    private void loop() throws Exception {
        MediaCodec.BufferInfo info = new MediaCodec.BufferInfo();
        boolean inputDone = false;
        boolean decoderDone = false;
        boolean encoderDone = false;
        long lastPts = 0;
        long total = job.frameCount();
        startNs = System.nanoTime();

        while (!encoderDone) {
            Jobs jobs = Jobs.get();
            while (jobs.paused && !jobs.abort) {
                Thread.sleep(200);
            }
            if (jobs.abort) {
                throw new InterruptedException();
            }

            if (!inputDone) {
                int i = decoder.dequeueInputBuffer(TIMEOUT_US);
                if (i >= 0) {
                    ByteBuffer buf = decoder.getInputBuffer(i);
                    int size = buf != null ? videoIn.readSampleData(buf, 0) : -1;
                    if (size < 0) {
                        decoder.queueInputBuffer(i, 0, 0, 0, MediaCodec.BUFFER_FLAG_END_OF_STREAM);
                        inputDone = true;
                    } else {
                        decoder.queueInputBuffer(i, 0, size, videoIn.getSampleTime(), 0);
                        videoIn.advance();
                    }
                }
            }

            encoderDone = drainEncoder(info, decoderDone);

            if (!decoderDone) {
                int o = decoder.dequeueOutputBuffer(info, TIMEOUT_US);
                if (o >= 0) {
                    boolean eos = (info.flags & MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0;
                    if (info.size > 0 || !eos) {
                        Image image = decoder.getOutputImage(o);
                        if (image != null) {
                            lastPts = info.presentationTimeUs;
                            encodeFrame(image, lastPts, info);
                            image.close();
                            framesDone++;
                            report(total);
                        }
                    }
                    decoder.releaseOutputBuffer(o, false);
                    if (eos) {
                        decoderDone = true;
                        int e = dequeueEncoderInput(info);
                        encoder.queueInputBuffer(e, 0, 0, lastPts, MediaCodec.BUFFER_FLAG_END_OF_STREAM);
                    }
                }
            }
        }
    }

    private int dequeueEncoderInput(MediaCodec.BufferInfo info) throws Exception {
        while (true) {
            int e = encoder.dequeueInputBuffer(TIMEOUT_US);
            if (e >= 0) {
                return e;
            }
            drainEncoder(info, false);
            if (Jobs.get().abort) {
                throw new InterruptedException();
            }
        }
    }

    private void encodeFrame(Image in, long pts, MediaCodec.BufferInfo info) throws Exception {
        int e = dequeueEncoderInput(info);
        Image out = encoder.getInputImage(e);
        if (out == null) {
            throw new IllegalStateException("Der Video-Speicher des Handys lässt sich nicht beschreiben.");
        }
        Image.Plane[] ip = in.getPlanes();
        Image.Plane[] op = out.getPlanes();
        boolean highBit = in.getFormat() == ImageFormat.YCBCR_P010 || ip[0].getPixelStride() == 2;
        int w = in.getCropRect() != null ? in.getCropRect().width() : in.getWidth();
        int h = in.getCropRect() != null ? in.getCropRect().height() : in.getHeight();
        w = Math.min(w, job.width) & ~1;
        h = Math.min(h, job.height) & ~1;
        if (framesDone == 0) {
            CrashReport.step("Erstes Bild: Format " + in.getFormat() + ", Pixelabstand " + ip[0].getPixelStride()
                    + "/" + ip[1].getPixelStride() + ", Ausgabe " + op[1].getPixelStride());
        }
        boolean ok = Native.processYuv(
                ip[0].getBuffer(), ip[0].getRowStride(), ip[0].getPixelStride(),
                ip[1].getBuffer(), ip[2].getBuffer(), ip[1].getRowStride(), ip[1].getPixelStride(),
                highBit, w, h, plan.prescaleWidth, plan.prescaleHeight,
                op[0].getBuffer(), op[0].getRowStride(),
                op[1].getBuffer(), op[2].getBuffer(), op[1].getRowStride(), op[1].getPixelStride(),
                plan.finalWidth, plan.finalHeight);
        out.close();
        if (!ok) {
            throw new IllegalStateException("Die KI konnte das Video nicht bearbeiten.");
        }
        int size = plan.finalWidth * plan.finalHeight * 3 / 2;
        encoder.queueInputBuffer(e, 0, size, pts, 0);
    }

    /** Takes finished pieces from the encoder and writes them into the file. Returns true at the end. */
    private boolean drainEncoder(MediaCodec.BufferInfo info, boolean wait) throws Exception {
        while (true) {
            int o = encoder.dequeueOutputBuffer(info, wait ? TIMEOUT_US : 0);
            if (o == MediaCodec.INFO_TRY_AGAIN_LATER) {
                return false;
            }
            if (o == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED) {
                startMuxer(encoder.getOutputFormat());
                continue;
            }
            if (o < 0) {
                continue;
            }
            ByteBuffer data = encoder.getOutputBuffer(o);
            boolean config = (info.flags & MediaCodec.BUFFER_FLAG_CODEC_CONFIG) != 0;
            if (data != null && info.size > 0 && !config && muxerStarted) {
                writeAudioUntil(info.presentationTimeUs);
                data.position(info.offset);
                data.limit(info.offset + info.size);
                muxer.writeSampleData(muxVideoTrack, data, info);
            }
            encoder.releaseOutputBuffer(o, false);
            if ((info.flags & MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0) {
                return true;
            }
        }
    }

    private void startMuxer(MediaFormat videoFormat) {
        if (muxerStarted) {
            return;
        }
        muxVideoTrack = muxer.addTrack(videoFormat);
        if (audioFormat != null) {
            try {
                muxAudioTrack = muxer.addTrack(audioFormat);
                audioDone = false;
            } catch (Exception e) {
                muxAudioTrack = -1;   // sound format not possible in MP4, video without sound
            }
        }
        muxer.start();
        muxerStarted = true;
    }

    /** Sound is copied unchanged, always a little ahead of the picture. */
    private void writeAudioUntil(long ptsUs) {
        while (!audioDone && muxAudioTrack >= 0) {
            long t = audioIn.getSampleTime();
            if (t < 0) {
                audioDone = true;
                return;
            }
            if (ptsUs >= 0 && t > ptsUs) {
                return;
            }
            audioBuffer.clear();
            int size = audioIn.readSampleData(audioBuffer, 0);
            if (size < 0) {
                audioDone = true;
                return;
            }
            audioInfo.set(0, size, t,
                    (audioIn.getSampleFlags() & MediaExtractor.SAMPLE_FLAG_SYNC) != 0
                            ? MediaCodec.BUFFER_FLAG_KEY_FRAME : 0);
            muxer.writeSampleData(muxAudioTrack, audioBuffer, audioInfo);
            audioIn.advance();
        }
    }

    private void finishAudio() {
        if (muxerStarted) {
            writeAudioUntil(-1);
        }
    }

    private void report(long total) {
        double seconds = (System.nanoTime() - startNs) / 1e9;
        job.progress = Math.min(0.999, (double) framesDone / total);
        String text = "Bild " + framesDone + " von " + total;
        if (framesDone >= 2 && seconds > 0) {
            double left = seconds / framesDone * Math.max(0, total - framesDone);
            text += " · noch ca. " + Format.duration(left);
        }
        job.detail = text;
        Jobs.get().changed();
        ProcessingService.updateNotification(context);
    }

    private void release() {
        try {
            if (decoder != null) {
                decoder.stop();
            }
        } catch (Exception ignored) {
        }
        try {
            if (decoder != null) {
                decoder.release();
            }
        } catch (Exception ignored) {
        }
        try {
            if (encoder != null) {
                encoder.stop();
            }
        } catch (Exception ignored) {
        }
        try {
            if (encoder != null) {
                encoder.release();
            }
        } catch (Exception ignored) {
        }
        try {
            if (muxer != null) {
                muxer.release();
            }
        } catch (Exception ignored) {
        }
        try {
            if (outFd != null) {
                outFd.close();
            }
        } catch (Exception ignored) {
        }
        if (videoIn != null) {
            videoIn.release();
        }
        if (audioIn != null) {
            audioIn.release();
        }
    }
}
