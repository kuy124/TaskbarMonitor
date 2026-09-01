#pragma once
#include "Common.h"

#define LICENSE_REGISTRY_KEY L"LicenseAccepted"

bool IsLicenseAccepted();
void SetLicenseAccepted(bool accepted);
bool ShowLicenseDialog(HINSTANCE hInstance);