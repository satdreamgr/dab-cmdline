#
/*
 *    Copyright (C) 2010, 2011, 2012, 2013
 *    Jan van Katwijk (J.vanKatwijk@gmail.com)
 *    Lazy Chair Programming
 *
 *    This file is part of the DAB library
 *    DAB library is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    DAB library is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with DAB library; if not, write to the Free Software
 *    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * 	This particular driver is a very simple wrapper around the
 * 	librtlsdr.  In order to keep things simple, we dynamically
 * 	load the dll (or .so). The librtlsdr is osmocom software and all rights
 * 	are greatly acknowledged
 */


#include	"rtl-sdr.h"
#include	"rtlsdr-handler.h"

#include <unistd.h>
#include <iostream>

#ifdef	__MINGW32__
#define	GETPROCADDRESS	GetProcAddress
#else
#define	GETPROCADDRESS	dlsym
#endif

#define	READLEN_DEFAULT	8192
//
//	For the callback, we do need some environment which
//	is passed through the ctx parameter
//
//	This is the user-side call back function
//	ctx is the calling task
static
void	RTLSDRCallBack (uint8_t *buf, uint32_t len, void *ctx) {
rtlsdrHandler	*theStick	= (rtlsdrHandler *)ctx;

	if ((theStick == NULL) || (len != READLEN_DEFAULT))
	   return;

	(void) theStick -> _I_Buffer -> putDataIntoBuffer (buf, len);

	// Check if device is overloaded
	uint8_t minValue = 255;
	uint8_t maxValue = 0;

	for (uint32_t i = 0; i < len; i ++) {
	   if (minValue > buf[i])
	      minValue = buf[i];
	   if(maxValue < buf[i])
	      maxValue = buf[i];
	}

	(void) theStick -> setMinMaxValue(minValue, maxValue);
}
//
//	for handling the events in libusb, we need a controlthread
//	whose sole purpose is to process the rtlsdr_read_async function
//	from the lib.
void	controlThread (rtlsdrHandler *theStick) {
	(theStick -> rtlsdr_read_async) (theStick -> device,
	                          (rtlsdr_read_async_cb_t)&RTLSDRCallBack,
	                          (void *)theStick,
	                          0,
	                          READLEN_DEFAULT);
}

void	agcThread(rtlsdrHandler *theStick) {
	while (theStick->isRunning()) {
	   usleep(1000 * 50); // wait 50 ms
	   if (!theStick->isRunning()) { // we might stopped running while sleeping
	       break;
	   }
	   theStick->checkAGC();
	}
}

