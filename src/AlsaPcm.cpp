/*
 *  Alsa pcm device wrapper
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
 * Copyright (C) 2016 EPAM Systems Inc.
 */

#include "AlsaPcm.hpp"

#include <xen/io/sndif.h>

using std::bind;
using std::chrono::milliseconds;
using std::string;
using std::to_string;

using SoundItf::PcmParams;
using SoundItf::SoundException;
using SoundItf::StreamType;

namespace Alsa {

/*******************************************************************************
 * AlsaPcm
 ******************************************************************************/

AlsaPcm::AlsaPcm(StreamType type, const std::string& deviceName) :
	mHandle(nullptr),
	mDeviceName(deviceName),
	mType(type),
	mTimer(bind(&AlsaPcm::getTimeStamp, this), milliseconds(10), true),
	mLog("AlsaPcm")
{
	if (mDeviceName.empty())
	{
		mDeviceName = "default";
	}

	LOG(mLog, DEBUG) << "Create pcm device: " << mDeviceName;
}

AlsaPcm::~AlsaPcm()
{
	LOG(mLog, DEBUG) << "Delete pcm device: " << mDeviceName;

	close();
}

/*******************************************************************************
 * Public
 ******************************************************************************/

void AlsaPcm::open(const PcmParams& params)
{
	try
	{
		DLOG(mLog, DEBUG) << "Open pcm device: " << mDeviceName
						  << ", format: " << static_cast<int>(params.format)
						  << ", rate: " << params.rate
						  << ", channels: " << static_cast<int>(params.numChannels);

		snd_pcm_stream_t streamType = mType == StreamType::PLAYBACK ?
				SND_PCM_STREAM_PLAYBACK : SND_PCM_STREAM_CAPTURE;

		int ret = 0;

		if ((ret = snd_pcm_open(&mHandle, mDeviceName.c_str(),
							    streamType, 0)) < 0)
		{
			throw SoundException("Can't open audio device " + mDeviceName,
								 ret);
		}

		setHwParams(params);
		setSwParams();

		if ((ret = snd_pcm_prepare(mHandle)) < 0)
		{
			throw SoundException(
					"Can't prepare audio interface for use", ret);
		}

		mFrameWritten = 0;
		mFrameUnderrun = 0;
	}
	catch(const SoundException& e)
	{
		close();

		throw;
	}
}

void AlsaPcm::close()
{
	if (mHandle)
	{
		DLOG(mLog, DEBUG) << "Close pcm device: " << mDeviceName;

		snd_pcm_drain(mHandle);

		mTimer.stop();

		snd_pcm_close(mHandle);
	}

	mHandle = nullptr;
}

void AlsaPcm::read(uint8_t* buffer, size_t size)
{
	DLOG(mLog, DEBUG) << "Read from pcm device: " << mDeviceName
					  << ", size: " << size;

	if (!mHandle)
	{
		throw SoundException("Alsa device is not opened: " +
							 mDeviceName + ". Error: " +
							 snd_strerror(-EFAULT), -EFAULT);
	}

	auto numFrames = snd_pcm_bytes_to_frames(mHandle, size);

	while(numFrames > 0)
	{
		if (auto status = snd_pcm_readi(mHandle, buffer, numFrames))
		{
			if (status == -EPIPE)
			{
				LOG(mLog, WARNING) << "Device: " << mDeviceName
								   << ", message: " << snd_strerror(status);

				snd_pcm_prepare(mHandle);
			}
			else if (status < 0)
			{
				throw SoundException("Read from audio interface failed: " +
									 mDeviceName + ". Error: " +
									 snd_strerror(status), status);
			}
			else
			{
				numFrames -= status;
				buffer = &buffer[snd_pcm_frames_to_bytes(mHandle, status)];
			}
		}
	}
}

void AlsaPcm::write(uint8_t* buffer, size_t size)
{
	if (!mHandle)
	{
		throw SoundException("Alsa device is not opened: " +
							 mDeviceName + ". Error: " +
							 snd_strerror(-EFAULT), -EFAULT);
	}

	auto numFrames = snd_pcm_bytes_to_frames(mHandle, size);

	bool restartAfterError = false;

	while(numFrames > 0)
	{
		if (auto status = snd_pcm_writei(mHandle, buffer, numFrames))
		{
			DLOG(mLog, DEBUG) << "Write to pcm device: " << mDeviceName
							  << ", size: " << status;


			if (status == -EPIPE)
			{
				LOG(mLog, WARNING) << "Device: " << mDeviceName
								   << ", message: " << snd_strerror(status);

				if ((status = snd_pcm_recover(mHandle, status, 0)) < 0)
				{
					throw SoundException("Can't recover underrun: " +
										 mDeviceName + ". Error: " +
										 snd_strerror(status), status);
				}

				mFrameUnderrun = mFrameWritten;

				restartAfterError = true;
			}
			else if (status < 0)
			{
				throw SoundException("Write to audio interface failed: " +
									 mDeviceName + ". Error: " +
									 snd_strerror(status), status);
			}
			else
			{
				numFrames -= status;
				buffer = &buffer[snd_pcm_frames_to_bytes(mHandle, status)];
				mFrameWritten += status;

				if (snd_pcm_state(mHandle) != SND_PCM_STATE_RUNNING && restartAfterError)
				{
					restartAfterError = false;

					if ((status = snd_pcm_start(mHandle)) < 0)
					{
						throw SoundException("Can't recover underrun: " +
											 mDeviceName + ". Error: " +
											 snd_strerror(status), status);
					}
				}
			}
		}
	}
}


void AlsaPcm::start()
{
	LOG(mLog, DEBUG) << "Start";

	if (!mHandle)
	{
		throw SoundException("Alsa device is not opened: " +
							 mDeviceName + ". Error: " +
							 snd_strerror(-EFAULT), -EFAULT);
	}

	int ret = 0;

	if ((ret = snd_pcm_start(mHandle)) < 0)
	{
		throw SoundException("Can't start device " + mDeviceName, ret);
	}

	mTimer.start();
}

void AlsaPcm::stop()
{
	LOG(mLog, DEBUG) << "Stop";

	if (!mHandle)
	{
		throw SoundException("Alsa device is not opened: " +
							 mDeviceName + ". Error: " +
							 snd_strerror(-EFAULT), -EFAULT);
	}

	int ret = 0;

	if ((ret = snd_pcm_drop(mHandle)) < 0)
	{
		throw SoundException("Can't stop device " + mDeviceName, ret);
	}

	mTimer.stop();
}

void AlsaPcm::pause()
{
	LOG(mLog, DEBUG) << "Pause";

	if (!mHandle)
	{
		throw SoundException("Alsa device is not opened: " +
							 mDeviceName + ". Error: " +
							 snd_strerror(-EFAULT), -EFAULT);
	}

	int ret = 0;

	if ((ret = snd_pcm_pause(mHandle, 1)) < 0)
	{
		throw SoundException("Can't pause device " + mDeviceName, ret);
	}
}

void AlsaPcm::resume()
{
	LOG(mLog, DEBUG) << "Resume";

	if (!mHandle)
	{
		throw SoundException("Alsa device is not opened: " +
							 mDeviceName + ". Error: " +
							 snd_strerror(-EFAULT), -EFAULT);
	}

	int ret = 0;

	if ((ret = snd_pcm_pause(mHandle, 0)) < 0)
	{
		throw SoundException("Can't resume device " + mDeviceName, ret);
	}
}

/*******************************************************************************
 * Private
 ******************************************************************************/

void AlsaPcm::setHwParams(const PcmParams& params)
{
	snd_pcm_hw_params_t *hwParams = nullptr;

	int ret = 0;

	if ((ret = snd_pcm_set_params(mHandle, convertPcmFormat(params.format),
								  SND_PCM_ACCESS_RW_INTERLEAVED,
								  params.numChannels, params.rate, 0,
								  500000)) < 0)
	{
		throw SoundException("Can't set hw params " + mDeviceName, ret);
	}

	snd_pcm_hw_params_alloca(&hwParams);

	if ((ret = snd_pcm_hw_params_current(mHandle, hwParams)) < 0)
	{
		throw SoundException("Can't get current hw params " + mDeviceName, ret);
	}

	if ((ret = snd_pcm_hw_params_get_rate(hwParams, &mRate, 0)) < 0)
	{
		throw SoundException("Can't get rate " + mDeviceName, ret);
	}

	if ((ret = snd_pcm_hw_params_get_buffer_size(hwParams, &mBufferSize)) < 0)
	{
		throw SoundException("Can't get buffer size " + mDeviceName, ret);
	}

	LOG(mLog, DEBUG) << "Rate: " << mRate << ", buffer size: " << mBufferSize;

	if (snd_pcm_hw_params_supports_audio_ts_type(hwParams, 0))
		LOG(mLog, DEBUG) << "Playback supports audio compat timestamps";
	if (snd_pcm_hw_params_supports_audio_ts_type(hwParams, 1))
		LOG(mLog, DEBUG) << "Playback supports audio default timestamps";
	if (snd_pcm_hw_params_supports_audio_ts_type(hwParams, 2))
		LOG(mLog, DEBUG) << "Playback supports audio link timestamps";
	if (snd_pcm_hw_params_supports_audio_ts_type(hwParams, 3))
		LOG(mLog, DEBUG) << "Playback supports audio link absolute timestamps";
	if (snd_pcm_hw_params_supports_audio_ts_type(hwParams, 4))
		LOG(mLog, DEBUG) << "Playback supports audio link estimated timestamps";
	if (snd_pcm_hw_params_supports_audio_ts_type(hwParams, 5))
		LOG(mLog, DEBUG) << "Playback supports audio link synchronized timestamps";
}

void AlsaPcm::setSwParams()
{
	snd_pcm_sw_params_t *swParams = nullptr;

	int ret = 0;

	snd_pcm_sw_params_alloca(&swParams);

	if ((ret = snd_pcm_sw_params_current(mHandle, swParams)) < 0)
	{
		throw SoundException("Can't get swParams " + mDeviceName, ret);
	}

	if ((ret = snd_pcm_sw_params_set_tstamp_mode(
			mHandle, swParams, SND_PCM_TSTAMP_ENABLE)) < 0)
	{
		throw SoundException("Can't set ts mode " + mDeviceName, ret);
	}

	if ((ret = snd_pcm_sw_params_set_tstamp_type(
			mHandle, swParams, SND_PCM_TSTAMP_TYPE_MONOTONIC_RAW)) < 0)
	{
		throw SoundException("Can't set ts type " + mDeviceName, ret);
	}

	if ((ret = snd_pcm_sw_params_set_start_threshold(mHandle, swParams,
													 mBufferSize * 2)) < 0)
	{
		throw SoundException("Can't set start threshold " + mDeviceName, ret);
	}

	if ((ret = snd_pcm_sw_params(mHandle, swParams)) < 0)
	{
		throw SoundException("Can't set swParams " + mDeviceName, ret);
	}
}

void AlsaPcm::getTimeStamp()
{
	snd_pcm_status_t *status;

	int ret = 0;

	snd_pcm_status_alloca(&status);

	if ((ret = snd_pcm_status(mHandle, status)) < 0)
	{
		LOG(mLog, ERROR) << "Can't get status. Err: " << ret;
	}

	auto state = snd_pcm_status_get_state(status);

	snd_htimestamp_t audioTimeStamp;

	snd_pcm_status_get_audio_htstamp(status, &audioTimeStamp);

	uint64_t frame = ((audioTimeStamp.tv_sec * 1000000000 +
					 audioTimeStamp.tv_nsec) * mRate) / 1000000000;

	frame += mFrameUnderrun;

	uint64_t bytes;

	if (state == SND_PCM_STATE_XRUN)
	{
		bytes = snd_pcm_frames_to_bytes(mHandle, mFrameWritten);
	}
	else
	{
		bytes = snd_pcm_frames_to_bytes(mHandle, frame);
	}

	LOG(mLog, DEBUG) << "Frame: " << frame << ", bytes: " << bytes << ", state: " << state;

	if (mProgressCbk)
	{
		mProgressCbk(bytes);
	}
}

AlsaPcm::PcmFormat AlsaPcm::sPcmFormat[] =
{
	{XENSND_PCM_FORMAT_U8,                 SND_PCM_FORMAT_U8 },
	{XENSND_PCM_FORMAT_S8,                 SND_PCM_FORMAT_S8 },
	{XENSND_PCM_FORMAT_U16_LE,             SND_PCM_FORMAT_U16_LE },
	{XENSND_PCM_FORMAT_U16_BE,             SND_PCM_FORMAT_U16_BE },
	{XENSND_PCM_FORMAT_S16_LE,             SND_PCM_FORMAT_S16_LE },
	{XENSND_PCM_FORMAT_S16_BE,             SND_PCM_FORMAT_S16_BE },
	{XENSND_PCM_FORMAT_U24_LE,             SND_PCM_FORMAT_U24_LE },
	{XENSND_PCM_FORMAT_U24_BE,             SND_PCM_FORMAT_U24_BE },
	{XENSND_PCM_FORMAT_S24_LE,             SND_PCM_FORMAT_S24_LE },
	{XENSND_PCM_FORMAT_S24_BE,             SND_PCM_FORMAT_S24_BE },
	{XENSND_PCM_FORMAT_U32_LE,             SND_PCM_FORMAT_U32_LE },
	{XENSND_PCM_FORMAT_U32_BE,             SND_PCM_FORMAT_U32_BE },
	{XENSND_PCM_FORMAT_S32_LE,             SND_PCM_FORMAT_S32_LE },
	{XENSND_PCM_FORMAT_S32_BE,             SND_PCM_FORMAT_S32_BE },
	{XENSND_PCM_FORMAT_A_LAW,              SND_PCM_FORMAT_A_LAW },
	{XENSND_PCM_FORMAT_MU_LAW,             SND_PCM_FORMAT_MU_LAW },
	{XENSND_PCM_FORMAT_F32_LE,             SND_PCM_FORMAT_FLOAT_LE },
	{XENSND_PCM_FORMAT_F32_BE,             SND_PCM_FORMAT_FLOAT_BE },
	{XENSND_PCM_FORMAT_F64_LE,             SND_PCM_FORMAT_FLOAT64_LE },
	{XENSND_PCM_FORMAT_F64_BE,             SND_PCM_FORMAT_FLOAT64_BE },
	{XENSND_PCM_FORMAT_IEC958_SUBFRAME_LE, SND_PCM_FORMAT_IEC958_SUBFRAME_LE },
	{XENSND_PCM_FORMAT_IEC958_SUBFRAME_BE, SND_PCM_FORMAT_IEC958_SUBFRAME_BE },
	{XENSND_PCM_FORMAT_IMA_ADPCM,          SND_PCM_FORMAT_IMA_ADPCM },
	{XENSND_PCM_FORMAT_MPEG,               SND_PCM_FORMAT_MPEG },
	{XENSND_PCM_FORMAT_GSM,                SND_PCM_FORMAT_GSM },
};

snd_pcm_format_t AlsaPcm::convertPcmFormat(uint8_t format)
{
	for (auto value : sPcmFormat)
	{
		if (value.sndif == format)
		{
			return value.alsa;
		}
	}

	throw SoundException("Can't convert format", -EINVAL);
}

}
