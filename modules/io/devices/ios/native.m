#include "session.h"

#import <AVFoundation/AVFoundation.h>

int32_t lnd_ios_native_configure(const LND_IOS_SESSION_CONFIG *config) {
    @autoreleasepool {
        AVAudioSession *session = [AVAudioSession sharedInstance];
        NSString *categories[] = {nil,
                                  AVAudioSessionCategoryAmbient,
                                  AVAudioSessionCategorySoloAmbient,
                                  AVAudioSessionCategoryPlayback,
                                  AVAudioSessionCategoryRecord,
                                  AVAudioSessionCategoryPlayAndRecord,
                                  AVAudioSessionCategoryMultiRoute};
        NSString *modes[] = {AVAudioSessionModeDefault,        AVAudioSessionModeVoiceChat,   AVAudioSessionModeVideoChat,     AVAudioSessionModeGameChat,
                             AVAudioSessionModeVideoRecording, AVAudioSessionModeMeasurement, AVAudioSessionModeMoviePlayback, AVAudioSessionModeSpokenAudio};
        const AVAudioSessionCategoryOptions option_bits[] = {AVAudioSessionCategoryOptionMixWithOthers,
                                                             AVAudioSessionCategoryOptionDuckOthers,
                                                             AVAudioSessionCategoryOptionAllowBluetooth,
                                                             AVAudioSessionCategoryOptionAllowBluetoothA2DP,
                                                             AVAudioSessionCategoryOptionAllowAirPlay,
                                                             AVAudioSessionCategoryOptionDefaultToSpeaker,
                                                             AVAudioSessionCategoryOptionInterruptSpokenAudioAndMixWithOthers};
        AVAudioSessionCategoryOptions options = 0;
        for (unsigned i = 0; i < sizeof option_bits / sizeof *option_bits; i++)
            if (config->options & (1u << i)) options |= option_bits[i];
        if (config->category || config->options) {
            NSString *category = config->category ? categories[config->category] : session.category;
            if (![session setCategory:category withOptions:options error:NULL]) return LND_ERR_EXTERNAL;
        }
        if ((config->category || config->mode) && ![session setMode:modes[config->mode] error:NULL]) return LND_ERR_EXTERNAL;
        if (config->sample_rate_hz && ![session setPreferredSampleRate:config->sample_rate_hz error:NULL]) return LND_ERR_EXTERNAL;
        if (config->buffer_duration_us && ![session setPreferredIOBufferDuration:config->buffer_duration_us / 1000000.0 error:NULL]) return LND_ERR_EXTERNAL;
        if (config->flags & LND_IOS_SESSION_ALLOW_HAPTICS) {
            if (@available(iOS 13.0, *)) {
                if (![session setAllowHapticsAndSystemSoundsDuringRecording:YES error:NULL]) return LND_ERR_EXTERNAL;
            } else {
                return LND_ERR_UNSUPPORTED;
            }
        }
        return LND_OK;
    }
}

int32_t lnd_ios_native_active(bool active) {
    @autoreleasepool {
        AVAudioSessionSetActiveOptions options = active ? 0 : AVAudioSessionSetActiveOptionNotifyOthersOnDeactivation;
        return [[AVAudioSession sharedInstance] setActive:active options:options error:NULL] ? LND_OK : LND_ERR_EXTERNAL;
    }
}

int32_t lnd_ios_native_orientation(int32_t orientation) {
    if (!orientation) return LND_OK;
    @autoreleasepool {
        if (@available(iOS 14.0, *)) {
            AVAudioStereoOrientation value;
            switch (orientation) {
            case LND_IOS_ORIENTATION_PORTRAIT:
                value = AVAudioStereoOrientationPortrait;
                break;
            case LND_IOS_ORIENTATION_PORTRAIT_UPSIDE_DOWN:
                value = AVAudioStereoOrientationPortraitUpsideDown;
                break;
            case LND_IOS_ORIENTATION_LANDSCAPE_LEFT:
                value = AVAudioStereoOrientationLandscapeLeft;
                break;
            case LND_IOS_ORIENTATION_LANDSCAPE_RIGHT:
                value = AVAudioStereoOrientationLandscapeRight;
                break;
            default:
                return LND_ERR_INVALID_ARG;
            }
            return [[AVAudioSession sharedInstance] setPreferredInputOrientation:value error:NULL] ? LND_OK : LND_ERR_EXTERNAL;
        }
        return LND_ERR_UNSUPPORTED;
    }
}

int32_t lnd_ios_native_input(int32_t recording, int32_t orientation) {
    if (recording == LND_IOS_RECORDING_DEFAULT) return LND_OK;
    @autoreleasepool {
        AVAudioSession *session = [AVAudioSession sharedInstance];
        if (recording == LND_IOS_RECORDING_MONO) return [session setPreferredInputNumberOfChannels:1 error:NULL] ? LND_OK : LND_ERR_EXTERNAL;
        AVAudioSessionPortDescription *port = session.currentRoute.inputs.firstObject;
        if (!port) port = session.preferredInput;
        if (!port) return LND_ERR_NO_DEVICE;
        if ([port.portType isEqualToString:AVAudioSessionPortBuiltInMic]) {
            if (@available(iOS 14.0, *)) {
                AVAudioSessionDataSourceDescription *selected = nil;
                for (AVAudioSessionDataSourceDescription *source in port.dataSources) {
                    if (![source.supportedPolarPatterns containsObject:AVAudioSessionPolarPatternStereo]) continue;
                    if (!selected) selected = source;
                    if ([source.orientation isEqualToString:AVAudioSessionOrientationFront]) {
                        selected = source;
                        break;
                    }
                }
                if (!selected) return LND_ERR_UNSUPPORTED;
                if (![selected setPreferredPolarPattern:AVAudioSessionPolarPatternStereo error:NULL] || ![port setPreferredDataSource:selected error:NULL] ||
                    ![session setPreferredInput:port error:NULL])
                    return LND_ERR_EXTERNAL;
                int32_t r = lnd_ios_native_orientation(orientation);
                if (r != LND_OK) return r;
            } else {
                return LND_ERR_UNSUPPORTED;
            }
        } else if (orientation) {
            return LND_ERR_UNSUPPORTED;
        }
        if (session.maximumInputNumberOfChannels < 2) return LND_ERR_UNSUPPORTED;
        return [session setPreferredInputNumberOfChannels:2 error:NULL] ? LND_OK : LND_ERR_EXTERNAL;
    }
}
