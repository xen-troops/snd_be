/*
 *  PipeWire pcm device wrapper
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 * Copyright (C) 2023 Jiang Su, Bo Peng.
 */

#include "PipeWirePcm.hpp"
#include <xen/io/sndif.h>

using std::bind;
using std::chrono::milliseconds;
using std::lock_guard;
using std::string;
using std::to_string;

using SoundItf::PcmParams;
using SoundItf::StreamType;

namespace PipeWire {

/*******************************************************************************
 * PipeWireMainloop
 ******************************************************************************/

PipeWireMainloop::PipeWireMainloop(const string& name) :
	mThreadLoop(nullptr),
	mContext(nullptr),
	mCore(nullptr),
	mMutex(nullptr),
    mLastSeq(0),
    mPendingSeq(0),
    mError(0),
	mLog("PipeWireMainloop")
{
	try
    {
		init(name);
	}
    catch(const std::exception& e)
    {
		release();
		throw;
	}
}

PipeWireMainloop::~PipeWireMainloop()
{
	release();
}

/*******************************************************************************
 * Public
 ******************************************************************************/

PipeWirePcm* PipeWireMainloop::createStream(StreamType type, const string& name,
									  const string& propName,
									  const string& propValue,
									  const string& deviceName)
{
	return new PipeWirePcm(mThreadLoop, mContext, mCore, type, name,
						propName, propValue, deviceName);
}

/*******************************************************************************
 * Private
 ******************************************************************************/

void PipeWireMainloop::init(const string& name)
{
	LOG(mLog, DEBUG) << "Init";

	pw_init(nullptr, nullptr);

	// Make a new thread loop with the given name and optional properties
	mThreadLoop = pw_thread_loop_new("PipeWire thread loop", nullptr /*p*/);
	if (!mThreadLoop)
	{
        throw Exception("Can't create PipeWire thread loop", -errno);
    }

    mContext = pw_context_new(pw_thread_loop_get_loop(mThreadLoop), nullptr, 0);
    if (!mContext)
    {
        throw Exception("Can't create PipeWire context", -errno);
    }

    pw_thread_loop_start(mThreadLoop);

    mMutex = PipeWireMutex(mThreadLoop);

    lock_guard<PipeWireMutex> lock(mMutex);

    mCore = pw_context_connect(mContext, nullptr, 0);
    if (!mCore)
    {
        throw Exception("Can't connect PipeWire daemon", -errno);
    }

    pw_core_add_listener(mCore, &mCoreListener, &sCoreStateChanged, this);

    waitResync();

	return;
}

void PipeWireMainloop::release()
{
	if (mThreadLoop) {
		pw_thread_loop_stop(mThreadLoop);
	}

	if (mCore) {
		spa_hook_remove(&mCoreListener);
		spa_zero(mCoreListener);
		pw_core_disconnect(mCore);
	}

	if (mContext) {
		pw_context_destroy(mContext);
	}

	if (mThreadLoop) {
		pw_thread_loop_destroy(mThreadLoop);
	}

	/* Deinitialize the PipeWire system and free up all resources allocated by pw_init(). */
	pw_deinit();

	LOG(mLog, DEBUG) << "Release";
}

void PipeWireMainloop::sCoreDone(void *data, uint32_t id, int seq)
{
    static_cast<PipeWireMainloop *>(data)->coreDone(id, seq);
}

void PipeWireMainloop::sCoreError(void *data, uint32_t id, int seq,
                                    int res, const char *message)
{
    static_cast<PipeWireMainloop *>(data)->coreError(id, seq, res, message);
}

const struct pw_core_events PipeWireMainloop::sCoreStateChanged = {
    PW_VERSION_CORE_EVENTS,
    .done = sCoreDone,
    .error = sCoreError,
};

int PipeWireMainloop::waitResync()
{
    int res;

    mPendingSeq = pw_core_sync(mCore, PW_ID_CORE, mPendingSeq);

    while (true)
    {
        pw_thread_loop_wait(mThreadLoop);

        res = mError;

        if (res < 0)
        {
            mError = 0;
            return res;
        }

        if (mPendingSeq == mLastSeq)
        {
            break;
        }
    }

    return 0;
}

void PipeWireMainloop::coreDone(uint32_t id, int seq)
{
    assert(id == PW_ID_CORE);

    mLastSeq = seq;

    if (mPendingSeq == seq)
    {
        /* stop and exit the thread loop */
        pw_thread_loop_signal(mThreadLoop, false);
    }
}

void PipeWireMainloop::coreError(uint32_t id, int seq, int res,
                                    const char *message)
{
    LOG(mLog, DEBUG) << "error id: " << id << ", seq: " << seq
                     << " " << spa_strerror(res) << " " << message;

	if (id == PW_ID_CORE)
    {
		switch (res) {
		case -ENOENT:
			break;
		default:
			mError = res;
		}
	}

	/* stop and exit the thread loop */
	pw_thread_loop_signal(mThreadLoop, false);
}

/*******************************************************************************
 * PipeWirePcm
 ******************************************************************************/

PipeWirePcm::PipeWirePcm(pw_thread_loop* threadloop, pw_context* context, pw_core *core,
			StreamType type,
			const std::string& name,
			const string& propName,
			const string& propValue,
			const std::string& deviceName) :
	mThreadLoop(threadloop),
	mContext(context),
	mCore(core),
	mStream(nullptr),
	mFrameSize(1),
	mMutex(threadloop),
	mType(type),
	mName(name),
	mPropName(propName),
	mPropValue(propValue),
	mDeviceName(deviceName),
    mTimer(bind(&PipeWirePcm::getTimeStamp, this), true),
	mLog("PipeWirePcm")
{
	LOG(mLog, DEBUG) << "Create pcm device: " << mName;
}

PipeWirePcm::~PipeWirePcm()
{
	close();

	LOG(mLog, DEBUG) << "Delete pcm device: "<< mName;
}

/*******************************************************************************
 * Public
 ******************************************************************************/

void PipeWirePcm::open(const PcmParams& params)
{
	lock_guard<PipeWireMutex> lock(mMutex);

	LOG(mLog, DEBUG) << "Open pcm device: " << mName;

	LOG(mLog, DEBUG) << "Format: "
	  << sample_format_to_string(convertPcmFormat(params.format))
	  << ", rate: " << params.rate
	  << ", channels: " << static_cast<int>(params.numChannels)
	  << ", period: " << params.periodSize
	  << ", buffer: " << params.bufferSize;

	mParams = params;

	createStream();

    /* Set timeout to one period */
    mTimerPeriodMs = milliseconds((params.periodSize / mFrameSize) * 1000 / params.rate);

	waitStreamReady();
}

void PipeWirePcm::close()
{
	lock_guard<PipeWireMutex> lock(mMutex);

	LOG(mLog, DEBUG) << "Close pcm device: " << mName;

	if (mStream)
	{
		flush();

		pw_stream_destroy(mStream);

		mStream = nullptr;
	}
}

void PipeWirePcm::read(uint8_t *buffer, size_t size)
{
	lock_guard<PipeWireMutex> lock(mMutex);

	int32_t avail;
	uint32_t index;

	DLOG(mLog, DEBUG) << "Read from pcm device: " << mName
					  << ", size: " << size;

	if (mType != StreamType::CAPTURE)
	{
		throw Exception("Wrong stream type", -EINVAL);
	}

	if (!buffer || !size)
	{
		throw Exception("Can't read stream", -EINVAL);
	}

	checkStatus();

	avail = spa_ringbuffer_get_read_index(&mRing, &index);

	if (avail < (int32_t)size)
    {
		size = avail;
	}

	spa_ringbuffer_read_data(&mRing, mBuffer, RINGBUFFER_SIZE,
                                index & RINGBUFFER_MASK, buffer, size);

	index += size;
	spa_ringbuffer_read_update(&mRing, index);
}

void PipeWirePcm::write(uint8_t *buffer, size_t size)
{
	lock_guard<PipeWireMutex> lock(mMutex);

	int32_t filled, avail;
	uint32_t index;

	DLOG(mLog, DEBUG) << "Write to pcm device: " << mName
					  << ", size: " << size;

	if (mType != StreamType::PLAYBACK)
	{
		throw Exception("Wrong stream type", -EINVAL);
	}

	if (!buffer || !size)
	{
		throw Exception("Can't write stream", -EINVAL);
	}

	checkStatus();

	filled = spa_ringbuffer_get_write_index(&mRing, &index);

    avail = RINGBUFFER_SIZE - filled;

    if (size > (size_t)avail)
    {
        size = avail;
    }

    if (filled < 0)
    {
		LOG(mLog, DEBUG) << "Underrun write: " << index
                         << ", filled: " << filled;
	}
    else
    {
		if ((uint32_t)filled + size > RINGBUFFER_SIZE)
        {
			LOG(mLog, DEBUG) << "Overrun write: " << index
                             << ", filled: " << filled
                             << " + size: " << size
                             << " > max: " << RINGBUFFER_SIZE;
		}
	}

	spa_ringbuffer_write_data(&mRing, mBuffer, RINGBUFFER_SIZE,
                                index & RINGBUFFER_MASK, buffer, size);

	index += size;
	spa_ringbuffer_write_update(&mRing, index);
}

void PipeWirePcm::start()
{
	lock_guard<PipeWireMutex> lock(mMutex);

	LOG(mLog, DEBUG) << "Start";

    if (!mStream)
	{
		throw Exception("Device not open", -EIO);
	}

    pw_stream_set_active(mStream, true);

    mTimer.start(mTimerPeriodMs);
}

void PipeWirePcm::stop()
{
	lock_guard<PipeWireMutex> lock(mMutex);

	LOG(mLog, DEBUG) << "Stop";

    if (!mStream)
	{
		throw Exception("Device not open", -EIO);
	}

    pw_stream_set_active(mStream, false);

    mTimer.stop();
}

void PipeWirePcm::pause()
{
	lock_guard<PipeWireMutex> lock(mMutex);

	LOG(mLog, DEBUG) << "Pause";

    if (!mCore)
	{
        throw Exception("Can't connect PipeWire daemon", -ENOTCONN);
	}

    int ret = pw_core_set_paused(mCore, true);
    if (ret < 0)
    {
        throw Exception("Can't pause stream", -errno);
    }

    mTimer.stop();
}

void PipeWirePcm::resume()
{
	lock_guard<PipeWireMutex> lock(mMutex);

	LOG(mLog, DEBUG) << "Resume";

    if (!mCore)
	{
        throw Exception("Can't connect PipeWire daemon", -ENOTCONN);
	}

    int ret = pw_core_set_paused(mCore, false);
    if (ret < 0)
    {
        throw Exception("Can't resume stream", -errno);
    }

    mTimer.start(mTimerPeriodMs);
}

/*******************************************************************************
 * Private
 ******************************************************************************/

void PipeWirePcm::sStreamDestroy(void *data)
{
	static_cast<PipeWirePcm *>(data)->streamDestroy();
}

void PipeWirePcm::sStreamStateChanged(void *data, enum pw_stream_state old,
        enum pw_stream_state state, const char *error)
{
	static_cast<PipeWirePcm *>(data)->streamStateChanged(old, state, error);
}

void PipeWirePcm::sStreamPlaybackProcess(void *data)
{
	static_cast<PipeWirePcm *>(data)->streamPlaybackProcess();
}

void PipeWirePcm::sStreamCaptureProcess(void *data)
{
	static_cast<PipeWirePcm *>(data)->streamCaptureProcess();
}

const struct pw_stream_events PipeWirePcm::sPlaybackStreamEvents = {
    PW_VERSION_STREAM_EVENTS,
    .destroy = sStreamDestroy,
    .state_changed = sStreamStateChanged,
    .process = sStreamPlaybackProcess
};

const struct pw_stream_events PipeWirePcm::sCaptureStreamEvents = {
    PW_VERSION_STREAM_EVENTS,
    .destroy = sStreamDestroy,
    .state_changed = sStreamStateChanged,
    .process = sStreamCaptureProcess
};

void PipeWirePcm::streamDestroy()
{
	spa_hook_remove(&mStreamListener);
}

void PipeWirePcm::streamStateChanged(enum pw_stream_state old,
        enum pw_stream_state state, const char *error)
{
    LOG(mLog, DEBUG) << "stream state changed " << pw_stream_state_as_string(old)
                     << " -> " << pw_stream_state_as_string(state);