//
//	Our wrapper is a simple classs
	rtlsdrHandler::rtlsdrHandler (int32_t	frequency,
	                              int16_t	ppmCorrection,
	                              int16_t	gain,
	                              bool	autogain,
	                              uint16_t	deviceIndex,
	                              const char *	deviceSerial,
	                              const char *	deviceOpts ) {
int16_t	deviceCount;
int32_t	r;
int16_t	i;

	fprintf (stderr, "going for rtlsdr %d %d\n", frequency, gain);
	this	-> frequency	= frequency;
	this	-> deviceOptions	= deviceOpts ? strdup(deviceOpts) : NULL;
	this	-> ppmCorrection	= ppmCorrection;
	this	-> theGain	= gain;
	this	-> autogain	= autogain;
	this	-> deviceIndex	= deviceIndex;

	inputRate		= 2048000;
	libraryLoaded		= false;
	open			= false;
	_I_Buffer		= NULL;
	this	-> sampleCounter= 0;
	gains			= NULL;
	running			= false;

	minValue		= 255;
	maxValue		= 0;
	currentGain		= 0;
	currentGainCount	= 0;
	isAGC			= false;
	isHwAGC			= false;

#ifdef	__MINGW32__
	const char *libraryString = "rtlsdr.dll";
	Handle		= LoadLibrary ((wchar_t *)L"rtlsdr.dll");
#else
	const char *libraryString = "librtlsdr.so";
	Handle		= dlopen ("librtlsdr.so", RTLD_NOW);
#endif

	if (Handle == NULL) {
	   fprintf (stderr, "failed to open %s, fatal\n", libraryString);
	   throw (41);
	}

	libraryLoaded	= true;
	if (!load_rtlFunctions ())
	   throw (42);
//
//	Ok, from here we have the library functions accessible
	deviceCount 		= this -> rtlsdr_get_device_count ();
	if (deviceCount == 0) {
	   fprintf (stderr, "No devices found, fatal\n");
#ifdef __MINGW32__
	   FreeLibrary (Handle);
#else
	   dlclose (Handle);
#endif
	   throw (43);
	}
//
//	OK, now open the hardware
	if ( deviceSerial ) {
		int		resultDevIdx = this -> rtlsdr_get_index_by_serial(deviceSerial);
		if ( resultDevIdx >= 0 ) {
			this	-> deviceIndex	= resultDevIdx;
			deviceIndex		= resultDevIdx;
		}
		else
			this	-> deviceIndex	= uint16_t(-1);
	}
	r			= this -> rtlsdr_open (&device, deviceIndex);
	if (r < 0) {
	   fprintf (stderr, "Opening rtlsdr failed, fatal\n");
#ifdef __MINGW32__
	   FreeLibrary (Handle);
#else
	   dlclose (Handle);
#endif
	   throw (44);
	}

	open			= true;
	r			= this -> rtlsdr_set_sample_rate (device,
	                                                          inputRate);
	if (r < 0) {
	   fprintf (stderr, "Setting samplerate failed, fatal\n");
	   this -> rtlsdr_close (device);
#ifdef __MINGW32__
	   FreeLibrary (Handle);
#else
	   dlclose (Handle);
#endif
	   throw (45);
	}

	r			= this -> rtlsdr_get_sample_rate (device);
	fprintf (stderr, "samplerate set to %d\n", r);
	//rtlsdr_set_tuner_gain_mode (device, 0);

	gainsCount	= rtlsdr_get_tuner_gains (device, NULL);
	fprintf (stderr, "Supported gain values (%d): ", gainsCount);
	gains		= new int [gainsCount];
	gainsCount	= rtlsdr_get_tuner_gains (device, gains);
	for (i = 0; i < gainsCount; i ++)
	   fprintf (stderr, "%d.%d ", gains [i] / 10, gains [i] % 10);
	fprintf (stderr, "\n");

	theGain = gain * gainsCount / 100;
	currentGainCount = theGain;

	if (ppmCorrection != 0)
	   rtlsdr_set_freq_correction (device, ppmCorrection);
	//if (autogain)
	//   rtlsdr_set_agc_mode (device, 1);
	(void)(this -> rtlsdr_set_center_freq (device, frequency));
	//fprintf (stderr, "effective gain: gain %d.%d\n",
	//                              gains [theGain * gainsCount / 100] / 10,
	//                              gains [theGain * gainsCount / 100] % 10);
	//rtlsdr_set_tuner_gain (device, gains [theGain * gainsCount / 100]);

	if ( this	-> deviceOptions && rtlsdr_set_opt_string )
		rtlsdr_set_opt_string(device, deviceOptions, 1);

	_I_Buffer		= new RingBuffer<uint8_t>(1024 * 1024);

	// Always use manual gain, the AGC is implemented in software
	rtlsdr_set_tuner_gain_mode(device, 1);

	setGain(currentGainCount);

	// Disable hardware AGC by default
	setHwAgc(autogain);

	// Enable AGC by default
	setAgc(true);
}

	rtlsdrHandler::~rtlsdrHandler	(void) {
	if (running) { // we are running
	   this -> rtlsdr_cancel_async (device);
	   workerHandle. join ();
	}

	running	= false;
	if (agcHandle.joinable())
		agcHandle.join();

	if (open)
	   this -> rtlsdr_close (device);
	if (this	-> deviceOptions)
		free( this	-> deviceOptions );
	if (Handle != NULL) 
#ifdef __MINGW32__
	   FreeLibrary (Handle);
#else
	   dlclose (Handle);
#endif
	if (_I_Buffer != NULL)
	   delete _I_Buffer;
	if (gains != NULL)
	   delete[] gains;
	open = false;
}

//
bool	rtlsdrHandler::restartReader	(int32_t frequency) {
int32_t	r;

	if (running)
	   return true;
	_I_Buffer	-> FlushRingBuffer ();
	r = this -> rtlsdr_reset_buffer (device);
        if (r < 0)
           return false;

	this	-> frequency	= frequency;
        (void)(this -> rtlsdr_set_center_freq (device, frequency));
	workerHandle = std::thread (controlThread, this);
	setGain(currentGainCount);
	setHwAgc(autogain);
	//rtlsdr_set_tuner_gain (device, gains [theGain * gainsCount / 100]);
	//if (autogain)
	//   rtlsdr_set_agc_mode (device, 1);
	if ( this	-> deviceOptions && rtlsdr_set_opt_string )
		rtlsdr_set_opt_string(device, deviceOptions, 1);
	running	= true;
	agcHandle = std::thread (agcThread, this);
	return true;
}

