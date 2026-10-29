// Automatically generated static data export commandlet implementation. Do not edit manually.
#include "Generated/Registration/UGameStaticDataExportCommandlet.h"

#include "Generated/Data/StaticData/UStaticDataSubsystem.h"

int32 UGameStaticDataExportCommandlet::Main([[maybe_unused]] const FString& Params) {
    return UStaticDataSubsystem::ExportStaticDataBin() ? 0 : 1;
}
