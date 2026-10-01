#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef signed char BOOL;
typedef unsigned long NSUInteger;
typedef long NSInteger;
typedef double NSTimeInterval;
#define YES ((BOOL)1)
#define NO ((BOOL)0)
#define nil ((id)0)

__attribute__((objc_root_class))
@interface NSObject
@end
@class NSString, NSError;
@protocol NSFastEnumeration
- (NSUInteger)countByEnumeratingWithState:(void *)state objects:(id *)objects count:(NSUInteger)count;
@end
@interface NSArray<__covariant ObjectType> : NSObject <NSFastEnumeration>
@property(readonly) ObjectType firstObject;
- (BOOL)containsObject:(ObjectType)object;
@end
@interface NSString : NSObject
- (BOOL)isEqualToString:(NSString *)other;
@end

typedef NSUInteger AVAudioSessionCategoryOptions;
typedef NSUInteger AVAudioSessionSetActiveOptions;
typedef NSInteger AVAudioStereoOrientation;
enum {
    AVAudioSessionCategoryOptionMixWithOthers = 1,
    AVAudioSessionCategoryOptionDuckOthers = 2,
    AVAudioSessionCategoryOptionAllowBluetooth = 4,
    AVAudioSessionCategoryOptionDefaultToSpeaker = 8,
    AVAudioSessionCategoryOptionInterruptSpokenAudioAndMixWithOthers = 17,
    AVAudioSessionCategoryOptionAllowBluetoothA2DP = 32,
    AVAudioSessionCategoryOptionAllowAirPlay = 64,
    AVAudioSessionSetActiveOptionNotifyOthersOnDeactivation = 1,
    AVAudioStereoOrientationPortrait = 1,
    AVAudioStereoOrientationPortraitUpsideDown = 2,
    AVAudioStereoOrientationLandscapeRight = 3,
    AVAudioStereoOrientationLandscapeLeft = 4
};
extern NSString *const AVAudioSessionCategoryAmbient;
extern NSString *const AVAudioSessionCategorySoloAmbient;
extern NSString *const AVAudioSessionCategoryPlayback;
extern NSString *const AVAudioSessionCategoryRecord;
extern NSString *const AVAudioSessionCategoryPlayAndRecord;
extern NSString *const AVAudioSessionCategoryMultiRoute;
extern NSString *const AVAudioSessionModeDefault;
extern NSString *const AVAudioSessionModeVoiceChat;
extern NSString *const AVAudioSessionModeVideoChat;
extern NSString *const AVAudioSessionModeGameChat;
extern NSString *const AVAudioSessionModeVideoRecording;
extern NSString *const AVAudioSessionModeMeasurement;
extern NSString *const AVAudioSessionModeMoviePlayback;
extern NSString *const AVAudioSessionModeSpokenAudio;
extern NSString *const AVAudioSessionPortBuiltInMic;
extern NSString *const AVAudioSessionPolarPatternStereo;
extern NSString *const AVAudioSessionOrientationFront;

@interface AVAudioSessionDataSourceDescription : NSObject
@property(readonly) NSArray<NSString *> *supportedPolarPatterns;
@property(readonly) NSString *orientation;
- (BOOL)setPreferredPolarPattern:(NSString *)pattern error:(NSError **)error;
@end
@interface AVAudioSessionPortDescription : NSObject
@property(readonly) NSString *portType;
@property(readonly) NSArray<AVAudioSessionDataSourceDescription *> *dataSources;
- (BOOL)setPreferredDataSource:(AVAudioSessionDataSourceDescription *)source error:(NSError **)error;
@end
@interface AVAudioSessionRouteDescription : NSObject
@property(readonly) NSArray<AVAudioSessionPortDescription *> *inputs;
@end
@interface AVAudioSession : NSObject
+ (AVAudioSession *)sharedInstance;
@property(readonly) NSString *category;
@property(readonly) AVAudioSessionRouteDescription *currentRoute;
@property(readonly) AVAudioSessionPortDescription *preferredInput;
@property(readonly) NSInteger maximumInputNumberOfChannels;
- (BOOL)setCategory:(NSString *)category withOptions:(AVAudioSessionCategoryOptions)options error:(NSError **)error;
- (BOOL)setMode:(NSString *)mode error:(NSError **)error;
- (BOOL)setPreferredSampleRate:(double)rate error:(NSError **)error;
- (BOOL)setPreferredIOBufferDuration:(NSTimeInterval)duration error:(NSError **)error;
- (BOOL)setAllowHapticsAndSystemSoundsDuringRecording:(BOOL)allow error:(NSError **)error;
- (BOOL)setActive:(BOOL)active options:(AVAudioSessionSetActiveOptions)options error:(NSError **)error;
- (BOOL)setPreferredInputOrientation:(AVAudioStereoOrientation)orientation error:(NSError **)error;
- (BOOL)setPreferredInputNumberOfChannels:(NSInteger)channels error:(NSError **)error;
- (BOOL)setPreferredInput:(AVAudioSessionPortDescription *)input error:(NSError **)error;
@end