void	rtlsdrHandler::stopReader	(void) {
	if (!running)
	   return;

	this -> rtlsdr_cancel_async (device);
	workerHandle. join ();
	running	= false;
	if (agcHandle.joinable())
		agcHandle.join();
}
//
//	when selecting with an integer in the range 0 .. 100
//	first find the table value
void	rtlsdrHandler::setGain	(int32_t g) {
	if (g > gainsCount) {
	   std::cerr << "RTL_SDR:" << "Unknown gain count:" << g;
	   return;
	}

	currentGainCount = g;
	currentGain = gains[g];

	int ret = rtlsdr_set_tuner_gain (device, currentGain);
	if (ret != 0) {
	   std::cerr << "RTL_SDR:" << "Setting gain failed";
	}
}

bool	rtlsdrHandler::has_autogain	(void) {
	return true;
}

void	rtlsdrHandler::set_autogain	(bool b) {
	rtlsdr_set_tuner_gain_mode (device, b);
	rtlsdr_set_tuner_gain (device, theGain);
}

//
//	we only have 8 bits, so rather than doing a float division to get
//	the float value we want, we precompute the possibilities
static 
float convTable [] = {
 -128 / 128.0 , -127 / 128.0 , -126 / 128.0 , -125 / 128.0 , -124 / 128.0 , -123 / 128.0 , -122 / 128.0 , -121 / 128.0 , -120 / 128.0 , -119 / 128.0 , -118 / 128.0 , -117 / 128.0 , -116 / 128.0 , -115 / 128.0 , -114 / 128.0 , -113 / 128.0 
, -112 / 128.0 , -111 / 128.0 , -110 / 128.0 , -109 / 128.0 , -108 / 128.0 , -107 / 128.0 , -106 / 128.0 , -105 / 128.0 , -104 / 128.0 , -103 / 128.0 , -102 / 128.0 , -101 / 128.0 , -100 / 128.0 , -99 / 128.0 , -98 / 128.0 , -97 / 128.0 
, -96 / 128.0 , -95 / 128.0 , -94 / 128.0 , -93 / 128.0 , -92 / 128.0 , -91 / 128.0 , -90 / 128.0 , -89 / 128.0 , -88 / 128.0 , -87 / 128.0 , -86 / 128.0 , -85 / 128.0 , -84 / 128.0 , -83 / 128.0 , -82 / 128.0 , -81 / 128.0 
, -80 / 128.0 , -79 / 128.0 , -78 / 128.0 , -77 / 128.0 , -76 / 128.0 , -75 / 128.0 , -74 / 128.0 , -73 / 128.0 , -72 / 128.0 , -71 / 128.0 , -70 / 128.0 , -69 / 128.0 , -68 / 128.0 , -67 / 128.0 , -66 / 128.0 , -65 / 128.0 
, -64 / 128.0 , -63 / 128.0 , -62 / 128.0 , -61 / 128.0 , -60 / 128.0 , -59 / 128.0 , -58 / 128.0 , -57 / 128.0 , -56 / 128.0 , -55 / 128.0 , -54 / 128.0 , -53 / 128.0 , -52 / 128.0 , -51 / 128.0 , -50 / 128.0 , -49 / 128.0 
, -48 / 128.0 , -47 / 128.0 , -46 / 128.0 , -45 / 128.0 , -44 / 128.0 , -43 / 128.0 , -42 / 128.0 , -41 / 128.0 , -40 / 128.0 , -39 / 128.0 , -38 / 128.0 , -37 / 128.0 , -36 / 128.0 , -35 / 128.0 , -34 / 128.0 , -33 / 128.0 
, -32 / 128.0 , -31 / 128.0 , -30 / 128.0 , -29 / 128.0 , -28 / 128.0 , -27 / 128.0 , -26 / 128.0 , -25 / 128.0 , -24 / 128.0 , -23 / 128.0 , -22 / 128.0 , -21 / 128.0 , -20 / 128.0 , -19 / 128.0 , -18 / 128.0 , -17 / 128.0 
, -16 / 128.0 , -15 / 128.0 , -14 / 128.0 , -13 / 128.0 , -12 / 128.0 , -11 / 128.0 , -10 / 128.0 , -9 / 128.0 , -8 / 128.0 , -7 / 128.0 , -6 / 128.0 , -5 / 128.0 , -4 / 128.0 , -3 / 128.0 , -2 / 128.0 , -1 / 128.0 
, 0 / 128.0 , 1 / 128.0 , 2 / 128.0 , 3 / 128.0 , 4 / 128.0 , 5 / 128.0 , 6 / 128.0 , 7 / 128.0 , 8 / 128.0 , 9 / 128.0 , 10 / 128.0 , 11 / 128.0 , 12 / 128.0 , 13 / 128.0 , 14 / 128.0 , 15 / 128.0 
, 16 / 128.0 , 17 / 128.0 , 18 / 128.0 , 19 / 128.0 , 20 / 128.0 , 21 / 128.0 , 22 / 128.0 , 23 / 128.0 , 24 / 128.0 , 25 / 128.0 , 26 / 128.0 , 27 / 128.0 , 28 / 128.0 , 29 / 128.0 , 30 / 128.0 , 31 / 128.0 
, 32 / 128.0 , 33 / 128.0 , 34 / 128.0 , 35 / 128.0 , 36 / 128.0 , 37 / 128.0 , 38 / 128.0 , 39 / 128.0 , 40 / 128.0 , 41 / 128.0 , 42 / 128.0 , 43 / 128.0 , 44 / 128.0 , 45 / 128.0 , 46 / 128.0 , 47 / 128.0 
, 48 / 128.0 , 49 / 128.0 , 50 / 128.0 , 51 / 128.0 , 52 / 128.0 , 53 / 128.0 , 54 / 128.0 , 55 / 128.0 , 56 / 128.0 , 57 / 128.0 , 58 / 128.0 , 59 / 128.0 , 60 / 128.0 , 61 / 128.0 , 62 / 128.0 , 63 / 128.0 
, 64 / 128.0 , 65 / 128.0 , 66 / 128.0 , 67 / 128.0 , 68 / 128.0 , 69 / 128.0 , 70 / 128.0 , 71 / 128.0 , 72 / 128.0 , 73 / 128.0 , 74 / 128.0 , 75 / 128.0 , 76 / 128.0 , 77 / 128.0 , 78 / 128.0 , 79 / 128.0 
, 80 / 128.0 , 81 / 128.0 , 82 / 128.0 , 83 / 128.0 , 84 / 128.0 , 85 / 128.0 , 86 / 128.0 , 87 / 128.0 , 88 / 128.0 , 89 / 128.0 , 90 / 128.0 , 91 / 128.0 , 92 / 128.0 , 93 / 128.0 , 94 / 128.0 , 95 / 128.0 
, 96 / 128.0 , 97 / 128.0 , 98 / 128.0 , 99 / 128.0 , 100 / 128.0 , 101 / 128.0 , 102 / 128.0 , 103 / 128.0 , 104 / 128.0 , 105 / 128.0 , 106 / 128.0 , 107 / 128.0 , 108 / 128.0 , 109 / 128.0 , 110 / 128.0 , 111 / 128.0 
, 112 / 128.0 , 113 / 128.0 , 114 / 128.0 , 115 / 128.0 , 116 / 128.0 , 117 / 128.0 , 118 / 128.0 , 119 / 128.0 , 120 / 128.0 , 121 / 128.0 , 122 / 128.0 , 123 / 128.0 , 124 / 128.0 , 125 / 128.0 , 126 / 128.0 , 127 / 128.0 };

