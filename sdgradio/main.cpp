#
/*
 *    Copyright (C) 2015, 2016, 2017
 *    Jan van Katwijk (J.vanKatwijk@gmail.com)
 *    Lazy Chair Computing
 *
 *    This file is part of the DAB-library
 *
 *    DAB-library is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    DAB-library is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with DAB-library; if not, write to the Free Software
 *    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *	E X A M P L E  P R O G R A M
 *	for the DAB-library
 */
#include "band-handler.h"
#include "dab-class.h"
#include <cstdio>
#include <getopt.h>
#include <iostream>
#include <signal.h>
#include <unistd.h>
#ifdef HAVE_SDRPLAY
#include "sdrplay-handler.h"
#elif HAVE_AIRSPY
#include "airspy-handler.h"
#elif HAVE_RTLSDR
#include "rtlsdr-handler.h"
#elif HAVE_WAVFILES
#include "wavfiles.h"
#elif HAVE_RAWFILES
#include "rawfiles.h"
#elif HAVE_RTL_TCP
#include "rtl_tcp-client.h"
#endif

#include <atomic>
#include <thread>

void printOptions(void); // forward declaration
void listener(void);
//	we deal with some callbacks, so we have some data that needs
//	to be accessed from global contexts
static std::atomic<bool> run;

static dabClass *theRadio = NULL;

static std::atomic<bool> timeSynced;

static std::atomic<bool> timesyncSet;

static std::atomic<bool> ensembleRecognized;

static std::atomic<int32_t> lastFreqOff;
static std::atomic<int16_t> lastSnr;
static std::atomic<int16_t> lastFicQuality;
static std::atomic<int16_t> lastMcsQuality;

std::string programName = "Classic FM";
int32_t serviceIdentifier = -1;

static const char *table12[] = {
    "None",
    "News",
    "Current affairs",
    "Information",
    "Sport",
    "Education",
    "Drama",
    "Arts",
    "Science",
    "Talk",
    "Pop music",
    "Rock music",
    "Easy listening",
    "Light classical",
    "Classical music",
    "Other music",
    "Wheather",
    "Finance",
    "Children\'s",
    "Factual",
    "Religion",
    "Phone in",
    "Travel",
    "Leisure",
    "Jazz and Blues",
    "Country music",
    "National music",
    "Oldies music",
    "Folk music",
    "entry 29 not used",
    "entry 30 not used",
    "entry 31 not used"};

/* European languages */
static const char *table9[] = {
    /* 00 */ "unknown language",
    /* 01 */ "Albanian",
    /* 02 */ "Breton",
    /* 03 */ "Catalan",
    /* 04 */ "Croatian",
    /* 05 */ "Welsh",
    /* 06 */ "Czech",
    /* 07 */ "Danish",
    /* 08 */ "German",
    /* 09 */ "English",
    /* 0A */ "Spanish",
    /* 0B */ "Esperanto",
    /* 0C */ "Estonian",
    /* 0D */ "Basque",
    /* 0E */ "Faroese",
    /* 0F */ "French",
    /* 10 */ "Frisian",
    /* 11 */ "Irish",
    /* 12 */ "Gaelic",
    /* 13 */ "Galician",
    /* 14 */ "Icelandic",
    /* 15 */ "Italian",
    /* 16 */ "Lappish",
    /* 17 */ "Latin",
    /* 18 */ "Latvian",
    /* 19 */ "Luxembourgian",
    /* 1A */ "Lithuanian",
    /* 1B */ "Hungarian",
    /* 1C */ "Maltese",
    /* 1D */ "Dutch",
    /* 1E */ "Norwegian",
    /* 1F */ "Occitan",
    /* 20 */ "Polish",
    /* 21 */ "Postuguese",
    /* 22 */ "Romanian",
    /* 23 */ "Romansh",
    /* 24 */ "Serbian",
    /* 25 */ "Slovak",
    /* 26 */ "Slovene",
    /* 27 */ "Finnish",
    /* 28 */ "Swedish",
    /* 29 */ "Tuskish",
    /* 2A */ "Flemish",
    /* 2B */ "Walloon",
    /* 2C */ "rfu",
    /* 2D */ "rfu",
    /* 2E */ "rfu",
    /* 2F */ "rfu",
    /* 30 */ "Reserved for national assignment",
    /* 31 */ "Reserved for national assignment",
    /* 32 */ "Reserved for national assignment",
    /* 33 */ "Reserved for national assignment",
    /* 34 */ "Reserved for national assignment",
    /* 35 */ "Reserved for national assignment",
    /* 36 */ "Reserved for national assignment",
    /* 37 */ "Reserved for national assignment",
    /* 38 */ "Reserved for national assignment",
    /* 39 */ "Reserved for national assignment",
    /* 3A */ "Reserved for national assignment",
    /* 3B */ "Reserved for national assignment",
    /* 3C */ "Reserved for national assignment",
    /* 3D */ "Reserved for national assignment",
    /* 3E */ "Reserved for national assignment",
    /* 3F */ "Reserved for national assignment"};

