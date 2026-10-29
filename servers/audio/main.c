/* WAV parsing, resampling, ownership and playback policy live in user space. */
#include <aurora/user.h>
#include <aurora/file.h>
#include <aurora/wav.h>
#include <aurora/model.h>
#include <string.h>
#define PENDING 8
static file_client_t files;
static ipc_message_t pending[PENDING];
static unsigned pending_count;
static uint64_t fd, owner, output_at, output_total, last_position;
static credentials_t credentials;
static wav_info_t wave;
static bool active, cancelled, finished;
static int64_t last_error;
static uint8_t cache[USER_IO_SIZE];
static uint64_t cache_offset;
static uint32_t cache_bytes;
static int16_t output[PCM_MAX_FRAMES * 2];
static uint32_t output_count;
static void delayed(const ipc_message_t *msg, void *context)
{
    (void)context;
    if (msg->from == KERNEL_ENDPOINT && msg->type == MSG_SESSION_CHANGED) {
        cancelled = true;
        return;
    }
    if (pending_count < PENDING)
        pending[pending_count++] = *msg;
    else {
        ipc_message_t reply = {
            .type = MSG_AUDIO_RESULT, .arg0 = msg->arg0, .arg1 = (uint64_t)-AURORA_EBUSY};
        user_send((int64_t)msg->from, &reply);
    }
}
static int64_t read_at(void *ctx, void *buffer, uint32_t count, uint64_t offset)
{
    (void)ctx;
    if (cancelled)
        return -AURORA_EPERM;
    return file_pread(&files, fd, buffer, count, offset);
}
static void stop(int64_t error)
{
    pcm_status_t status;
    if (active && sys_pcm_control(PCM_STATUS, &status) == 0)
        last_position = status.played_frames;
    sys_pcm_control(PCM_STOP, &status);
    uint64_t old = fd;
    fd = 0;
    active = false;
    output_count = 0;
    finished = false;
    cache_bytes = 0;
    last_error = error;
    if (old)
        (void)file_close(&files, old);
}
static bool source_frame(uint64_t index, int16_t sample[2])
{
    if (index >= wave.frames)
        index = wave.frames - 1;
    uint64_t offset = wave.data_offset + index * wave.block_align;
    if (!cache_bytes || offset < cache_offset ||
        offset + wave.block_align > cache_offset + cache_bytes) {
        uint64_t remaining = wave.data_offset + wave.data_bytes - offset;
        uint32_t count = remaining < USER_IO_SIZE ? (uint32_t)remaining : USER_IO_SIZE;
        int64_t got = read_at(NULL, cache, count, offset);
        if (got != (int64_t)count)
            return false;
        cache_offset = offset;
        cache_bytes = count;
    }
    wav_frame_s16(&wave, cache + (size_t)(offset - cache_offset), sample);
    return true;
}
static bool make_output(void)
{
    output_count = 0;
    while (output_count < PCM_MAX_FRAMES && output_at < output_total) {
        uint64_t numerator = output_at * wave.sample_rate, index = numerator / PCM_RATE;
        uint32_t fraction = (uint32_t)(numerator % PCM_RATE);
        int16_t a[2], b[2];
        if (!source_frame(index, a))
            return false;
        if (fraction) {
            if (!source_frame(index + 1, b))
                return false;
        } else {
            b[0] = a[0];
            b[1] = a[1];
        }
        for (unsigned ch = 0; ch < 2; ch++)
            output[output_count * 2 + ch] =
                (int16_t)((int64_t)a[ch] + ((int64_t)b[ch] - a[ch]) * fraction / PCM_RATE);
        output_at++;
        output_count++;
    }
    return true;
}
static void pump(void)
{
    if (!active)
        return;
    credentials_t current, session;
    sys_session_get(&session);
    if (cancelled || sys_peer_credentials((int)owner, &current) < 0 ||
        current.session != credentials.session || current.uid != credentials.uid ||
        session.session != credentials.session) {
        stop(-AURORA_EPERM);
        return;
    }
    pcm_status_t status;
    if (sys_pcm_control(PCM_STATUS, &status) < 0) {
        stop(-AURORA_ENOSYS);
        return;
    }
    if (finished) {
        if (!status.queued_frames) {
            uint64_t played = status.played_frames;
            uint32_t underruns = status.underruns;
            stop(0);
            char log[100] = "audiod: WAV complete frames=";
            text_u64(log, sizeof(log), played);
            text_append(log, sizeof(log), " underruns=");
            text_u64(log, sizeof(log), underruns);
            user_log(log);
        }
        return;
    }
    for (unsigned budget = 0; budget < 24; budget++) {
        if (!output_count && !make_output()) {
            stop(-AURORA_EIO);
            user_log("audiod: WAV read failed");
            return;
        }
        if (cancelled) {
            stop(-AURORA_EPERM);
            return;
        }
        if (output_count) {
            int64_t result = sys_pcm_write(output, output_count);
            if (result == -AURORA_EAGAIN)
                return;
            if (result < 0) {
                stop(result);
                return;
            }
            output_count = 0;
        }
        if (output_at == output_total) {
            finished = true;
            sys_pcm_control(PCM_FINISH, &status);
            return;
        }
    }
}
static void result(const ipc_message_t *msg, int64_t code)
{
    ipc_message_t reply = {.type = MSG_AUDIO_RESULT, .arg0 = msg->arg0, .arg1 = (uint64_t)code};
    user_send((int64_t)msg->from, &reply);
}
static void handle(const ipc_message_t *msg)
{
    if (msg->from == KERNEL_ENDPOINT) {
        if (msg->type == MSG_SESSION_CHANGED) {
            cancelled = true;
            stop(-AURORA_EPERM);
            memset(&wave, 0, sizeof(wave));
            output_total = last_position = 0;
        }
        return;
    }

    int64_t caps = sys_peer_caps((int)msg->from);
    if (caps < 0 || !(caps & CAP_AUDIO)) {
        result(msg, -AURORA_EPERM);
        return;
    }
    if (msg->type == MSG_AUDIO_PLAY) {
        int64_t r = msg->arg1 > 8000 || msg->arg2 > 200
                        ? -AURORA_EINVAL
                        : sys_audio_tone((uint32_t)msg->arg1, (uint32_t)msg->arg2);
        result(msg, r);
        if (!r)
            user_log("audiod: tone accepted");
        return;
    }
    credentials_t peer, session;
    sys_session_get(&session);
    if (sys_peer_credentials((int)msg->from, &peer) < 0 || peer.session != session.session ||
        !peer.session) {
        result(msg, -AURORA_EPERM);
        return;
    }
    if (msg->type == MSG_AUDIO_STATUS) {
        pcm_status_t status = {0};
        sys_pcm_control(PCM_STATUS, &status);
        audio_info_t info = {active,
                             wave.sample_rate,
                             wave.channels,
                             wave.bits,
                             active ? status.played_frames : last_position,
                             output_total,
                             last_error};
        ipc_message_t reply = {.type = MSG_AUDIO_INFO, .arg0 = msg->arg0};
        memcpy(reply.data, &info, sizeof(info));
        user_send((int64_t)msg->from, &reply);
        return;
    }
    if (msg->type == MSG_AUDIO_STOP) {
        if (active && owner != msg->from && peer.uid != 0) {
            result(msg, -AURORA_EPERM);
            return;
        }
        stop(0);
        result(msg, 0);
        user_log("audiod: playback stopped");
        return;
    }
    if (msg->type != MSG_AUDIO_WAV) {
        result(msg, -AURORA_EINVAL);
        return;
    }
    if (active) {
        result(msg, -AURORA_EBUSY);
        return;
    }
    uint32_t size;
    if (file_claim(&files, msg->arg1, (int64_t)msg->from, &size) < 0) {
        result(msg, -AURORA_EPERM);
        return;
    }
    fd = msg->arg1;
    cancelled = false;
    owner = msg->from;
    credentials = peer;
    if (!wav_parse(read_at, NULL, size, &wave)) {
        stop(-AURORA_EINVAL);
        result(msg, -AURORA_EINVAL);
        user_log("audiod: unsupported or malformed WAV");
        return;
    }
    pcm_status_t status;
    if (cancelled || sys_pcm_control(PCM_STOP, &status) < 0) {
        stop(-AURORA_ENOSYS);
        result(msg, -AURORA_ENOSYS);
        return;
    }
    active = true;
    finished = false;
    output_at = 0;
    last_position = 0;
    output_count = 0;
    cache_bytes = 0;
    last_error = 0;
    output_total = (wave.frames * PCM_RATE + wave.sample_rate - 1) / wave.sample_rate;
    result(msg, 0);
    char log[100] = "audiod: WAV started ";
    text_u64(log, sizeof(log), wave.sample_rate);
    text_append(log, sizeof(log), " Hz ");
    text_u64(log, sizeof(log), wave.bits);
    text_append(log, sizeof(log), "-bit channels=");
    text_u64(log, sizeof(log), wave.channels);
    user_log(log);
}
int umain(uint64_t argument)
{
    (void)argument;
    file_client_init(&files, delayed, NULL);
    user_log("audiod: WAV service online");
    for (;;) {
        ipc_message_t msg;
        int budget = 16;
        while (budget--) {
            if (pending_count) {
                msg = pending[0];
                memmove(pending, pending + 1, (--pending_count) * sizeof(msg));
            } else if (sys_recv(&msg) < 0)
                break;
            handle(&msg);
        }
        pump();
        sys_wait(active ? 1 : 100);
    }
}
