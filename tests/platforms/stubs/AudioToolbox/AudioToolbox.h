#pragma once
#include <stdint.h>
#include <stddef.h>
typedef uint32_t UInt32;
typedef int32_t OSStatus;
typedef double Float64;
typedef uint32_t AudioObjectID;
typedef uint32_t AudioDeviceID;
typedef uint32_t AudioObjectPropertySelector;
typedef uint32_t AudioObjectPropertyScope;
typedef uint32_t AudioUnitPropertyID;
typedef uint32_t AudioUnitScope;
typedef uint32_t AudioUnitElement;
typedef uint32_t AudioUnitRenderActionFlags;
typedef struct MockUnit *AudioUnit;
typedef AudioUnit AudioComponentInstance;
typedef void *AudioComponent;
typedef struct AudioTimeStamp { Float64 mSampleTime; uint64_t mHostTime; } AudioTimeStamp;
typedef struct AudioBuffer { UInt32 mNumberChannels, mDataByteSize; void *mData; } AudioBuffer;
typedef struct AudioBufferList { UInt32 mNumberBuffers; AudioBuffer mBuffers[1]; } AudioBufferList;
typedef struct AudioObjectPropertyAddress { UInt32 mSelector, mScope, mElement; } AudioObjectPropertyAddress;
typedef struct AudioValueRange { Float64 mMinimum, mMaximum; } AudioValueRange;
typedef struct AudioStreamBasicDescription {
    Float64 mSampleRate;
    UInt32 mFormatID, mFormatFlags, mBytesPerPacket, mFramesPerPacket, mBytesPerFrame, mChannelsPerFrame, mBitsPerChannel, mReserved;
} AudioStreamBasicDescription;
typedef struct AudioComponentDescription { UInt32 componentType, componentSubType, componentManufacturer, componentFlags, componentFlagsMask; } AudioComponentDescription;
typedef OSStatus (*AURenderCallback)(void *, AudioUnitRenderActionFlags *, const AudioTimeStamp *, UInt32, UInt32, AudioBufferList *);
typedef struct AURenderCallbackStruct { AURenderCallback inputProc; void *inputProcRefCon; } AURenderCallbackStruct;
typedef OSStatus (*AudioObjectPropertyListenerProc)(AudioObjectID, UInt32, const AudioObjectPropertyAddress *, void *);
enum {
    noErr = 0, kAudioUnitErr_TooManyFramesToProcess = -1, kAudioUnitErr_InvalidPropertyValue = -2,
    kAudioObjectUnknown = 0, kAudioObjectSystemObject = 1, kAudioObjectPropertyElementMain = 0,
    kAudioObjectPropertyScopeGlobal = 1, kAudioDevicePropertyScopeOutput = 2, kAudioDevicePropertyScopeInput = 3,
    kAudioHardwarePropertyDevices = 10, kAudioHardwarePropertyDefaultOutputDevice, kAudioHardwarePropertyDefaultInputDevice,
    kAudioObjectPropertyName, kAudioDevicePropertyDeviceUID, kAudioDevicePropertyStreamConfiguration, kAudioDevicePropertyNominalSampleRate,
    kAudioDevicePropertyBufferFrameSize, kAudioDevicePropertyBufferFrameSizeRange, kAudioDevicePropertyDeviceIsAlive,
    kAudioUnitScope_Global = 0, kAudioUnitScope_Input = 1, kAudioUnitScope_Output = 2,
    kAudioUnitProperty_StreamFormat = 30, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitProperty_ShouldAllocateBuffer,
    kAudioUnitProperty_SetRenderCallback, kAudioUnitProperty_Latency, kAudioOutputUnitProperty_EnableIO,
    kAudioOutputUnitProperty_CurrentDevice, kAudioOutputUnitProperty_SetInputCallback,
    kAudioUnitType_Output = 50, kAudioUnitSubType_HALOutput, kAudioUnitSubType_RemoteIO, kAudioUnitManufacturer_Apple,
    kAudioFormatLinearPCM = 70, kAudioFormatFlagIsPacked = 1, kAudioFormatFlagIsFloat = 2, kAudioFormatFlagIsSignedInteger = 4,
    kAudioUnitRenderAction_OutputIsSilence = 16
};
AudioComponent AudioComponentFindNext(AudioComponent, const AudioComponentDescription *);
OSStatus AudioComponentInstanceNew(AudioComponent, AudioComponentInstance *);
OSStatus AudioComponentInstanceDispose(AudioComponentInstance);
OSStatus AudioUnitGetProperty(AudioUnit, AudioUnitPropertyID, AudioUnitScope, AudioUnitElement, void *, UInt32 *);
OSStatus AudioUnitSetProperty(AudioUnit, AudioUnitPropertyID, AudioUnitScope, AudioUnitElement, const void *, UInt32);
OSStatus AudioUnitInitialize(AudioUnit);
OSStatus AudioUnitUninitialize(AudioUnit);
OSStatus AudioOutputUnitStart(AudioUnit);
OSStatus AudioOutputUnitStop(AudioUnit);
OSStatus AudioUnitRender(AudioUnit, AudioUnitRenderActionFlags *, const AudioTimeStamp *, UInt32, UInt32, AudioBufferList *);
OSStatus AudioObjectGetPropertyData(AudioObjectID, const AudioObjectPropertyAddress *, UInt32, const void *, UInt32 *, void *);
OSStatus AudioObjectGetPropertyDataSize(AudioObjectID, const AudioObjectPropertyAddress *, UInt32, const void *, UInt32 *);
OSStatus AudioObjectAddPropertyListener(AudioObjectID, const AudioObjectPropertyAddress *, AudioObjectPropertyListenerProc, void *);
OSStatus AudioObjectRemovePropertyListener(AudioObjectID, const AudioObjectPropertyAddress *, AudioObjectPropertyListenerProc, void *);

typedef int64_t SInt64;
typedef struct MockAudioFile *AudioFileID;
typedef struct MockExtAudioFile *ExtAudioFileRef;
typedef OSStatus (*AudioFile_ReadProc)(void *, SInt64, UInt32, void *, UInt32 *);
typedef OSStatus (*AudioFile_WriteProc)(void *, SInt64, UInt32, const void *, UInt32 *);
typedef SInt64 (*AudioFile_GetSizeProc)(void *);
typedef OSStatus (*AudioFile_SetSizeProc)(void *, SInt64);
enum {
    kAudioFileUnspecifiedError = 'wht?', kAudioFilePositionError = -40, kAudioFormatFlagsNativeEndian = 0,
    kExtAudioFileProperty_FileDataFormat = 100, kExtAudioFileProperty_ClientDataFormat, kExtAudioFileProperty_FileLengthFrames
};
OSStatus AudioFileOpenWithCallbacks(void *, AudioFile_ReadProc, AudioFile_WriteProc, AudioFile_GetSizeProc, AudioFile_SetSizeProc, UInt32, AudioFileID *);
OSStatus AudioFileClose(AudioFileID);
OSStatus ExtAudioFileWrapAudioFileID(AudioFileID, unsigned char, ExtAudioFileRef *);
OSStatus ExtAudioFileDispose(ExtAudioFileRef);
OSStatus ExtAudioFileGetProperty(ExtAudioFileRef, UInt32, UInt32 *, void *);
OSStatus ExtAudioFileSetProperty(ExtAudioFileRef, UInt32, UInt32, const void *);
OSStatus ExtAudioFileRead(ExtAudioFileRef, UInt32 *, AudioBufferList *);
OSStatus ExtAudioFileSeek(ExtAudioFileRef, SInt64);

