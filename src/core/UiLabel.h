#pragma once
// Marks a core label for the editor's translation catalog. Core and API callers still receive the
// original label. Qt Linguist extracts these markers without a Qt dependency in the core.
#define KE_UI_LABEL(context, source) source
