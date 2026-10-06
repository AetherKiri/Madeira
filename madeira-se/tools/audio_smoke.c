/* A short, silent WinMM workload used to verify Wine's CoreAudio path. */

#include <windows.h>
#include <mmsystem.h>

int main(void)
{
    WAVEFORMATEX format = {0};
    WAVEHDR header = {0};
    HWAVEOUT output = NULL;
    HANDLE event;
    short samples[480 * 2] = {0};
    MMRESULT result;
    DWORD wait_result;

    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 2;
    format.nSamplesPerSec = 48000;
    format.wBitsPerSample = 16;
    format.nBlockAlign = format.nChannels * format.wBitsPerSample / 8;
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;

    event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!event) return 51;
    result = waveOutOpen(&output, WAVE_MAPPER, &format, (DWORD_PTR)event, 0,
                         CALLBACK_EVENT);
    if (result != MMSYSERR_NOERROR) {
        CloseHandle(event);
        return 52;
    }

    ResetEvent(event); /* waveOutOpen signals the callback event once. */
    header.lpData = (char *)samples;
    header.dwBufferLength = sizeof(samples);
    result = waveOutPrepareHeader(output, &header, sizeof(header));
    if (result != MMSYSERR_NOERROR) {
        waveOutClose(output);
        CloseHandle(event);
        return 53;
    }
    result = waveOutWrite(output, &header, sizeof(header));
    if (result != MMSYSERR_NOERROR) {
        waveOutUnprepareHeader(output, &header, sizeof(header));
        waveOutClose(output);
        CloseHandle(event);
        return 54;
    }
    wait_result = WaitForSingleObject(event, 5000);
    waveOutReset(output);
    waveOutUnprepareHeader(output, &header, sizeof(header));
    waveOutClose(output);
    CloseHandle(event);
    if (wait_result != WAIT_OBJECT_0 || !(header.dwFlags & WHDR_DONE)) return 55;
    return 47;
}