//
//	The brave old getSamples. For the dab stick, we get
//	size samples: still in I/Q pairs, but we have to convert the data from
//	uint8_t to DSPCOMPLEX *
int32_t	rtlsdrHandler::getSamples (std::complex<float> *V, int32_t size) { 
int32_t	amount, i;
uint8_t	*tempBuffer = (uint8_t *)alloca (2 * size * sizeof (uint8_t));
//
	amount = _I_Buffer	-> getDataFromBuffer (tempBuffer, 2 * size);
	for (i = 0; i < amount / 2; i ++)
	    V [i] = std::complex<float>
	                    (convTable [tempBuffer [2 * i]],
	                     convTable [tempBuffer [2 * i + 1]]);;
	return amount / 2;
}

int32_t	rtlsdrHandler::Samples	(void) {
	return _I_Buffer	-> GetRingBufferReadAvailable () / 2;
}
//
bool	rtlsdrHandler::load_rtlFunctions (void) {
//
//	link the required procedures
	rtlsdr_get_index_by_serial = (pfnrtlsdr_get_index_by_serial)
	                       GETPROCADDRESS (Handle, "rtlsdr_get_index_by_serial");
	if (rtlsdr_get_index_by_serial == NULL) {
	   fprintf (stderr, "Could not find rtlsdr_get_index_by_serial\n");
	   return false;
	}
	rtlsdr_open	= (pfnrtlsdr_open)
	                       GETPROCADDRESS (Handle, "rtlsdr_open");
	if (rtlsdr_open == NULL) {
	   fprintf (stderr, "Could not find rtlsdr_open\n");
	   return false;
	}
	rtlsdr_close	= (pfnrtlsdr_close)
	                     GETPROCADDRESS (Handle, "rtlsdr_close");
	if (rtlsdr_close == NULL) {
	   fprintf (stderr, "Could not find rtlsdr_close\n");
	   return false;
	}

	rtlsdr_set_sample_rate =
	    (pfnrtlsdr_set_sample_rate)GETPROCADDRESS (Handle, "rtlsdr_set_sample_rate");
	if (rtlsdr_set_sample_rate == NULL) {
	   fprintf (stderr, "Could not find rtlsdr_set_sample_rate\n");
	   return false;
	}

	rtlsdr_get_sample_rate	=
	    (pfnrtlsdr_get_sample_rate)GETPROCADDRESS (Handle, "rtlsdr_get_sample_rate");
	if (rtlsdr_get_sample_rate == NULL) {
	   fprintf (stderr, "Could not find rtlsdr_get_sample_rate\n");
	   return false;
	}

	rtlsdr_get_tuner_gains		= (pfnrtlsdr_get_tuner_gains)
	                     GETPROCADDRESS (Handle, "rtlsdr_get_tuner_gains");
	if (rtlsdr_get_tuner_gains == NULL) {
	   fprintf (stderr, "Could not find rtlsdr_get_tuner_gains\n");
	   return false;
	}


	rtlsdr_set_tuner_gain_mode	= (pfnrtlsdr_set_tuner_gain_mode)
	                     GETPROCADDRESS (Handle, "rtlsdr_set_tuner_gain_mode");
	if (rtlsdr_set_tuner_gain_mode == NULL) {
	   fprintf (stderr, "Could not find rtlsdr_set_tuner_gain_mode\n");
	   return false;
	}

	rtlsdr_set_agc_mode	= (pfnrtlsdr_set_agc_mode)
	                     GETPROCADDRESS (Handle, "rtlsdr_set_agc_mode");
	if (rtlsdr_set_agc_mode == NULL) {
	   fprintf (stderr, "Could not find rtlsdr_set_agc_mode\n");
	   return false;
	}

	rtlsdr_set_tuner_gain	= (pfnrtlsdr_set_tuner_gain)
	                     GETPROCADDRESS (Handle, "rtlsdr_set_tuner_gain");
	if (rtlsdr_set_tuner_gain == NULL) {
	   fprintf (stderr, "Cound not find rtlsdr_set_tuner_gain\n");
	   return false;
	}

	rtlsdr_get_tuner_gain	= (pfnrtlsdr_get_tuner_gain)
	                     GETPROCADDRESS (Handle, "rtlsdr_get_tuner_gain");
	if (rtlsdr_get_tuner_gain == NULL) {
	   fprintf (stderr, "Could not find rtlsdr_get_tuner_gain\n");
	   return false;
	}
	rtlsdr_set_center_freq	= (pfnrtlsdr_set_center_freq)
	                     GETPROCADDRESS (Handle, "rtlsdr_set_center_freq");
	if (rtlsdr_set_center_freq == NULL) {
	   fprintf (stderr, "Could not find rtlsdr_set_center_freq\n");
	   return false;
	}

	rtlsdr_get_center_freq	= (pfnrtlsdr_get_center_freq)
	                     GETPROCADDRESS (Handle, "rtlsdr_get_center_freq");
	if (rtlsdr_get_center_freq == NULL) {
	   fprintf (stderr, "Could not find rtlsdr_get_center_freq\n");
	   return false;
	}

	rtlsdr_reset_buffer	= (pfnrtlsdr_reset_buffer)
	                     GETPROCADDRESS (Handle, "rtlsdr_reset_buffer");
	if (rtlsdr_reset_buffer == NULL) {
	   fprintf (stderr, "Could not find rtlsdr_reset_buffer\n");
	   return false;
	}

	rtlsdr_read_async	= (pfnrtlsdr_read_async)
	                     GETPROCADDRESS (Handle, "rtlsdr_read_async");
	if (rtlsdr_read_async == NULL) {
	   fprintf (stderr, "Cound not find rtlsdr_read_async\n");
	   return false;
	}

	rtlsdr_get_device_count	= (pfnrtlsdr_get_device_count)
	                     GETPROCADDRESS (Handle, "rtlsdr_get_device_count");
	if (rtlsdr_get_device_count == NULL) {
	   fprintf (stderr, "Could not find rtlsdr_get_device_count\n");
	   return false;
	}

	rtlsdr_cancel_async	= (pfnrtlsdr_cancel_async)
	                     GETPROCADDRESS (Handle, "rtlsdr_cancel_async");
	if (rtlsdr_cancel_async == NULL) {
	   fprintf (stderr, "Could not find rtlsdr_cancel_async\n");
	   return false;
	}

	rtlsdr_set_direct_sampling = (pfnrtlsdr_set_direct_sampling)
	                  GETPROCADDRESS (Handle, "rtlsdr_set_direct_sampling");
	if (rtlsdr_set_direct_sampling == NULL) {
	   fprintf (stderr, "Could not find rtlsdr_set_direct_sampling\n");
	   return false;
	}

	rtlsdr_set_freq_correction = (pfnrtlsdr_set_freq_correction)
	                  GETPROCADDRESS (Handle, "rtlsdr_set_freq_correction");
	if (rtlsdr_set_freq_correction == NULL) {
	   fprintf (stderr, "Could not find rtlsdr_set_freq_correction\n");
	   return false;
	}
	
	rtlsdr_get_device_name = (pfnrtlsdr_get_device_name)
	                  GETPROCADDRESS (Handle, "rtlsdr_get_device_name");
	if (rtlsdr_get_device_name == NULL) {
	   fprintf (stderr, "Could not find rtlsdr_get_device_name\n");
	   return false;
	}

	rtlsdr_set_opt_string = (pfnrtlsdr_set_opt_string)
	                  GETPROCADDRESS (Handle, "rtlsdr_set_opt_string");
	if (rtlsdr_get_device_name == NULL) {
	   fprintf (stderr, "Could not find rtlsdr_set_opt_string\n");
	}

	fprintf (stderr, "OK, functions seem to be loaded\n");
	return true;
}