/* Other languages */
static const char *table10[] = {
    /* 40 */ "Background sound/clean feed",
    /* 41 */ "rfu",
    /* 42 */ "rfu",
    /* 43 */ "rfu",
    /* 44 */ "rfu",
    /* 45 */ "Zulu",
    /* 46 */ "Vietnamese",
    /* 47 */ "Uzbek",
    /* 48 */ "Urdu",
    /* 49 */ "Ukranian",
    /* 4A */ "Thai",
    /* 4B */ "Telugu",
    /* 4C */ "Tatar",
    /* 4D */ "Tamil",
    /* 4E */ "Tadzhik",
    /* 4F */ "Swahili",
    /* 50 */ "Sranan Tongo",
    /* 51 */ "Somali",
    /* 52 */ "Sinhalese",
    /* 53 */ "Shona",
    /* 54 */ "Serbo-Croat",
    /* 55 */ "Rusyn",
    /* 56 */ "Russian",
    /* 57 */ "Quechua",
    /* 58 */ "Pushtu",
    /* 59 */ "Punjabi",
    /* 5A */ "Persian",
    /* 5B */ "Papiamento",
    /* 5C */ "Oriya",
    /* 5D */ "Nepali",
    /* 5E */ "Ndebele",
    /* 5F */ "Marathi",
    /* 60 */ "Moldavian",
    /* 61 */ "Malaysian",
    /* 62 */ "Malagasay",
    /* 63 */ "Macedonian",
    /* 64 */ "Laotian",
    /* 65 */ "Korean",
    /* 66 */ "Khmer",
    /* 67 */ "Kazakh",
    /* 68 */ "Kannada",
    /* 69 */ "Japanese",
    /* 6A */ "Indonesian",
    /* 6B */ "Hindi",
    /* 6C */ "Hebrew",
    /* 6D */ "Hausa",
    /* 6E */ "Gurani",
    /* 6F */ "Gujurati",
    /* 70 */ "Greek",
    /* 71 */ "Georgian",
    /* 72 */ "Fulani",
    /* 73 */ "Dari",
    /* 74 */ "Chuvash",
    /* 75 */ "Chinese",
    /* 76 */ "Burmese",
    /* 77 */ "Bulgarian",
    /* 78 */ "Bengali",
    /* 79 */ "Belorussian",
    /* 7A */ "Bambora",
    /* 7B */ "Azerbaijani",
    /* 7C */ "Assamese",
    /* 7D */ "Armenian",
    /* 7E */ "Arabic",
    /* 7F */ "Amharic"};

static const char *get_programm_type_string(int16_t type)
{
    if (type > 0x40)
    {
        fprintf(stderr, "GUI: program type wrong (%d)\n", type);
        return table12[0];
    }
    if (type < 0)
        return " ";

    return table12[type];
}

