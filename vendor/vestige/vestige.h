/*
 * IMPORTANT: The author of Keepsake has no connection with the
 * author of the VeSTige VST-compatibility header, has had no
 * involvement in its creation.
 *
 * The VeSTige header is included in this package in the good-faith
 * belief that it has been cleanly and legally reverse engineered
 * without reference to the official VST SDK and without its
 * developer(s) having agreed to the VST SDK license agreement.
 */

/*
 * simple header to allow VeSTige compilation and eventually work
 *
 * Copyright (c) 2006 Javier Serrano Polo <jasp00/at/users.sourceforge.net>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */

#ifndef _VESTIGE_H
#define _VESTIGE_H

#include <stdint.h>

#if defined(__WINE__)
# undef __cdecl
# define __cdecl __attribute__((ms_abi))
#elif ! (defined(WIN32) || defined(_WIN32) || defined(__WIN32__))
# define __cdecl
#endif

#define CCONST(a, b, c, d)( ( ( (int) a ) << 24 ) |		\
				( ( (int) b ) << 16 ) |		\
				( ( (int) c ) << 8 ) |		\
				( ( (int) d ) << 0 ) )

#define audioMasterAutomate 0
#define audioMasterVersion 1
#define audioMasterCurrentId 2
#define audioMasterIdle 3
#define audioMasterPinConnected 4
#define audioMasterWantMidi 6
#define audioMasterGetTime 7
#define audioMasterProcessEvents 8
#define audioMasterSetTime 9
#define audioMasterTempoAt 10
#define audioMasterGetNumAutomatableParameters 11
#define audioMasterGetParameterQuantization 12
#define audioMasterIOChanged 13
#define audioMasterNeedIdle 14
#define audioMasterSizeWindow 15
#define audioMasterGetSampleRate 16
#define audioMasterGetBlockSize 17
#define audioMasterGetInputLatency 18
#define audioMasterGetOutputLatency 19
#define audioMasterGetPreviousPlug 20
#define audioMasterGetNextPlug 21
#define audioMasterWillReplaceOrAccumulate 22
#define audioMasterGetCurrentProcessLevel 23
#define audioMasterGetAutomationState 24
#define audioMasterOfflineStart 25
#define audioMasterOfflineRead 26
#define audioMasterOfflineWrite 27
#define audioMasterOfflineGetCurrentPass 28
#define audioMasterOfflineGetCurrentMetaPass 29
#define audioMasterSetOutputSampleRate 30
#define audioMasterGetSpeakerArrangement 31
#define audioMasterGetVendorString 32
#define audioMasterGetProductString 33
#define audioMasterGetVendorVersion 34
#define audioMasterVendorSpecific 35
#define audioMasterSetIcon 36
#define audioMasterCanDo 37
#define audioMasterGetLanguage 38
#define audioMasterOpenWindow 39
#define audioMasterCloseWindow 40
#define audioMasterGetDirectory 41
#define audioMasterUpdateDisplay 42
#define audioMasterBeginEdit 43
#define audioMasterEndEdit 44
#define audioMasterOpenFileSelector 45
#define audioMasterCloseFileSelector 46
#define audioMasterEditFile 47
#define audioMasterGetChunkFile 48
#define audioMasterGetInputSpeakerArrangement 49

#define effFlagsHasEditor 1
#define effFlagsCanReplacing (1 << 4)
#define effFlagsIsSynth (1 << 8)

#define effOpen 0
#define effClose 1
#define effSetProgram 2
#define effGetProgram 3
#define effGetProgramName 5
#define effGetParamLabel 6
#define effGetParamDisplay 7
#define effGetParamName 8
#define effSetSampleRate 10
#define effSetBlockSize 11
#define effMainsChanged 12
#define effEditGetRect 13
#define effEditOpen 14
#define effEditClose 15
#define effEditIdle 19
#define effEditTop 20
#define effGetChunk 23
#define effSetChunk 24
#define effProcessEvents 25
#define effGetPlugCategory 35
#define effGetEffectName 45
#define effGetVendorString 47
#define effGetProductString 48
#define effGetVendorVersion 49
#define effCanDo 51
#define effIdle 53
#define effGetParameterProperties 56
#define effGetVstVersion 58
#define effBeginSetProgram 67
#define effEndSetProgram 68
#define effShellGetNextPlugin 70
#define effStartProcess 71
#define effStopProcess 72

