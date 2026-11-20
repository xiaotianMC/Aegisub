// SPDX-License-Identifier: ISC
#pragma once
class wxWindow;
namespace agi { struct Context; }
wxWindow *CreateWorkbenchHome(wxWindow *parent, agi::Context *context, wxWindow *&editor_button);
