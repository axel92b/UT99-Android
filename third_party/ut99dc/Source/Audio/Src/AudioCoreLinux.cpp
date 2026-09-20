/*=============================================================================
	AudioCoreLinux.h: Core audio implementation for Linux / Android SDL.
	Copyright 1999 Epic Games, Inc. All Rights Reserved.

Revision history:
	* Created by Brandon Reinhart.
	* UT99 Android milestone: use SDL queued audio instead of /dev/dsp when
	  building for Android.  The old OSS path is kept unchanged for Linux.
=============================================================================*/

/*------------------------------------------------------------------------------------
	Audio includes.
------------------------------------------------------------------------------------*/

#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#if defined(__ANDROID__)
#include <sys/select.h>
#include "SDL.h"
#else
#include <sys/ioctl.h>
#include <linux/soundcard.h>
#endif
#include "AudioPrivate.h"

/*------------------------------------------------------------------------------------
	AudioCore Globals.
------------------------------------------------------------------------------------*/

// Audio Device
INT AudioDevice = -1;

#define FRAGMENT_SIZE 0x0002000C

/*------------------------------------------------------------------------------------
	AudioCore Implementation.
------------------------------------------------------------------------------------*/

// Audio state control.
void* GetAudioBuffer()
{
	return AudioBuffer;
}

#if defined(__ANDROID__)

static SDL_AudioDeviceID AndroidAudioDevice = 0;
static DOUBLE LastAndroidAudioQueueTime = 0.0;
static DOUBLE LastAndroidAudioLateLog = 0.0;

static INT NextPowerOfTwo( INT Value )
{
	INT Result = 1;
	while( Result < Value )
		Result <<= 1;
	return Result;
}

INT OpenAudio( DWORD Rate, INT OutputMode, INT Latency )
{
	if( AndroidAudioDevice )
		CloseAudio();

	if( !SDL_WasInit( SDL_INIT_AUDIO ) )
	{
		if( SDL_InitSubSystem( SDL_INIT_AUDIO ) != 0 )
		{
			debugf( NAME_Init, TEXT("Android SDL audio: SDL_InitSubSystem(SDL_INIT_AUDIO) failed: %s"), appFromAnsi(SDL_GetError()) );
			return 0;
		}
	}

	SDL_AudioSpec Want;
	SDL_AudioSpec Have;
	appMemset( &Want, 0, sizeof(Want) );
	appMemset( &Have, 0, sizeof(Have) );

	INT Channels = (OutputMode & AUDIO_STEREO) ? 2 : 1;
	const char* Driver = SDL_GetCurrentAudioDriver();
	const UBOOL LowLatencyBackend = Driver && SDL_strcasecmp( Driver, "AAudio" ) == 0;
	INT WantedSamples = (INT)((Rate * Max(Latency,10)) / 1000);
	WantedSamples = Clamp( NextPowerOfTwo(WantedSamples), 256, LowLatencyBackend ? 512 : 1024 );

	Want.freq     = Rate;
	Want.format   = AUDIO_S16SYS;
	Want.channels = Channels;
	Want.samples  = WantedSamples;
	Want.callback = NULL;
	Want.userdata = NULL;

	AndroidAudioDevice = SDL_OpenAudioDevice( NULL, 0, &Want, &Have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE );
	if( !AndroidAudioDevice )
	{
		debugf( NAME_Init, TEXT("Android SDL audio: SDL_OpenAudioDevice failed: %s"), appFromAnsi(SDL_GetError()) );
		AudioDevice = -1;
		return 0;
	}

	if( Have.format != AUDIO_S16SYS || Have.freq <= 0 || Have.samples == 0
		|| (Have.channels != 1 && Have.channels != 2) )
	{
		debugf( NAME_Init, TEXT("Android SDL audio: unsupported spec format=%i rate=%i channels=%i samples=%i."),
			(INT)Have.format, Have.freq, (INT)Have.channels, (INT)Have.samples );
		SDL_CloseAudioDevice( AndroidAudioDevice );
		AndroidAudioDevice = 0;
		AudioDevice = -1;
		return 0;
	}

	AudioFormat = AUDIO_16BIT;
	if( Have.channels >= 2 )
		AudioFormat |= AUDIO_STEREO;
	else
		AudioFormat &= ~AUDIO_STEREO;

	AudioRate  = Have.freq;
	BufferSize = Have.samples * Have.channels * sizeof(SWORD);

	AudioBuffer = (BYTE*) appMalloc( BufferSize, TEXT("Android SDL Audio Buffer") );
	appMemset( AudioBuffer, 0, BufferSize );

	SDL_ClearQueuedAudio( AndroidAudioDevice );
	SDL_PauseAudioDevice( AndroidAudioDevice, 0 );
	AudioDevice = 1;
	LastAndroidAudioQueueTime = LastAndroidAudioLateLog = 0.0;

	debugf( NAME_Init, TEXT("Android SDL audio opened: driver=%s, %i Hz, %i channel(s), %i samples, %.2f ms per block, one queued block."),
		appFromAnsi(Driver ? Driver : "unknown"), AudioRate, (INT)Have.channels,
		(INT)Have.samples, 1000.0 * Have.samples / AudioRate );
	return 1;
}