#ifdef WORDS_BIGENDIAN
#define kEffectMagic 0x50747356
#else
#define kEffectMagic 0x56737450
#endif

#define kVstLangEnglish 1
#define kVstMidiType 1

#define kVstTransportChanged 1
#define kVstTransportPlaying (1 << 1)
#define kVstTransportCycleActive (1 << 2)
#define kVstTransportRecording (1 << 3)

#define kVstAutomationWriting (1 << 6)
#define kVstAutomationReading (1 << 7)

#define kVstNanosValid (1 << 8)
#define kVstPpqPosValid (1 << 9)
#define kVstTempoValid (1 << 10)
#define kVstBarsValid (1 << 11)
#define kVstCyclePosValid (1 << 12)
#define kVstTimeSigValid (1 << 13)
#define kVstSmpteValid (1 << 14)
#define kVstClockValid (1 << 15)

enum VstPlugCategory
{
	kPlugCategUnknown = 0,
	kPlugCategEffect,
	kPlugCategSynth,
	kPlugCategAnalysis,
	kPlugCategMastering,
	kPlugCategSpacializer,
	kPlugCategRoomFx,
	kPlugSurroundFx,
	kPlugCategRestoration,
	kPlugCategOfflineProcess,
	kPlugCategShell,
	kPlugCategGenerator,
	kPlugCategMaxCount
};

enum Vestige2StringConstants
{
	VestigeMaxNameLen       = 64,
	VestigeMaxLabelLen      = 64,
	VestigeMaxShortLabelLen = 8,
	VestigeMaxCategLabelLen = 24,
	VestigeMaxFileNameLen   = 100
};

struct _VstMidiEvent
{
	int type;
	int byteSize;
	int deltaFrames;
	int flags;
	int noteLength;
	int noteOffset;
	char midiData[4];
	char detune;
	char noteOffVelocity;
	char reserved1;
	char reserved2;
};

typedef struct _VstMidiEvent VstMidiEvent;

struct _VstEvent
{
	char dump[sizeof (VstMidiEvent)];
};

typedef struct _VstEvent VstEvent;

struct _VstEvents
{
	int numEvents;
	void *reserved;
	VstEvent * events[2];
};

typedef struct _VstEvents VstEvents;

struct _AEffect
{
	int magic;
	intptr_t (__cdecl *dispatcher) (struct _AEffect *, int, int, intptr_t, void *, float);
	void (__cdecl *process) (struct _AEffect *, float **, float **, int);
	void (__cdecl *setParameter) (struct _AEffect *, int, float);
	float (__cdecl *getParameter) (struct _AEffect *, int);
	int numPrograms;
	int numParams;
	int numInputs;
	int numOutputs;
	int flags;
	void *ptr1;
	void *ptr2;
	int initialDelay;
	char empty2[4 + 4];
	float unkown_float;
	void *object;
	void *user;
	int32_t uniqueID;
	int32_t version;
	void (__cdecl *processReplacing) (struct _AEffect *, float **, float **, int);
};

typedef struct _AEffect AEffect;

typedef struct _VstTimeInfo
{
	double samplePos;
	double sampleRate;
	double nanoSeconds;
	double ppqPos;
	double tempo;
	double barStartPos;
	double cycleStartPos;
	double cycleEndPos;
	int32_t timeSigNumerator;
	int32_t timeSigDenominator;
	int32_t smpteOffset;
	int32_t smpteFrameRate;
	int32_t samplesToNextClock;
	int32_t flags;
} VstTimeInfo;

typedef intptr_t (__cdecl *audioMasterCallback) (AEffect *, int32_t, int32_t, intptr_t, void *, float);

#endif
