/*
 *  Command handler
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
 * Copyright (C) 2017 EPAM Systems Inc.
 */

#include <xen/io/sndif.h>

#include <atomic>
#include <fstream>
#include <thread>
#include <xen/be/Log.hpp>

#include "AlsaPcm.hpp"
#include "PulsePcm.hpp"

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
    try {
        auto playback = mainLoop.createStream(StreamType::PLAYBACK, "Playback");

        playback->open({48000, XENSND_PCM_FORMAT_S16_LE, 2, 32768, 8192});

        ifstream file(fileName, std::ifstream::in);

        if (!file.is_open()) {
            throw XenBackend::Exception("Can't open input file", -1);
        }

        uint8_t buffer[10000];
        streamsize size;

        playback->start();

        while (file) {
            file.read(reinterpret_cast<char*>(buffer), 10000);
            size = file.gcount();

            if (size != 0) {
                playback->write(buffer, size);
            }
        }

        file.close();

        playback->close();

        delete playback;
    }
    catch (const std::exception& e) {
        LOG("Test", ERROR) << e.what();
    }
}

void capture(PulseMainloop& mainLoop)
{
    try {
        ofstream file("out.wav", std::ifstream::out);

        if (!file.is_open()) {
            throw XenBackend::Exception("Can't open output file", -1);
        }

        auto capture = mainLoop.createStream(StreamType::CAPTURE, "Capture");

        capture->open({48000, XENSND_PCM_FORMAT_S16_LE, 2, 32768, 8192});

        capture->start();

        uint8_t buffer[10000];

        while (!mTerminate) {
            capture->read(buffer, 10000);
            file.write(reinterpret_cast<const char*>(buffer), 10000);
        }

        capture->stop();

        capture->close();

        delete capture;

        file.close();
    }
    catch (const std::exception& e) {
        LOG("Test", ERROR) << e.what();
    }
}

int main()
{
    XenBackend::Log::setLogLevel(LogLevel::logDEBUG);

    try {
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
    catch (const exception& e) {
        LOG("Test", ERROR) << e.what();

        return -1;
    }

    return 0;
}