INT ReopenAudioDevice( DWORD Rate, INT OutputMode, INT Latency )
{
	// UT99_ANDROID_V63_AUDIO_REOPEN_SAFE:
	// The generic audio library allocates MixBuffer once in the mixer thread using
	// the initial BufferSize.  Reopening SDL audio here can change BufferSize while
	// that thread is still alive, which can overflow MixBuffer on the next memcpy
	// and crash shortly after the title/intro appears.  For Android, keep the SDL
	// queued-audio device opened by AudioInit and let AudioStartOutput only unpause
	// the existing mixer path.
	if( AndroidAudioDevice && AudioBuffer && BufferSize > 0 )
	{
		if( SDL_GetAudioDeviceStatus(AndroidAudioDevice) == SDL_AUDIO_STOPPED )
		{
			debugf( NAME_Warning, TEXT("Android SDL audio: output device stopped; restart audio before reopening it.") );
			return 0;
		}
		debugf( NAME_Init, TEXT("Android SDL audio: keeping existing device for AudioStartOutput (%i Hz, %i bytes)."), AudioRate, BufferSize );
		return 1;
	}

	if( MixingThread.Valid )
	{
		debugf( NAME_Warning, TEXT("Android SDL audio: cannot resize buffers while the mixer thread is active.") );
		return 0;
	}
	debugf( NAME_Init, TEXT("Android SDL audio: opening device during AudioStartOutput fallback.") );
	return OpenAudio( Rate, OutputMode, Latency );
}

void CloseAudio()
{
	if( AndroidAudioDevice )
	{
		SDL_PauseAudioDevice( AndroidAudioDevice, 1 );
		SDL_ClearQueuedAudio( AndroidAudioDevice );
		SDL_CloseAudioDevice( AndroidAudioDevice );
		AndroidAudioDevice = 0;
	}
	if( AudioBuffer != NULL )
	{
		appFree( AudioBuffer );
		AudioBuffer = NULL;
	}
	AudioDevice = -1;
}

// Audio flow control.
void ClearAudioQueue()
{
	ALock;
	LastAndroidAudioQueueTime = 0.0;
	AUnlock;
	if( AndroidAudioDevice )
		SDL_ClearQueuedAudio( AndroidAudioDevice );
}

void PlayAudio()
{
	if( !AndroidAudioDevice || AudioDevice == -1 || AudioBuffer == NULL || BufferSize <= 0 )
		return;

	const DOUBLE Now = appSeconds();
	const INT Channels = (AudioFormat & AUDIO_STEREO) ? 2 : 1;
	const DOUBLE BlockSeconds = (DOUBLE)BufferSize / (AudioRate * Channels * sizeof(SWORD));
	if( LastAndroidAudioQueueTime > 0.0
		&& Now - LastAndroidAudioQueueTime > BlockSeconds * 2.0 + 0.005
		&& Now - LastAndroidAudioLateLog > 1.0 )
	{
		LastAndroidAudioLateLog = Now;
		debugf( NAME_Warning, TEXT("Android audio mixer late: %.2f ms between blocks (block %.2f ms)."),
			(Now - LastAndroidAudioQueueTime) * 1000.0, BlockSeconds * 1000.0 );
	}

	// AudioWait already made room, before mixing and outside the audio mutex.
	if( SDL_QueueAudio( AndroidAudioDevice, AudioBuffer, BufferSize ) != 0 )
		debugf( NAME_Warning, TEXT("Android SDL audio: SDL_QueueAudio failed: %s"), appFromAnsi(SDL_GetError()) );
	else
		LastAndroidAudioQueueTime = appSeconds();
}

#else