static const char *get_programm_language_string(int16_t language)
{
    if (language < 0)
        return " ";
    else if (language < 0x40)
        return table9[language];
    else if (language < 0x7d)
        return table10[language-0x40];
    fprintf(stderr, "GUI: wrong language (%d)\n", language);
    return table9[0];
}

static void sighandler(int signum)
{
    fprintf(stderr, "Signal caught, terminating!\n");
    run.store(false);
}

static void syncsignalHandler(bool b, void *userData)
{
    timeSynced.store(b);
    timesyncSet.store(true);
    (void)userData;
}
//
//	This function is called whenever the dab engine has taken
//	some time to gather information from the FIC bloks
//	the Boolean b tells whether or not an ensemble has been
//	recognized, the names of the programs are in the
//	ensemble
static void ensemblenameHandler(std::string name, int Id, void *userData)
{
    fprintf(stderr, "ensemble %s is (%X) recognized\n", name.c_str(),
            (uint32_t)Id);
    ensembleRecognized.store(true);
}

std::vector<std::string> programNames;
std::vector<int> programSIds;

static void programnameHandler(std::string s, int SId, void *userdata)
{
    fprintf(stderr, "%s (%X) is part of the ensemble\n", s.c_str(), SId);
    if (serviceIdentifier == -1 && s.find("(data)") == -1)
    {
        serviceIdentifier = SId;
        fprintf(stderr, "{\"ps\":\"%s\"}\n", s.c_str());
        fprintf(stderr, "%s (%X) selected as default program\n", s.c_str(), SId);
    }

    for (std::vector<std::string>::iterator it = programNames.begin();
         it != programNames.end(); ++it)
        if (*it == s)
            return;
    programNames.push_back(s);
    programSIds.push_back(SId);
    fprintf(stderr, "{\"programName\":\"%s\",\"programId\":\"%X\"}\n", s.c_str(),
            SId);
}

static void programdataHandler(audiodata *d, void *ctx)
{
    (void)ctx;
    fprintf(stderr, "\tstartaddress\t= %d\n", d->startAddr);
    fprintf(stderr, "\tlength\t\t= %d\n", d->length);
    fprintf(stderr, "\tsubChId\t\t= %d\n", d->subchId);
    fprintf(stderr, "\tprotection\t= %d\n", d->protLevel);
    fprintf(stderr, "\tbitrate\t\t= %d\n", d->bitRate);

    uint16_t h = d->protLevel;
    std::string protL;
    if (!d->shortForm)
    {
        protL = "EEP ";
        if ((h & (1 << 2)) == 0)
            protL.append("A ");
        else
            protL.append("B ");
        h = (h & 03) + 1;
        protL.append(std::to_string(h));
    }
    else
    {
        h = h & 03;
        protL = "UEP ";
        protL.append(std::to_string(h));
    }

    fprintf(stderr, "{\"length\":\"%d\",\"bitrate\":\"%d\",\"protectionLevel\":\"%s\",\"dabType\":\"%s\",\"language\":\"%s\",\"programType\":\"%s\"}\n",
            d->length, d->bitRate, protL.c_str(), (d->ASCTy == 077 ? "DAB+" : "DAB"),
            get_programm_language_string(d->language), get_programm_type_string(d->programType));
}

