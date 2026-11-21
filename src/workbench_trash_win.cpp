// SPDX-License-Identifier: ISC
#include "workbench_trash.h"
#include "compat.h"

#include <windows.h>
#include <shlobj.h>
#include <wx/intl.h>
#include <wx/log.h>
#include <stdexcept>

namespace {
void RequireSuccess(HRESULT result) {
	if (result == S_OK) return;
	auto code = static_cast<unsigned long>(result);
	auto message = _("The system could not move the folder to Trash.") + wxString::Format(" (0x%08lx)", code);
	auto reason = wxSysErrorMsgStr(code).Trim();
	if (!reason.empty()) message += "\n" + reason;
	throw std::runtime_error(from_wx(message));
}

struct Apartment {
	Apartment() {
		auto result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
		if (result != S_FALSE) RequireSuccess(result);
	}
	~Apartment() { CoUninitialize(); }
};

struct ReleaseInterface {
	void operator()(IUnknown* object) const { object->Release(); }
};

template<class Interface> using ComPtr = std::unique_ptr<Interface, ReleaseInterface>;
}

void workbench::TrashFolder(Path const& folder) {
	Apartment apartment;
	IShellItem* raw_item = nullptr;
	auto result = SHCreateItemFromParsingName(folder.c_str(), nullptr, IID_PPV_ARGS(&raw_item));
	ComPtr<IShellItem> item(raw_item);
	RequireSuccess(result);

	IShellItem* raw_parent = nullptr;
	result = item->GetParent(&raw_parent);
	ComPtr<IShellItem> parent(raw_parent);
	RequireSuccess(result);

	ITransferSource* raw_source = nullptr;
	result = parent->BindToHandler(nullptr, BHID_Transfer, IID_PPV_ARGS(&raw_source));
	ComPtr<ITransferSource> source(raw_source);
	RequireSuccess(result);

	IShellItem* raw_trash = nullptr;
	result = SHGetKnownFolderItem(FOLDERID_RecycleBinFolder, KF_FLAG_DEFAULT, nullptr, IID_PPV_ARGS(&raw_trash));
	ComPtr<IShellItem> trash(raw_trash);
	RequireSuccess(result);

	// Request recycling directly. A general shell delete can choose permanent deletion.
	IShellItem* raw_recycled = nullptr;
	result = source->RecycleItem(item.get(), trash.get(), TSF_NORMAL, &raw_recycled);
	ComPtr<IShellItem> recycled(raw_recycled);
	if (FAILED(result) || !recycled) RequireSuccess(result == S_OK ? E_UNEXPECTED : result);
}