	switch (state)
	{
		case PW_STREAM_STATE_ERROR:
		case PW_STREAM_STATE_UNCONNECTED:
			break;
		case PW_STREAM_STATE_PAUSED:
		case PW_STREAM_STATE_CONNECTING:
		case PW_STREAM_STATE_STREAMING:
			break;
	}

    pw_thread_loop_signal(mThreadLoop, false);
}

void PipeWirePcm::streamPlaybackProcess()
{
	void *p;
	struct pw_buffer *b;
	struct spa_buffer *buf;
	uint32_t req, index, bytes_n;
	int32_t avail;

	if (!mStream) {
		throw Exception("Device not open", -EIO);
	}

	/* Obtain a buffer to read from */
	if ((b = pw_stream_dequeue_buffer(mStream)) == nullptr) {
		return;
	}

	buf = b->buffer;
	p = buf->datas[0].data;
	if (p == nullptr) {
		return;
	}

	/* Calculate the total no of bytes to read data from buffer */
	req = b->requested * mFrameSize;

	bytes_n = SPA_MIN(req, buf->datas[0].maxsize);

	/* Get no of available bytes to read data from buffer */
	avail = spa_ringbuffer_get_read_index(&mRing, &index);

	if (avail < (int32_t)bytes_n) {
		bytes_n = avail;
	}

	spa_ringbuffer_read_data(&mRing, mBuffer, RINGBUFFER_SIZE,
								index & RINGBUFFER_MASK, p, bytes_n);

	index += bytes_n;
	spa_ringbuffer_read_update(&mRing, index);

	buf->datas[0].chunk->offset = 0;
	buf->datas[0].chunk->stride = mFrameSize;
	buf->datas[0].chunk->size = bytes_n;

	/* Queue the buffer for playback */
	pw_stream_queue_buffer(mStream, b);
}

void PipeWirePcm::streamCaptureProcess()
{
	void *p;
	struct pw_buffer *b;
	struct spa_buffer *buf;
	uint32_t index, offs, bytes_n;
	int32_t filled;

	if (!mStream) {
		throw Exception("Device not open", -EIO);
	}

	/* Obtain a buffer */
	if ((b = pw_stream_dequeue_buffer(mStream)) == nullptr) {
		return;
	}

	/* Write data into buffer */
	buf = b->buffer;
	p = buf->datas[0].data;
	if (p == nullptr) {
		return;
	}

	offs = SPA_MIN(buf->datas[0].chunk->offset, buf->datas[0].maxsize);
	bytes_n = SPA_MIN(buf->datas[0].chunk->size, buf->datas[0].maxsize - offs);

	filled = spa_ringbuffer_get_write_index(&mRing, &index);

	if (filled < 0)
    {
		LOG(mLog, DEBUG) << "Underrun write: " << index
                         << ", filled: " << filled;
	}
    else
    {
		if ((uint32_t)filled + bytes_n > RINGBUFFER_SIZE)
        {
			LOG(mLog, DEBUG) << "Overrun write: " << index
                             << ", filled: " << filled
                             << " + bytes_n: " << bytes_n
                             << " > max: " << RINGBUFFER_SIZE;
		}
	}

	spa_ringbuffer_write_data(&mRing, mBuffer, RINGBUFFER_SIZE,
								index & RINGBUFFER_MASK, SPA_PTROFF(p, offs, void), bytes_n);

	index += bytes_n;
	spa_ringbuffer_write_update(&mRing, index);

	pw_stream_queue_buffer(mStream, b);
}

void PipeWirePcm::waitStreamReady()
{
    const char *error = nullptr;

	for (;;)
	{
		auto state = pw_stream_get_state(mStream, &error);

		if (state == PW_STREAM_STATE_CONNECTING)
		{
			break;
		}

		pw_thread_loop_wait(mThreadLoop);
	}
}

void PipeWirePcm::flush()
{
    uint32_t index;

	if (mType == StreamType::PLAYBACK)
    {
        pw_stream_flush(mStream, true);

        /* writeindex and readindex must be kept in sync in any case before closing the pcm device */
        spa_ringbuffer_get_write_index(&mRing, &index);
        spa_ringbuffer_read_update(&mRing, index);
    }
    else
    {
        spa_ringbuffer_get_read_index(&mRing, &index);
        spa_ringbuffer_write_update(&mRing, index);
    }
}

int PipeWirePcm::getStatus()
{
    int err = -1;

    const char *error = nullptr;

    auto state = pw_stream_get_state(mStream, &error);

	switch (state)
    {
		case PW_STREAM_STATE_ERROR:
		case PW_STREAM_STATE_UNCONNECTED:
		case PW_STREAM_STATE_PAUSED:
			return err;
		default:
			return 0;
	}
}

void PipeWirePcm::checkStatus()
{
	int error = getStatus();

	if (error == -1)
	{
		throw Exception("Stream state error", error);
	}
}

void PipeWirePcm::getTimeStamp()
{
    const char *error = nullptr;

	if (!mStream) {
		throw Exception("Device not open", -EIO);
	}

    auto state = pw_stream_get_state(mStream, &error);

    if (state == PW_STREAM_STATE_STREAMING && mProgressCbk)
    {
        if (mType == StreamType::PLAYBACK)
        {
            mProgressCbk(mRing.readindex);
        }
        else
        {
            mProgressCbk(mRing.writeindex);
        }
    }
}

PipeWirePcm::PcmFormat PipeWirePcm::sPcmFormat[] =
{
	{XENSND_PCM_FORMAT_U8,                 SPA_AUDIO_FORMAT_U8 },
	{XENSND_PCM_FORMAT_S16_LE,             SPA_AUDIO_FORMAT_S16_LE },
	{XENSND_PCM_FORMAT_S16_BE,             SPA_AUDIO_FORMAT_S16_BE },
	{XENSND_PCM_FORMAT_S24_LE,             SPA_AUDIO_FORMAT_S24_LE },
	{XENSND_PCM_FORMAT_S24_BE,             SPA_AUDIO_FORMAT_S24_BE },
	{XENSND_PCM_FORMAT_S32_LE,             SPA_AUDIO_FORMAT_S32_LE },
	{XENSND_PCM_FORMAT_S32_BE,             SPA_AUDIO_FORMAT_S32_BE },
	{XENSND_PCM_FORMAT_A_LAW,              SPA_AUDIO_FORMAT_ALAW },
	{XENSND_PCM_FORMAT_MU_LAW,             SPA_AUDIO_FORMAT_ULAW },
	{XENSND_PCM_FORMAT_F32_LE,             SPA_AUDIO_FORMAT_F32_LE },
	{XENSND_PCM_FORMAT_F32_BE,             SPA_AUDIO_FORMAT_F32_BE },
};

spa_audio_format PipeWirePcm::convertPcmFormat(uint8_t format)
{
	for (auto value : sPcmFormat)
	{
		if (value.sndif == format)
		{
			return value.format;
		}
	}

	throw Exception("Can't convert format", SPA_AUDIO_FORMAT_UNKNOWN);
}

PipeWirePcm::StringFormat PipeWirePcm::sStringFormat[] =
{
	{ SPA_AUDIO_FORMAT_U8,                 "SPA_AUDIO_FORMAT_U8" 	},
	{ SPA_AUDIO_FORMAT_ALAW,               "SPA_AUDIO_FORMAT_ALAW" 	},
	{ SPA_AUDIO_FORMAT_ULAW,               "SPA_AUDIO_FORMAT_ULAW" 	},
	{ SPA_AUDIO_FORMAT_S16_LE,             "SPA_AUDIO_FORMAT_S16_LE" },
	{ SPA_AUDIO_FORMAT_S16_BE,             "SPA_AUDIO_FORMAT_S16_BE" },
	{ SPA_AUDIO_FORMAT_S24_LE,             "SPA_AUDIO_FORMAT_S24_LE" },
	{ SPA_AUDIO_FORMAT_S24_BE,             "SPA_AUDIO_FORMAT_S24_BE" },
	{ SPA_AUDIO_FORMAT_S32_LE,             "SPA_AUDIO_FORMAT_S32_LE" },
	{ SPA_AUDIO_FORMAT_S32_BE,             "SPA_AUDIO_FORMAT_S32_BE" },
	{ SPA_AUDIO_FORMAT_F32_LE,             "SPA_AUDIO_FORMAT_F32_LE" },
	{ SPA_AUDIO_FORMAT_F32_BE,             "SPA_AUDIO_FORMAT_F32_BE" },
};

uint32_t PipeWirePcm::convertSampleBytes(spa_audio_format format)
{
	uint32_t frameSize = 1;

	switch (format)	{
	case SPA_AUDIO_FORMAT_U8:
	case SPA_AUDIO_FORMAT_ALAW:
	case SPA_AUDIO_FORMAT_ULAW:
		frameSize = 1;
		return frameSize;
	case SPA_AUDIO_FORMAT_S16_LE:
	case SPA_AUDIO_FORMAT_S16_BE:
		frameSize = 2;
		return frameSize;
	case SPA_AUDIO_FORMAT_S24_LE:
	case SPA_AUDIO_FORMAT_S24_BE:
		frameSize = 3;
		return frameSize;
	case SPA_AUDIO_FORMAT_S32_LE:
	case SPA_AUDIO_FORMAT_S32_BE:
	case SPA_AUDIO_FORMAT_F32_LE:
	case SPA_AUDIO_FORMAT_F32_BE:
		frameSize = 4;
		return frameSize;
	default:
		break;
	}

	throw Exception("Can't convert format", SPA_AUDIO_FORMAT_UNKNOWN);
}

std::string PipeWirePcm::sample_format_to_string(spa_audio_format format)
{
	for (auto value : sStringFormat)
	{
		if (value.format == format)
		{
			return value.strFormat;
		}
	}

	throw Exception("Can't convert format", SPA_AUDIO_FORMAT_UNKNOWN);
}

void PipeWirePcm::createStream()
{
	if (mStream) {
		throw Exception("PCM device " + mName + " already opened", PW_STREAM_STATE_STREAMING);
	}

	int ret = 0;
    uint8_t buffer[1024];

    pw_properties *properties = pw_properties_new(nullptr, nullptr);

    if (!mPropValue.empty())
    {
        pw_properties_set(properties, PW_KEY_MEDIA_ROLE, mPropValue.c_str());
	}

	mStream = pw_stream_new(mCore, mName.c_str(), properties);
	if (!mStream)
	{
		throw Exception("Can't open PCM device " + mName, -1);
	}

	mInfo.format = convertPcmFormat(mParams.format);
	mFrameSize = convertSampleBytes(mInfo.format);
	mInfo.rate = mParams.rate;
	mInfo.channels = static_cast<uint32_t>(mParams.numChannels);
    mFrameSize *= mInfo.channels;

	switch (mInfo.channels) {
		case 8:
			mInfo.position[0] = SPA_AUDIO_CHANNEL_FL;
			mInfo.position[1] = SPA_AUDIO_CHANNEL_FR;
			mInfo.position[2] = SPA_AUDIO_CHANNEL_FC;
			mInfo.position[3] = SPA_AUDIO_CHANNEL_LFE;
			mInfo.position[4] = SPA_AUDIO_CHANNEL_RL;
			mInfo.position[5] = SPA_AUDIO_CHANNEL_RR;
			mInfo.position[6] = SPA_AUDIO_CHANNEL_SL;
			mInfo.position[7] = SPA_AUDIO_CHANNEL_SR;
			break;
		case 6:
			mInfo.position[0] = SPA_AUDIO_CHANNEL_FL;
			mInfo.position[1] = SPA_AUDIO_CHANNEL_FR;
			mInfo.position[2] = SPA_AUDIO_CHANNEL_FC;
			mInfo.position[3] = SPA_AUDIO_CHANNEL_LFE;
			mInfo.position[4] = SPA_AUDIO_CHANNEL_RL;
			mInfo.position[5] = SPA_AUDIO_CHANNEL_RR;
			break;
		case 5:
			mInfo.position[0] = SPA_AUDIO_CHANNEL_FL;
			mInfo.position[1] = SPA_AUDIO_CHANNEL_FR;
			mInfo.position[2] = SPA_AUDIO_CHANNEL_FC;
			mInfo.position[3] = SPA_AUDIO_CHANNEL_LFE;
			mInfo.position[4] = SPA_AUDIO_CHANNEL_RC;
			break;
		case 4:
			mInfo.position[0] = SPA_AUDIO_CHANNEL_FL;
			mInfo.position[1] = SPA_AUDIO_CHANNEL_FR;
			mInfo.position[2] = SPA_AUDIO_CHANNEL_FC;
			mInfo.position[3] = SPA_AUDIO_CHANNEL_RC;
			break;
		case 3:
			mInfo.position[0] = SPA_AUDIO_CHANNEL_FL;
			mInfo.position[1] = SPA_AUDIO_CHANNEL_FR;
			mInfo.position[2] = SPA_AUDIO_CHANNEL_LFE;
			break;
		case 2:
			mInfo.position[0] = SPA_AUDIO_CHANNEL_FL;
			mInfo.position[1] = SPA_AUDIO_CHANNEL_FR;
			break;
		case 1:
			mInfo.position[0] = SPA_AUDIO_CHANNEL_MONO;
			break;
		default:
			for (uint32_t i = 0; i < mInfo.channels; i++)
			{
				mInfo.position[i] = SPA_AUDIO_CHANNEL_UNKNOWN;
			}
			break;
	}

	if (mType == StreamType::PLAYBACK)
    {
		pw_stream_add_listener(mStream, &mStreamListener, &sPlaybackStreamEvents, this);
	}
    else
    {
		pw_stream_add_listener(mStream, &mStreamListener, &sCaptureStreamEvents, this);
	}

	mSpaPod_n = 0;
	spa_pod_builder_init(&mBuilder, buffer, sizeof(buffer));
	mSpaPod[mSpaPod_n++] = spa_format_audio_raw_build(&mBuilder, SPA_PARAM_EnumFormat, &mInfo);

	ret = pw_stream_connect(mStream, mType == StreamType::CAPTURE ? PW_DIRECTION_INPUT : PW_DIRECTION_OUTPUT, PW_ID_ANY,
							static_cast<pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_RT_PROCESS),
                            mSpaPod, mSpaPod_n);
	if (ret < 0)
	{
		pw_stream_destroy(mStream);
        return;
	}
}

void PipeWirePcm::queryHwRanges(SoundItf::PcmParamRanges& req, SoundItf::PcmParamRanges& resp)
{
	resp = req;
}

}