//
//	The function is called from within the library with
//	a string, the so-called dynamic label
static void dataOut_Handler(std::string dynamicLabel, void *ctx)
{
    (void)ctx;
    fprintf(stderr, "{\"radiotext\":\"%s\"}\n", dynamicLabel.c_str());
}
//
//	Note: the function is called from the tdcHandler with a
//	frame, either frame 0 or frame 1.
//	The frames are packed bytes, here an additional header
//	is added, a header of 8 bytes:
//	the first 4 bytes for a pattern 0xFF 0x00 0xFF 0x00 0xFF
//	the length of the contents, i.e. framelength without header
//	is stored in bytes 5 (high byte) and byte 6.
//	byte 7 contains 0x00, byte 8 contains 0x00 for frametype 0
//	and 0xFF for frametype 1
//	Note that the callback function is executed in the thread
//	that executes the tdcHandler code.
static void bytesOut_Handler(uint8_t *data, int16_t amount, uint8_t type,
                             void *ctx)
{
    (void)data;
    (void)amount;
    (void)ctx;
}
//
//	This function is overloaded. In the normal form it
//	handles a buffer full of PCM samples. We pass them on to the
//	audiohandler, based on portaudio. Feel free to modify this
//	and send the samples elsewhere
//
static void pcmHandler(int16_t *buffer, int size, int rate, bool isStereo,
                       void *ctx)
{
#ifdef  AAC_OUT
//      Now we know that we have been cheating, the int16_t * buffer
//      is actually an uint8_t * buffer, however, the size
//      gives the correct amount of elements
    fwrite((void *)buffer, size, 1, stdout);
#else
    fwrite((void *)buffer, size, 2, stdout);
#endif
}

static void systemData(bool flag, int16_t snr, int32_t freqOff, void *ctx)
{
    (void)ctx;
    if (abs(lastFreqOff - freqOff) > 100 || abs(lastSnr - snr) > 1)
    {
        fprintf(stderr, "{\"snr\":\"%d\",\"synced\":\"%s\",\"offset\":\"%d\"}\n",
                snr, flag ? "on" : "off", freqOff);
        lastFreqOff = freqOff;
        lastSnr = snr;
    }
}

static void fibQuality(int16_t q, void *ctx)
{
    (void)ctx;
    if (abs(lastFicQuality - q) > 1)
    {
        fprintf(stderr, "{\"fic_quality\":\"%d\"}\n", q);
        lastFicQuality = q;
    }
}

static void mscQuality(int16_t fe, int16_t rsE, int16_t aacE, void *ctx)
{
    (void)ctx;
    if (abs(lastMcsQuality - fe) > 1)
    {
        fprintf(stderr, "{\"msc_quality\":\"%d %d %d\"}\n", fe, rsE, aacE);
        lastMcsQuality = fe;
    }
}

