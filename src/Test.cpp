/*
 * Test.cpp
 *
 *  Created on: Aug 10, 2017
 *      Author: al1
 */

#include <fstream>

#include <xen/be/Log.hpp>

#include "AlsaPcm.hpp"
#include "PulsePcm.hpp"

#include <xen/io/sndif.h>

using std::exception;
using std::ifstream;
using std::streamsize;

using XenBackend::Log;
using XenBackend::LogLevel;

using Alsa::AlsaPcm;
using Pulse::PulseMainloop;
using SoundItf::StreamType;
using SoundItf::SoundException;

int main()
{
	XenBackend::Log::setLogLevel(LogLevel::logDEBUG);

	try
	{
		AlsaPcm* stream = new AlsaPcm(StreamType::PLAYBACK);

#if 0
		PulseMainloop mainLoop("Test");

		auto stream = mainLoop.createStream(StreamType::PLAYBACK, "Test");
#endif


		stream->open({44100, XENSND_PCM_FORMAT_S16_LE, 2});

		ifstream file("car_reverse.wav", std::ifstream::in);

		if (!file.is_open())
		{
			throw SoundException("Can't open input file", -1);
		}

		uint8_t buffer[1000];
		streamsize size;

		sleep(1);

		file.read(reinterpret_cast<char*>(buffer), 1000);
		size = file.gcount();

		stream->write(buffer, size);
		stream->start();

		while(file)
		{
			file.read(reinterpret_cast<char*>(buffer), 1000);
			size = file.gcount();

			stream->write(buffer, size);
			sleep(1);

		}


		sleep(1);

		file.close();

		stream->close();

		delete stream;
	}
	catch(const exception& e)
	{
		LOG("Test", ERROR) << e.what();

		return -1;
	}

	return 0;
}
