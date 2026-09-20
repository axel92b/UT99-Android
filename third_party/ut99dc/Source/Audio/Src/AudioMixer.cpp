/*=============================================================================
	AudioMixer.cpp: Unreal sound mixer.
	Copyright 1999 Epic Games, Inc. All Rights Reserved.

Revision history:
	* Created by Brandon Reinhart.
=============================================================================*/

/*------------------------------------------------------------------------------------
	Audio includes.
------------------------------------------------------------------------------------*/

#include "AudioPrivate.h"
#include <limits.h>
#if defined(__ANDROID__)
#include "SDL.h"
#endif

/*------------------------------------------------------------------------------------
	Mixing functions.
------------------------------------------------------------------------------------*/

#define ADJUST_VOLUME(s, v) (s = (s*v)/AUDIO_MAXVOLUME)

void* DoSound(void* Arguments)
{
#if defined(__ANDROID__)
	if( SDL_SetThreadPriority(SDL_THREAD_PRIORITY_HIGH) != 0 )
		debugf( NAME_Warning, TEXT("Android audio mixer: could not raise thread priority: %s"), appFromAnsi(SDL_GetError()) );
#endif
	// Allocate the mixing buffer.
	ALock;
	MixBuffer = appMalloc(BufferSize, TEXT("Mixing Buffer"));
	AUnlock;

	for( ;; )
	{
		ALock;
		const UBOOL Valid = MixingThread.Valid;
		AUnlock;
		if( !Valid )
			break;

		// Wait before taking a snapshot of voices/music, never with the audio mutex held.
		if( !AudioWait() )
		{
			AudioSleep( 5 );
			continue;
		}

		ALock;
		if( !MixingThread.Valid || !AudioInitialized || AudioPaused )
		{
			AUnlock;
			continue;
		}
		appMemset( MixBuffer, 0, BufferSize );
		for( INT i=0; i<AUDIO_TOTALVOICES; ++i )
		{
			if( (Voices[i].State & VOICE_ENABLED) && (Voices[i].State & VOICE_ACTIVE) )
			{
				const INT Format = Voices[i].pSample->Type & SAMPLE_16BIT ? SAMPLE_16BIT : SAMPLE_8BIT;
				if( AudioFormat & AUDIO_16BIT )
				{
					if( Format == SAMPLE_8BIT )
						MixVoice8to16( i );
					else
						MixVoice16to16( i );
				}
			}
		}
		MixMusicIntoBuffer();
		appMemcpy( AudioBuffer, MixBuffer, BufferSize );
		PlayAudio();
		AUnlock;
	}

	// Free the mixing buffer.
	ALock;
	if (MixBuffer != NULL)
	{
		appFree(MixBuffer);
		MixBuffer = NULL;
	}
	AUnlock;
	
	ExitAudioThread(&MixingThread);
	return NULL;
}

// Mixes an 8 bit unsigned voice into 16 bit signed output.
void MixVoice8to16(INT VoiceIndex)
{
	Voice* CurrentVoice = &Voices[VoiceIndex];

	if (CurrentVoice->State & VOICE_FINISHED)
		return;

	if (AudioRate <= 0 || CurrentVoice->pSample->SamplesPerSec != (DWORD)AudioRate)
		ConvertVoice8( CurrentVoice );
	
	// How many samples are in this sound?
	INT SoundSamples = CurrentVoice->pSample->Length;

	// What is our sample size.
	INT SampleSize;
	if (AudioFormat & AUDIO_STEREO)
		SampleSize = 4;
	else
		SampleSize = 2;

	// Mix a buffer's worth of samples.
	INT SamplesToMix = BufferSize / SampleSize;

	// Get our start position...
	BYTE* Src8 = (BYTE*) CurrentVoice->pSample->Data;

	// ...fast forward to where we left off...
	Src8 += CurrentVoice->PlayPosition;

	// ...and get the initial target.
	SBYTE* Dst8 = (SBYTE*) MixBuffer;

	// Scale playing volume by the volume set by the player.
	INT VolumeAdjust = ((CurrentVoice->Volume*2) * SampleVolume) / AUDIO_MAXVOLUME;

	// Calculate panning offset.
	FLOAT PanningOffset = CurrentVoice->Panning - AUDIO_MIDPAN;
	FLOAT PanningFactor = PanningOffset / AUDIO_MIDPAN;
	UBOOL PanSample = AudioFormat & AUDIO_STEREO;

	// For each target sample, mix two input samples.
	INT MixedSample, SourceSample;
	for (INT i=0; i<SamplesToMix; i++)
	{
		// Perform this operation for each speaker.
		for (INT j=0; j<SampleSize; j += 2)
		{
			// Ignore low byte.
			Dst8++;
		
			// Scale the source by the volume.
			SourceSample =  *Src8 - 128;
			SourceSample = (SourceSample * VolumeAdjust) / AUDIO_MAXVOLUME;

			// Pan the source.
			if (PanSample)
			{
				// Even the sound.
				SourceSample /= (SampleSize/2);
				// Pan according to our factor.
				if (j == 0)
					SourceSample -= (INT) (SourceSample * PanningFactor);
				else
					SourceSample += (INT) (SourceSample * PanningFactor);
			}

			// Mix the result.
			MixedSample = SourceSample + *Dst8;

			// Bound the result.
			if (MixedSample > AUDIO_MAXSAMPLE8) {
				*Dst8 = AUDIO_MAXSAMPLE8;
			} else if (MixedSample < AUDIO_MINSAMPLE8) {
				*Dst8 = AUDIO_MINSAMPLE8;
			} else {
				*Dst8 = MixedSample;
			}

			// Destination is 16 bit, so increment another byte.
			Dst8++;
		}
		// Source is 8 bit, so increment 1 byte.
		Src8++;

		// Keep track of how much we've mixed.
		CurrentVoice->PlayPosition++;
		if (CurrentVoice->PlayPosition >= SoundSamples)
		{
			// We've run out of source.
			// Finish if this isn't a looping sound.
			CurrentVoice->PlayPosition = 0;
			if (CurrentVoice->pSample->Type & SAMPLE_LOOPED)
			{
				Src8 = (BYTE*) CurrentVoice->pSample->Data;
			} else {
				i = SamplesToMix;
				CurrentVoice->State |= VOICE_FINISHED;
				CurrentVoice->State &= ~VOICE_ACTIVE;
			}
		}
	}
}

