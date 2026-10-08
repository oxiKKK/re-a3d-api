/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * process_audio_mute.hpp - mute this process's default audio session.
 *
 * NOT PART OF THE ORIGINAL.  Project tooling.
 *
 * Requires COM. Default-session mute also covers later DirectSound streams
 * and is applied after mixing, leaving comparison values unaffected.
 *
 *---------------------------------------------------------------------------
 */

#ifndef A3DTEST_PROCESS_AUDIO_MUTE_HPP
#define A3DTEST_PROCESS_AUDIO_MUTE_HPP

#include <windows.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>

namespace a3dtest {

class process_audio_mute {
public:
	process_audio_mute() : m_pVolume(nullptr), m_bWasMuted(FALSE)
	{
		IMMDeviceEnumerator   *pEnum    = nullptr;
		IMMDevice             *pDevice  = nullptr;
		IAudioSessionManager  *pManager = nullptr;

		if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
					       CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
					       reinterpret_cast<void **>(&pEnum))) &&
		    SUCCEEDED(pEnum->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice)) &&
		    SUCCEEDED(pDevice->Activate(__uuidof(IAudioSessionManager), CLSCTX_ALL,
						nullptr, reinterpret_cast<void **>(&pManager))) &&
		    SUCCEEDED(pManager->GetSimpleAudioVolume(nullptr, FALSE, &m_pVolume)))
		{
			m_pVolume->GetMute(&m_bWasMuted);
			m_pVolume->SetMute(TRUE, nullptr);
		}

		if (pManager)
			pManager->Release();
		if (pDevice)
			pDevice->Release();
		if (pEnum)
			pEnum->Release();
	}

	~process_audio_mute()
	{
		if (m_pVolume) {
			m_pVolume->SetMute(m_bWasMuted, nullptr);
			m_pVolume->Release();
		}
	}

	bool active() const { return (m_pVolume != nullptr); }

	process_audio_mute(const process_audio_mute &) = delete;
	process_audio_mute &operator=(const process_audio_mute &) = delete;

private:
	ISimpleAudioVolume *m_pVolume;
	BOOL                m_bWasMuted;
};

}	/* namespace a3dtest */

#endif /* A3DTEST_PROCESS_AUDIO_MUTE_HPP */