INT OpenAudio( DWORD Rate, INT OutputMode, INT Latency )
{
	// Open the audio device.
	AudioDevice = open("/dev/dsp", O_WRONLY | O_NONBLOCK, 0);

	if (AudioDevice == -1)
	{
		debugf( NAME_Init, TEXT("Failed to open audio device.") );
		return 0;
	}

	// Set the buffer size.
	INT Fragment = FRAGMENT_SIZE;
	if (ioctl(AudioDevice, SNDCTL_DSP_SETFRAGMENT, &Fragment) == -1)
	{
		debugf( NAME_Init, TEXT("Failed to set fragment format.") );
		close(AudioDevice);
		AudioDevice = -1;
		return 0;
	}
	
	// Set the output format.
	INT Format;
	if (OutputMode & AUDIO_16BIT)
	{
		Format = AFMT_S16_LE;
		AudioFormat |= AUDIO_16BIT;
	} else {
		Format = AFMT_U8;
		AudioFormat &= ~AUDIO_16BIT;
	}
	if (ioctl(AudioDevice, SNDCTL_DSP_SETFMT, &Format) == -1)
	{
		debugf( NAME_Init, TEXT("Failed to set audio format.") );
		close(AudioDevice);
		AudioDevice = -1;
		return 0;
	}

	// Set stereo.
	INT Stereo;
	if (OutputMode & AUDIO_STEREO)
	{
		Stereo = 1;
		AudioFormat |= AUDIO_STEREO;
	} else {
 		Stereo = 0;
		AudioFormat &= ~AUDIO_STEREO;
	}
	if (ioctl(AudioDevice, SNDCTL_DSP_STEREO, &Stereo) == -1)
	{
		debugf( NAME_Init, TEXT("Failed to enable stereo audio.") );
		close(AudioDevice);
		AudioDevice = -1;
		return 0;
	}

	// Get buffer size.
	if (ioctl(AudioDevice, SNDCTL_DSP_GETBLKSIZE, &BufferSize) == -1)
	{
		debugf( NAME_Init, TEXT("Failed to get audio buffer size.") );
		close(AudioDevice);
		AudioDevice = -1;
		return 0;
	}

	// Set the rate.
	if (ioctl(AudioDevice, SNDCTL_DSP_SPEED, &Rate) == -1)
	{
		debugf( NAME_Init, TEXT("Failed to set playback rate to %iHz"), Rate );
		close(AudioDevice);
		AudioDevice = -1;
		return 0;
	}
	AudioRate = Rate;

	// Initialize AudioBuffer.
	debugf( NAME_Init, TEXT("Allocating an audio buffer of %i bytes."), BufferSize );
	AudioBuffer = (BYTE*) appMalloc( BufferSize, TEXT("Audio Buffer") );

	return 1;
}

INT ReopenAudioDevice( DWORD Rate, INT OutputMode, INT Latency )
{
	// Called from AudioReinit while the audio mutex is already held by AudioStartOutput.
	debugf( NAME_Init, TEXT("Reopening audio device.") );
	CloseAudio();
	INT Result = OpenAudio( Rate, OutputMode, Latency );

	return Result;
}

void CloseAudio()
{
	if (AudioBuffer != NULL)
	{
		appFree(AudioBuffer);
		AudioBuffer = NULL;
	}
	if (AudioDevice > -1)
	{
		close(AudioDevice);
		AudioDevice = -1;
	}
}

// Audio flow control.
void PlayAudio()
{
	if (AudioDevice == -1)
		return;
		
	ssize_t WriteResult;
	WriteResult = write( AudioDevice, AudioBuffer, BufferSize );

	if (WriteResult == -1)
	{
		switch (errno)
		{
			case 0:				// No error condition, try again?
			case EAGAIN:		// No room, write again.
			case EINTR:			// Write blocked, try again.
				PlayAudio();
				break;			
			default:
				// Handle legit error.
				break;
		}
	}
}

#endif

/*------------------------------------------------------------------------------------
	Read helpers.
------------------------------------------------------------------------------------*/

void* ReadMem( void* DestData, INT NumChunks, INT ChunkSize, MemChunk* SrcData )
{
	void* Result = memcpy( DestData, SrcData->Data + SrcData->Position, ChunkSize*NumChunks );
	SrcData->Position += ChunkSize*NumChunks;

	return Result;
}

void* SeekMem( MemChunk* SrcData, INT Position, INT SeekMode )
{
	switch (SeekMode)
	{
		case MEM_SEEK_CUR:
			SrcData->Position += Position;
			break;
		case MEM_SEEK_ABS:
			SrcData->Position = Position;
			break;
	}
	return SrcData->Data;
}

BYTE Read8_Mem( void* Data )
{
	BYTE Result;

	memcpy( Data, &Result, (sizeof Result) );

	return Result;
}

_WORD Read16_Mem( void* Data )
{
	_WORD Result;

	memcpy( Data, &Result, (sizeof Result) );

	return Result;
}

DWORD Read32_Mem( void* Data )
{
	DWORD Result;

	memcpy( Data, &Result, (sizeof Result) );

	return Result;
}

QWORD Read64_Mem( void* Data )
{
	QWORD Result;

	memcpy( Data, &Result, (sizeof Result) );

	return Result;
}

/*------------------------------------------------------------------------------------
	Thread control.
------------------------------------------------------------------------------------*/

