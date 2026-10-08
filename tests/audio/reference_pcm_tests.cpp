/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * reference_pcm_tests.cpp - sample-exact PCM comparison.
 *
 * NOT PART OF THE ORIGINAL.  Project tooling. See tests/README.md.
 *
 * Runs a3d_pcm_capture against the reconstruction and ref/a3dapi_33_rtl.dll.
 * Compares the common PCM prefix exactly; capture lengths depend on timing.
 * Process-local CLSID_A3d and CLSID_A3dDal blockers prevent registered
 * servers from loading another engine. Both DLLs use the A2D software path.
 *
 *---------------------------------------------------------------------------
 */

#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <gtest/gtest.h>

namespace {

struct CaptureResult {
    DWORD exit_code = STILL_ACTIVE;
    std::string output_directory;
	long			create  = -1;	/* CreateSoundBuffer calls */
	long			peak    = -1;
	unsigned long long	samples = 0;
	unsigned long		outbytes = 0;	/* the output buffer's descriptor */
	unsigned long		outflags = 0;
	int			outidx  = -1;
	std::string		wav;		/* the first buffer that got PCM */
	std::vector<short>	pcm;
	bool			ok      = false;
};

/* Read a 16-bit PCM WAV a3d_pcm_capture wrote: a canonical 44-byte header, then data. */
bool read_wav(const std::string &path, std::vector<short> &out)
{
	FILE *f = std::fopen(path.c_str(), "rb");
	if (!f)
		return (false);

	char hdr[44];
	if (std::fread(hdr, 1, sizeof hdr, f) != sizeof hdr ||
	    std::memcmp(hdr, "RIFF", 4) || std::memcmp(hdr + 8, "WAVE", 4)) {
		std::fclose(f);
		return (false);
	}

	unsigned long cb = 0;
	std::memcpy(&cb, hdr + 40, 4);

	out.resize(cb / sizeof(short));

	size_t got = out.empty() ? 0 : std::fread(&out[0], sizeof(short),
						  out.size(), f);
	out.resize(got);

	std::fclose(f);
	return (!out.empty());
}

/* Run a3d_pcm_capture against one DLL in one scene, and read back its stats and its
   stream. */
CaptureResult RunPcmCapture(const char *pszDll, const char *pszTag, int nMs,
			    const char *pszScene, const std::string &wave,
			    const char *pszEffect)
{

    CaptureResult s;
    namespace fs = std::filesystem;
    const auto directory = fs::path(A3D_CAPTURE_OUTPUT_DIR) /
        (std::string(pszTag) + "-" + pszScene + "-" + std::to_string(GetCurrentProcessId()) +
         "-" + std::to_string(GetTickCount64()));
    s.output_directory = directory.string();
    fs::create_directories(directory.parent_path());
    std::string command = "\"" + std::string(A3D_PCM_CAPTURE_EXE) + "\" --dll \"" + pszDll +
        "\" --wave \"" + wave + "\" --duration-ms " + std::to_string(nMs) +
        " --scene " + pszScene + " --effect " + pszEffect +
        " --output \"" + directory.string() + "\"";
    std::vector<char> mutable_command(command.begin(), command.end());
    mutable_command.push_back(0);
    STARTUPINFOA startup = {};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process = {};
    SECURITY_ATTRIBUTES security = {sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    const auto log_path = directory.string() + ".log";
    HANDLE log = CreateFileA(log_path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &security,
                             CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (log == INVALID_HANDLE_VALUE) return s;
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = startup.hStdError = log;
    startup.hStdInput = nullptr;
    const BOOL launched = CreateProcessA(nullptr, mutable_command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                                        nullptr, nullptr, &startup, &process);
    CloseHandle(log);
    if (!launched) return s;
    DWORD wait = WaitForSingleObject(process.hProcess, 35000);
    if (wait != WAIT_OBJECT_0) {
        TerminateProcess(process.hProcess, 124);
        WaitForSingleObject(process.hProcess, 2000);
    }
    DWORD exit_code = 1;
    GetExitCodeProcess(process.hProcess, &exit_code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    s.exit_code = exit_code;
    if (fs::is_directory(directory)) fs::rename(log_path, directory / "capture.log");
    if (exit_code != 0) return s;
    std::ifstream result(directory / "capture.tsv");
    std::string line;
    if (!std::getline(result, line) || line != "A3D_PCM_CAPTURE\t1") return s;
    while (std::getline(result, line)) {
        std::istringstream fields(line);
        std::string kind;
        std::getline(fields, kind, '\t');
        if (kind == "STATS") {
            fields >> s.create >> s.samples >> s.peak;
            if (fields.fail()) return CaptureResult{};
            s.ok = true;
        } else if (kind == "BUFFER") {
            int index, channels, overflow;
            unsigned long bytes, flags, rate, captured_bytes;
            long unlocks;
            fields >> index >> bytes >> flags >> rate >> channels >> unlocks >> captured_bytes >> overflow;
            if (fields.fail() || overflow) return CaptureResult{};
            if (s.outidx < 0 && unlocks > 0) { s.outidx = index; s.outbytes = bytes; s.outflags = flags; }
        } else if (kind == "WAV" && s.wav.empty()) {
            std::string index, filename;
            std::getline(fields, index, '\t');
            std::getline(fields, filename);
            if (fs::path(filename).filename() != filename) return CaptureResult{};
            s.wav = (directory / filename).string();
        }
    }

	if (!s.wav.empty() && read_wav(s.wav, s.pcm)) {
		s.samples = s.pcm.size();

		long nPeak = 0;
		for (size_t i = 0; i < s.pcm.size(); i++) {
			long v = s.pcm[i] < 0 ? -(long) s.pcm[i] : s.pcm[i];
			if (v > nPeak)
				nPeak = v;
		}
		s.peak = nPeak;
	}

	return s;
}

double rms_of(const std::vector<short> &v, size_t n)
{
	if (!n)
		return (0.0);

	double sq = 0.0;
	for (size_t i = 0; i < n; i++)
		sq += (double) v[i] * (double) v[i];

	return (std::sqrt(sq / (double) n));
}

/*
 * Drive one scene into both DLLs and require the two captures to agree on the
 * DirectSound buffers they made, the output descriptor and every sample.
 */
void expect_scene_parity(const char *pszScene, const char *pszName,
			 const std::string &wave = A3D_AB_WAV,
			 const char *pszEffect = "none")
{
	SCOPED_TRACE(pszName);

	CaptureResult ours = RunPcmCapture(A3D_AB_OUR_DLL, "ours", 700, pszScene, wave, pszEffect);
	CaptureResult ref  = RunPcmCapture(A3D_AB_REF_DLL, "ref", 700, pszScene, wave, pszEffect);

	ASSERT_TRUE(ours.ok) << "a3d_pcm_capture reported no stats for our DLL "
			     << A3D_AB_OUR_DLL << "; exit=" << ours.exit_code << "; artifacts=" << ours.output_directory;
	ASSERT_TRUE(ref.ok) << "a3d_pcm_capture reported no stats for the reference DLL "
			    << A3D_AB_REF_DLL << "; exit=" << ref.exit_code << "; artifacts=" << ref.output_directory;

	ASSERT_FALSE(ours.pcm.empty()) << "our DLL wrote no PCM stream";
	ASSERT_FALSE(ref.pcm.empty()) << "the reference wrote no PCM stream";

	const size_t n = ours.pcm.size() < ref.pcm.size() ? ours.pcm.size()
							  : ref.pcm.size();

	std::printf("[   audio  ] %-8s ours: peak=%ld rms=%.1f samples=%llu buffers=%ld\n",
		    pszName, ours.peak, rms_of(ours.pcm, n), ours.samples,
		    ours.create);
	std::printf("[   audio  ] %-8s ref:  peak=%ld rms=%.1f samples=%llu buffers=%ld\n",
		    pszName, ref.peak, rms_of(ref.pcm, n), ref.samples, ref.create);
	std::fflush(stdout);

	/* Both DLLs make the same set of DirectSound buffers on this path: the
	   0x3000 output and the primary.  A different count means one of them
	   took a different DAL. */
	EXPECT_EQ(ours.create, ref.create)
		<< "the two DLLs built a different number of DirectSound buffers";

	/* Require matching output-buffer flags. DAL_A2D requests
	   DSBCAPS_CTRLVOLUME (dbg:0x1004AEF0), then calls SetVolume, which fails
	   without that capability. */
	EXPECT_EQ(ours.outbytes, ref.outbytes)
		<< "the output buffer sizes differ";
	EXPECT_EQ(ours.outflags, ref.outflags)
		<< "the output buffer descriptor flags differ: ours 0x"
		<< std::hex << ours.outflags << " ref 0x" << ref.outflags;

	/* Audible, so an all-zero stream cannot pass the equality below. */
	EXPECT_GT(ours.peak, 1000) << "our DLL rendered (near) silence";
	EXPECT_GT(ref.peak, 1000) << "the reference rendered (near) silence";

	/* Compare every sample in the common prefix and report the first
	   mismatch and total mismatch count. Both builds use x87 arithmetic. */
	size_t cDiff = 0;
	size_t iFirst = 0;
	for (size_t i = 0; i < n; i++) {
		if (ours.pcm[i] != ref.pcm[i]) {
			if (!cDiff)
				iFirst = i;
			cDiff++;
		}
	}

	EXPECT_EQ(cDiff, 0u)
		<< cDiff << " of " << n << " samples differ; first at " << iFirst
		<< " ours=" << (cDiff ? ours.pcm[iFirst] : 0)
		<< " ref=" << (cDiff ? ref.pcm[iFirst] : 0)
		<< "\nours: " << ours.wav << "\nref:  " << ref.wav;
}

/* Write a deterministic PCM signal and return its capture-directory path. */
std::string synthetic_wave(const char *pszName, int nChannels, int nRate, int nBits)
{
	namespace fs = std::filesystem;

	const fs::path path = fs::path(A3D_CAPTURE_OUTPUT_DIR) / (std::string("synthetic-") + pszName + ".wav");
	fs::create_directories(path.parent_path());

	const int frames = nRate;
	const int block  = nChannels * nBits / 8;
	std::vector<unsigned char> data((size_t) frames * block);

	for (int i = 0; i < frames; i++) {
		const int  period = 20 + 10 * ((i / 4096) % 4);
		const bool high   = (i % period) < period / 2;
		for (int c = 0; c < nChannels; c++) {
			unsigned char *pb = &data[(size_t) i * block + c * nBits / 8];
			if (nBits == 8) {
				pb[0] = (unsigned char) (high ? (c ? 0xB0 : 0xE0) : (c ? 0x50 : 0x20));
			} else {
				const short v = (short) ((high ? 24000 : -24000) / (c ? 2 : 1));
				std::memcpy(pb, &v, sizeof v);
			}
		}
	}

	std::ofstream out(path, std::ios::binary);
	const unsigned long cbData = (unsigned long) data.size();
	const unsigned long cbRiff = 36 + cbData;
	const unsigned long cbFmt  = 16;
	const unsigned short tag = 1, ch = (unsigned short) nChannels, align = (unsigned short) block;
	const unsigned short bits = (unsigned short) nBits;
	const unsigned long rate = (unsigned long) nRate, avg = (unsigned long) (nRate * block);
	out.write("RIFF", 4);
	out.write((const char *) &cbRiff, 4);
	out.write("WAVEfmt ", 8);
	out.write((const char *) &cbFmt, 4);
	out.write((const char *) &tag, 2);
	out.write((const char *) &ch, 2);
	out.write((const char *) &rate, 4);
	out.write((const char *) &avg, 4);
	out.write((const char *) &align, 2);
	out.write((const char *) &bits, 2);
	out.write("data", 4);
	out.write((const char *) &cbData, 4);
	out.write((const char *) data.data(), (std::streamsize) data.size());
	return path.string();
}

}	/* namespace */

/* Keep scenes static so wall-clock timing cannot shift source movement
   between samples. Scenes 2, 3 and 5 use positioned rendering; scene 4
   uses native mode. */

TEST(AudioVsReference, SceneAtListener)	{ expect_scene_parity("default", "default"); }
TEST(AudioVsReference, SceneMonoMode)	{ expect_scene_parity("mono", "mono"); }
TEST(AudioVsReference, ScenePositioned)	{ expect_scene_parity("right", "right"); }
TEST(AudioVsReference, SceneBehindOffGains) { expect_scene_parity("behind", "behind"); }
TEST(AudioVsReference, SceneNativeMode)	{ expect_scene_parity("native", "native"); }
TEST(AudioVsReference, ScenePitchShifted) { expect_scene_parity("pitch", "pitch"); }

/* Velocity is constant, so the Doppler factor does not depend on timing. */
TEST(AudioVsReference, SceneDoppler)	{ expect_scene_parity("doppler", "doppler"); }
TEST(AudioVsReference, SceneEq)		{ expect_scene_parity("eq", "eq"); }
TEST(AudioVsReference, SceneBeyondMax)	{ expect_scene_parity("far", "far"); }
TEST(AudioVsReference, SceneInsideMin)	{ expect_scene_parity("near", "near"); }

TEST(AudioVsReference, Format8BitMono11k)
{
	expect_scene_parity("right", "8m11", synthetic_wave("8m11", 1, 11025, 8));
}

TEST(AudioVsReference, Format16BitStereo22k)
{
	expect_scene_parity("right", "16s22", synthetic_wave("16s22", 2, 22050, 16));
}

TEST(AudioVsReference, Format8BitStereo44kNative)
{
	expect_scene_parity("native", "8s44n", synthetic_wave("8s44n", 2, 44100, 8));
}

TEST(AudioVsReference, Format16BitStereo48kMono)
{
	expect_scene_parity("mono", "16s48m", synthetic_wave("16s48m", 2, 48000, 16));
}

/* Reverb is excluded: both DLLs block in the property-set wait until the
   capture deadline without A3D_FIXES' FixPropertyDeadlocks. */
TEST(AudioVsReference, EffectReflections)
{
	expect_scene_parity("right", "reflect", A3D_AB_WAV, "reflect");
}

TEST(AudioVsReference, SceneListenerPlaced)
{
	expect_scene_parity("listener", "listener");
}

TEST(AudioVsReference, EffectManualReflections)
{
	expect_scene_parity("right", "manual", A3D_AB_WAV, "manual");
}
