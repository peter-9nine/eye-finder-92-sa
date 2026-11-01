#include "BrowserClient.h"

#include <cstring>
#include <new>
#include <vector>

#pragma comment(lib, "version.lib")

namespace {

std::string LastError(const char* what) {
	return std::string(what) + " failed, Windows error " + std::to_string(GetLastError());
}

std::string FileVersionText(const std::wstring& path) {
	const DWORD size = GetFileVersionInfoSizeW(path.c_str(), nullptr);
	std::vector<char> data(size);
	if (size == 0 || !GetFileVersionInfoW(path.c_str(), 0, size, data.data())) return {};

	struct Translation { WORD language; WORD codePage; };
	Translation* translation = nullptr;
	UINT length = 0;
	if (!VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void**>(&translation), &length) || length < sizeof(Translation)) return {};

	wchar_t query[64];
	swprintf_s(query, L"\\StringFileInfo\\%04x%04x\\FileVersion", translation->language, translation->codePage);
	wchar_t* version = nullptr;
	if (!VerQueryValueW(data.data(), query, reinterpret_cast<void**>(&version), &length) || length == 0) return {};

	const int bytes = WideCharToMultiByte(CP_UTF8, 0, version, -1, nullptr, 0, nullptr, nullptr);
	std::string text(bytes > 0 ? bytes - 1 : 0, '\0');
	if (bytes > 1) WideCharToMultiByte(CP_UTF8, 0, version, -1, text.data(), bytes, nullptr, nullptr);
	return text;
}

}  // namespace

std::string BrowserRuntimeProblem(const std::wstring& folder) {
	if (GetFileAttributesW((folder + L"\\EyeFinderBrowser.exe").c_str()) == INVALID_FILE_ATTRIBUTES) return "EyeFinderBrowser.exe is missing from the browser folder";

	static const wchar_t* const kRequired[] = {
		L"libcef.dll", L"chrome_elf.dll", L"icudtl.dat", L"v8_context_snapshot.bin",
		L"resources.pak", L"chrome_100_percent.pak", L"locales\\en-US.pak",
	};
	for (const wchar_t* name : kRequired) {
		if (GetFileAttributesW((folder + L"\\" + name).c_str()) != INVALID_FILE_ATTRIBUTES) continue;

		char narrow[64]{};
		WideCharToMultiByte(CP_UTF8, 0, name, -1, narrow, sizeof(narrow) - 1, nullptr, nullptr);
		return std::string("CEF isn't installed: ") + narrow + " is missing from the browser folder. See INSTALL_CEF.txt there";
	}

	// The helper only runs with the exact build it was compiled against.
	const std::string version = FileVersionText(folder + L"\\libcef.dll");
	if (version != ef::kCefVersion) return "Wrong CEF version: found " + version + ", need " + ef::kCefVersion + ". See INSTALL_CEF.txt in the browser folder";
	return {};
}

bool BrowserClient::Start(const std::wstring& helperPath, const std::wstring& url, const std::wstring& mailPath, uint32_t width, uint32_t height, std::string& error) {
	Stop();

	const uint32_t stride = width * 4;
	const size_t size = ef::SectionSize(stride, height);
	const std::wstring base = L"Local\\EyeFinder92SA_" + std::to_wstring(GetCurrentProcessId()) + L"_" + std::to_wstring(GetTickCount());
	const std::wstring wakeName = base + L"_wake";

	mapping_ = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, static_cast<DWORD>(size), base.c_str());
	if (!mapping_) {
		error = LastError("CreateFileMapping");
		Stop();
		return false;
	}

	void* view = MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, size);
	if (!view) {
		// In a 32-bit process this fails if the address space is fragmented.
		error = LastError("MapViewOfFile");
		Stop();
		return false;
	}

	header_ = new (view) ef::Header{};
	header_->magic = ef::kMagic;
	header_->protocol = ef::kProtocol;
	header_->width = width;
	header_->height = height;
	header_->stride = stride;
	header_->ready.store(1);
	readerSlot_ = 2;

	wake_ = CreateEventW(nullptr, FALSE, FALSE, wakeName.c_str());
	job_ = CreateJobObjectW(nullptr, nullptr);
	if (!wake_ || !job_) {
		error = LastError("CreateEvent/CreateJobObject");
		Stop();
		return false;
	}

	// The helper and its subprocesses die with the game, even if it crashes.
	JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
	limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
	SetInformationJobObject(job_, JobObjectExtendedLimitInformation, &limits, sizeof(limits));

	std::wstring command = L"\"" + helperPath + L"\" --ef-shm=" + base + L" --ef-parent=" +
		std::to_wstring(GetCurrentProcessId()) + L" --ef-url=\"" + url + L"\" --ef-mail=\"" + mailPath + L"\"";

	std::wstring directory = helperPath.substr(0, helperPath.find_last_of(L"\\/"));

	STARTUPINFOW startup{};
	startup.cb = sizeof(startup);
	PROCESS_INFORMATION info{};
	if (!CreateProcessW(helperPath.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED, nullptr, directory.c_str(), &startup, &info)) {
		error = LastError("CreateProcess");
		Stop();
		return false;
	}

	AssignProcessToJobObject(job_, info.hProcess);
	ResumeThread(info.hThread);
	CloseHandle(info.hThread);
	process_ = info.hProcess;
	return true;
}