void	rtlsdrHandler::resetBuffer (void) {
	_I_Buffer -> FlushRingBuffer ();
}

int16_t	rtlsdrHandler::maxGain	(void) {
	return gainsCount;
}

int16_t	rtlsdrHandler::bitDepth	(void) {
	return 8;
}

void rtlsdrHandler::setMinMaxValue(uint8_t minValue, uint8_t maxValue) {
	this->minValue = minValue;
	this->maxValue = maxValue;
}

void rtlsdrHandler::checkAGC(void)
{
    if(isAGC)
    {
        // Check for overloading
        if(minValue == 0 || maxValue == 255)
        {
            // We have to decrease the gain
            if(currentGainCount > 0)
            {
                setGain(currentGainCount - 1);
                //std::cerr << "RTL_SDR:" << "Decreased gain to " << (float) currentGain / 10 << std::endl;
            }
        }
        else
        {
            if(currentGainCount < (gainsCount - 1))
            {
                // Calc if a gain increase overloads the device. Calc it from the gain values
                int newGain = gains[currentGainCount + 1];
                float deltaGain = ((float) newGain / 10) - ((float) currentGain / 10);
                float linGain = pow(10, deltaGain / 20);

                int newMaxValue = (float) maxValue * linGain;
                int newMinValue = (float) minValue / linGain;

                // We have to increase the gain
                if(newMinValue >=0 && newMaxValue <= 255)
                {
                    setGain(currentGainCount + 1);
                    //std::cerr << "RTL_SDR:" << "Increased gain to " << (float) currentGain / 10 << std::endl;
                }
            }
        }
    }
    else // AGC is off
    {
        if(minValue == 0 || maxValue == 255)
        {
            std::cerr << "RTL_SDR:" << "ADC overload. Maybe you are using a to high gain." << std::endl;
        }
    }
}

bool rtlsdrHandler::isRunning(void) {
	return running;
}

void rtlsdrHandler::setHwAgc(bool hwAGC) {
	isHwAGC = hwAGC;
	rtlsdr_set_agc_mode(device, hwAGC ? 1 : 0);
}

void rtlsdrHandler::setAgc(bool AGC) {
	if (AGC == true) {
	   isAGC = true;
	}
	else
	{
	   isAGC = false;
	   setGain(currentGainCount);
	}
}
