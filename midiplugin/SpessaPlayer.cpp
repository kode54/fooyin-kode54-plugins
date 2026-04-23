#include "SpessaPlayer.h"
#include "spessasynth/sflist/sflist.h"

#include <iomanip>
#include <stdlib.h>

#include <string>

#include <chrono>
#include <cmath>
#include <list>
#include <map>
#include <mutex>
#include <thread>

#define _countof(arr) (sizeof(arr) / sizeof((arr)[0]))

namespace {
bool has_ext_ci(const char *path, const char *ext) {
	size_t plen = strlen(path);
	size_t elen = strlen(ext);
	if(plen < elen) return false;
	const char *tail = path + plen - elen;
	for(size_t i = 0; i < elen; i++) {
		char a = tail[i], b = ext[i];
		if(a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
		if(b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
		if(a != b) return false;
	}
	return true;
}

SS_FilteredBanks *open_sflist(const char *path) {
	SS_FilteredBanks *banks = nullptr;
	char base_path[4096];

	const char *slash = strrchr(path, '/');
	if(slash) {
		size_t n = (size_t)(slash - path);
		if(n >= sizeof(base_path)) n = sizeof(base_path) - 1;
		memcpy(base_path, path, n);
		base_path[n] = '\0';
	} else {
		strcpy(base_path, ".");
	}

	SS_File *sflistFile = ss_file_open_from_file(path);
	if(sflistFile) {
		size_t sflistSize = ss_file_size(sflistFile);
		char *sflist = (char *)malloc(sflistSize);
		if(sflist && sflistSize) {
			ss_file_read_bytes(sflistFile, 0, (uint8_t *)sflist, sflistSize);
			ss_file_close(sflistFile);

			char err[sflist_max_error] = "";
			banks = sflist_load(sflist, sflistSize, base_path, err);
			free(sflist);

			return banks;
		}
		ss_file_close(sflistFile);
	}
	return nullptr;
}
}

struct Spessa_Cached_SoundFont {
	unsigned long ref_count;
	std::chrono::steady_clock::time_point time_released;
	SS_SoundBank *bank;
	Spessa_Cached_SoundFont()
	: bank(nullptr) {
	}
	Spessa_Cached_SoundFont(const Spessa_Cached_SoundFont &in) {
		ref_count = in.ref_count;
		time_released = in.time_released;
		bank = in.bank;
	}
	Spessa_Cached_SoundFont(SS_SoundBank *in)
	: bank(in), ref_count(1) {
	}
};

static std::mutex *Cache_Lock;

static std::map<std::string, Spessa_Cached_SoundFont> *Cache_List;

static bool Cache_Running = false;

static std::thread *Cache_Thread = NULL;

static void cache_run();

static void cache_init() {
	Cache_Lock = new std::mutex;
	Cache_List = new std::map<std::string, Spessa_Cached_SoundFont>;
	Cache_Thread = new std::thread(cache_run);
}

static void cache_deinit() {
	Cache_Running = false;
	Cache_Thread->join();
	delete Cache_Thread;

	for(auto it = Cache_List->begin(); it != Cache_List->end(); ++it) {
		if(it->second.bank)
			ss_soundbank_free(it->second.bank);
	}
	delete Cache_List;
}

static SS_SoundBank *cache_open_font(const char *path) {
	SS_SoundBank *bank = nullptr;

	std::lock_guard<std::mutex> lock(*Cache_Lock);

	auto &entry = (*Cache_List)[path];

	if(!entry.bank) {
		SS_File *bankFile = ss_file_open_from_file(path);
		if(bankFile) {
			bank = ss_soundbank_load(bankFile);
			ss_file_close(bankFile);
			if(bank) {
				entry.bank = bank;
				entry.ref_count = 1;
			} else {
				Cache_List->erase(path);
			}
		} else {
			Cache_List->erase(path);
		}
	} else {
		bank = entry.bank;
		++(entry.ref_count);
	}

	return bank;
}

static void cache_close_font(SS_SoundBank *bank) {
	std::lock_guard<std::mutex> lock(*Cache_Lock);

	for(auto it = Cache_List->begin(); it != Cache_List->end(); ++it) {
		if(it->second.bank == bank) {
			if(--it->second.ref_count == 0)
				it->second.time_released = std::chrono::steady_clock::now();
			break;
		}
	}
}

static void cache_run() {
	std::chrono::milliseconds dura(250);

	Cache_Running = true;

	while(Cache_Running) {
		std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();

		{
			std::lock_guard<std::mutex> lock(*Cache_Lock);
			for(auto it = Cache_List->begin(); it != Cache_List->end();) {
				if(it->second.ref_count == 0) {
					auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - it->second.time_released);
					if(elapsed.count() >= 10) {
						if(it->second.bank)
							ss_soundbank_free(it->second.bank);
						it = Cache_List->erase(it);
						continue;
					}
				}
				++it;
			}
		}

		std::this_thread::sleep_for(dura);
	}
}

static class Spessa_Initializer {
	std::mutex lock;

	bool initialized;

	std::string base_path;