int main(int argc, char **argv)
{
    // Default values
    uint8_t theMode = 1;
    std::string theChannel = "11C";
    uint8_t theBand = BAND_III;
    int16_t ppmCorrection = 0;
    int32_t khzOffset = 0;
    int theGain = 35; // scale = 0 .. 100
    int16_t waitingTime = 10;
    bool autogain = false;
    int opt;
    struct sigaction sigact;
    bandHandler dabBand;
    deviceHandler *theDevice;
#ifdef HAVE_WAVFILES
    std::string fileName;
#elif HAVE_RAWFILES
    std::string fileName;
#elif HAVE_RTL_TCP
    std::string hostname = "127.0.0.1"; // default
    int32_t basePort = 1234;            // default
#endif
    bool err;

    fprintf(stderr, "dab-sdgradio by SatDreamGr based on dab-cmdline examples\n");
    timeSynced.store(false);
    timesyncSet.store(false);
    run.store(false);

    if (argc == 1)
    {
        printOptions();
        exit(1);
    }

//	For file input we do not need options like Q, G and C,
//	We do need an option to specify the filename
#if (!defined(HAVE_WAVFILES) && !defined(HAVE_RAWFILES))
    while ((opt = getopt(argc, argv, "W:M:B:C:P:G:S:Qp:k:")) != -1)
    {
#elif HAVE_RTL_TCP
    while ((opt = getopt(argc, argv, "W:M:B:C:P:G:S:H:I:Qp:k:")) != -1)
    {
#else
    while ((opt = getopt(argc, argv, "W:M:B:P:S:F:p:")) != -1)
    {
#endif
        fprintf(stderr, "opt = %c %s\n", opt, optarg);
        switch (opt)
        {

        case 'W':
            waitingTime = atoi(optarg);
            break;

        case 'M':
            theMode = atoi(optarg);
            if (!((theMode == 1) || (theMode == 2) || (theMode == 4)))
                theMode = 1;
            break;

        case 'B':
            theBand =
                std::string(optarg) == std::string("L_BAND") ? L_BAND : BAND_III;
            break;

        case 'P':
            programName = optarg;
            break;

        case 'p':
            ppmCorrection = atoi(optarg);
            break;

        case 'k':
            khzOffset = atoi(optarg);
            break;
#if defined(HAVE_WAVFILES) || defined(HAVE_RAWFILES)
        case 'F':
            fileName = std::string(optarg);
            break;
#else
        case 'C':
            theChannel = std::string(optarg);
            break;

        case 'G':
            theGain = atoi(optarg);
            break;

        case 'Q':
            autogain = true;
            break;

#ifdef HAVE_RTL_TCP
        case 'H':
            hostname = std::string(optarg);
            break;

        case 'I':
            basePort = atoi(optarg);
            break;
#endif
#endif

        case 'S':
        {
            std::stringstream ss;
            ss << std::hex << optarg;
            ss >> serviceIdentifier;
            break;
        }

        default:
            printOptions();
            exit(1);
        }
    }
    //
    sigact.sa_handler = sighandler;
    sigemptyset(&sigact.sa_mask);
    sigact.sa_flags = 0;

    int32_t frequency = dabBand.Frequency(theBand, theChannel);
    try
    {
#ifdef HAVE_SDRPLAY
        theDevice =
            new sdrplayHandler(frequency, ppmCorrection, theGain, autogain, 0, 0);
#elif HAVE_AIRSPY
        theDevice = new airspyHandler(frequency, ppmCorrection, theGain);
#elif HAVE_RTLSDR
        theDevice = new rtlsdrHandler(frequency, ppmCorrection, theGain, autogain);
#elif HAVE_WAVFILES
    theDevice = new wavFiles(fileName);
#elif HAVE_RAWFILES
    theDevice = new rawFiles(fileName);
#elif HAVE_RTL_TCP
    theDevice = new rtl_tcp_client(hostname, basePort, frequency, theGain,
                                   autogain, ppmCorrection);
#endif
    }
    catch (int e)
    {
        fprintf(stderr, "allocating device failed (%d), fatal\n", e);
        exit(32);
    }
    //
    //	and with a sound device we can create a "backend"
    theRadio =
        new dabClass(theDevice, theMode,
                     NULL, // no spectrum shown
                     NULL, // no constellations
                     syncsignalHandler, systemData, ensemblenameHandler,
                     programnameHandler, fibQuality, pcmHandler, dataOut_Handler,
                     bytesOut_Handler, programdataHandler, mscQuality, NULL);
    if (theRadio == NULL)
    {
        fprintf(stderr, "sorry, no radio available, fatal\n");
        exit(4);
    }

    //theDevice->setGain(theGain);
    //if (autogain)
    //    theDevice->set_autogain(autogain);
    if (khzOffset)
        theDevice->set_KhzOffset(khzOffset);
    theDevice->setVFOFrequency(frequency);
    theDevice->restartReader();
    //
    //	The device should be working right now

    timesyncSet.store(false);
    ensembleRecognized.store(false);
    theRadio->startProcessing();

    int timeOut = 0;
    while (!timesyncSet.load() && (++timeOut < waitingTime))
        sleep(1);

    if (!timeSynced.load())
    {
        fprintf(stderr, "There does not seem to be a DAB signal here\n");
        theDevice->stopReader();
        sleep(1);
        theRadio->stop();
        delete theRadio;
        delete theDevice;
        exit(22);
    }
    else
        fprintf(stderr, "There might be a DAB signal here\n");

    if (!ensembleRecognized.load())
        while (!ensembleRecognized.load() && (++timeOut < waitingTime))
        {
            fprintf(stderr, "%d seconds remaining...\n", waitingTime - timeOut);
            sleep(1);
        }

    if (!ensembleRecognized.load())
    {
        fprintf(stderr, "no ensemble data found, fatal\n");
        theDevice->stopReader();
        sleep(1);
        theRadio->reset();
        delete theRadio;
        delete theDevice;
        exit(22);
    }

    run.store(true);
    std::thread keyboard_listener = std::thread(&listener);
    if (serviceIdentifier != -1)
        programName = theRadio->dab_getserviceName(serviceIdentifier);
    fprintf(stderr, "going to start program %s\n", programName.c_str());
    if (theRadio->dab_service(programName) < 0)
    {
        fprintf(stderr, "sorry  we cannot handle service %s\n",
                programName.c_str());
        run.store(false);
    }

    while (run.load())
        sleep(1);
    theDevice->stopReader();
    theRadio->reset();
    delete theRadio;
    delete theDevice;
}

void printOptions(void)
{
    fprintf(stderr, "                          dab-cmdline options are\n\
                          -W number   amount of time to look for an ensemble\n\
                          -M Mode     Mode is 1, 2 or 4. Default is Mode 1\n\
                          -B Band     Band is either L_BAND or BAND_III (default)\n\
                          -P name     program to be selected in the ensemble\n\
                          -C channel  channel to be used\n\
                          -G Gain     gain for device (range 1 .. 100)\n\
                          -Q          if set, set autogain for device true\n\
	                  -F filename in case the input is from file\n\
                          -S hexnumber use hexnumber to identify program\n\
                          -p ppmoffset use ppmoffset to correct oscillator frequency\n\
                          -k khzOffset use khzOffset to correct initial tuning offset\n\n");
}

bool matches(std::string s1, std::string s2)
{
    const char *ss1 = s1.c_str();
    const char *ss2 = s2.c_str();

    while ((*ss1 != 0) && (*ss2 != 0))
    {
        if (*ss2 != *ss1)
            return false;
        ss1++;
        ss2++;
    }
    return *ss2 == 0;
}

void selectNext(void)
{
    int foundIndex = -1;

    for (size_t i = 0; i < programNames.size(); i++)
    {
        if (matches(programNames[i], programName))
        {
            if (i == programNames.size() - 1)
                foundIndex = 0;
            else
                foundIndex = i + 1;
            break;
        }
    }
    if (foundIndex == -1)
    {
        fprintf(stderr, "system error\n");
        sighandler(9);
    }
    //	skip the data services. Slightly dangerous here, may be
    //	add a guard for "only data services" ensembles
    while (!theRadio->is_audioService(programNames[foundIndex]))
        foundIndex = (foundIndex + 1) % programNames.size();

    programName = programNames[foundIndex];
    fprintf(stderr, "we now try to start program %s\n", programName.c_str());
    if (theRadio->dab_service(programName) < 0)
    {
        fprintf(stderr, "sorry  we cannot handle service %s\n",
                programName.c_str());
        sighandler(9);
    }
    fprintf(stderr, "{\"ps\":\"%s\"}\n", programName.c_str());
}

void listener(void)
{
    fprintf(stderr, "listener is running\n");
    while (run.load())
    {
        std::string line;
        std::getline(std::cin, line);

        if (line.empty())
        {
            selectNext();
        }
        else
        {
            std::stringstream ss;
            ss << std::hex << line;
            ss >> serviceIdentifier;
            programName = theRadio->dab_getserviceName(serviceIdentifier);
            fprintf(stderr, "going to start program %s\n", programName.c_str());
            if (theRadio->dab_service(programName) < 0)
            {
                fprintf(stderr, "sorry  we cannot handle service %s\n",
                        programName.c_str());
                sighandler(9);
            }
            fprintf(stderr, "{\"ps\":\"%s\"}\n", programName.c_str());
        }
    }
}