UBOOL CreateAudioThread(AudioThread* NewThread, void* (*ThreadRoutine)(void*) )
{
	// Allocate a new thread.
	pthread_t* NewPosixThread;
	NewPosixThread = (pthread_t*) appMalloc(sizeof(pthread_t), TEXT("POSIX Thread"));

	// Initialize parameters.
	pthread_attr_t NewThreadAttributes;
	pthread_attr_init(&NewThreadAttributes);
	pthread_attr_setdetachstate(&NewThreadAttributes, PTHREAD_CREATE_JOINABLE);

	// Try to create the thread.
	NewThread->Valid = 1;
	INT Error = pthread_create(NewPosixThread, &NewThreadAttributes, ThreadRoutine, NULL);
	if (Error != 0)
	{
		// Some error occured.
		NewThread->Valid = 0;
		appErrorf( TEXT("Failed to create a valid mixing thread.") );
		return 0;
	}
	NewThread->Thread = NewPosixThread;
	NewThread->Exited = 0;
	debugf( NAME_Init, TEXT("Created a new audio thread.") );

	return 1;
}

UBOOL DestroyAudioThread(AudioThread* OldThread)
{
	ALock;
	OldThread->Valid = 0;
	AUnlock;
	pthread_t* Thread = (pthread_t*) OldThread->Thread;
	pthread_join(*Thread, NULL);
	appFree(OldThread->Thread);
	return 1;
}

UBOOL ExitAudioThread(AudioThread* Thread)
{
	Thread->Exited = 1;
	pthread_exit(NULL);
	return 1;
}

UBOOL CreateAudioMutex(AudioMutex* Mutex)
{
	pthread_mutex_t* NewMutex;
	NewMutex = (pthread_mutex_t*) appMalloc(sizeof(pthread_mutex_t), TEXT("POSIX Mutex"));

	pthread_mutexattr_t MutexAttr;
	pthread_mutexattr_init(&MutexAttr);
	#if defined(__ANDROID__)
	pthread_mutexattr_settype(&MutexAttr, PTHREAD_MUTEX_RECURSIVE);
#else
	pthread_mutexattr_setkind_np(&MutexAttr, PTHREAD_MUTEX_RECURSIVE_NP);
#endif
	
	pthread_mutex_init(NewMutex, &MutexAttr);
	Mutex->Mutex = NewMutex;
	return 1;
}

UBOOL DestroyAudioMutex(AudioMutex* Mutex)
{
	pthread_mutex_destroy((pthread_mutex_t*) Mutex->Mutex);
	appFree(Mutex->Mutex);
	return 1;
}

UBOOL AudioLock(AudioMutex* Mutex)
{
	pthread_mutex_lock((pthread_mutex_t*) Mutex->Mutex);
	return 1;
}

UBOOL AudioUnlock(AudioMutex* Mutex)
{
	pthread_mutex_unlock((pthread_mutex_t*) Mutex->Mutex);
	return 1;
}

/*------------------------------------------------------------------------------------
	Timing
------------------------------------------------------------------------------------*/

// Sleeps for ms milliseconds.
void AudioSleep(INT	ms)
{
	struct timeval tv;

	tv.tv_sec = ms/1000;
	tv.tv_usec = (ms%1000)*1000;
	select(0, NULL, NULL, NULL, &tv);
}

// Blocks until audio device is ready for another mixed buffer.
INT AudioWait()
{
#if defined(__ANDROID__)
	static UBOOL DeviceWasPaused = 0;
	for( ;; )
	{
		ALock;
		const UBOOL Active = MixingThread.Valid && AudioInitialized && !AudioPaused;
		AUnlock;
		if( !Active || !AndroidAudioDevice )
			return 0;

		if( SDL_GetAudioDeviceStatus(AndroidAudioDevice) != SDL_AUDIO_PLAYING )
		{
			DeviceWasPaused = 1;
			ALock;
			LastAndroidAudioQueueTime = 0.0;
			AUnlock;
			AudioSleep( 5 );
			continue;
		}
		if( DeviceWasPaused )
		{
			SDL_ClearQueuedAudio( AndroidAudioDevice );
			DeviceWasPaused = 0;
		}
		if( SDL_GetQueuedAudioSize(AndroidAudioDevice) == 0 )
			return 1;
		AudioSleep( 1 );
	}
#else
	ALock;
	const UBOOL Active = MixingThread.Valid && AudioInitialized && !AudioPaused;
	AUnlock;
	if (AudioDevice != -1 && Active)
	{
		fd_set fdset;
		FD_ZERO(&fdset);
		FD_SET(AudioDevice, &fdset);
		select(AudioDevice+1, NULL, &fdset, NULL, NULL);
		return 1;
	} else
		return 0;
#endif
}
