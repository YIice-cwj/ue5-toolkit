#include "NetSmoke/NetSmokeResult.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

void NetSmokeResult::Write(const TCHAR* InFileName, const FString& InContent) {
    const FString Directory = FPaths::ProjectSavedDir() / TEXT("NetSmoke");
    IFileManager::Get().MakeDirectory(*Directory, true);
    FFileHelper::SaveStringToFile(InContent, *(Directory / InFileName));
}

