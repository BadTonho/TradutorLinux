#define COBJMACROS

#include <windows.h>
#include <xaudio2.h>

static const char kSuccessMessage[] = "Proton audio ready\n";

__attribute__((dllimport)) HRESULT XAudio2Create(
    IXAudio2** audio, UINT32 flags, XAUDIO2_PROCESSOR processor);

static void write_message(const char* const message, const DWORD length) {
    const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD written = 0;
    (void)WriteFile(output, message, length, &written, NULL);
}

static void release_audio(IXAudio2* const audio, IXAudio2MasteringVoice* const master,
                          IXAudio2SourceVoice* const source) {
    if (source != NULL) IXAudio2SourceVoice_DestroyVoice(source);
    if (master != NULL) IXAudio2MasteringVoice_DestroyVoice(master);
    if (audio != NULL) {
        IXAudio2_StopEngine(audio);
        IXAudio2_Release(audio);
    }
}

void tl_entry(void);
__attribute__((used, section(".rdata"))) void (*const tl_relocation_anchor)(void) = &tl_entry;

void tl_entry(void) {
    IXAudio2* audio = NULL;
    HRESULT result = XAudio2Create(&audio, 0U, XAUDIO2_DEFAULT_PROCESSOR);
    if (FAILED(result) || audio == NULL) ExitProcess(1U);

    IXAudio2MasteringVoice* master = NULL;
    result = IXAudio2_CreateMasteringVoice(
        audio, &master, 2U, 48000U, 0U, NULL, NULL, AudioCategory_GameMedia);
    if (FAILED(result) || master == NULL) {
        release_audio(audio, NULL, NULL);
        ExitProcess(2U);
    }

    WAVEFORMATEX format = {0};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 1U;
    format.nSamplesPerSec = 48000U;
    format.wBitsPerSample = 16U;
    format.nBlockAlign = (WORD)(format.nChannels * format.wBitsPerSample / 8U);
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;

    IXAudio2SourceVoice* source = NULL;
    result = IXAudio2_CreateSourceVoice(
        audio, &source, &format, 0U, XAUDIO2_DEFAULT_FREQ_RATIO, NULL, NULL, NULL);
    if (FAILED(result) || source == NULL) {
        release_audio(audio, master, NULL);
        ExitProcess(3U);
    }

    static const SHORT samples[4800] = {0};
    XAUDIO2_BUFFER buffer = {0};
    buffer.AudioBytes = (UINT32)sizeof(samples);
    buffer.pAudioData = (const BYTE*)samples;
    buffer.Flags = XAUDIO2_END_OF_STREAM;
    result = IXAudio2SourceVoice_SubmitSourceBuffer(source, &buffer, NULL);
    if (SUCCEEDED(result)) result = IXAudio2_StartEngine(audio);
    if (SUCCEEDED(result)) result = IXAudio2SourceVoice_Start(source, 0U, 0U);
    if (FAILED(result)) {
        release_audio(audio, master, source);
        ExitProcess(4U);
    }

    Sleep(100U);
    (void)IXAudio2SourceVoice_Stop(source, 0U, 0U);
    release_audio(audio, master, source);
    write_message(kSuccessMessage, (DWORD)(sizeof(kSuccessMessage) - 1U));
    ExitProcess(0U);
}