	public:
	Spessa_Initializer()
	: initialized(false) {
	}

	~Spessa_Initializer() {
		if(initialized) {
			cache_deinit();
		}
	}

	bool check_initialized() {
		std::lock_guard<std::mutex> lock(this->lock);
		return initialized;
	}

	bool initialize() {
		std::lock_guard<std::mutex> lock(this->lock);
		if(!initialized) {
			cache_init();
			ss_unit_converter_init();
			initialized = true;
		}
		return initialized;
	}
} g_initializer;

SpessaPlayer::SpessaPlayer()
: MIDIPlayer() {
	_synth = nullptr;
	interp = SS_INTERP_LINEAR;
	voiceCount = 512;
	fileBankOffset = 0;

	if(!g_initializer.initialize()) throw std::runtime_error("Unable to initialize SpessaSynth");
}

SpessaPlayer::~SpessaPlayer() {
	shutdown();
}

void SpessaPlayer::setSoundFont(const char *in) {
	sSoundFontName = in;
	shutdown();
}

void SpessaPlayer::setFileBankOffset(uint16_t bank_offset) {
	fileBankOffset = bank_offset;
}

void SpessaPlayer::setFileSoundFont(const char *in) {
	sFileSoundFontName = in;
	shutdown();
}

void SpessaPlayer::setFileSoundFontData(const uint8_t *bank, size_t bank_size) {
	fileBankData.assign(bank, bank + bank_size);
}

void SpessaPlayer::setInterpolation(SS_InterpolationType interp) {
	this->interp = interp;
	shutdown();
}

void SpessaPlayer::setVoiceCount(int polyphony) {
	if(polyphony > 0) {
		this->voiceCount = polyphony;
		shutdown();
	}
}

void SpessaPlayer::shutdown() {
	if(_synth) {
		ss_processor_remove_soundbank(_synth, "fileBank", true);
		ss_processor_remove_soundbank(_synth, "globalBank", true);
		ss_processor_free(_synth);
		_synth = nullptr;
	}
	for(auto it = _banks.begin(); it != _banks.end(); ++it)
		cache_close_font(*it);
	_banks.resize(0);
	initialized = false;
}

bool SpessaPlayer::startup() {
	if(_synth) return true;

	SS_SoundBank *fileBank = nullptr;
	SS_FilteredBanks *filteredFileBank = nullptr;
	if(sFileSoundFontName.length()) {
		const char *path = sFileSoundFontName.c_str();
		if(has_ext_ci(path, ".sflist") || has_ext_ci(path, ".json"))
			filteredFileBank = open_sflist(path);
		else
			fileBank = cache_open_font(path);
	}

	SS_SoundBank *globalBank = nullptr;
	SS_FilteredBanks *filteredGlobalBank = nullptr;
	if(sSoundFontName.length()) {
		const char *path = sSoundFontName.c_str();
		if(has_ext_ci(path, ".sflist") || has_ext_ci(path, ".json"))
			filteredGlobalBank = open_sflist(path);
		else
			globalBank = cache_open_font(path);
	}

	SS_SoundBank* fileBankAsData = NULL;
	if (fileBankData.size()) {
		SS_File *bankFile = ss_file_open_from_memory(fileBankData.data(), fileBankData.size(), false);
		if(bankFile) {
			fileBankAsData = ss_soundbank_load(bankFile);
			ss_file_close(bankFile);
		}
	}

	const bool has_embedded = midi_file && midi_file->embedded_soundbank &&
	                          midi_file->embedded_soundbank_size > 0;

	if(!fileBank && !filteredFileBank && !fileBankAsData && !globalBank && !filteredGlobalBank && !has_embedded) {
		return false;
	}

	if(fileBank) _banks.push_back(fileBank);
	if(globalBank) _banks.push_back(globalBank);

	SS_ProcessorOptions opts;
	opts.enable_effects = true;
	opts.voice_cap = voiceCount;
	opts.interpolation = interp;

	_synth = ss_processor_create((uint32_t)std::lround(dSampleRate), &opts);
	if(!_synth) {
		return false;
	}

	/* This bank will be owned by the synthesizer */
	if(fileBankAsData && !ss_processor_load_soundbank(_synth, fileBankAsData, "fileBankAsData", fileBankOffset, false))
		return false;

	if(fileBank && !ss_processor_load_soundbank(_synth, fileBank, "fileBank", fileBankOffset, false))
		return false;
	if(filteredFileBank && !ss_processor_load_filtered_banks(_synth, filteredFileBank, "fileBank", false))
		return false;
	if(globalBank && !ss_processor_load_soundbank(_synth, globalBank, "globalBank", 0, false))
		return false;
	if(filteredGlobalBank && !ss_processor_load_filtered_banks(_synth, filteredGlobalBank, "globalBank", false))
		return false;

	/* Embedded RMID soundbank is auto-loaded by ss_sequencer_load_midi. */

	initialized = true;
	return true;
}

void SpessaPlayer::renderChunk(float *out, uint32_t sample_count) {
	if(!_synth) return;
	ss_processor_render_interleaved(_synth, out, sample_count);
}