void BrowserClient::Stop() {
	if (process_) {
		Send({ ef::EventType::Quit });
		WaitForSingleObject(process_, 2000);
		CloseHandle(process_);
		process_ = nullptr;
	}
	if (job_) {
		CloseHandle(job_);
		job_ = nullptr;
	}
	if (wake_) {
		CloseHandle(wake_);
		wake_ = nullptr;
	}
	if (header_) {
		UnmapViewOfFile(header_);
		header_ = nullptr;
	}
	if (mapping_) {
		CloseHandle(mapping_);
		mapping_ = nullptr;
	}
}

bool BrowserClient::HasExited() const {
	return process_ && WaitForSingleObject(process_, 0) == WAIT_OBJECT_0;
}

bool BrowserClient::IsReady() const {
	return header_ && header_->state.load(std::memory_order_acquire) == static_cast<uint32_t>(ef::HelperState::Ready);
}

void BrowserClient::Send(const ef::Event& event) {
	if (!header_) return;

	const uint32_t write = header_->ringWrite.load(std::memory_order_relaxed);
	const uint32_t read = header_->ringRead.load(std::memory_order_acquire);
	if (write - read >= ef::kRingSize) return;  // Never block the game: if the ring is full, the event is dropped.

	header_->ring[write % ef::kRingSize] = event;
	header_->ringWrite.store(write + 1, std::memory_order_release);
	if (wake_) SetEvent(wake_);
}

const uint8_t* BrowserClient::AcquireFrame() {
	if (!header_) return nullptr;
	if (!(header_->ready.load(std::memory_order_acquire) & ef::kNewFrame)) return nullptr;

	readerSlot_ = header_->ready.exchange(readerSlot_, std::memory_order_acq_rel) & ~ef::kNewFrame;
	return ef::SlotPixels(header_, readerSlot_);
}

ef::CursorKind BrowserClient::Cursor() const {
	return header_ ? static_cast<ef::CursorKind>(header_->cursor.load(std::memory_order_relaxed)) : ef::CursorKind::Arrow;
}

bool BrowserClient::TakeMailOp(ef::MailOp& operation) {
	if (!header_) return false;

	const uint32_t read = header_->mailOpRead.load(std::memory_order_relaxed);
	if (read == header_->mailOpWrite.load(std::memory_order_acquire)) return false;

	operation = header_->mailOps[read % ef::kMailOpRingSize];
	header_->mailOpRead.store(read + 1, std::memory_order_release);
	return true;
}

bool BrowserClient::TakeSiteScript(std::string& name) {
	if (!header_) return false;

	const uint32_t read = header_->siteScriptRead.load(std::memory_order_relaxed);
	if (read == header_->siteScriptWrite.load(std::memory_order_acquire)) return false;

	const char* entry = header_->siteScripts[read % ef::kSiteScriptRingSize];
	name.assign(entry, strnlen(entry, ef::kSiteScriptNameSize));
	header_->siteScriptRead.store(read + 1, std::memory_order_release);
	return true;
}

bool BrowserClient::TakeValueOp(std::string& key, std::string& value) {
	if (!header_) return false;

	const uint32_t read = header_->valueOpRead.load(std::memory_order_relaxed);
	if (read == header_->valueOpWrite.load(std::memory_order_acquire)) return false;

	const ef::ValueOp& entry = header_->valueOps[read % ef::kValueOpRingSize];
	key.assign(entry.key, strnlen(entry.key, ef::kValueKeySize));
	value.assign(entry.value, strnlen(entry.value, ef::kValueTextSize));
	header_->valueOpRead.store(read + 1, std::memory_order_release);
	return true;
}

void BrowserClient::PublishGameState(const ef::GameStateData& state) {
	if (!header_) return;

	const uint32_t sequence = header_->gameStateSequence.load(std::memory_order_relaxed);
	header_->gameStateSequence.store(sequence + 1, std::memory_order_release);
	std::atomic_thread_fence(std::memory_order_release);
	header_->gameState = state;
	header_->gameStateSequence.store(sequence + 2, std::memory_order_release);
}

bool BrowserClient::KeyboardOpen() const {
	return header_ && header_->keyboardOpen.load(std::memory_order_acquire) != 0;
}

bool BrowserClient::TakeCloseRequest() {
	return header_ && header_->closeRequest.exchange(0, std::memory_order_acq_rel) != 0;
}
