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

#ifndef SRC_PIPEWIREPCM_HPP_
#define SRC_PIPEWIREPCM_HPP_

#include <pipewire/thread-loop.h>
#include <pipewire/properties.h>
#include <pipewire/context.h>
#include <pipewire/stream.h>
#include <pipewire/pipewire.h>
#include <pipewire/core.h>

#include <xen/be/Exception.hpp>
#include <xen/be/Log.hpp>
#include <xen/be/Utils.hpp>

#include "SoundItf.hpp"
#include <spa/utils/result.h>
#include <spa/param/audio/format-utils.h>
#include <spa/utils/ringbuffer.h>
#include <spa/param/audio/raw.h>
#include <spa/pod/builder.h>
#include <assert.h>

#define RINGBUFFER_SIZE    (1u << 22)
#define RINGBUFFER_MASK    (RINGBUFFER_SIZE - 1)

namespace PipeWire {

class PipeWirePcm;

/***************************************************************************//**
 * @defgroup PipeWire
 * PipeWire related classes.
 ******************************************************************************/

/***************************************************************************//**
 * Exception generated by PipeWire.
 * @ingroup PipeWire
 ******************************************************************************/
class Exception : public XenBackend::Exception
{
public:
	using XenBackend::Exception::Exception;

private:
	std::string formatMessage(const std::string& msg, int errCode) const override
	{
		return msg + " (" + spa_strerror(errCode) + ")";
	}
};

/***************************************************************************//**
 * Wrapper for thread loop lock
 * @ingroup PipeWire
 ******************************************************************************/
class PipeWireMutex
{
public:

	PipeWireMutex(pw_thread_loop *threadloop) : mThreadloop(threadloop) {}
	void lock() { pw_thread_loop_lock(mThreadloop); }
	void unlock() { pw_thread_loop_unlock(mThreadloop); }

private:

	pw_thread_loop *mThreadloop;
};

/***************************************************************************//**
 * PipeWire main loop
 * @ingroup PipeWire
 ******************************************************************************/
class PipeWireMainloop
{
public:

	PipeWireMainloop(const std::string& name);
	~PipeWireMainloop();

	PipeWirePcm* createStream(SoundItf::StreamType type, const std::string& name,
						   const std::string& propName = "",
						   const std::string& propValue = "",
						   const std::string& deviceValue = "");

private:
	pw_thread_loop *mThreadLoop;
	pw_context *mContext;
	pw_core *mCore;
	PipeWireMutex mMutex;
	spa_hook mCoreListener;
    int mLastSeq;
    int mPendingSeq;
    int mError;

	XenBackend::Log mLog;

	static void sCoreDone(void *data, uint32_t id, int seq);
	static void sCoreError(void *data, uint32_t id, int seq, int res, const char *message);

	static const struct pw_core_events sCoreStateChanged;

    int waitResync();
    void coreDone(uint32_t id, int seq);
	void coreError(uint32_t id, int seq, int res, const char *message);
	void init(const std::string& name);
	void release();
};

/***************************************************************************//**
 * Provides PipeWire pcm functionality.
 * @ingroup PipeWire
 ******************************************************************************/
class PipeWirePcm : public SoundItf::PcmDevice
{
public:
	/**
	 * @param type stream type
	 * @param name pcm device name
	 */

	PipeWirePcm(pw_thread_loop* thread_loop, pw_context* context, pw_core *mCore,
			 SoundItf::StreamType type,
			 const std::string& name,
			 const std::string& propName,
			 const std::string& propValue,
			 const std::string& deviceName = "");

	~PipeWirePcm();

	/**
	 * Queries the device for HW intervals and masks.
	 * @req HW parameters that the frontend wants to set
	 * @resp refined HW parameters that backend can support
	 */
	void queryHwRanges(SoundItf::PcmParamRanges& req, SoundItf::PcmParamRanges& resp) override;

	/**
	 * Opens the pcm device.
	 * @param params pcm parameters
	 */
	void open(const SoundItf::PcmParams& params) override;

	/**
	 * Closes the pcm device.
	 */
	void close() override;

	/**
	 * Reads data from the pcm device.
	 * @param buffer buffer where to put data
	 * @param size   number of bytes to read
	 */
	void read(uint8_t* buffer, size_t size) override;

	/**
	 * Writes data to the pcm device.
	 * @param buffer buffer with data
	 * @param size   number of bytes to write
	 */
	void write(uint8_t* buffer, size_t size) override;

	/**
	 * Starts the pcm device.
	 */
	void start() override;

	/**
	 * Stops the pcm device.
	 */
	void stop() override;

	/**
	 * Pauses the pcm device.
	 */
	void pause() override;

	/**
	 * Resumes the pcm device.
	 */
	void resume() override;

	/**
	 * Sets progress callback.
	 * @param cbk callback
	 */
	void setProgressCbk(SoundItf::ProgressCbk cbk) override
	{
		mProgressCbk = cbk;
	}

private:

	struct PcmFormat
	{
		uint8_t sndif;
		spa_audio_format format;
	};

	static PcmFormat sPcmFormat[];

	struct StringFormat
	{
		spa_audio_format format;
		std::string strFormat;
	};

	static StringFormat sStringFormat[];

	struct pw_thread_loop *mThreadLoop;
	struct pw_context *mContext;
	struct pw_core *mCore;
	struct spa_hook mCoreListener;
	int mSeq;

	struct pw_stream* mStream;
	struct spa_hook mStreamListener;
	struct spa_audio_info_raw mInfo;
	uint32_t mFrameSize;
	struct spa_ringbuffer mRing;
	uint8_t mBuffer[RINGBUFFER_SIZE];

	uint32_t mSpaPod_n;
	const struct spa_pod *mSpaPod[5];
	struct spa_pod_builder mBuilder;

	PipeWireMutex mMutex;
	SoundItf::StreamType mType;
	std::string mName;
	std::string mPropName;
	std::string mPropValue;
	std::string mDeviceName;
	SoundItf::PcmParams mParams;

    XenBackend::Timer mTimer;
    std::chrono::milliseconds mTimerPeriodMs;
	XenBackend::Log mLog;

	SoundItf::ProgressCbk mProgressCbk;

	static void sStreamDestroy(void *data);
	static void sStreamStateChanged(void *data, enum pw_stream_state old, enum pw_stream_state state, const char *error);
	static void sStreamPlaybackProcess(void *data);
	static void sStreamCaptureProcess(void *data);

	static const struct pw_stream_events sPlaybackStreamEvents;
	static const struct pw_stream_events sCaptureStreamEvents;

	void streamDestroy();
	void streamStateChanged(enum pw_stream_state old, enum pw_stream_state state, const char *error);
	void streamPlaybackProcess();
	void streamCaptureProcess();

	void waitStreamReady();
	void flush();
	int getStatus();
	void checkStatus();

	void createStream();

    void getTimeStamp();
	spa_audio_format convertPcmFormat(uint8_t format);
	uint32_t convertSampleBytes(spa_audio_format format);
	std::string sample_format_to_string(spa_audio_format format);
};

}

#endif /* SRC_PIPEWIREPCM_HPP_ */