// Mix a 16 bit signed sample into 16 bit signed output.
void MixVoice16to16(INT VoiceIndex)
{
	Voice* CurrentVoice = &Voices[VoiceIndex];

	if (CurrentVoice->State & VOICE_FINISHED)
		return;

	if (AudioRate <= 0 || CurrentVoice->pSample->SamplesPerSec != (DWORD)AudioRate)
		ConvertVoice16( CurrentVoice );

	// How many samples are in this sound?
	INT SoundSamples = CurrentVoice->pSample->Length;

	// What is our sample size.
	INT SampleSize;
	if (AudioFormat & AUDIO_STEREO)
		SampleSize = 4;
	else
		SampleSize = 2;

	// Mix a buffer's worth of samples.
	INT SamplesToMix = BufferSize/SampleSize;

	// Get our start position...
	SWORD* Src16 = (SWORD*) CurrentVoice->pSample->Data;

	// ...fast forward to where we left off...
	Src16 += CurrentVoice->PlayPosition;

	// ...and get the initial target.
	SWORD* Dst16 = (SWORD*) MixBuffer;

	// Determine volume.
	INT VolumeAdjust = ((CurrentVoice->Volume*2) * SampleVolume) / AUDIO_MAXVOLUME;

	// Calculate panning offset.
	FLOAT PanningOffset = CurrentVoice->Panning - AUDIO_MIDPAN;
	FLOAT PanningFactor = PanningOffset / AUDIO_MIDPAN;
	UBOOL PanSample = AudioFormat & AUDIO_STEREO;

	// For each target sample, mix two input samples.
	INT MixedSample, SourceSample;
	for (INT i=0; i<SamplesToMix; i++)
	{
		// Perform this operation for each speaker.
		for (INT j=0; j<SampleSize; j+= 2)
		{
			// Adjust the volume.
			SourceSample = *Src16;
			SourceSample = (SourceSample * VolumeAdjust) / AUDIO_MAXVOLUME;

			// Pan the source.
			if (PanSample)
			{
				// Even the sound.
				SourceSample /= (SampleSize/2);
				// Pan according to our factor.
				if (j == 0)
					SourceSample -= (INT) (SourceSample * PanningFactor);
				else
					SourceSample += (INT) (SourceSample * PanningFactor);
			}

			// Mix the result.
			MixedSample = SourceSample + *Dst16;

			// Apply limits.
			if (MixedSample > AUDIO_MAXSAMPLE16) {
				*Dst16 = AUDIO_MAXSAMPLE16;
			} else if (MixedSample < AUDIO_MINSAMPLE16) {
				*Dst16 = AUDIO_MINSAMPLE16;
			} else {
				*Dst16 = MixedSample;
			}

			// Destination is 16 bit, so increment by an SWORD.
			Dst16++;
		}

		// Source is 16 bit, so increment by an SWORD.
		Src16++;

		// Keep track of how much we've mixed.
		CurrentVoice->PlayPosition++;
		if (CurrentVoice->PlayPosition >= SoundSamples)
		{
			// We've run out of source.
			// Finish if this isn't a looping sound.
			CurrentVoice->PlayPosition = 0;
			if (CurrentVoice->pSample->Type & SAMPLE_LOOPED)
			{
				Src16 = (SWORD*) CurrentVoice->pSample->Data;
			} else {
				i = SamplesToMix;
				CurrentVoice->State |= VOICE_FINISHED;
				CurrentVoice->State &= ~VOICE_ACTIVE;
			}
		}
	}
}

