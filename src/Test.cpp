/*
 * Test.cpp
 *
 *  Created on: Aug 10, 2017
 *      Author: al1
 */

#include <atomic>
#include <fstream>
#include <thread>

#include <xen/be/Log.hpp>

#include "AlsaPcm.hpp"
#include "PulsePcm.hpp"

#include <xen/io/sndif.h>

using std::exception;
using std::ifstream;
using std::ofstream;
using std::streamsize;

using XenBackend::Log;
using XenBackend::LogLevel;

using Alsa::AlsaPcm;
using Pulse::PulseMainloop;
using SoundItf::StreamType;

using std::thread;

std::atomic_bool mTerminate(false);

void playback(PulseMainloop& mainLoop, const char* fileName)
{
	try
	{
		auto playback = mainLoop.createStream(StreamType::PLAYBACK, "Playback");

		playback->open({48000, XENSND_PCM_FORMAT_S16_LE, 2, 32768, 8192});

		ifstream file(fileName, std::ifstream::in);

		if (!file.is_open())
		{
			throw XenBackend::Exception("Can't open input file", -1);
		}

		uint8_t buffer[10000];
		streamsize size;

		playback->start();

		while(file)
		{
			file.read(reinterpret_cast<char*>(buffer), 10000);
			size = file.gcount();

			if (size != 0)
			{
				playback->write(buffer, size);
			}
		}

		file.close();

		playback->close();

		delete playback;
	}
	catch(const std::exception& e)
	{
		LOG("Test", ERROR) << e.what();
	}
}

void capture(PulseMainloop& mainLoop)
{
	try
	{
		ofstream file("out.wav", std::ifstream::out);

		if (!file.is_open())
		{
			throw XenBackend::Exception("Can't open output file", -1);
		}

		auto capture = mainLoop.createStream(StreamType::CAPTURE, "Capture");

		capture->open({48000, XENSND_PCM_FORMAT_S16_LE, 2, 32768, 8192});

		capture->start();

		uint8_t buffer[10000];

		while(!mTerminate)
		{
			capture->read(buffer, 10000);
			file.write(reinterpret_cast<const char*>(buffer), 10000);
		}

		capture->stop();

		capture->close();

		delete capture;

		file.close();
	}
	catch(const std::exception& e)
	{
		LOG("Test", ERROR) << e.what();
	}
}

int main()
{
	XenBackend::Log::setLogLevel(LogLevel::logDEBUG);

	try
	{
//		AlsaPcm* stream = new AlsaPcm(StreamType::PLAYBACK);

		PulseMainloop mainLoop("Test");

		auto playbackThread = thread(playback, std::ref(mainLoop), "car_reverse.wav");
		auto captureThread = thread(capture, std::ref(mainLoop));

		playbackThread.join();

		mTerminate = true;

		captureThread.join();

		auto repeatThread = thread(playback, std::ref(mainLoop), "out.wav");

		repeatThread.join();
	}
	catch(const exception& e)
	{
		LOG("Test", ERROR) << e.what();

		return -1;
	}

	return 0;
}