static INT ResamplePosition( INT Position, DWORD SourceRate, DWORD TargetRate, DWORD NewLength )
{
	const QWORD Scaled = (QWORD)Max(Position,0) * TargetRate / SourceRate;
	return (INT)Min<QWORD>( Scaled, NewLength - 1 );
}

// Called with the audio mutex held; samples can be shared by several active voices.
static void ResampleVoice( Voice* InVoice, INT BytesPerSample )
{
	if( !InVoice || !InVoice->pSample || !InVoice->pSample->Data
		|| !InVoice->pSample->Length || !InVoice->pSample->SamplesPerSec || AudioRate <= 0 )
	{
		appErrorf( TEXT("Cannot resample an invalid audio sample.") );
		return;
	}

	Sample* Sound = InVoice->pSample;
	const DWORD SourceRate = Sound->SamplesPerSec;
	const DWORD TargetRate = (DWORD)AudioRate;
	if( SourceRate == TargetRate )
		return;

	const DWORD Channels = (Sound->Type & SAMPLE_STEREO) ? 2 : 1;
	const DWORD OldLength = Sound->Length;
	const QWORD NewFrames = ((QWORD)OldLength * TargetRate + SourceRate - 1) / SourceRate;
	const QWORD NewBytes = NewFrames * Channels * BytesPerSample;
	if( NewFrames > INT_MAX || NewBytes > INT_MAX
		|| (QWORD)OldLength * Channels * BytesPerSample > INT_MAX )
	{
		appErrorf( TEXT("Audio sample is too large to resample safely.") );
		return;
	}
	const DWORD NewLength = (DWORD)NewFrames;
	void* NewData = appMalloc( (DWORD)NewBytes, TEXT("Sample Data") );
	check(NewData);

	for( DWORD Frame=0; Frame<NewLength; ++Frame )
	{
		const QWORD Position = (QWORD)Frame * SourceRate;
		const DWORD Left = (DWORD)(Position / TargetRate);
		const DWORD Right = Min( Left + 1, OldLength - 1 );
		const DWORD Fraction = (DWORD)(Position % TargetRate);
		for( DWORD Channel=0; Channel<Channels; ++Channel )
		{
			const DWORD AIndex = Left * Channels + Channel;
			const DWORD BIndex = Right * Channels + Channel;
			const INT A = BytesPerSample == 1 ? ((BYTE*)Sound->Data)[AIndex] : ((SWORD*)Sound->Data)[AIndex];
			const INT B = BytesPerSample == 1 ? ((BYTE*)Sound->Data)[BIndex] : ((SWORD*)Sound->Data)[BIndex];
			const INT Value = A + (INT)((SQWORD)(B - A) * Fraction / TargetRate);
			const DWORD Output = Frame * Channels + Channel;
			if( BytesPerSample == 1 )
				((BYTE*)NewData)[Output] = (BYTE)Value;
			else
				((SWORD*)NewData)[Output] = (SWORD)Value;
		}
	}

	for( INT i=0; i<AUDIO_TOTALVOICES; ++i )
		if( &Voices[i] != InVoice && Voices[i].pSample == Sound && (Voices[i].State & VOICE_ACTIVE) )
			Voices[i].PlayPosition = ResamplePosition( Voices[i].PlayPosition, SourceRate, TargetRate, NewLength );
	InVoice->PlayPosition = ResamplePosition( InVoice->PlayPosition, SourceRate, TargetRate, NewLength );
	if( Sound->Type & SAMPLE_LOOPED )
	{
		Sound->LoopStart = (DWORD)Min<QWORD>( (QWORD)Sound->LoopStart * TargetRate / SourceRate, NewLength - 1 );
		const QWORD End = Min<QWORD>( (QWORD)Sound->LoopEnd + 1, OldLength );
		Sound->LoopEnd = (DWORD)Min<QWORD>( (End * TargetRate + SourceRate - 1) / SourceRate - 1, NewLength - 1 );
	}
	void* OldData = Sound->Data;
	Sound->Data = NewData;
	Sound->Length = NewLength;
	Sound->SamplesPerSec = TargetRate;
	appFree( OldData );
}

void ConvertVoice8( Voice* InVoice )
{
	ResampleVoice( InVoice, sizeof(BYTE) );
}

void ConvertVoice16( Voice* InVoice )
{
	ResampleVoice( InVoice, sizeof(SWORD) );
}